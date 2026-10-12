package web

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"slices"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/hub"
)

// Runs page (docs/design/plugins.md 4jh-2): list, detail with status actions, participants, the board of each plugin
// drawn by static/plugin-blocks.js, and the event log with a JSON export. The web interface is admin-only: the admin
// may pause and resume any run, archive finished ones and delete runs; the plugin content is read-only.

const (
	runEventsShown = 100
	exportPage     = 2000
)

type runFilter struct {
	Key, Label string
	On         bool
	URL        string
}

type avatarView struct {
	Initial, Class, Title string
}

type runItem struct {
	ID, Name, StatusLabel, StatusClass, Sub, URL string
	Selected                                     bool
	Avatars                                      []avatarView
}

type partView struct {
	Name, Initial, ColorClass, Game, Status, StatusClass string
}

type boardView struct {
	Plugin, Meta, JSON string
	Empty              bool
}

type eventView struct {
	Time, Author, Text, Source, Seq string
	Hub                             bool
}

type runDetail struct {
	ID, Name, StatusLabel, StatusClass, Subline string
	Paused, Archived                            bool
	CanPause, CanResume, CanArchive             bool
	Participants                                []partView
	Boards                                      []boardView
	Events                                      []eventView
	EventTotal                                  int64
}

type runsBody struct {
	Self    string
	Filters []runFilter
	Items   []runItem
	Detail  *runDetail
}

var runStatusLabels = map[string]string{
	hub.RunStarting: "Starting", hub.RunActive: "Active", hub.RunPaused: "Paused", hub.RunFinished: "Finished", hub.RunAbandoned: "Abandoned",
}

// runStatusClass: Active is info (cyan), Paused and Starting warning (amber), Finished ok, Abandoned neutral.
var runStatusClasses = map[string]string{
	hub.RunStarting: "amber", hub.RunActive: "warn", hub.RunPaused: "amber", hub.RunFinished: "ok", hub.RunAbandoned: "",
}

var participantLabels = map[string]string{
	hub.ParticipantJoined: "Joined", hub.ParticipantInvited: "Invited", hub.ParticipantDeclined: "Declined", hub.ParticipantLeft: "Left",
}

var runFilters = []struct{ Key, Label string }{
	{"all", "All"}, {"active", "Active"}, {"paused", "Paused"}, {"finished", "Finished"}, {"archived", "Archived"},
}

func runMatches(filter string, r hub.Run) bool {
	switch filter {
	case "archived":
		return r.Archived
	case "active":
		return !r.Archived && (r.Status == hub.RunActive || r.Status == hub.RunStarting)
	case "paused":
		return !r.Archived && r.Status == hub.RunPaused
	case "finished":
		return !r.Archived && r.Finished()
	}
	return !r.Archived
}

// playerClass maps the run colour (0-7) to the player colour classes of the building blocks (1-8).
func playerClass(c *int) string {
	if c == nil {
		return ""
	}
	return fmt.Sprintf("pc pb-pc-%d", *c+1)
}

func runURL(id, filter string) string {
	u := "/runs/" + id
	if filter != "" && filter != "all" {
		u += "?filter=" + url.QueryEscape(filter)
	}
	return u
}

func (s *Server) runsList(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderRuns(w, r, sess, "")
}

func (s *Server) runsDetail(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderRuns(w, r, sess, r.PathValue("id"))
}

func (s *Server) renderRuns(w http.ResponseWriter, r *http.Request, sess *session, selectedID string) {
	ctx := r.Context()
	filter := r.URL.Query().Get("filter")
	if !slices.ContainsFunc(runFilters, func(f struct{ Key, Label string }) bool { return f.Key == filter }) {
		filter = "all"
	}
	all, err := s.svc.ListRunsAdmin(ctx, true)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	games, err := s.svc.ListGames(ctx)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	titles := map[string]string{}
	for _, g := range games {
		titles[g.ID] = g.Title
	}
	var shown []hub.Run
	for _, run := range all {
		if runMatches(filter, run) {
			shown = append(shown, run)
		}
	}
	if selectedID == "" && len(shown) > 0 {
		selectedID = shown[0].ID
	}
	body := runsBody{Self: runURL(selectedID, filter)}
	if selectedID == "" {
		body.Self = "/runs"
		if filter != "all" {
			body.Self += "?filter=" + url.QueryEscape(filter)
		}
	}
	for _, f := range runFilters {
		u := "/runs"
		if selectedID != "" {
			u = "/runs/" + selectedID
		}
		if f.Key != "all" {
			u += "?filter=" + url.QueryEscape(f.Key)
		}
		body.Filters = append(body.Filters, runFilter{Key: f.Key, Label: f.Label, On: f.Key == filter, URL: u})
	}
	for _, run := range shown {
		it := runItem{ID: run.ID, Name: run.Name, StatusLabel: runStatusLabels[run.Status], StatusClass: runStatusClasses[run.Status],
			URL: runURL(run.ID, filter), Selected: run.ID == selectedID}
		joined := 0
		for _, p := range run.Participants {
			if p.Status == hub.ParticipantJoined {
				joined++
			}
			it.Avatars = append(it.Avatars, avatarView{Initial: initial(p.UserName), Class: playerClass(p.Color), Title: p.UserName})
		}
		it.Sub = fmt.Sprintf("%d of %d joined · %s", joined, len(run.Participants), run.UpdatedAt.Local().Format("02.01.2006"))
		body.Items = append(body.Items, it)
	}
	if selectedID != "" {
		run, err := s.svc.GetRunAdmin(ctx, selectedID)
		if errors.Is(err, hub.ErrNotFound) {
			http.Redirect(w, r, "/runs?err=norun", http.StatusSeeOther)
			return
		}
		if err != nil {
			s.fail(w, r, err)
			return
		}
		det, err := s.runDetail(r, run, titles)
		if err != nil {
			s.fail(w, r, err)
			return
		}
		body.Detail = det
	}
	d := s.base(r, sess, "runs", "Runs")
	d.Body = body
	if isHX(r, "runs-view") {
		d.Fragment = true
		s.render(w, http.StatusOK, "runs", "runs-view", d)
		return
	}
	s.render(w, http.StatusOK, "runs", "layout", d)
}

func (s *Server) runDetail(r *http.Request, run hub.Run, titles map[string]string) (*runDetail, error) {
	ctx := r.Context()
	names := map[string]string{}
	for _, p := range run.Participants {
		names[p.UserID] = p.UserName
	}
	nameOf := func(id string) string {
		if n, ok := names[id]; ok {
			return n
		}
		if u, err := s.svc.GetUser(ctx, id); err == nil {
			names[id] = u.DisplayName
			return u.DisplayName
		}
		return "a deleted user"
	}
	det := &runDetail{ID: run.ID, Name: run.Name, StatusLabel: runStatusLabels[run.Status], StatusClass: runStatusClasses[run.Status],
		Paused: run.Status == hub.RunPaused, Archived: run.Archived,
		CanPause: run.Status == hub.RunActive, CanResume: run.Status == hub.RunPaused, CanArchive: run.Finished()}
	var plugins []string
	for _, p := range run.Plugins {
		plugins = append(plugins, p.PluginID+" "+p.Version)
	}
	parts := []string{}
	switch run.Status {
	case hub.RunPaused:
		parts = append(parts, "Paused by "+nameOf(run.StatusBy))
	case hub.RunFinished, hub.RunAbandoned:
		parts = append(parts, strings.ToUpper(runStatusLabels[run.Status][:1])+runStatusLabels[run.Status][1:]+" by "+nameOf(run.StatusBy))
	default:
		parts = append(parts, "Created by "+nameOf(run.CreatedBy))
	}
	parts = append(parts, run.UpdatedAt.Local().Format("02.01.2006"), strings.Join(plugins, " & "))
	if det.Paused {
		parts = append(parts, "events still visible, no new ones recorded")
	}
	det.Subline = strings.Join(parts, " · ")

	for _, p := range run.Participants {
		pv := partView{Name: p.UserName, Initial: initial(p.UserName), ColorClass: playerClass(p.Color), Status: participantLabels[p.Status]}
		if p.Status == hub.ParticipantJoined {
			pv.StatusClass = "ok"
		}
		if p.GameID != nil {
			pv.Game = titles[*p.GameID]
			if pv.Game == "" {
				pv.Game = "Game no longer in the library"
			}
		}
		det.Participants = append(det.Participants, pv)
	}

	boards, err := s.svc.RunBoards(ctx, run.ID)
	if err != nil {
		return nil, err
	}
	byPlugin := map[string]hub.RunBoard{}
	for _, b := range boards {
		byPlugin[b.PluginID] = b
	}
	for _, p := range run.Plugins {
		bv := boardView{Plugin: p.PluginID + " " + p.Version, Empty: true}
		if b, ok := byPlugin[p.PluginID]; ok {
			bv.Empty = false
			bv.JSON = string(b.Tree) // html/template escapes it for the data-tree attribute
			bv.Meta = "Updated " + b.UpdatedAt.Local().Format("02.01.2006 15:04")
			if b.UpdatedBy != "" {
				bv.Meta += " by " + b.UpdatedBy + "'s Player"
			}
		}
		det.Boards = append(det.Boards, bv)
	}

	det.EventTotal = run.LastSeq
	after := max(0, run.LastSeq-runEventsShown)
	evs, _, err := s.svc.ListRunEventsAdmin(ctx, run.ID, after, runEventsShown)
	if err != nil {
		return nil, err
	}
	for i := len(evs) - 1; i >= 0; i-- { // newest first
		det.Events = append(det.Events, eventRow(evs[i]))
	}
	return det, nil
}

func eventRow(e hub.RunEvent) eventView {
	v := eventView{Time: e.CreatedAt.Local().Format("02.01. 15:04:05"), Seq: fmt.Sprint(e.Seq), Hub: e.Source == hub.EventHub}
	if v.Hub {
		v.Author, v.Source = "Hub", "Hub"
		v.Text = hubEventText(e)
		return v
	}
	v.Author = e.UserName
	v.Text = e.Text
	if v.Text == "" {
		v.Text = e.Type
	}
	if e.Source == hub.EventAutomatic {
		v.Source = "automatic · " + e.UserName + "'s Player"
	} else {
		v.Source = "manual · " + e.UserName
	}
	return v
}

// hubEventText words the events the Hub itself appends (the payload says who it is about).
func hubEventText(e hub.RunEvent) string {
	var p struct {
		UserName   string `json:"user_name"`
		Status     string `json:"status"`
		Previous   string `json:"previous"`
		ByUserName string `json:"by_user_name"`
	}
	_ = json.Unmarshal(e.Payload, &p)
	who := p.UserName
	if who == "" {
		who = "Someone"
	}
	switch e.Type {
	case "run.created":
		return "Run created by " + who
	case "run.joined":
		return who + " joined"
	case "run.declined":
		return who + " declined the invite"
	case "run.left":
		return who + " left"
	case "run.status":
		by := p.ByUserName
		if by == "" {
			by = "Someone"
		}
		switch {
		case p.Status == hub.RunPaused:
			return by + " paused the run"
		case p.Status == hub.RunActive && p.Previous == hub.RunPaused:
			return by + " resumed the run"
		case p.Status == hub.RunActive:
			return by + " started the run"
		case p.Status == hub.RunFinished:
			return by + " finished the run"
		case p.Status == hub.RunAbandoned:
			return by + " abandoned the run"
		}
	}
	return e.Type
}

func (s *Server) runStatusAction(status, ok string) handlerFunc {
	return func(w http.ResponseWriter, r *http.Request, sess *session) {
		id := r.PathValue("id")
		_, err := s.svc.SetRunStatusAdmin(r.Context(), sess.User.ID, id, status)
		s.runResult(w, r, id, ok, err)
	}
}

func (s *Server) runArchiveAction(archived bool, ok string) handlerFunc {
	return func(w http.ResponseWriter, r *http.Request, sess *session) {
		id := r.PathValue("id")
		s.runResult(w, r, id, ok, s.svc.ArchiveRun(r.Context(), id, archived))
	}
}

func (s *Server) runResult(w http.ResponseWriter, r *http.Request, id, ok string, err error) {
	switch {
	case err == nil:
		http.Redirect(w, r, "/runs/"+id+"?ok="+ok, http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/runs?err=norun", http.StatusSeeOther)
	case errors.Is(err, hub.ErrConflict):
		http.Redirect(w, r, "/runs/"+id+"?err=runstate", http.StatusSeeOther)
	default:
		s.fail(w, r, err)
	}
}

func (s *Server) runDeleteConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	run, err := s.svc.GetRunAdmin(r.Context(), r.PathValue("id"))
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/runs?err=norun", http.StatusSeeOther)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	d := s.base(r, sess, "runs", "Delete run")
	d.Body = confirmBody{
		Heading: "Delete “" + run.Name + "”?",
		Intro:   "This permanently deletes the run for all its participants and cannot be undone. The following is removed:",
		Removes: []string{
			fmt.Sprintf("The run with its %d participants and their states", len(run.Participants)),
			fmt.Sprintf("The event log (%d events)", run.LastSeq),
			"The boards the plugins published",
		},
		Note:   "Saves are not touched. Players that still show the run will not find it on the Hub any more.",
		Action: "/runs/" + run.ID + "/delete", Button: "Delete run", Cancel: "/runs/" + run.ID,
	}
	s.render(w, http.StatusOK, "confirm", "layout", d)
}

func (s *Server) runDelete(w http.ResponseWriter, r *http.Request, sess *session) {
	switch err := s.svc.DeleteRun(r.Context(), r.PathValue("id")); {
	case err == nil:
		http.Redirect(w, r, "/runs?ok=rundeleted", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/runs?err=norun", http.StatusSeeOther)
	default:
		s.fail(w, r, err)
	}
}

// runExport streams the whole event log as one JSON document.
func (s *Server) runExport(w http.ResponseWriter, r *http.Request, sess *session) {
	ctx := r.Context()
	run, err := s.svc.GetRunAdmin(ctx, r.PathValue("id"))
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/runs?err=norun", http.StatusSeeOther)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	head, _ := json.Marshal(map[string]any{"run_id": run.ID, "name": run.Name, "status": run.Status, "exported_at": s.svc.Now().Format(time.RFC3339), "last_seq": run.LastSeq})
	h := w.Header()
	h.Set("Content-Type", "application/json")
	h.Set("Content-Disposition", `attachment; filename="run-`+run.ID+`-events.json"`)
	// {"run_id":...,"events":[ ... ]} : the header object without its closing brace, then the events.
	fmt.Fprintf(w, "%s,\"events\":[", head[:len(head)-1])
	first := true
	var after int64
	for after < run.LastSeq {
		evs, _, err := s.svc.ListRunEventsAdmin(ctx, run.ID, after, exportPage)
		if err != nil || len(evs) == 0 {
			break
		}
		for _, e := range evs {
			b, _ := json.Marshal(e)
			if !first {
				fmt.Fprint(w, ",")
			}
			first = false
			w.Write(b)
			after = e.Seq
		}
	}
	fmt.Fprint(w, "]}\n")
}
