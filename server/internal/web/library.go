package web

import (
	"context"
	"errors"
	"fmt"
	"html/template"
	"io"
	"mime"
	"net/http"
	"net/url"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/rollwrite"
)

type libRow struct {
	ID, Title, Initial, System, Size, SHA, ShortSHA, Uploader string
	Added                                                     When
	GameCode                                                  string // identity read from the ROM header, "" when unknown
	LocalOnly                                                 bool   // no file of the game is stored on the server
	Files                                                     int    // known files of the game
	Owners                                                    string // users who registered a file as local
	LocalLine                                                 string // "Only on Lena-PC · not uploaded"
	Meta                                                      string // one meta line for the card layout
	Saves                                                     int
	Conflict                                                  bool
	UploadHref                                                string // the slot detail of the signed-in user with the upload panel open
	// Panel is "" (row closed), "rename", "delete" or "details": the inline panel under the row.
	Panel string
	// PanelTitle is the value of the rename field; PanelErr the refusal shown in the panel.
	PanelTitle, PanelErr string
	// CSRF, FSystem, FQuery, FSort and FLimit feed the hidden fields of the panel forms; CancelHref reloads the list without the panel.
	CSRF, FSystem, FQuery, FSort, CancelHref string
	FLimit                                   int
}

// libPanel selects the inline panel of one row for libraryBody.
type libPanel struct {
	ID, Kind, Title, Err string
}

// libFilter is the list state of the Library: system chip, search, sort and how many rows are shown.
type libFilter struct {
	System, Query, Sort string
	Limit               int
}

const libPage = 25

var libSorts = []struct{ ID, Label string }{{"name", "Name"}, {"added", "Added"}, {"size", "Size"}}

func (f libFilter) norm() libFilter {
	ok := false
	for _, so := range libSorts {
		ok = ok || so.ID == f.Sort
	}
	if !ok {
		f.Sort = "name"
	}
	if f.Limit < libPage {
		f.Limit = libPage
	}
	return f
}

// URL is the list URL for this state; with (if not nil) is applied to a copy first.
func (f libFilter) URL(with func(*libFilter)) string {
	if with != nil {
		with(&f)
		f = f.norm()
	}
	v := url.Values{}
	if f.System != "" {
		v.Set("system", f.System)
	}
	if f.Query != "" {
		v.Set("q", f.Query)
	}
	if f.Sort != "" && f.Sort != "name" {
		v.Set("sort", f.Sort)
	}
	if f.Limit > libPage {
		v.Set("n", strconv.Itoa(f.Limit))
	}
	if len(v) == 0 {
		return "/library"
	}
	return "/library?" + v.Encode()
}

func libFilterFrom(v url.Values) libFilter {
	n, _ := strconv.Atoi(v.Get("n"))
	return libFilter{System: v.Get("system"), Query: v.Get("q"), Sort: v.Get("sort"), Limit: n}.norm()
}

// libFilterForm reads the same state from the hidden fields of a panel form.
func libFilterForm(r *http.Request) libFilter {
	n, _ := strconv.Atoi(r.PostFormValue("n"))
	return libFilter{System: r.PostFormValue("system"), Query: r.PostFormValue("q"), Sort: r.PostFormValue("sort"), Limit: n}.norm()
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
	Href     string
	Active   bool
}

type sortOpt struct {
	ID, Label, Href string
	Active          bool
}

// libScan is the "Scanning import folder" card and the result of the last scan.
type libScan struct {
	Running    bool
	Done       int
	Total      int
	Percent    int
	Added      int
	Path       string // middle-ellipsised import path
	FullPath   string
	Result     string // summary of a scan that finished recently
	ResultErr  string
	ResultWarn string
}

type libBody struct {
	Count      int
	Used, Free string
	Rows       []libRow
	Systems    []sysOpt
	AllHref    string
	System     string
	Query      string
	Filtered   bool
	Sort       string
	SortLabel  string
	Sorts      []sortOpt
	Limit      int
	Total      int // rows matching the filter
	Shown      int
	LoadMore   int    // rows the next page adds (0: none left)
	MoreHref   string // loads the next page
	// ImportDir is the import folder for "Rescan folder" (empty: not configured).
	ImportDir   string
	ImportShort string
	Scan        libScan
	// OOB makes the chips and the sort menu out-of-band parts of a results fragment.
	OOB bool
	// RefreshURL is the URL the live region reloads itself from (keeps the current filter).
	RefreshURL string
	// FilterQS is the current filter as "&system=..&q=.." for the row action links.
	FilterQS template.URL // trusted: built by libFilter.URL, so '&' is not re-encoded inside href
}

// middleEllipsis shortens a long path to its first two and its last segment: /srv/uzume/…/library-import.
func middleEllipsis(p string) string {
	if len(p) <= 34 {
		return p
	}
	seg := strings.Split(strings.Trim(p, "/"), "/")
	if len(seg) <= 3 {
		return p
	}
	pre := ""
	if strings.HasPrefix(p, "/") {
		pre = "/"
	}
	return pre + seg[0] + "/" + seg[1] + "/…/" + seg[len(seg)-1]
}

func (s *Server) libraryBody(ctx context.Context, viewer string, f libFilter, panel libPanel, csrf string) (libBody, error) {
	f = f.norm()
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
		names[u.ID] = u.DisplayName
		if names[u.ID] == "" {
			names[u.ID] = u.Username
		}
	}
	owners, err := s.svc.GameOwners(ctx)
	if err != nil {
		return libBody{}, err
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
		System: f.System, Query: f.Query, Filtered: f.System != "" || f.Query != "", Sort: f.Sort, Limit: f.Limit,
		RefreshURL: f.URL(nil), ImportDir: s.cfg.ImportDir, ImportShort: middleEllipsis(s.cfg.ImportDir)}
	if q := strings.TrimPrefix(b.RefreshURL, "/library"); q != "" {
		b.FilterQS = template.URL("&" + strings.TrimPrefix(q, "?"))
	}
	if st.FreeBytes == 0 {
		b.Free = "– "
	}
	for _, so := range libSorts {
		so := so
		b.Sorts = append(b.Sorts, sortOpt{ID: so.ID, Label: so.Label, Active: so.ID == f.Sort,
			Href: f.URL(func(x *libFilter) { x.Sort, x.Limit = so.ID, 0 })})
		if so.ID == f.Sort {
			b.SortLabel = so.Label
		}
	}
	counts := map[string]int{}
	needle := strings.ToLower(strings.TrimSpace(f.Query))
	now := s.svc.Now()
	var matched []hub.Game
	for _, g := range games {
		counts[g.System]++
		if f.System != "" && g.System != f.System {
			continue
		}
		if needle != "" && !strings.Contains(strings.ToLower(g.Title), needle) && !gameHasHash(g, needle) {
			continue
		}
		matched = append(matched, g)
	}
	sort.SliceStable(matched, func(i, j int) bool {
		switch f.Sort {
		case "added":
			return matched[i].AddedAt.After(matched[j].AddedAt)
		case "size":
			return matched[i].ROMSize > matched[j].ROMSize
		}
		return strings.ToLower(matched[i].Title) < strings.ToLower(matched[j].Title)
	})
	b.Total = len(matched)
	if len(matched) > f.Limit {
		matched = matched[:f.Limit]
	}
	b.Shown = len(matched)
	if rest := b.Total - b.Shown; rest > 0 {
		b.LoadMore = min(rest, libPage)
		b.MoreHref = f.URL(func(x *libFilter) { x.Limit += libPage })
	}
	systems, err := s.svc.Systems(ctx)
	if err != nil {
		return libBody{}, err
	}
	sysName := map[string]string{}
	for _, sys := range systems {
		sysName[sys.ID] = sys.Name
	}
	for _, g := range matched {
		up := names[g.UploadedBy]
		if up == "" {
			up = "–"
		}
		var on []string
		for _, id := range owners[g.ID] {
			if n := names[id]; n != "" {
				on = append(on, n)
			}
		}
		sn := sysName[g.System]
		if sn == "" {
			sn = g.System
		}
		row := libRow{ID: g.ID, Title: g.Title, Initial: initial(g.Title), System: sn,
			Size: humanBytes(g.ROMSize), SHA: g.ROMSHA256, ShortSHA: shortHash(g.ROMSHA256), Uploader: up,
			Added: when(g.AddedAt, now), GameCode: g.GameCode, LocalOnly: !g.OnHub, Files: len(g.ROMs), Owners: strings.Join(on, ", "),
			Saves: saves[g.ID], Conflict: conflicts[g.ID], UploadHref: uploadHref(viewer, g.ID, own[g.ID])}
		if row.LocalOnly {
			row.LocalLine = "Not uploaded to the server"
			if row.Owners != "" {
				row.LocalLine = "Only on " + row.Owners + " · not uploaded"
			}
		}
		row.Meta = row.System + " · " + row.Size + " · " + row.Added.Text
		if panel.ID == g.ID && (panel.Kind == "rename" || panel.Kind == "delete" || panel.Kind == "details") {
			row.Panel, row.PanelErr = panel.Kind, panel.Err
			row.CSRF, row.FSystem, row.FQuery, row.FSort, row.FLimit, row.CancelHref = csrf, f.System, f.Query, f.Sort, f.Limit, b.RefreshURL
			row.PanelTitle = g.Title
			if panel.Title != "" {
				row.PanelTitle = panel.Title
			}
		}
		b.Rows = append(b.Rows, row)
	}
	b.AllHref = f.URL(func(x *libFilter) { x.System, x.Limit = "", 0 })
	for _, sys := range systems {
		sys := sys
		b.Systems = append(b.Systems, sysOpt{ID: sys.ID, Name: sys.Name, Count: counts[sys.ID], Active: f.System == sys.ID,
			Href: f.URL(func(x *libFilter) { x.System, x.Limit = sys.ID, 0 })})
	}
	// Folder scan: the running card, or the result of a scan that finished in the last 15 minutes.
	if sp := s.svc.ScanProgress(); sp.Running {
		b.Scan = libScan{Running: true, Done: sp.Done, Total: sp.Total, Added: sp.Added, Path: b.ImportShort, FullPath: b.ImportDir}
		if sp.Total > 0 {
			b.Scan.Percent = sp.Done * 100 / sp.Total
		}
	} else if sp.Finished && now.Sub(sp.FinishedAt) < 15*time.Minute {
		sum := sp.Summary
		b.Scan.Result = fmt.Sprintf("Rescan finished: %d added, %d already in the library, %d unsupported, %d failed.",
			sum.Added, sum.AlreadyHere, sum.Unsupported, len(sum.Failed))
		b.Scan.ResultErr = sp.LastError
		if len(sum.Failed) > 0 {
			var parts []string
			for i, fl := range sum.Failed {
				if i == 5 {
					parts = append(parts, fmt.Sprintf("and %d more", len(sum.Failed)-5))
					break
				}
				parts = append(parts, fl.File+": "+fl.Reason)
			}
			b.Scan.ResultWarn = "Not imported: " + strings.Join(parts, "; ") + "."
		}
	}
	return b, nil
}

// gameHasHash reports whether the (lowercase) needle is part of the SHA-256 of any file of the game.
func gameHasHash(g hub.Game, needle string) bool {
	if strings.Contains(g.ROMSHA256, needle) {
		return true
	}
	for _, f := range g.ROMs {
		if strings.Contains(f.SHA256, needle) {
			return true
		}
	}
	return false
}

func (s *Server) renderLibrary(w http.ResponseWriter, r *http.Request, sess *session, status int, f libFilter, errMsg string) {
	s.renderLibraryFlash(w, r, sess, status, f, "", errMsg)
}

func (s *Server) renderLibraryFlash(w http.ResponseWriter, r *http.Request, sess *session, status int, f libFilter, flash, errMsg string) {
	s.renderLibraryPanel(w, r, sess, status, f, flash, errMsg, libPanel{})
}

func (s *Server) renderLibraryPanel(w http.ResponseWriter, r *http.Request, sess *session, status int, f libFilter, flash, errMsg string, panel libPanel) {
	body, err := s.libraryBody(r.Context(), sess.User.ID, f, panel, sess.CSRFToken)
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
		body.OOB = true
		d.Body = body
		s.render(w, status, "library", "library-results", d)
		return
	}
	s.render(w, status, "library", "layout", d)
}

func (s *Server) libraryGet(w http.ResponseWriter, r *http.Request, sess *session) {
	qv := r.URL.Query()
	panel := libPanel{}
	switch {
	case qv.Get("rename") != "":
		panel = libPanel{ID: qv.Get("rename"), Kind: "rename"}
	case qv.Get("delete") != "":
		panel = libPanel{ID: qv.Get("delete"), Kind: "delete"}
	case qv.Get("details") != "":
		panel = libPanel{ID: qv.Get("details"), Kind: "details"}
	}
	s.renderLibraryPanel(w, r, sess, http.StatusOK, libFilterFrom(qv), "", "", panel)
}

// libraryRename changes the display title (admin only). A refused title keeps the inline form open with the reason.
func (s *Server) libraryRename(w http.ResponseWriter, r *http.Request, sess *session) {
	id, f := r.PathValue("id"), libFilterForm(r)
	title := r.PostFormValue("title")
	_, err := s.svc.RenameGame(r.Context(), id, title)
	var he *hub.Error
	switch {
	case err == nil, errors.Is(err, hub.ErrNotFound):
		if r.Header.Get("HX-Request") != "true" {
			http.Redirect(w, r, "/library?ok=renamed", http.StatusSeeOther)
			return
		}
		r.Header.Set("HX-Target", "library-results")
		s.renderLibraryPanel(w, r, sess, http.StatusOK, f, "Title changed.", "", libPanel{})
	case errors.As(err, &he) && he.Code == hub.CodeBadRequest:
		r.Header.Set("HX-Target", "library-results")
		s.renderLibraryPanel(w, r, sess, http.StatusBadRequest, f, "", "",
			libPanel{ID: id, Kind: "rename", Title: title, Err: he.Message + "."})
	default:
		s.fail(w, r, err)
	}
}

// libraryDownload sends the ROM file stored on the server (admin only).
func (s *Server) libraryDownload(w http.ResponseWriter, r *http.Request, _ *session) {
	g, err := s.svc.GetGame(r.Context(), r.PathValue("id"))
	if err == nil && !g.OnHub {
		err = hub.ErrNotFound
	}
	if errors.Is(err, hub.ErrNotFound) {
		s.notFound(w, r)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	f, stored, err := s.svc.OpenROM(r.Context(), g.ROMSHA256)
	if errors.Is(err, hub.ErrNotFound) {
		s.notFound(w, r)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	defer f.Close()
	name := stored.Filename
	if name == "" {
		name = g.Filename
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	cd := mime.FormatMediaType("attachment", map[string]string{"filename": name})
	if cd == "" {
		cd = "attachment"
	}
	w.Header().Set("Content-Disposition", cd)
	rw, done := rollwrite.Wrap(w) // a client that stops reading must not hold the connection for hours
	defer done()
	http.ServeContent(rw, r, "", stored.AddedAt, f)
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
		s.renderLibrary(w, r, sess, http.StatusBadRequest, libFilter{}, "Please select a ROM file.")
		return
	}
	http.Redirect(w, r, "/library?ok=uploaded", http.StatusSeeOther)
}

func (s *Server) uploadError(w http.ResponseWriter, r *http.Request, sess *session, err error) {
	var mbe *http.MaxBytesError
	var he *hub.Error
	switch {
	case errors.As(err, &mbe):
		s.renderLibrary(w, r, sess, http.StatusRequestEntityTooLarge, libFilter{},
			"The file is too large (maximum "+humanBytes(s.cfg.MaxUploadBytes)+").")
	case errors.Is(err, hub.ErrConflict):
		msg := "This ROM is already in the library. Nothing was added."
		if errors.As(err, &he) && he.ExistingGameID != "" {
			if g, gerr := s.svc.GetGame(r.Context(), he.ExistingGameID); gerr == nil {
				msg = "This file has the same SHA-256 as " + g.Title + ". Nothing was added."
			}
		}
		s.renderLibrary(w, r, sess, http.StatusConflict, libFilter{}, msg)
	case errors.As(err, &he) && he.Code == hub.CodeBadRequest:
		s.renderLibrary(w, r, sess, http.StatusBadRequest, libFilter{}, he.Message+".")
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
	s.renderLibrary(w, r, sess, http.StatusOK, libFilterForm(r), "")
}

// libraryRescan starts the import of the import folder in the background (admin only, like uploads) and shows the
// progress card. Source files are only read. Only one scan runs at a time.
func (s *Server) libraryRescan(w http.ResponseWriter, r *http.Request, sess *session) {
	if s.cfg.ImportDir == "" {
		s.renderLibrary(w, r, sess, http.StatusBadRequest, libFilter{}, "No import folder is configured.")
		return
	}
	if st, err := os.Stat(s.cfg.ImportDir); err != nil || !st.IsDir() {
		s.renderLibrary(w, r, sess, http.StatusBadRequest, libFilter{}, "The import folder "+s.cfg.ImportDir+" does not exist.")
		return
	}
	if !s.svc.BeginScan() {
		http.Redirect(w, r, "/library", http.StatusSeeOther)
		return
	}
	dir, user, maxBytes := s.cfg.ImportDir, sess.User.ID, s.cfg.MaxUploadBytes
	ctx := context.WithoutCancel(r.Context())
	go func() {
		sum, err := s.svc.ImportFolder(ctx, dir, user, maxBytes)
		s.svc.EndScan(sum, err)
	}()
	http.Redirect(w, r, "/library?ok=rescan", http.StatusSeeOther)
}
