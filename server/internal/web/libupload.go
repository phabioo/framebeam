package web

import (
	"encoding/json"
	"errors"
	"net/http"
	"regexp"
	"strconv"

	"github.com/phabioo/uzume/server/internal/hub"
)

// Resumable ROM upload for the web interface: the same upload sessions as the API (hub.Service.CreateUpload and
// AppendUpload), reached with the session cookie and the CSRF token. static/libupload.js sends 8 MiB chunks.

type uploadJSON struct {
	UploadID  string    `json:"upload_id,omitempty"`
	Offset    *int64    `json:"offset,omitempty"`
	Size      int64     `json:"size,omitempty"`
	ChunkSize int64     `json:"chunk_size,omitempty"`
	Done      bool      `json:"done,omitempty"`
	Game      *gameJSON `json:"game,omitempty"`
	Error     string    `json:"error,omitempty"`
}

type gameJSON struct {
	ID    string `json:"id"`
	Title string `json:"title"`
}

func writeUploadJSON(w http.ResponseWriter, status int, v uploadJSON) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(v)
}

func (s *Server) uploadFail(w http.ResponseWriter, r *http.Request, err error) {
	var he *hub.Error
	var mbe *http.MaxBytesError
	switch {
	case errors.As(err, &mbe):
		writeUploadJSON(w, http.StatusRequestEntityTooLarge, uploadJSON{Error: "The chunk is too large."})
	case errors.As(err, &he):
		st := http.StatusBadRequest
		switch he.Code {
		case hub.CodeNotFound:
			st = http.StatusNotFound
		case hub.CodeConflict:
			st = http.StatusConflict
		case hub.CodePayloadTooLarge:
			st = http.StatusRequestEntityTooLarge
		case hub.CodeRateLimited:
			st = http.StatusTooManyRequests
		}
		msg := he.Message
		if he.Code == hub.CodeConflict && he.ExistingGameID != "" {
			msg = "This ROM is already in the library."
		}
		writeUploadJSON(w, st, uploadJSON{Error: msg})
	default:
		s.log.Error("error", "method", r.Method, "path", r.URL.Path, "err", err)
		writeUploadJSON(w, http.StatusInternalServerError, uploadJSON{Error: "Internal error"})
	}
}

func (s *Server) uploadCreate(w http.ResponseWriter, r *http.Request, sess *session) {
	var in struct {
		Filename string `json:"filename"`
		Size     int64  `json:"size"`
		SHA256   string `json:"sha256"`
		Title    string `json:"title"`
		System   string `json:"system"`
	}
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, maxFormBytes)).Decode(&in); err != nil {
		writeUploadJSON(w, http.StatusBadRequest, uploadJSON{Error: "Invalid request"})
		return
	}
	// The web uploader always sends the SHA-256 of the file (computed in the browser): a session is resumed by hash
	// only, never by file name and size, so a different file with the same name and size cannot continue it.
	if !hub.ValidSHA256(in.SHA256) {
		writeUploadJSON(w, http.StatusBadRequest, uploadJSON{Error: "The file's SHA-256 is missing or invalid."})
		return
	}
	if s.cfg.MaxUploadBytes > 0 && in.Size > s.cfg.MaxUploadBytes {
		writeUploadJSON(w, http.StatusRequestEntityTooLarge, uploadJSON{Error: "The file is too large (maximum " + humanBytes(s.cfg.MaxUploadBytes) + ")."})
		return
	}
	u, err := s.svc.CreateUpload(r.Context(), sess.User.ID, hub.UploadInput{Filename: in.Filename, Size: in.Size,
		SHA256: in.SHA256, Title: in.Title, System: in.System})
	if err != nil {
		s.uploadFail(w, r, err)
		return
	}
	writeUploadJSON(w, http.StatusOK, uploadJSON{UploadID: u.ID, Offset: &u.Offset, Size: u.Size, ChunkSize: hub.UploadChunkBytes})
}

func (s *Server) uploadStatus(w http.ResponseWriter, r *http.Request, sess *session) {
	u, err := s.svc.GetUpload(r.Context(), sess.User.ID, r.PathValue("id"))
	if err != nil {
		s.uploadFail(w, r, err)
		return
	}
	writeUploadJSON(w, http.StatusOK, uploadJSON{UploadID: u.ID, Offset: &u.Offset, Size: u.Size})
}

var webRangeRe = regexp.MustCompile(`^bytes ([0-9]{1,19})-([0-9]{1,19})/([0-9]{1,19})$`)

func (s *Server) uploadChunk(w http.ResponseWriter, r *http.Request, sess *session) {
	m := webRangeRe.FindStringSubmatch(r.Header.Get("Content-Range"))
	if m == nil {
		writeUploadJSON(w, http.StatusBadRequest, uploadJSON{Error: "Content-Range missing or invalid"})
		return
	}
	a, e1 := strconv.ParseInt(m[1], 10, 64)
	b, e2 := strconv.ParseInt(m[2], 10, 64)
	total, e3 := strconv.ParseInt(m[3], 10, 64)
	if e1 != nil || e2 != nil || e3 != nil {
		writeUploadJSON(w, http.StatusBadRequest, uploadJSON{Error: "Content-Range invalid"})
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, hub.UploadChunkBytes+1)
	u, g, err := s.svc.AppendUpload(r.Context(), sess.User.ID, r.PathValue("id"), hub.UploadChunk{Start: a, End: b, Total: total}, r.Body)
	if err != nil {
		s.uploadFail(w, r, err)
		return
	}
	out := uploadJSON{UploadID: u.ID, Offset: &u.Offset, Size: u.Size}
	if g != nil {
		out.Done, out.Game = true, &gameJSON{ID: g.ID, Title: g.Title}
	}
	writeUploadJSON(w, http.StatusOK, out)
}

func (s *Server) uploadAbort(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.AbortUpload(r.Context(), sess.User.ID, r.PathValue("id")); err != nil && !errors.Is(err, hub.ErrNotFound) {
		s.uploadFail(w, r, err)
		return
	}
	writeUploadJSON(w, http.StatusOK, uploadJSON{})
}
