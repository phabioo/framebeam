package web

import (
	"context"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"
	"unicode"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/rollwrite"
)

// saveRow is one game (of one user) in the list; its slots are the tabs of the detail.
type saveRow struct {
	Href, Title, Initial, Line string
	User                       string
	Conflict, Selected         bool
}

type saveCheckpointView struct {
	Rev             int
	Label           string // "v6" when the current checkpoint has a history version, else "Rev 6"
	Device, Reason  string
	At              When
	Size, ShortHash string
	DownloadHref    string
}

type saveSideView struct {
	Label, Heading, Device, Base, ShortHash string
	At                                      When
}

type saveConflictView struct {
	Title, Note string
	Hub, Local  saveSideView
	ResolveHref string
	ExpectedRev int
	KeepHref    string
}

// saveHistoryView is one row of the history timeline. Kind is "current", "snapshot" or "" (other).
type saveHistoryView struct {
	Version      int
	Device, Meta string // Meta is the reason line without the date (the date is At)
	At           When
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
	DeleteHref   string // POST: delete the snapshot
	DeleteAsk    string // GET: the inline delete confirmation (snapshots only)
	DeleteOpen   bool   // the delete confirmation is shown under this row
	ExpectedRev  int    // current revision the restore is based on
	Slot         string
	SlotLabel    string
	Blocked      string // why Restore is disabled ("" = possible)
	Menu         bool   // the row has a menu (delete version)
}

// saveTabView is a slot tab.
type saveTabView struct {
	Name, Label, Href, Meta string
	Selected                bool
	Conflict                bool
}

type saveDetailView struct {
	Title, User, System string
	Conflict            bool // any slot of the game has an open conflict
	ConflictN           int  // open conflicts of all slots of the game
	Tabs                []saveTabView
	Slot                string
	SnapshotHref        string
	ExpectedRev         int
	Current             saveCheckpointView
	Conflicts           []saveConflictView
	History             []saveHistoryView
	HistoryEmpty        string
	Running             string // banner: restore is not possible while a Session of this game runs ("" = none)
	SnapOpen            bool   // the snapshot label field is open
	SnapOpenHref        string
	SnapCloseHref       string
	SlotLabel           string
	HistoryCount        int
	Snapshots           bool // filter "Snapshots" active
	AllHref, SnapHref   string
	Confirm             int // version whose restore confirmation is open (0 = none)
	Retention           string
	RefreshURL          string
	NewSlotAsk          string // GET: the inline "New slot" form
	NewSlot             *newSlotView
	CSRF                string // token for the multipart upload form
	Ops                 slotOpsView
	Empty               bool // the user has no save for this game yet: only the upload panel is shown
	Up                  uploadView
}

// slotOpsView is the Rename and Delete row actions of the selected slot (not offered for the default slot).
type slotOpsView struct {
	Show               bool
	Main               bool // the main slot: it can be deleted but not renamed
	Running            bool // a Session of the game runs: the actions are disabled
	RenameOpen         bool
	DeleteOpen         bool
	RenameAsk          string // GET: the slot with the rename form open
	DeleteAsk          string // GET: the slot with the delete confirmation open
	CancelHref         string
	RenameHref         string // POST
	DeleteHref         string // POST
	Value              string // prefilled new name
	Max                int
	Rev                int
	When, Device, Size string // the latest checkpoint the delete confirmation names
	Versions           int    // history versions that go with the slot
}

// uploadView is the upload action of the CURRENT bar: the button, the inline panel and the last result.
type uploadView struct {
	Can       bool   // the viewer owns the slot (admins cannot upload into another user's slot)
	Running   bool   // a Session of this game runs: the button is disabled with a hint
	Open      bool   // the panel is expanded
	OpenHref  string // GET: the slot with the panel open (fragment with htmx, page without)
	CloseHref string
	Action    string // POST (multipart)
	Version   int    // revision of the selected slot's current version (the note and the no-op text)
	Slots     []uploadSlot
	Result    *uploadResult
}

type uploadSlot struct {
	Name     string
	Label    string
	Rev      int
	Selected bool
}

// uploadResult is the inline outcome of an upload. Kind is "ok", "same" or "error".
type uploadResult struct {
	Kind, Text, Sub string
	UndoLabel       string
	UndoHref        string // POST: restore the version the upload replaced
	ExpectedRev     int
}

// newSlotView is the inline "New slot" form. Error is set when a submit was refused.
type newSlotView struct {
	Game, From, FromLabel, Value, Error string
	PostHref, CancelHref, Cancel        string
	Max                                 int
}

type savesBody struct {
	Users      []userOpt
	UserFilter string
	Query      string // search text for games and users
	Total      int    // games in the list before the search
	Filtered   bool   // a search or user filter is active
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
	hub.SyncUpload:          "Uploaded",
}

var historyReasonLabels = map[string]string{
	hub.HistorySessionEnd:       "Session end",
	hub.HistoryDeviceChange:     "Device change",
	hub.HistoryBeforeResolution: "Before conflict resolution",
	hub.HistoryConflictUpload:   "Conflict upload",
	hub.HistoryManualSnapshot:   "Manual snapshot",
	hub.HistoryBeforeRestore:    "Before restore",
	hub.HistoryBeforeUpload:     "Before upload",
}

// slotLabel is the display name of a slot: the Player calls the default slot "Main" (the API name stays "default").
func slotLabel(name string) string {
	if name == hub.DefaultSlot {
		return "Main"
	}
	return name
}

func saveHref(userID, gameID, slot string) string {
	return "/saves/" + url.PathEscape(userID) + "/" + url.PathEscape(gameID) + "/" + url.PathEscape(slot)
}

// sfilt is the list filter of the Saves page: one user and a search text for games and users.
type sfilt struct{ User, Q string }

func (f sfilt) empty() bool { return f.User == "" && f.Q == "" }

// savesURL builds a Saves URL: path plus the list filter, the history filter and an open restore confirmation.
func savesURL(path string, f sfilt, hist string, confirm int) string {
	v := url.Values{}
	if f.User != "" {
		v.Set("user", f.User)
	}
	if f.Q != "" {
		v.Set("q", f.Q)
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

// withParam appends one query parameter to a URL.
func withParam(u, k, v string) string {
	sep := "?"
	if strings.Contains(u, "?") {
		sep = "&"
	}
	return u + sep + url.QueryEscape(k) + "=" + url.QueryEscape(v)
}

func cancelParam(f sfilt, hist string) string {
	if f.empty() && hist == "" {
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

// uploadErrTexts are the inline texts of failed uploads (query key up=...).
var uploadErrTexts = map[string]string{
	"stale":     "The slot changed in the meantime. Reload the page and upload again.",
	"toolarge":  "The save file is too large (maximum 64 MiB).",
	"emptyfile": "Please select a save file that is not empty.",
	"running":   "A Session of this game is running. Upload the save file after it ends.",
}

// webDeviceLabel names the device of a change; changes made in the web interface carry the user.
func webDeviceLabel(name, id string, names map[string]string) string {
	if u, ok := strings.CutPrefix(id, "web:"); ok && names[u] != "" {
		return name + " · " + names[u]
	}
	return name
}

// savesBody collects the page data. viewer is the signed-in user (only the owner of a slot can upload into it).
func (s *Server) savesBody(r *http.Request, nf *newSlotView, viewer string) (savesBody, error) {
	ctx := r.Context()
	q := r.URL.Query()
	filter, hist := sfilt{User: q.Get("user"), Q: strings.TrimSpace(q.Get("q"))}, q.Get("hist")
	if hist != "snapshots" {
		hist = ""
	}
	confirm, _ := strconv.Atoi(q.Get("confirm"))
	del, _ := strconv.Atoi(q.Get("delete"))
	users, err := s.svc.ListUsers(ctx)
	if err != nil {
		return savesBody{}, err
	}
	b := savesBody{UserFilter: filter.User, Query: filter.Q, ListURL: savesURL(r.URL.Path, filter, "", 0)}
	for _, u := range users {
		b.Users = append(b.Users, userOpt{u.ID, u.DisplayName})
	}
	names := map[string]string{}
	for _, u := range users {
		names[u.ID] = u.DisplayName
	}
	rows, err := s.svc.ListSaveSlots(ctx, filter.User)
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
	b.Total = len(groups)
	b.Filtered = !filter.empty()
	if needle := strings.ToLower(filter.Q); needle != "" {
		kept := groups[:0:0]
		for _, g := range groups {
			f0 := g.slots[0]
			if strings.Contains(strings.ToLower(f0.GameTitle), needle) || strings.Contains(strings.ToLower(f0.Username), needle) ||
				strings.Contains(strings.ToLower(names[g.user]), needle) {
				kept = append(kept, g)
			}
		}
		groups = kept
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
			v.Line = "▲ " + plural(conflicts, "conflict", "conflicts")
		} else {
			v.Line = "Synced · " + ago(latest, now)
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
		d, err := s.saveDetail(r, sel, selName, filter, hist, confirm, del, nf, now, viewer, names)
		if err != nil {
			return savesBody{}, err
		}
		b.Detail = d
	} else if selUser != "" && selUser == viewer && q.Get("upload") != "" && hub.ValidSlotName(selSlot) {
		// No save of this game yet (opened from the Library): only the upload panel, which creates the slot.
		if g, err := s.svc.GetGame(ctx, selGame); err == nil {
			href := saveHref(selUser, selGame, selSlot)
			running, err := s.svc.SessionRunningFor(ctx, selUser, selGame)
			if err != nil {
				return savesBody{}, err
			}
			b.Detail = &saveDetailView{Title: g.Title, User: names[selUser], Slot: selSlot, Empty: true, RefreshURL: href + "?upload=1",
				Up: uploadView{Can: true, Running: running, Open: true, Action: href + "/upload", Slots: []uploadSlot{{Name: selSlot, Selected: true}},
					Result: uploadResultFor(q, hub.SaveCheckpoint{}, nil, "")}}
		}
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
	return fmt.Sprintf("%sOther history versions (session ends, device changes, uploads, checkpoints before a resolution, restore or upload): the newest %d are kept, "+
		"plus the newest version %s and %s. Everything else is deleted for good, after each new version and in a daily sweep.", kept, recent, day, week)
}

// uploadFailed reports an upload error result in the query (the panel stays open, the file has to be chosen again).
func uploadFailed(q url.Values) bool {
	_, ok := uploadErrTexts[q.Get("up")]
	return ok || q.Get("up") == "same"
}

// uploadResultFor builds the inline result from the query (up=ok|same|<error key>, name=file name). "ok" is only shown
// while the current version really is an upload; the undo link restores the version that the upload replaced.
func uploadResultFor(q url.Values, cur hub.SaveCheckpoint, hist []hub.SaveVersion, histHref string) *uploadResult {
	switch key := q.Get("up"); {
	case key == "ok" && cur.Reason == hub.SyncUpload:
		res := &uploadResult{Kind: "ok", Text: fmt.Sprintf("✓ Uploaded as Rev %d.", cur.Revision)}
		for _, v := range hist { // newest first
			if v.Reason == hub.HistoryBeforeUpload && v.Revision == cur.Revision-1 {
				res.Sub = fmt.Sprintf("Rev %d is in the history as “Before upload”. Players get Rev %d on their next start.", cur.Revision-1, cur.Revision)
				res.UndoLabel = fmt.Sprintf("Undo: restore v%d", v.Version)
				res.UndoHref = histHref + strconv.Itoa(v.Version) + "/restore"
				return res
			}
		}
		res.Sub = fmt.Sprintf("Players get Rev %d on their next start.", cur.Revision)
		return res
	case key == "same" && cur.Revision > 0:
		name := q.Get("name")
		if name == "" {
			name = "The file"
		}
		return &uploadResult{Kind: "same", Text: "This file matches the current version. Nothing changed.",
			Sub: fmt.Sprintf("%s has the same hash as Rev %d (%s). No new version was created.", name, cur.Revision, shortHash(cur.SHA256))}
	case uploadErrTexts[key] != "":
		return &uploadResult{Kind: "error", Text: uploadErrTexts[key]}
	}
	return nil
}

func (s *Server) saveDetail(r *http.Request, grp *saveGroup, slotName string, filter sfilt, hist string, confirm, del int, nf *newSlotView, now time.Time, viewer string, names map[string]string) (*saveDetailView, error) {
	ctx := r.Context()
	first := grp.slots[0]
	d := &saveDetailView{Title: first.GameTitle, User: first.Username, Slot: slotName, SlotLabel: slotLabel(slotName), Snapshots: hist == "snapshots", Retention: s.retentionText()}
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
		t := saveTabView{Name: sl.Slot, Label: slotLabel(sl.Slot), Href: savesURL(saveHref(grp.user, grp.game, sl.Slot), filter, "", 0), Selected: sl.Slot == slotName,
			Conflict: sl.OpenConflictCount > 0, Meta: plural(len(h), "version", "versions")}
		if t.Conflict {
			t.Meta = plural(sl.OpenConflictCount, "conflict", "conflicts")
			d.Conflict = true
			d.ConflictN += sl.OpenConflictCount
		}
		d.Tabs = append(d.Tabs, t)
	}
	slot, err := s.svc.GetSaveSlot(ctx, grp.user, grp.game, slotName)
	if err != nil {
		return nil, err
	}
	href := saveHref(grp.user, grp.game, slotName)
	d.RefreshURL = savesURL(href, filter, hist, 0)
	d.NewSlotAsk = withParam(savesURL(href, filter, "", 0), "newslot", "1")
	rs, running, err := s.svc.RunningSessionFor(ctx, grp.user, grp.game)
	if err != nil {
		return nil, err
	}
	blocked := ""
	if running {
		blocked = "Not possible while " + rs.DeviceName + " is playing"
		d.Running = "Restore not possible while a Session of this game is running · " + rs.DeviceName + " is playing " + first.GameTitle + " since " + fmtWhen(rs.Since, now).Text
	}
	q := r.URL.Query()
	d.Up = uploadView{Can: grp.user == viewer, Running: running, Open: !running && (q.Get("upload") != "" || uploadFailed(q)), Action: href + "/upload",
		OpenHref: withParam(savesURL(href, filter, hist, 0), "upload", "1"), CloseHref: savesURL(href, filter, hist, 0), Version: slot.Current.Revision}
	for _, sl := range grp.slots {
		d.Up.Slots = append(d.Up.Slots, uploadSlot{Name: sl.Slot, Label: slotLabel(sl.Slot), Rev: sl.Current.Revision, Selected: sl.Slot == slotName})
	}
	if nf != nil {
		nf.Game, nf.From, nf.FromLabel, nf.Max = d.Title, slotName, slotLabel(slotName), hub.MaxSlotName
		nf.PostHref = href + "/slots"
		nf.CancelHref = savesURL(href, filter, hist, 0)
		nf.Cancel = withParam(nf.CancelHref, "cancel", "1")
		d.NewSlot = nf
	}
	d.AllHref, d.SnapHref = savesURL(href, filter, "", 0), savesURL(href, filter, "snapshots", 0)
	d.SnapOpenHref, d.SnapCloseHref = withParam(savesURL(href, filter, hist, 0), "snap", "1"), savesURL(href, filter, hist, 0)
	if !running && q.Get("snap") != "" {
		d.SnapOpen = true
		d.RefreshURL = withParam(d.RefreshURL, "snap", "1")
	}
	cur := slot.Current
	d.SnapshotHref, d.ExpectedRev = href+"/snapshots", cur.Revision
	d.Current = saveCheckpointView{Rev: cur.Revision, Label: fmt.Sprintf("Rev %d", cur.Revision), Device: webDeviceLabel(cur.DeviceName, cur.DeviceID, names),
		At: when(cur.CreatedAt, now), Reason: syncReasonLabels[cur.Reason], Size: humanBytes(cur.Size), ShortHash: shortHash(cur.SHA256), DownloadHref: href + "/download"}
	// History versions by revision: the Current marker belongs to exactly one version (a snapshot of the
	// current checkpoint has the same revision; the original version wins), and conflicts name versions, not revisions.
	verOfRev := map[int]int{}
	for _, v := range selHist { // newest first
		if v.Reason == hub.HistoryConflictUpload {
			continue
		}
		if have, ok := verOfRev[v.Revision]; !ok || (v.Reason != hub.HistoryManualSnapshot && snapshotVersion(selHist, have)) {
			verOfRev[v.Revision] = v.Version
		}
	}
	currentVersion := verOfRev[cur.Revision]
	if currentVersion > 0 {
		d.Current.Label = fmt.Sprintf("v%d", currentVersion)
	}
	revLabel := func(rev int) string {
		if v := verOfRev[rev]; v > 0 {
			return fmt.Sprintf("v%d", v)
		}
		return fmt.Sprintf("Rev %d", rev)
	}
	{
		o := slotOpsView{Show: true, Main: slotName == hub.DefaultSlot, Running: running, Max: hub.MaxSlotName, Rev: cur.Revision, When: ago(cur.CreatedAt, now),
			Device: webDeviceLabel(cur.DeviceName, cur.DeviceID, names), Size: humanBytes(cur.Size), Versions: len(selHist),
			CancelHref: savesURL(href, filter, hist, 0), RenameHref: href + "/rename", DeleteHref: href + "/delete",
			RenameAsk: withParam(savesURL(href, filter, hist, 0), "rename", "1"), DeleteAsk: withParam(savesURL(href, filter, hist, 0), "delslot", "1")}
		if !running {
			if q.Get("rename") != "" {
				o.RenameOpen, o.Value = true, q.Get("name")
				d.RefreshURL = withParam(d.RefreshURL, "rename", "1")
			} else if q.Get("delslot") != "" {
				o.DeleteOpen = true
				d.RefreshURL = withParam(d.RefreshURL, "delslot", "1")
			}
		}
		d.Ops = o
	}
	for _, c := range slot.OpenConflicts {
		d.Conflicts = append(d.Conflicts, saveConflictView{
			Title: fmt.Sprintf("▲ Conflict: v%d is based on %s, the current version is %s", c.Secured.Version, revLabel(c.Secured.BaseRevision), revLabel(c.Hub.Revision)),
			Note:  "Both are kept in the history. Nothing is overwritten until you choose.",
			Hub: saveSideView{Label: "Current checkpoint on the Hub", Heading: revLabel(c.Hub.Revision), Device: c.Hub.DeviceName,
				At: when(c.Hub.CreatedAt, now), Base: "–", ShortHash: shortHash(c.Hub.SHA256)},
			Local: saveSideView{Label: "Secured upload · sync pending", Heading: fmt.Sprintf("Local save (v%d)", c.Secured.Version), Device: c.Secured.DeviceName,
				At: when(c.Secured.CreatedAt, now), Base: revLabel(c.Secured.BaseRevision), ShortHash: shortHash(c.Secured.SHA256)},
			ResolveHref: href + "/conflicts/" + url.PathEscape(c.ID) + "/resolve", ExpectedRev: c.Hub.Revision, KeepHref: savesURL(href, filter, hist, 0),
		})
	}
	securedByConflict := map[int]bool{}
	for _, c := range slot.OpenConflicts {
		securedByConflict[c.Secured.Version] = true
	}
	for _, v := range selHist {
		meta := historyReasonLabels[v.Reason]
		switch {
		case v.Reason == hub.HistoryConflictUpload && v.BaseRevision != nil:
			meta += " · based on " + revLabel(*v.BaseRevision)
		case v.Reason == hub.HistoryManualSnapshot:
			meta = "Manual snapshot of " + revLabel(v.Revision)
		}
		vs := strconv.Itoa(v.Version)
		hv := saveHistoryView{Version: v.Version, Device: webDeviceLabel(v.DeviceName, v.DeviceID, names), Meta: meta, At: when(v.CreatedAt, now),
			Current:      v.Version == currentVersion,
			Snapshot:     v.Reason == hub.HistoryManualSnapshot,
			DownloadHref: href + "/history/" + vs + "/download",
			RestoreHref:  href + "/history/" + vs + "/restore",
			ConfirmHref:  savesURL(href, filter, hist, v.Version),
			DeleteHref:   href + "/history/" + vs + "/delete",
			DeleteAsk:    withParam(savesURL(href, filter, hist, 0), "delete", vs),
			CancelHref:   savesURL(href, filter, hist, 0), CancelXHR: savesURL(href, filter, hist, 0) + cancelParam(filter, hist), ExpectedRev: cur.Revision, Slot: slotName, SlotLabel: slotLabel(slotName), Blocked: blocked}
		hv.Menu = !securedByConflict[v.Version] // the secured version of an open conflict cannot be deleted
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
		if del == v.Version && hv.Menu {
			d.Confirm, hv.DeleteOpen = v.Version, true
		}
		d.History = append(d.History, hv)
	}
	if d.Up.Can {
		d.Up.Result = uploadResultFor(q, cur, selHist, href+"/history/")
		if res := d.Up.Result; res != nil {
			res.ExpectedRev = cur.Revision
			d.RefreshURL = withParam(d.RefreshURL, "up", q.Get("up"))
			if res.Kind == "same" {
				d.RefreshURL = withParam(d.RefreshURL, "name", q.Get("name"))
			}
		}
		if d.Up.Open {
			d.RefreshURL = withParam(d.RefreshURL, "upload", "1")
		}
	}
	d.HistoryCount = len(selHist)
	d.HistoryEmpty = "No history versions yet."
	if d.Snapshots {
		d.HistoryEmpty = "No snapshots yet."
	}
	return d, nil
}

func (s *Server) savesGet(w http.ResponseWriter, r *http.Request, sess *session) {
	hx := r.Header.Get("HX-Request") == "true"
	inConfirm := hx && strings.HasPrefix(r.Header.Get("HX-Target"), "confirm-v")
	inNew := hx && r.Header.Get("HX-Target") == "newslot-form"
	if (inConfirm || inNew) && r.URL.Query().Get("cancel") != "" {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		return
	}
	var nf *newSlotView
	if r.URL.Query().Get("newslot") != "" {
		nf = &newSlotView{}
	}
	body, err := s.savesBody(r, nf, sess.User.ID)
	if err != nil {
		if errors.Is(err, hub.ErrNotFound) {
			s.notFound(w, r)
			return
		}
		s.fail(w, r, err)
		return
	}
	if r.PathValue("user") != "" && body.Detail == nil {
		s.notFound(w, r)
		return
	}
	d := s.base(r, sess, "saves", "Saves")
	if body.Detail != nil {
		body.Detail.CSRF = sess.CSRFToken
	}
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
	case inNew && body.Detail != nil && body.Detail.NewSlot != nil:
		d.Fragment = true
		s.render(w, http.StatusOK, "saves", "newslot-fragment", d)
	case inConfirm, inNew:
		s.notFound(w, r)
	default:
		s.render(w, http.StatusOK, "saves", "layout", d)
	}
}

var unsafeName = regexp.MustCompile(`[^A-Za-z0-9._-]+`)

// downloadNames returns the file name for a save download: an ASCII fallback for `filename=` and the original
// title (cleaned for Windows file names) for `filename*`. A title without ASCII characters falls back to the
// game id prefix so downloads of different games do not collide.
func downloadNames(title, gameID, suffix string) (ascii, full string) {
	base := strings.Trim(unsafeName.ReplaceAllString(title, "_"), "_.")
	if base == "" || base == "save" {
		base = "save"
		if len(gameID) >= 8 {
			base = gameID[:8]
		}
	}
	ascii = base + "-" + suffix + ".sav"
	// Keep the original title; drop what Windows and header syntax do not allow.
	clean := strings.Map(func(r rune) rune {
		if unicode.IsControl(r) || strings.ContainsRune(`/\:*?"<>|`, r) {
			return -1
		}
		return r
	}, title)
	if rs := []rune(clean); len(rs) > 100 {
		clean = string(rs[:100])
	}
	clean = strings.TrimRight(strings.TrimSpace(clean), ". ")
	if clean == "" {
		return ascii, ascii
	}
	return ascii, clean + "-" + suffix + ".sav"
}

// contentDisposition builds an attachment header with the ASCII fallback and the RFC 5987 UTF-8 name.
func contentDisposition(ascii, full string) string {
	var b strings.Builder
	for _, c := range []byte(full) {
		if c < 0x80 && (c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || strings.IndexByte("!#$&+-.^_`|~", c) >= 0) {
			b.WriteByte(c)
		} else {
			fmt.Fprintf(&b, "%%%02X", c)
		}
	}
	return `attachment; filename="` + ascii + `"; filename*=UTF-8''` + b.String()
}

func (s *Server) downloadName(ctx context.Context, gameID, suffix string) (ascii, full string) {
	title := ""
	if g, err := s.svc.GetGame(ctx, gameID); err == nil {
		title = g.Title
	}
	return downloadNames(title, gameID, suffix)
}

func (s *Server) saveDownload(w http.ResponseWriter, r *http.Request, _ *session) {
	u, g, sl := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	f, c, err := s.svc.OpenSaveContent(r.Context(), u, g, sl)
	if err != nil {
		s.downloadErr(w, r, err)
		return
	}
	defer f.Close()
	ascii, full := s.downloadName(r.Context(), g, fmt.Sprintf("rev%d", c.Revision))
	s.serveSave(w, r, f, c.SHA256, ascii, full, c.CreatedAt)
}

func (s *Server) saveHistoryDownload(w http.ResponseWriter, r *http.Request, _ *session) {
	u, g, sl := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	ver, err := strconv.Atoi(r.PathValue("version"))
	if err != nil {
		s.notFound(w, r)
		return
	}
	f, v, err := s.svc.OpenSaveVersion(r.Context(), u, g, sl, ver)
	if err != nil {
		s.downloadErr(w, r, err)
		return
	}
	defer f.Close()
	ascii, full := s.downloadName(r.Context(), g, fmt.Sprintf("v%d", v.Version))
	s.serveSave(w, r, f, v.SHA256, ascii, full, v.CreatedAt)
}

func (s *Server) serveSave(w http.ResponseWriter, r *http.Request, f http.File, sha, ascii, full string, mod time.Time) {
	h := w.Header()
	h.Set("Content-Type", "application/octet-stream")
	h.Set("Content-Disposition", contentDisposition(ascii, full))
	h.Set("ETag", `"`+sha+`"`)
	rw, done := rollwrite.Wrap(w)
	defer done()
	http.ServeContent(rw, r, ascii, mod, f)
}

func (s *Server) downloadErr(w http.ResponseWriter, r *http.Request, err error) {
	if errors.Is(err, hub.ErrNotFound) {
		s.notFound(w, r)
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
		s.notFound(w, r)
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
		s.notFound(w, r)
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
		s.notFound(w, r)
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
		s.notFound(w, r)
	case errors.Is(err, hub.ErrBadRequest):
		s.redirect(w, r, saveHref(u, g, sl)+"?err=label")
	default:
		s.fail(w, r, err)
	}
}

func (s *Server) saveDeleteSnapshot(w http.ResponseWriter, r *http.Request, _ *session) {
	u, g, sl := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	ver, err := strconv.Atoi(r.PathValue("version"))
	if err != nil {
		s.notFound(w, r)
		return
	}
	err = s.svc.DeleteSaveVersion(r.Context(), u, g, sl, ver)
	switch {
	case err == nil:
		s.redirect(w, r, saveHref(u, g, sl)+"?ok=snapdeleted")
	case errors.Is(err, hub.ErrNotFound):
		s.notFound(w, r)
	case errors.Is(err, hub.ErrConflict):
		s.redirect(w, r, saveHref(u, g, sl)+"?err=openconflict")
	default:
		s.fail(w, r, err)
	}
}

// saveNewSlot creates a slot from the current version of the slot in the path. A refused name shows the form again.
func (s *Server) saveNewSlot(w http.ResponseWriter, r *http.Request, sess *session) {
	u, g, from := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	name := strings.TrimSpace(r.PostFormValue("name"))
	_, err := s.svc.CreateSaveSlot(r.Context(), u, g, from, name, hub.WebDeviceID(sess.User.ID))
	var msg string
	switch {
	case err == nil:
		s.redirect(w, r, saveHref(u, g, name)+"?ok=slotcreated")
		return
	case errors.Is(err, hub.ErrNotFound):
		s.notFound(w, r)
		return
	case errors.Is(err, hub.ErrSaveSlotExists):
		msg = "A slot with this name already exists."
	case errors.Is(err, hub.ErrBadRequest):
		msg = fmt.Sprintf("Use lowercase letters, digits, - and _ (up to %d characters).", hub.MaxSlotName)
	default:
		s.fail(w, r, err)
		return
	}
	body, err := s.savesBody(r, &newSlotView{Value: name, Error: msg}, sess.User.ID)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	if body.Detail == nil {
		s.notFound(w, r)
		return
	}
	d := s.base(r, sess, "saves", "Saves")
	body.Detail.CSRF = sess.CSRFToken
	d.Body = body
	if r.Header.Get("HX-Request") == "true" {
		d.Fragment = true
		s.render(w, http.StatusOK, "saves", "newslot-fragment", d)
		return
	}
	s.render(w, http.StatusOK, "saves", "layout", d)
}

// readSaveUpload streams a multipart save upload: the fields (including _csrf) come before the file part. file is nil
// when no file was selected. A non-zero code is the HTTP status for the failure (message already written).
func (s *Server) readSaveUpload(w http.ResponseWriter, r *http.Request, sess *session) (fields map[string]string, file *multipart.Part, code int) {
	r.Body = http.MaxBytesReader(w, r.Body, hub.MaxSaveBytes+multipartSlack)
	mr, err := r.MultipartReader()
	if err != nil {
		http.Error(w, "Invalid request", http.StatusBadRequest)
		return nil, nil, http.StatusBadRequest
	}
	csrfOK := r.Context().Value(keyCSRFChecked) == true
	fields = map[string]string{}
	for {
		part, err := mr.NextPart()
		if err == io.EOF {
			break
		}
		if err != nil {
			var mbe *http.MaxBytesError
			if errors.As(err, &mbe) {
				fields["_toolarge"] = "1"
				break
			}
			http.Error(w, "Invalid request", http.StatusBadRequest)
			return nil, nil, http.StatusBadRequest
		}
		name := part.FormName()
		if name == "_csrf" && hub.TokenEqual(readField(part), sess.CSRFToken) {
			csrfOK = true
		}
		if name != "file" {
			if name != "_csrf" {
				fields[name] = readField(part)
			}
			part.Close()
			continue
		}
		if !csrfOK {
			http.Error(w, "CSRF check failed", http.StatusForbidden)
			return nil, nil, http.StatusForbidden
		}
		if part.FileName() == "" {
			part.Close()
			continue
		}
		return fields, part, 0
	}
	if !csrfOK {
		http.Error(w, "CSRF check failed", http.StatusForbidden)
		return nil, nil, http.StatusForbidden
	}
	return fields, nil, 0
}

// uploadErrKey maps an UploadSaveFile error to the result key (up=...); "" = not an expected failure.
func uploadErrKey(err error) string {
	var mbe *http.MaxBytesError
	switch {
	case errors.Is(err, hub.ErrSaveConflictStale):
		return "stale"
	case errors.Is(err, hub.ErrSessionRunning):
		return "running"
	case errors.Is(err, hub.ErrPayloadTooLarge), errors.As(err, &mbe):
		return "toolarge"
	case errors.Is(err, hub.ErrBadRequest):
		return "emptyfile" // the slot name and the form fields are checked before
	}
	return ""
}

// saveUpload makes an uploaded file the new current version of a slot of the signed-in user (the old one goes to the
// history as "Before upload"). Only the owner can upload; the slot field selects another slot of the same game
// (the revision of every offered slot is in the form as rev.<slot>). The result is shown inline: htmx gets the
// slot detail back, a plain form post is redirected to the slot with up=<result> in the query.
func (s *Server) saveUpload(w http.ResponseWriter, r *http.Request, sess *session) {
	u, g, slot := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	if !hub.ValidSlotName(slot) {
		s.notFound(w, r)
		return
	}
	if u != sess.User.ID { // admins cannot upload into another user's slot
		http.Error(w, "Forbidden", http.StatusForbidden)
		return
	}
	fields, file, code := s.readSaveUpload(w, r, sess)
	if code != 0 {
		return
	}
	if file != nil {
		defer file.Close()
	}
	if sel := fields["slot"]; sel != "" && sel != slot {
		if _, err := s.svc.GetSaveSlot(r.Context(), u, g, sel); err != nil { // only existing slots of the user are offered
			http.Error(w, "Invalid request", http.StatusBadRequest)
			return
		}
		slot = sel
	}
	exp, err := strconv.Atoi(fields["rev."+slot])
	if err != nil || exp < 0 {
		http.Error(w, "Invalid request", http.StatusBadRequest)
		return
	}
	q := url.Values{}
	switch {
	case fields["_toolarge"] != "":
		q.Set("up", "toolarge")
	case file == nil:
		q.Set("up", "emptyfile")
	default:
		out, err := s.svc.UploadSaveFile(r.Context(), hub.UploadSaveInput{UserID: u, DeviceID: hub.WebDeviceID(sess.User.ID), GameID: g, Slot: slot,
			ExpectedRevision: &exp, Body: file})
		switch key := uploadErrKey(err); {
		case err == nil && out.Current.Revision == exp: // identical content changes nothing
			q.Set("up", "same")
			q.Set("name", file.FileName())
		case err == nil:
			q.Set("up", "ok")
		case errors.Is(err, hub.ErrNotFound):
			s.notFound(w, r)
			return
		case key != "":
			q.Set("up", key)
		default:
			s.fail(w, r, err)
			return
		}
	}
	to := saveHref(u, g, slot) + "?" + q.Encode()
	if r.Header.Get("HX-Request") != "true" {
		s.redirect(w, r, to)
		return
	}
	// htmx: render the slot detail with the result (the same page the redirect would show), no reload.
	r.URL.Path, r.URL.RawQuery = saveHref(u, g, slot), q.Encode()
	r.SetPathValue("slot", slot)
	body, err := s.savesBody(r, nil, sess.User.ID)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	if body.Detail == nil {
		s.notFound(w, r)
		return
	}
	body.Detail.CSRF, body.OOB = sess.CSRFToken, true
	d := s.base(r, sess, "saves", "Saves")
	d.Fragment, d.Body = true, body
	w.Header().Set("HX-Push-Url", to)
	s.render(w, http.StatusOK, "saves", "saves-detail-fragment", d)
}

// saveRenameSlot renames the slot in the path. A refused name shows the form again with the error.
func (s *Server) saveRenameSlot(w http.ResponseWriter, r *http.Request, sess *session) {
	u, g, from := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	name := strings.TrimSpace(r.PostFormValue("name"))
	_, err := s.svc.RenameSaveSlot(r.Context(), hub.RenameInput{UserID: u, DeviceID: hub.WebDeviceID(sess.User.ID), GameID: g, Slot: from, NewSlot: name})
	back := func(key string) {
		s.redirect(w, r, withParam(withParam(saveHref(u, g, from)+"?err="+key, "rename", "1"), "name", name))
	}
	switch {
	case err == nil:
		s.redirect(w, r, saveHref(u, g, name)+"?ok=slotrenamed")
	case errors.Is(err, hub.ErrNotFound):
		s.notFound(w, r)
	case errors.Is(err, hub.ErrSaveSlotProtected):
		s.redirect(w, r, saveHref(u, g, from)+"?err=slotprotected")
	case errors.Is(err, hub.ErrSessionRunning):
		s.redirect(w, r, saveHref(u, g, from)+"?err=running")
	case errors.Is(err, hub.ErrSaveSlotExists):
		back("slotexists")
	case errors.Is(err, hub.ErrBadRequest):
		back("slotname")
	default:
		s.fail(w, r, err)
	}
}

// saveDeleteSlot deletes the slot in the path after the inline confirmation (expected_revision = the checkpoint the page showed).
func (s *Server) saveDeleteSlot(w http.ResponseWriter, r *http.Request, sess *session) {
	u, g, sl := r.PathValue("user"), r.PathValue("game"), r.PathValue("slot")
	exp, err := strconv.Atoi(r.PostFormValue("expected_revision"))
	if err != nil {
		http.Error(w, "Invalid request", http.StatusBadRequest)
		return
	}
	err = s.svc.DeleteSaveSlot(r.Context(), hub.DeleteInput{UserID: u, DeviceID: hub.WebDeviceID(sess.User.ID), GameID: g, Slot: sl, ConfirmRevision: exp})
	here := saveHref(u, g, sl)
	switch {
	case err == nil && sl == hub.DefaultSlot:
		s.redirect(w, r, "/saves?ok=maindeleted")
	case err == nil:
		s.redirect(w, r, "/saves?ok=slotdeleted")
	case errors.Is(err, hub.ErrNotFound):
		s.notFound(w, r)
	case errors.Is(err, hub.ErrSaveSlotProtected):
		s.redirect(w, r, here+"?err=slotprotected")
	case errors.Is(err, hub.ErrSessionRunning):
		s.redirect(w, r, here+"?err=running")
	case errors.Is(err, hub.ErrSaveConflictStale):
		s.redirect(w, r, here+"?err=stale")
	case errors.Is(err, hub.ErrConflict):
		s.redirect(w, r, here+"?err=openconflict")
	default:
		s.fail(w, r, err)
	}
}

// snapshotVersion reports whether the history version is a manual snapshot.
func snapshotVersion(hist []hub.SaveVersion, version int) bool {
	for _, v := range hist {
		if v.Version == version {
			return v.Reason == hub.HistoryManualSnapshot
		}
	}
	return false
}
