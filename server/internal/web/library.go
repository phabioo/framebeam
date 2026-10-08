package web

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"

	"github.com/phabioo/framebeam/server/internal/hub"
)

type libRow struct {
	ID, Title, Initial, System, Size, SHA, ShortSHA, Uploader, Added string
	Saves                                                            int
	Conflict                                                         bool
	UploadHref                                                       string // the slot detail of the signed-in user with the upload panel open
}

// uploadHref opens the upload panel of the user's slot (a game without a save of the user starts the slot "default").
func uploadHref(user, game, slot string) string {
	if slot == "" {
		slot = "default"
	}
	return saveHref(user, game, slot) + "?upload=1"
}

type sysOpt struct {
	ID, Name string
	Count    int
}

type libBody struct {
	Count      int
	Used, Free string
	Rows       []libRow
	Systems    []sysOpt
	System     string
	Query      string
	Filtered   bool
	// ImportDir is the import folder for "Rescan folder" (empty: not configured).
	ImportDir string
	// RefreshURL is the URL the live region reloads itself from (keeps the current filter).
	RefreshURL string
}

func (s *Server) libraryBody(ctx context.Context, viewer, system, q string) (libBody, error) {
	games, err := s.svc.ListGames(ctx)
	if err != nil {
		return libBody{}, err
	}
	users, err := s.svc.ListUsers(ctx)
	if err != nil {
		return libBody{}, err
	}
	names := map[string]string{}
	for _, u := range users {
		names[u.ID] = u.Username
	}
	st, err := s.svc.Storage(ctx)
	if err != nil {
		return libBody{}, err
	}
	slots, err := s.svc.ListSaveSlots(ctx, "")
	if err != nil {
		return libBody{}, err
	}
	saves, conflicts := map[string]int{}, map[string]bool{}
	own := map[string]string{} // game -> slot of the viewer that "Upload save" opens ("default" preferred, else the first)
	for _, sl := range slots {
		if sl.UserID == viewer && (own[sl.GameID] == "" || sl.Slot == "default") {
			own[sl.GameID] = sl.Slot
		}
		saves[sl.GameID]++
		if sl.OpenConflictCount > 0 {
			conflicts[sl.GameID] = true
		}
	}
	b := libBody{Count: len(games), Used: humanBytes(st.ROMBytes), Free: humanBytes(st.FreeBytes),
		System: system, Query: q, Filtered: system != "" || q != "", RefreshURL: "/library", ImportDir: s.cfg.ImportDir}
	f := url.Values{}
	if system != "" {
		f.Set("system", system)
	}
	if q != "" {
		f.Set("q", q)
	}
	if len(f) > 0 {
		b.RefreshURL += "?" + f.Encode()
	}
	if st.FreeBytes == 0 {
		b.Free = "– "
	}
	counts := map[string]int{}
	needle := strings.ToLower(strings.TrimSpace(q))
	for _, g := range games {
		counts[g.System]++
		if system != "" && g.System != system {
			continue
		}
		if needle != "" && !strings.Contains(strings.ToLower(g.Title), needle) && !strings.Contains(g.ROMSHA256, needle) {
			continue
		}
		up := names[g.UploadedBy]
		if up == "" {
			up = "–"
		}
		b.Rows = append(b.Rows, libRow{ID: g.ID, Title: g.Title, Initial: initial(g.Title), System: g.System,
			Size: humanBytes(g.ROMSize), SHA: g.ROMSHA256, ShortSHA: shortHash(g.ROMSHA256), Uploader: up,
			Added: g.AddedAt.Local().Format("02.01."), Saves: saves[g.ID], Conflict: conflicts[g.ID], UploadHref: uploadHref(viewer, g.ID, own[g.ID])})
	}
	systems, err := s.svc.Systems(ctx)
	if err != nil {
		return libBody{}, err
	}
	for _, sys := range systems {
		b.Systems = append(b.Systems, sysOpt{ID: sys.ID, Name: sys.Name, Count: counts[sys.ID]})
	}
	return b, nil
}

func (s *Server) renderLibrary(w http.ResponseWriter, r *http.Request, sess *session, status int, system, q, errMsg string) {
	s.renderLibraryFlash(w, r, sess, status, system, q, "", errMsg)
}

func (s *Server) renderLibraryFlash(w http.ResponseWriter, r *http.Request, sess *session, status int, system, q, flash, errMsg string) {
	body, err := s.libraryBody(r.Context(), sess.User.ID, system, q)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	d := s.base(r, sess, "library", "Library")
	d.Body, d.Error = body, errMsg
	if flash != "" {
		d.Flash = flash
	}
	if isHX(r, "library-results") {
		d.Fragment = true
		s.render(w, status, "library", "library-results", d)
		return
	}
	s.render(w, status, "library", "layout", d)
}

func (s *Server) libraryGet(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderLibrary(w, r, sess, http.StatusOK, r.URL.Query().Get("system"), r.URL.Query().Get("q"), "")
}

func readField(p io.Reader) string {
	b, _ := io.ReadAll(io.LimitReader(p, 1024))
	return string(b)
}

func (s *Server) libraryUpload(w http.ResponseWriter, r *http.Request, sess *session) {
	r.Body = http.MaxBytesReader(w, r.Body, s.cfg.MaxUploadBytes+multipartSlack)
	mr, err := r.MultipartReader()
	if err != nil {
		http.Error(w, "Invalid request", http.StatusBadRequest)
		return
	}
	csrfOK := r.Context().Value(keyCSRFChecked) == true
	var title, system string
	var added bool
	for {
		part, err := mr.NextPart()
		if err == io.EOF {
			break
		}
		if err != nil {
			s.uploadError(w, r, sess, err)
			return
		}
		switch part.FormName() {
		case "_csrf":
			if hub.TokenEqual(readField(part), sess.CSRFToken) {
				csrfOK = true
			}
		case "title":
			title = readField(part)
		case "system":
			system = readField(part)
		case "file":
			if !csrfOK {
				http.Error(w, "CSRF check failed", http.StatusForbidden)
				return
			}
			if part.FileName() == "" {
				continue
			}
			if _, err := s.svc.AddROM(r.Context(), part, part.FileName(), title, system, sess.User.ID); err != nil {
				s.uploadError(w, r, sess, err)
				return
			}
			added = true
		}
		part.Close()
	}
	if !csrfOK {
		http.Error(w, "CSRF check failed", http.StatusForbidden)
		return
	}
	if !added {
		s.renderLibrary(w, r, sess, http.StatusBadRequest, "", "", "Please select a ROM file.")
		return
	}
	http.Redirect(w, r, "/library?ok=uploaded", http.StatusSeeOther)
}

func (s *Server) uploadError(w http.ResponseWriter, r *http.Request, sess *session, err error) {
	var mbe *http.MaxBytesError
	var he *hub.Error
	switch {
	case errors.As(err, &mbe):
		s.renderLibrary(w, r, sess, http.StatusRequestEntityTooLarge, "", "",
			"The file is too large (maximum "+humanBytes(s.cfg.MaxUploadBytes)+").")
	case errors.Is(err, hub.ErrConflict):
		s.renderLibrary(w, r, sess, http.StatusConflict, "", "", "This ROM is already in the library.")
	case errors.As(err, &he) && he.Code == hub.CodeBadRequest:
		s.renderLibrary(w, r, sess, http.StatusBadRequest, "", "", he.Message+".")
	default:
		s.fail(w, r, err)
	}
}

func (s *Server) libraryDelete(w http.ResponseWriter, r *http.Request, sess *session) {
	if err := s.svc.DeleteGame(r.Context(), r.PathValue("id")); err != nil && !errors.Is(err, hub.ErrNotFound) {
		s.fail(w, r, err)
		return
	}
	if r.Header.Get("HX-Request") != "true" {
		http.Redirect(w, r, "/library?ok=deleted", http.StatusSeeOther)
		return
	}
	r.Header.Set("HX-Target", "library-results")
	s.renderLibrary(w, r, sess, http.StatusOK, r.PostFormValue("system"), r.PostFormValue("q"), "")
}

// libraryRescan imports the files of the import folder (admin only, like uploads). Source files are only read.
func (s *Server) libraryRescan(w http.ResponseWriter, r *http.Request, sess *session) {
	if s.cfg.ImportDir == "" {
		s.renderLibrary(w, r, sess, http.StatusBadRequest, "", "", "No import folder is configured.")
		return
	}
	sum, err := s.svc.ImportFolder(r.Context(), s.cfg.ImportDir, sess.User.ID, s.cfg.MaxUploadBytes)
	if errors.Is(err, hub.ErrImportDirMissing) {
		s.renderLibrary(w, r, sess, http.StatusBadRequest, "", "", "The import folder "+s.cfg.ImportDir+" does not exist.")
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	flash := fmt.Sprintf("Rescan finished: %d added, %d already in the library, %d unsupported, %d failed.",
		sum.Added, sum.AlreadyHere, sum.Unsupported, len(sum.Failed))
	errMsg := ""
	if len(sum.Failed) > 0 {
		var parts []string
		for i, f := range sum.Failed {
			if i == 5 {
				parts = append(parts, fmt.Sprintf("and %d more", len(sum.Failed)-5))
				break
			}
			parts = append(parts, f.File+": "+f.Reason)
		}
		errMsg = "Not imported: " + strings.Join(parts, "; ") + "."
	}
	s.renderLibraryFlash(w, r, sess, http.StatusOK, "", "", flash, errMsg)
}
