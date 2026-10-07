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

// saveRow is one game (of one user) in the list; its slots are the tabs of the detail.
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

// saveHistoryView is one row of the history timeline. Kind is "current", "snapshot" or "" (other).
type saveHistoryView struct {
	Version      int
	Device, Meta string
	Current      bool
	Snapshot     bool
	Kind         string
	Label        string
	DownloadHref string
	RestoreHref  string
	ConfirmHref  string // GET: the inline confirmation (fragment with htmx, page without)
	CancelHref   string
	CancelXHR    string // fragment URL that answers an empty body
	Open         bool   // the restore confirmation is shown under this row
	ExpectedRev  int    // current revision the restore is based on
	Slot         string
}

// saveTabView is a slot tab.
type saveTabView struct {
	Name, Href, Meta string
	Selected         bool
	Conflict         bool
}

type saveDetailView struct {
	Title, User, System string
	Conflict            bool // any slot of the game has an open conflict
	Tabs                []saveTabView
	Slot                string
	SnapshotHref        string
	ExpectedRev         int
	Current             saveCheckpointView
	Conflicts           []saveConflictView
	History             []saveHistoryView
	HistoryEmpty        string
	Snapshots           bool // filter "Snapshots" active
	AllHref, SnapHref   string
	Confirm             int // version whose restore confirmation is open (0 = none)
	Retention           string
	RefreshURL          string
}

type savesBody struct {
	Users      []userOpt
	UserFilter string
	Rows       []saveRow
	Detail     *saveDetailView
	// ListURL is the URL the list region reloads itself from; OOB marks the list as an out-of-band part of a detail fragment.
	ListURL string
	OOB     bool
}

var syncReasonLabels = map[string]string{
	hub.SyncCheckpoint:      "Auto checkpoint",
	hub.SyncFinal:           "Final sync",
	hub.SyncFinalSessionEnd: "Session end",
	hub.SyncRestore:         "Restored",
}

var historyReasonLabels = map[string]string{
	hub.HistorySessionEnd:       "Session end",
	hub.HistoryDeviceChange:     "Device change",
	hub.HistoryBeforeResolution: "Before conflict resolution",
	hub.HistoryConflictUpload:   "Conflict upload",
	hub.HistoryManualSnapshot:   "Manual snapshot",
	hub.HistoryBeforeRestore:    "Before restore",
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

// savesURL builds a Saves URL: path plus the user filter, the history filter and an open restore confirmation.
func savesURL(path, user, hist string, confirm int) string {
	v := url.Values{}
	if user != "" {
		v.Set("user", user)
	}
	if hist != "" {
		v.Set("hist", hist)
	}
	if confirm > 0 {
		v.Set("confirm", strconv.Itoa(confirm))
	}
	if len(v) == 0 {
		return path
	}
	return path + "?" + v.Encode()
}

func cancelParam(user, hist string) string {
	if user == "" && hist == "" {
		return "?cancel=1"
	}
	return "&cancel=1"
}

func plural(n int, one, many string) string {
	if n == 1 {
		return "1 " + one
	}
	return strconv.Itoa(n) + " " + many
}

// saveGroup is the slots of one game of one user, newest and conflicted first.
type saveGroup struct {
	user, game string
	slots      []hub.SaveSummary
}

func (s *Server) savesBody(r *http.Request) (savesBody, error) {
	ctx := r.Context()
	q := r.URL.Query()
	filter, hist := q.Get("user"), q.Get("hist")
	if hist != "snapshots" {
		hist = ""
	}
	confirm, _ := strconv.Atoi(q.Get("confirm"))
	users, err := s.svc.ListUsers(ctx)
	if err != nil {
		return savesBody{}, err
	}
	b := savesBody{UserFilter: filter, ListURL: savesURL(r.URL.Path, filter, "", 0)}
	for _, u := range users {
		b.Users = append(b.Users, userOpt{u.ID, u.DisplayName})
	}
	rows, err := s.svc.ListSaveSlots(ctx, filter)
	if err != nil {
		return savesBody{}, err
	}
	now := s.svc.Now()
	var groups []*saveGroup
	byKey := map[string]*saveGroup{}
	for _, row := range rows {
		k := row.UserID + "/" + row.GameID
		g := byKey[k]
		if g == nil {
			g = &saveGroup{user: row.UserID, game: row.GameID}
			byKey[k] = g
			groups = append(groups, g)
		}
		g.slots = append(g.slots, row)
	}
	selUser, selGame, selSlot := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	var sel *saveGroup
	for _, g := range groups {
		if g.user == selUser && g.game == selGame {
			sel = g
		}
	}
	if sel == nil && selUser == "" && len(groups) > 0 {
		sel = groups[0]
	}
	selName := ""
	if sel != nil {
		for _, sl := range sel.slots {
			if sl.Slot == selSlot {
				selName = sl.Slot
			}
		}
		if selName == "" && selSlot == "" {
			selName = sel.slots[0].Slot
		}
		if selName == "" {
			sel = nil // unknown slot
		}
	}
	multiUser := len(users) > 1
	for _, g := range groups {
		first := g.slots[0]
		conflicts, latest := 0, first.Current.CreatedAt
		for _, sl := range g.slots {
			conflicts += sl.OpenConflictCount
			if sl.Current.CreatedAt.After(latest) {
				latest = sl.Current.CreatedAt
			}
		}
		v := saveRow{Href: savesURL(saveHref(g.user, g.game, first.Slot), filter, "", 0), Title: first.GameTitle, Initial: initial(first.GameTitle),
			Conflict: conflicts > 0, Selected: sel == g}
		if conflicts > 0 {
			v.Line = plural(conflicts, "conflict", "conflicts")
		} else {
			v.Line = "Synced · " + stamp(latest, now)
			if len(g.slots) > 1 {
				v.Line += fmt.Sprintf(" · %d slots", len(g.slots))
			}
		}
		if multiUser {
			v.User = first.Username
		}
		b.Rows = append(b.Rows, v)
	}
	if sel != nil {
		d, err := s.saveDetail(r, sel, selName, filter, hist, confirm, now)
		if err != nil {
			return savesBody{}, err
		}
		b.Detail = d
	}
	return b, nil
}

// retentionText describes the history retention rules in effect (thinSlot in the service).
func (s *Server) retentionText() string {
	recent, daily, weekly := s.svc.SaveRetention()
	const kept = "Manual snapshots and versions that belong to an open conflict are always kept. "
	if recent <= 0 {
		return kept + "Thinning is off: every other history version is kept as well."
	}
	day, week := "of every day", "of every week"
	if daily > 0 {
		day = fmt.Sprintf("of each day for %d days", daily)
	}
	if weekly > 0 {
		week = fmt.Sprintf("of each week for %d weeks", weekly)
	}
	return fmt.Sprintf("%sOther history versions (session ends, device changes, checkpoints before a resolution or restore): the newest %d are kept, "+
		"plus the newest version %s and %s. Everything else is deleted for good, after each new version and in a daily sweep.", kept, recent, day, week)
}

func (s *Server) saveDetail(r *http.Request, grp *saveGroup, slotName, filter, hist string, confirm int, now time.Time) (*saveDetailView, error) {
	ctx := r.Context()
	first := grp.slots[0]
	d := &saveDetailView{Title: first.GameTitle, User: first.Username, Slot: slotName, Snapshots: hist == "snapshots", Retention: s.retentionText()}
	if g, err := s.svc.GetGame(ctx, grp.game); err == nil {
		d.System = g.System
		if systems, err := s.svc.Systems(ctx); err == nil {
			for _, sys := range systems {
				if sys.ID == g.System {
					d.System = sys.Name
				}
			}
		}
	}
	var selHist []hub.SaveVersion
	for _, sl := range grp.slots {
		h, err := s.svc.ListSaveHistory(ctx, grp.user, grp.game, sl.Slot)
		if err != nil {
			return nil, err
		}
		if sl.Slot == slotName {
			selHist = h
		}
		t := saveTabView{Name: sl.Slot, Href: savesURL(saveHref(grp.user, grp.game, sl.Slot), filter, "", 0), Selected: sl.Slot == slotName,
			Conflict: sl.OpenConflictCount > 0, Meta: plural(len(h), "version", "versions")}
		if t.Conflict {
			t.Meta = plural(sl.OpenConflictCount, "conflict", "conflicts")
			d.Conflict = true
		}
		d.Tabs = append(d.Tabs, t)
	}
	slot, err := s.svc.GetSaveSlot(ctx, grp.user, grp.game, slotName)
	if err != nil {
		return nil, err
	}
	href := saveHref(grp.user, grp.game, slotName)
	d.RefreshURL = savesURL(href, filter, hist, 0)
	d.AllHref, d.SnapHref = savesURL(href, filter, "", 0), savesURL(href, filter, "snapshots", 0)
	cur := slot.Current
	d.SnapshotHref, d.ExpectedRev = href+"/snapshots", cur.Revision
	d.Current = saveCheckpointView{Rev: cur.Revision, Device: cur.DeviceName, When: stamp(cur.CreatedAt, now), Reason: syncReasonLabels[cur.Reason],
		Size: humanBytes(cur.Size), ShortHash: shortHash(cur.SHA256), DownloadHref: href + "/download"}
	for _, c := range slot.OpenConflicts {
		d.Conflicts = append(d.Conflicts, saveConflictView{
			Title: fmt.Sprintf("▲ Conflict: upload is based on Rev %d, current is Rev %d", c.Secured.BaseRevision, c.Hub.Revision),
			Hub: saveSideView{Label: "Current checkpoint on the Hub", Heading: fmt.Sprintf("Rev %d", c.Hub.Revision), Device: c.Hub.DeviceName,
				When: stamp(c.Hub.CreatedAt, now), Base: "–", ShortHash: shortHash(c.Hub.SHA256)},
			Local: saveSideView{Label: "Secured upload · sync pending", Heading: fmt.Sprintf("Local save (v%d)", c.Secured.Version), Device: c.Secured.DeviceName,
				When: stamp(c.Secured.CreatedAt, now), Base: fmt.Sprintf("Rev %d", c.Secured.BaseRevision), ShortHash: shortHash(c.Secured.SHA256)},
			ResolveHref: href + "/conflicts/" + url.PathEscape(c.ID) + "/resolve", ExpectedRev: c.Hub.Revision, KeepHref: savesURL(href, filter, hist, 0),
		})
	}
	for _, v := range selHist {
		meta := stamp(v.CreatedAt, now) + " · " + historyReasonLabels[v.Reason]
		if v.Reason == hub.HistoryConflictUpload && v.BaseRevision != nil {
			meta += fmt.Sprintf(" · based on Rev %d", *v.BaseRevision)
		} else {
			meta += fmt.Sprintf(" · Rev %d", v.Revision)
		}
		vs := strconv.Itoa(v.Version)
		hv := saveHistoryView{Version: v.Version, Device: v.DeviceName, Meta: meta,
			Current:      v.Reason != hub.HistoryConflictUpload && v.Revision == cur.Revision,
			Snapshot:     v.Reason == hub.HistoryManualSnapshot,
			DownloadHref: href + "/history/" + vs + "/download",
			RestoreHref:  href + "/history/" + vs + "/restore",
			ConfirmHref:  savesURL(href, filter, hist, v.Version),
			CancelHref:   savesURL(href, filter, hist, 0), CancelXHR: savesURL(href, filter, hist, 0) + cancelParam(filter, hist), ExpectedRev: cur.Revision, Slot: slotName}
		switch {
		case hv.Current:
			hv.Kind = "current"
		case hv.Snapshot:
			hv.Kind = "snapshot"
		}
		if v.Label != nil {
			hv.Label = *v.Label
		}
		if d.Snapshots && !hv.Snapshot && !hv.Current {
			continue
		}
		if confirm == v.Version && !hv.Current {
			d.Confirm, hv.Open = v.Version, true
		}
		d.History = append(d.History, hv)
	}
	d.HistoryEmpty = "No history versions yet."
	if d.Snapshots {
		d.HistoryEmpty = "No snapshots yet."
	}
	return d, nil
}

func (s *Server) savesGet(w http.ResponseWriter, r *http.Request, sess *session) {
	hx := r.Header.Get("HX-Request") == "true"
	inConfirm := hx && strings.HasPrefix(r.Header.Get("HX-Target"), "confirm-v")
	if inConfirm && r.URL.Query().Get("cancel") != "" {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		return
	}
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
	switch {
	case isHX(r, "saves-list"):
		d.Fragment = true
		s.render(w, http.StatusOK, "saves", "saves-list", d)
	case isHX(r, "save-detail") && body.Detail != nil:
		d.Fragment = true
		body.OOB = true
		d.Body = body
		s.render(w, http.StatusOK, "saves", "saves-detail-fragment", d)
	case inConfirm && body.Detail != nil && body.Detail.Confirm > 0:
		d.Fragment = true
		s.render(w, http.StatusOK, "saves", "restore-confirm", d)
	case inConfirm:
		http.NotFound(w, r)
	default:
		s.render(w, http.StatusOK, "saves", "layout", d)
	}
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

func (s *Server) saveRestore(w http.ResponseWriter, r *http.Request, sess *session) {
	u, g, sl := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	ver, err1 := strconv.Atoi(r.PathValue("version"))
	exp, err2 := strconv.Atoi(r.PostFormValue("expected_revision"))
	if err1 != nil {
		http.NotFound(w, r)
		return
	}
	if err2 != nil {
		http.Error(w, "Invalid request", http.StatusBadRequest)
		return
	}
	// The admin has no device: the change is recorded as made by the web interface (hub.WebDeviceID).
	_, err := s.svc.RestoreSaveVersion(r.Context(), hub.RestoreInput{UserID: u, DeviceID: hub.WebDeviceID(sess.User.ID), GameID: g,
		Slot: sl, Version: ver, ExpectedRevision: exp})
	switch {
	case err == nil:
		s.redirect(w, r, saveHref(u, g, sl)+"?ok=restored")
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

func (s *Server) saveSnapshot(w http.ResponseWriter, r *http.Request, _ *session) {
	u, g, sl := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	label := r.PostFormValue("label")
	_, err := s.svc.CreateSaveSnapshot(r.Context(), u, g, sl, &label)
	switch {
	case err == nil:
		s.redirect(w, r, saveHref(u, g, sl)+"?ok=snapshot")
	case errors.Is(err, hub.ErrNotFound):
		http.NotFound(w, r)
	case errors.Is(err, hub.ErrBadRequest):
		s.redirect(w, r, saveHref(u, g, sl)+"?err=label")
	default:
		s.fail(w, r, err)
	}
}
