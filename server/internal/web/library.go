package web

import (
	"context"
	"errors"
	"io"
	"net/http"
	"strings"

	"github.com/phabioo/framebeam/server/internal/hub"
)

type libRow struct {
	ID, Title, Initial, System, Size, SHA, ShortSHA, Uploader, Added string
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
}

func (s *Server) libraryBody(ctx context.Context, system, q string) (libBody, error) {
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
	b := libBody{Count: len(games), Used: humanBytes(st.ROMBytes), Free: humanBytes(st.FreeBytes),
		System: system, Query: q, Filtered: system != "" || q != ""}
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
			Added: g.AddedAt.Local().Format("01-02")})
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
	body, err := s.libraryBody(r.Context(), system, q)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	d := s.base(r, sess, "library", "Library")
	d.Body, d.Error = body, errMsg
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
