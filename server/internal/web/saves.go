package web

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
)

type saveRow struct {
	Href, Title, Initial, Line string
	User                       string
	Conflict, Selected         bool
}

type saveCheckpointView struct {
	Rev                  int
	Device, When, Reason string
	Size, ShortHash      string
	DownloadHref         string
}

type saveSideView struct {
	Label, Heading, Device, When, Base, ShortHash string
}

type saveConflictView struct {
	Title       string
	Hub, Local  saveSideView
	ResolveHref string
	ExpectedRev int
	KeepHref    string
}

type saveHistoryView struct {
	Version      int
	Device, Meta string
	Current      bool
	DownloadHref string
}

type saveDetailView struct {
	Title, User string
	Current     saveCheckpointView
	Conflicts   []saveConflictView
	History     []saveHistoryView
}

type savesBody struct {
	Users      []userOpt
	UserFilter string
	Rows       []saveRow
	Detail     *saveDetailView
}

var syncReasonLabels = map[string]string{
	hub.SyncCheckpoint:      "Auto checkpoint",
	hub.SyncFinal:           "Final sync",
	hub.SyncFinalSessionEnd: "Session end",
}

var historyReasonLabels = map[string]string{
	hub.HistorySessionEnd:       "Session end",
	hub.HistoryDeviceChange:     "Device change",
	hub.HistoryBeforeResolution: "Before conflict resolution",
	hub.HistoryConflictUpload:   "Conflict upload",
	hub.HistoryManualSnapshot:   "Manual snapshot",
}

// stamp formats a time as "today 19:10", "yesterday 18:05", "10-03 21:12" or "2025-10-03 21:12".
func stamp(t, now time.Time) string {
	lt, ln := t.Local(), now.Local()
	switch {
	case lt.Format("2006-01-02") == ln.Format("2006-01-02"):
		return "today " + lt.Format("15:04")
	case lt.AddDate(0, 0, 1).Format("2006-01-02") == ln.Format("2006-01-02"):
		return "yesterday " + lt.Format("15:04")
	case lt.Year() == ln.Year():
		return lt.Format("01-02 15:04")
	}
	return lt.Format("2006-01-02 15:04")
}

func saveHref(userID, gameID, slot string) string {
	return "/saves/" + url.PathEscape(userID) + "/" + url.PathEscape(gameID) + "/" + url.PathEscape(slot)
}

func (s *Server) savesBody(r *http.Request) (savesBody, error) {
	ctx := r.Context()
	filter := r.URL.Query().Get("user")
	users, err := s.svc.ListUsers(ctx)
	if err != nil {
		return savesBody{}, err
	}
	b := savesBody{UserFilter: filter}
	for _, u := range users {
		b.Users = append(b.Users, userOpt{u.ID, u.DisplayName})
	}
	rows, err := s.svc.ListSaveSlots(ctx, filter)
	if err != nil {
		return savesBody{}, err
	}
	now := s.svc.Now()
	selUser, selGame, selSlot := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	var sel *hub.SaveSummary
	q := ""
	if filter != "" {
		q = "?user=" + url.QueryEscape(filter)
	}
	for i, row := range rows {
		if row.UserID == selUser && row.GameID == selGame && row.Slot == selSlot {
			sel = &rows[i]
		}
	}
	if sel == nil && selUser == "" && len(rows) > 0 {
		sel = &rows[0]
	}
	multiUser := len(users) > 1
	for _, row := range rows {
		v := saveRow{Href: saveHref(row.UserID, row.GameID, row.Slot) + q, Title: row.GameTitle, Initial: initial(row.GameTitle),
			Conflict: row.OpenConflictCount > 0, Selected: sel != nil && sel.UserID == row.UserID && sel.GameID == row.GameID && sel.Slot == row.Slot}
		if v.Conflict {
			v.Line = "▲ Conflict · unresolved"
		} else {
			v.Line = "Checkpoint · " + stamp(row.Current.CreatedAt, now)
		}
		if multiUser {
			v.User = row.Username
		}
		b.Rows = append(b.Rows, v)
	}
	if sel != nil {
		d, err := s.saveDetail(r, *sel, q, now)
		if err != nil {
			return savesBody{}, err
		}
		b.Detail = d
	}
	return b, nil
}

func (s *Server) saveDetail(r *http.Request, sel hub.SaveSummary, q string, now time.Time) (*saveDetailView, error) {
	ctx := r.Context()
	slot, err := s.svc.GetSaveSlot(ctx, sel.UserID, sel.GameID, sel.Slot)
	if err != nil {
		return nil, err
	}
	hist, err := s.svc.ListSaveHistory(ctx, sel.UserID, sel.GameID, sel.Slot)
	if err != nil {
		return nil, err
	}
	href := saveHref(sel.UserID, sel.GameID, sel.Slot)
	cur := slot.Current
	d := &saveDetailView{Title: sel.GameTitle, User: sel.Username, Current: saveCheckpointView{Rev: cur.Revision,
		Device: cur.DeviceName, When: stamp(cur.CreatedAt, now), Reason: syncReasonLabels[cur.Reason], Size: humanBytes(cur.Size),
		ShortHash: shortHash(cur.SHA256), DownloadHref: href + "/download"}}
	for _, c := range slot.OpenConflicts {
		d.Conflicts = append(d.Conflicts, saveConflictView{
			Title: fmt.Sprintf("▲ Conflict: upload is based on Rev %d, current is Rev %d", c.Secured.BaseRevision, c.Hub.Revision),
			Hub: saveSideView{Label: "Current checkpoint on the Hub", Heading: fmt.Sprintf("Rev %d", c.Hub.Revision), Device: c.Hub.DeviceName,
				When: stamp(c.Hub.CreatedAt, now), Base: "–", ShortHash: shortHash(c.Hub.SHA256)},
			Local: saveSideView{Label: "Secured upload · sync pending", Heading: fmt.Sprintf("Local save (v%d)", c.Secured.Version), Device: c.Secured.DeviceName,
				When: stamp(c.Secured.CreatedAt, now), Base: fmt.Sprintf("Rev %d", c.Secured.BaseRevision), ShortHash: shortHash(c.Secured.SHA256)},
			ResolveHref: href + "/conflicts/" + url.PathEscape(c.ID) + "/resolve", ExpectedRev: c.Hub.Revision, KeepHref: href + q,
		})
	}
	for _, v := range hist {
		meta := stamp(v.CreatedAt, now) + " · " + historyReasonLabels[v.Reason]
		if v.Reason == hub.HistoryConflictUpload && v.BaseRevision != nil {
			meta += fmt.Sprintf(" · based on Rev %d", *v.BaseRevision)
		} else {
			meta += fmt.Sprintf(" · Rev %d", v.Revision)
		}
		d.History = append(d.History, saveHistoryView{Version: v.Version, Device: v.DeviceName, Meta: meta,
			Current:      v.Reason != hub.HistoryConflictUpload && v.Revision == cur.Revision,
			DownloadHref: href + "/history/" + strconv.Itoa(v.Version) + "/download"})
	}
	return d, nil
}

func (s *Server) savesGet(w http.ResponseWriter, r *http.Request, sess *session) {
	body, err := s.savesBody(r)
	if err != nil {
		if errors.Is(err, hub.ErrNotFound) {
			http.NotFound(w, r)
			return
		}
		s.fail(w, r, err)
		return
	}
	if r.PathValue("user") != "" && body.Detail == nil {
		http.NotFound(w, r)
		return
	}
	d := s.base(r, sess, "saves", "Saves")
	d.Body = body
	s.render(w, http.StatusOK, "saves", "layout", d)
}

var unsafeName = regexp.MustCompile(`[^A-Za-z0-9._-]+`)

func (s *Server) downloadName(ctx context.Context, gameID, suffix string) string {
	name := "save"
	if g, err := s.svc.GetGame(ctx, gameID); err == nil {
		name = g.Title
	}
	name = strings.Trim(unsafeName.ReplaceAllString(name, "_"), "_.")
	if name == "" {
		name = "save"
	}
	return name + "-" + suffix + ".sav"
}

func (s *Server) saveDownload(w http.ResponseWriter, r *http.Request, _ *session) {
	u, g, sl := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	f, c, err := s.svc.OpenSaveContent(r.Context(), u, g, sl)
	if err != nil {
		s.downloadErr(w, r, err)
		return
	}
	defer f.Close()
	s.serveSave(w, r, f, c.SHA256, s.downloadName(r.Context(), g, fmt.Sprintf("rev%d", c.Revision)), c.CreatedAt)
}

func (s *Server) saveHistoryDownload(w http.ResponseWriter, r *http.Request, _ *session) {
	u, g, sl := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	ver, err := strconv.Atoi(r.PathValue("version"))
	if err != nil {
		http.NotFound(w, r)
		return
	}
	f, v, err := s.svc.OpenSaveVersion(r.Context(), u, g, sl, ver)
	if err != nil {
		s.downloadErr(w, r, err)
		return
	}
	defer f.Close()
	s.serveSave(w, r, f, v.SHA256, s.downloadName(r.Context(), g, fmt.Sprintf("v%d", v.Version)), v.CreatedAt)
}

func (s *Server) serveSave(w http.ResponseWriter, r *http.Request, f http.File, sha, name string, mod time.Time) {
	h := w.Header()
	h.Set("Content-Type", "application/octet-stream")
	h.Set("Content-Disposition", `attachment; filename="`+name+`"`)
	h.Set("ETag", `"`+sha+`"`)
	http.ServeContent(w, r, name, mod, f)
}

func (s *Server) downloadErr(w http.ResponseWriter, r *http.Request, err error) {
	if errors.Is(err, hub.ErrNotFound) {
		http.NotFound(w, r)
		return
	}
	s.fail(w, r, err)
}

func (s *Server) saveResolve(w http.ResponseWriter, r *http.Request, sess *session) {
	u, g, sl := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	exp, err := strconv.Atoi(r.PostFormValue("expected_revision"))
	if err != nil {
		http.Error(w, "Invalid request", http.StatusBadRequest)
		return
	}
	_, err = s.svc.ResolveSaveConflict(r.Context(), hub.ResolveInput{UserID: u, GameID: g, Slot: sl, ConflictID: r.PathValue("id"),
		Resolution: r.PostFormValue("resolution"), ExpectedRevision: exp, ResolvedBy: "user:" + sess.User.ID})
	switch {
	case err == nil:
		s.redirect(w, r, saveHref(u, g, sl)+"?ok=resolved")
	case errors.Is(err, hub.ErrSaveConflictStale):
		s.redirect(w, r, saveHref(u, g, sl)+"?err=stale")
	case errors.Is(err, hub.ErrNotFound):
		http.NotFound(w, r)
	case errors.Is(err, hub.ErrBadRequest):
		http.Error(w, "Invalid request", http.StatusBadRequest)
	default:
		s.fail(w, r, err)
	}
}
