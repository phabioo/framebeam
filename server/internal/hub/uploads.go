package hub

import (
	"context"
	"crypto/sha256"
	"database/sql"
	"encoding"
	"encoding/hex"
	"errors"
	"io"
	"log/slog"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/google/uuid"
)

const (
	// UploadChunkBytes is the largest chunk of a resumable upload (64 MiB); it is also the chunk size the server proposes.
	UploadChunkBytes int64 = 64 << 20
	// UploadIdleTTL: a session untouched for this long is removed with its data.
	UploadIdleTTL = 7 * 24 * time.Hour
	// MaxOpenUploads caps the open sessions of one user.
	MaxOpenUploads = 16
)

// UploadSession is a resumable ROM upload.
type UploadSession struct {
	ID, UserID, Filename, Title, System, SHA256 string
	Size, Offset                                int64
	CreatedAt, UpdatedAt                        time.Time
}

// UploadInput starts (or resumes) an upload.
type UploadInput struct {
	Filename, SHA256, Title, System string
	Size                            int64
}

func (s *Service) uploadDir() string { return filepath.Join(s.dataDir, "tmp", "uploads") }

// uploadPath returns the data file of a session. The ID must be a UUID, and the cleaned path must stay inside the
// upload directory.
func (s *Service) uploadPath(id string) (string, error) {
	if _, err := uuid.Parse(id); err != nil {
		return "", ErrNotFound
	}
	base := filepath.Clean(s.uploadDir())
	p := filepath.Clean(filepath.Join(base, id))
	if !strings.HasPrefix(p, base+string(os.PathSeparator)) {
		return "", ErrNotFound
	}
	return p, nil
}

const uploadCols = `id, user_id, filename, size, COALESCE(sha256,''), title, system, "offset", created_at, updated_at`

func scanUpload(r scanner) (UploadSession, error) {
	var u UploadSession
	var c, up int64
	if err := r.Scan(&u.ID, &u.UserID, &u.Filename, &u.Size, &u.SHA256, &u.Title, &u.System, &u.Offset, &c, &up); err != nil {
		return UploadSession{}, err
	}
	u.CreatedAt, u.UpdatedAt = time.Unix(c, 0).UTC(), time.Unix(up, 0).UTC()
	return u, nil
}

// CreateUpload opens a resumable upload. An open session of the same user for the same SHA-256 (or the same file
// name and size) is returned instead, so a client can resume after a restart. A SHA-256 that is already stored on
// the server is a conflict naming the existing game.
func (s *Service) CreateUpload(ctx context.Context, userID string, in UploadInput) (UploadSession, error) {
	filename := cleanText(filepath.Base(strings.ReplaceAll(in.Filename, `\`, "/")), 255)
	if filename == "" || filename == "." || filename == "/" {
		return UploadSession{}, badRequest("File name missing")
	}
	if in.Size <= 0 {
		return UploadSession{}, badRequest("size must be at least 1 byte")
	}
	if in.Size > MaxROMBytes {
		return UploadSession{}, &Error{Code: CodePayloadTooLarge, Message: "ROM exceeds the upload limit"}
	}
	if in.SHA256 != "" && !ValidSHA256(in.SHA256) {
		return UploadSession{}, badRequest("sha256 must be 64 lowercase hex characters")
	}
	system, err := s.resolveSystem(ctx, in.System, filename)
	if err != nil {
		return UploadSession{}, err
	}
	if in.SHA256 != "" {
		var gid string
		err := s.db.QueryRowContext(ctx, `SELECT game_id FROM game_roms WHERE sha256 = ? AND stored = 1`, in.SHA256).Scan(&gid)
		if err == nil {
			return UploadSession{}, conflictWith("This ROM is already in the library", gid)
		}
		if !errors.Is(err, sql.ErrNoRows) {
			return UploadSession{}, internal(err)
		}
	}
	title := cleanText(in.Title, MaxGameTitle)

	// Resume: same SHA-256, or same file name and size (a session without a hash yet).
	var cur UploadSession
	err = s.db.QueryRowContext(ctx, `SELECT `+uploadCols+` FROM uploads WHERE user_id = ? AND (
			(? <> '' AND sha256 = ?) OR (filename = ? AND size = ? AND (sha256 IS NULL OR ? = '' OR sha256 = ?)))
		ORDER BY updated_at DESC LIMIT 1`, userID, in.SHA256, in.SHA256, filename, in.Size, in.SHA256, in.SHA256).
		Scan(&cur.ID, &cur.UserID, &cur.Filename, &cur.Size, &cur.SHA256, &cur.Title, &cur.System, &cur.Offset, new(int64), new(int64))
	if err == nil {
		if cur.Size == in.Size {
			if in.SHA256 != "" && cur.SHA256 == "" {
				if _, err := s.db.ExecContext(ctx, `UPDATE uploads SET sha256 = ? WHERE id = ?`, in.SHA256, cur.ID); err != nil {
					return UploadSession{}, internal(err)
				}
			}
			return s.GetUpload(ctx, userID, cur.ID)
		}
		// Same hash but another size cannot be the same file: start a new session below.
	} else if !errors.Is(err, sql.ErrNoRows) {
		return UploadSession{}, internal(err)
	}

	var open int
	if err := s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM uploads WHERE user_id = ?`, userID).Scan(&open); err != nil {
		return UploadSession{}, internal(err)
	}
	// The data directory also holds the database and the saves: keep a reserve and count what open sessions
	// still have to write.
	var pending int64
	if err := s.db.QueryRowContext(ctx, `SELECT COALESCE(SUM(size - "offset"), 0) FROM uploads`).Scan(&pending); err != nil {
		return UploadSession{}, internal(err)
	}
	if err := s.checkDiskSpace(in.Size + pending); err != nil {
		return UploadSession{}, err
	}
	if open >= MaxOpenUploads {
		return UploadSession{}, &Error{Code: CodeRateLimited, Message: "Too many open uploads; finish or abort one first"}
	}
	id := uuid.NewString()
	p, err := s.uploadPath(id)
	if err != nil {
		return UploadSession{}, err
	}
	if err := os.MkdirAll(s.uploadDir(), 0o750); err != nil {
		return UploadSession{}, internal(err)
	}
	now := s.Now().Unix()
	var shaArg any
	if in.SHA256 != "" {
		shaArg = in.SHA256
	}
	if _, err := s.db.ExecContext(ctx, `INSERT INTO uploads(id, user_id, filename, size, sha256, title, system, "offset", created_at, updated_at)
		VALUES (?,?,?,?,?,?,?,0,?,?)`, id, userID, filename, in.Size, shaArg, title, system, now, now); err != nil {
		return UploadSession{}, internal(err)
	}
	f, err := os.OpenFile(p, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0o600)
	if err != nil {
		s.db.ExecContext(ctx, `DELETE FROM uploads WHERE id = ?`, id)
		return UploadSession{}, internal(err)
	}
	f.Close()
	return s.GetUpload(ctx, userID, id)
}

// DiskReserveBytes is the free space on the data volume that ROM uploads must leave for the database and saves.
const DiskReserveBytes int64 = 1 << 30

// freeBytesFn reads the free space of the data volume (0 = unknown; tests replace it).
var freeBytesFn = freeBytes

// checkDiskSpace refuses a ROM that would not fit next to the reserve (413 payload_too_large). An unknown free
// size (non-Linux/Windows platforms) never refuses.
func (s *Service) checkDiskSpace(need int64) error {
	free := freeBytesFn(s.dataDir)
	if free == 0 {
		return nil
	}
	if need < 0 || uint64(need)+uint64(DiskReserveBytes) > free {
		return &Error{Code: CodePayloadTooLarge, Message: "Not enough free space on the server's disk for this ROM"}
	}
	return nil
}

// GetUpload returns a session of the user (another user's session does not exist for the caller).
func (s *Service) GetUpload(ctx context.Context, userID, id string) (UploadSession, error) {
	u, err := scanUpload(s.db.QueryRowContext(ctx, `SELECT `+uploadCols+` FROM uploads WHERE id = ? AND user_id = ?`, id, userID))
	if errors.Is(err, sql.ErrNoRows) {
		return UploadSession{}, ErrNotFound
	}
	if err != nil {
		return UploadSession{}, internal(err)
	}
	return u, nil
}

// AbortUpload deletes a session and its data.
func (s *Service) AbortUpload(ctx context.Context, userID, id string) error {
	u, err := s.GetUpload(ctx, userID, id)
	if err != nil {
		return err
	}
	if !s.lockUpload(u.ID) {
		return conflict("A chunk of this upload is still being written")
	}
	defer s.unlockUpload(u.ID)
	return s.dropUpload(ctx, u.ID)
}

func (s *Service) dropUpload(ctx context.Context, id string) error {
	if _, err := s.db.ExecContext(ctx, `DELETE FROM uploads WHERE id = ?`, id); err != nil {
		return internal(err)
	}
	if p, err := s.uploadPath(id); err == nil {
		if err := os.Remove(p); err != nil && !errors.Is(err, os.ErrNotExist) {
			slog.Warn("remove upload data failed", "upload_id", id, "err", err)
		}
	}
	return nil
}

func (s *Service) lockUpload(id string) bool {
	s.upMu.Lock()
	defer s.upMu.Unlock()
	if s.upBusy == nil {
		s.upBusy = map[string]bool{}
	}
	if s.upBusy[id] {
		return false
	}
	s.upBusy[id] = true
	return true
}

func (s *Service) unlockUpload(id string) {
	s.upMu.Lock()
	delete(s.upBusy, id)
	s.upMu.Unlock()
}

// UploadChunk is one received range (inclusive bounds, as in `Content-Range: bytes start-end/total`).
type UploadChunk struct {
	Start, End, Total int64
}

// AppendUpload writes one chunk. Start must equal the session's offset, the chunk is at most UploadChunkBytes and
// Total must equal the announced size. The SHA-256 state is persisted after each chunk. The chunk that completes
// the file finishes it: the hash is checked (when announced), then the file becomes a stored file of the game with
// that SHA-256, an extra file of the game with the same identity, or a new game. The game is returned only then.
func (s *Service) AppendUpload(ctx context.Context, userID, id string, c UploadChunk, body io.Reader) (_ UploadSession, game *Game, err error) {
	defer s.publishOK(&err, TopicLibrary)
	u, err := s.GetUpload(ctx, userID, id)
	if err != nil {
		return UploadSession{}, nil, err
	}
	n := c.End - c.Start + 1
	switch {
	case c.Start < 0 || c.End < c.Start || c.Total != u.Size || c.End >= u.Size:
		return UploadSession{}, nil, badRequest("Content-Range does not fit the upload")
	case n > UploadChunkBytes:
		return UploadSession{}, nil, &Error{Code: CodePayloadTooLarge, Message: "Chunk exceeds 64 MiB"}
	}
	if !s.lockUpload(u.ID) {
		return UploadSession{}, nil, conflict("Another chunk of this upload is being written")
	}
	defer s.unlockUpload(u.ID)
	// Re-read under the lock: the offset may have moved.
	if u, err = s.GetUpload(ctx, userID, id); err != nil {
		return UploadSession{}, nil, err
	}
	if c.Start != u.Offset {
		return u, nil, conflict("Chunk does not start at the current offset")
	}
	p, err := s.uploadPath(u.ID)
	if err != nil {
		return UploadSession{}, nil, err
	}
	h := sha256.New()
	if u.Offset > 0 {
		var state []byte
		if err := s.db.QueryRowContext(ctx, `SELECT hash_state FROM uploads WHERE id = ?`, u.ID).Scan(&state); err != nil {
			return UploadSession{}, nil, internal(err)
		}
		if err := h.(encoding.BinaryUnmarshaler).UnmarshalBinary(state); err != nil {
			return UploadSession{}, nil, internal(err)
		}
	}
	f, err := os.OpenFile(p, os.O_WRONLY|os.O_CREATE, 0o600)
	if err != nil {
		return UploadSession{}, nil, internal(err)
	}
	defer f.Close()
	// The data file must hold at least the bytes the hash state covers. A shorter file (cleaned up by hand,
	// antivirus, disk-full recovery) would be zero-extended below while the hash state still describes the old
	// bytes, storing a ROM that does not match its hash: start the session over instead.
	if st, err := f.Stat(); err != nil {
		return UploadSession{}, nil, internal(err)
	} else if st.Size() < u.Offset {
		if err := f.Truncate(0); err != nil {
			return UploadSession{}, nil, internal(err)
		}
		if _, err := s.db.ExecContext(ctx, `UPDATE uploads SET "offset" = 0, hash_state = NULL, updated_at = ? WHERE id = ?`, s.Now().Unix(), u.ID); err != nil {
			return UploadSession{}, nil, internal(err)
		}
		slog.Warn("upload data shorter than its offset; session restarted", "upload_id", u.ID, "offset", u.Offset, "size", st.Size())
		return UploadSession{}, nil, conflict("The stored part of this upload is incomplete; it restarts at offset 0")
	}
	// Bytes beyond the offset are the leftover of an interrupted chunk.
	if err := f.Truncate(u.Offset); err != nil {
		return UploadSession{}, nil, internal(err)
	}
	if _, err := f.Seek(u.Offset, io.SeekStart); err != nil {
		return UploadSession{}, nil, internal(err)
	}
	got, err := io.Copy(io.MultiWriter(f, h), io.LimitReader(body, n))
	if err != nil {
		return UploadSession{}, nil, err // includes *http.MaxBytesError and connection errors; the offset stays
	}
	if got != n {
		return UploadSession{}, nil, badRequest("Chunk is shorter than Content-Range announces")
	}
	if err := f.Sync(); err != nil {
		return UploadSession{}, nil, internal(err)
	}
	state, err := h.(encoding.BinaryMarshaler).MarshalBinary()
	if err != nil {
		return UploadSession{}, nil, internal(err)
	}
	u.Offset += n
	if _, err := s.db.ExecContext(ctx, `UPDATE uploads SET "offset" = ?, hash_state = ?, updated_at = ? WHERE id = ?`,
		u.Offset, state, s.Now().Unix(), u.ID); err != nil {
		return UploadSession{}, nil, internal(err)
	}
	if u.Offset < u.Size {
		return u, nil, nil
	}
	f.Close()
	g, err := s.finishUpload(ctx, u, p, hex.EncodeToString(h.Sum(nil)))
	if err != nil {
		return u, nil, err
	}
	return u, &g, nil
}

func (s *Service) finishUpload(ctx context.Context, u UploadSession, path, sum string) (Game, error) {
	if u.SHA256 != "" && u.SHA256 != sum {
		s.dropUpload(ctx, u.ID)
		return Game{}, badRequest("SHA-256 of the upload does not match the announced hash")
	}
	title := u.Title
	if title == "" {
		title = strings.TrimSuffix(u.Filename, filepath.Ext(u.Filename))
	}
	code, kind, ver := ReadGameIdentityVersion(path, u.System)
	g, err := s.commitStored(ctx, path, romMeta{sha: sum, size: u.Size, filename: u.Filename, title: title, system: u.System,
		addedBy: u.UserID, code: code, kind: kind, version: ver, uploadID: u.ID})
	if err != nil {
		if errors.Is(err, ErrConflict) { // another copy was stored meanwhile: nothing left to resume
			s.dropUpload(ctx, u.ID)
		}
		return Game{}, err
	}
	return g, nil
}

// CleanupUploads removes sessions idle for UploadIdleTTL with their data, and data files without a session.
func (s *Service) CleanupUploads(ctx context.Context) error {
	rows, err := s.db.QueryContext(ctx, `SELECT id FROM uploads WHERE updated_at <= ?`, s.Now().Add(-UploadIdleTTL).Unix())
	if err != nil {
		return internal(err)
	}
	var ids []string
	for rows.Next() {
		var id string
		if err := rows.Scan(&id); err != nil {
			rows.Close()
			return internal(err)
		}
		ids = append(ids, id)
	}
	rows.Close()
	for _, id := range ids {
		if !s.lockUpload(id) {
			continue
		}
		err := s.dropUpload(ctx, id)
		s.unlockUpload(id)
		if err != nil {
			return err
		}
	}
	// Orphans: a data file whose session is gone (crash between the steps) and that has not been touched for a day.
	ents, err := os.ReadDir(s.uploadDir())
	if err != nil {
		return nil
	}
	for _, e := range ents {
		info, err := e.Info()
		if err != nil || s.Now().Sub(info.ModTime()) < 24*time.Hour {
			continue
		}
		var n int
		if s.db.QueryRowContext(ctx, `SELECT COUNT(*) FROM uploads WHERE id = ?`, e.Name()).Scan(&n) == nil && n == 0 {
			if p, err := s.uploadPath(e.Name()); err == nil {
				os.Remove(p)
			}
		}
	}
	return nil
}
