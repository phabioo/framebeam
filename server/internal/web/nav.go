package web

import (
	"fmt"
	"net/url"
	"strings"
)

// Sidebar navigation in groups (design 0-3 E): Play (no eyebrow) · SETUP · ADMIN. Members see only Play and
// Settings; SETUP and ADMIN are hidden, not disabled. Adding a page is one navEntry in navGroups.

type navEntry struct {
	ID, Href, Label string
	Badge           string // text right of the label
	BadgeClass      string // "" (amber), "error"
	Active          bool
}

type navGroup struct {
	Eyebrow string // "" for the first group
	Items   []navEntry
}

// navInput is everything the navigation shows; pageData fills it.
type navInput struct {
	Admin           bool
	Active          string
	Conflicts       int
	Firmware        int
	Pending         int
	ActiveRuns      int
	UpdateAvailable bool
	StatusProblem   string // title of the most urgent problem ("" none)
	NewReports      int    // reports not yet marked seen (cyan badge on Reports)
}

// navIDs are the values of pageData.Nav (the part of the path after the slash).
var navIDs = []string{"library", "saves", "runs", "plugins", "systems", "clients", "users", "status", "reports", "settings"}

func navGroups(in navInput) []navGroup {
	e := func(id, label string) navEntry {
		return navEntry{ID: id, Href: "/" + id, Label: label, Active: in.Active == id}
	}
	with := func(n navEntry, badge, class string) navEntry {
		n.Badge, n.BadgeClass = badge, class
		return n
	}

	play := navGroup{}
	lib := e("library", "Library")
	saves := e("saves", "Saves")
	if in.Conflicts > 0 {
		saves = with(saves, plural(in.Conflicts, "conflict", "conflicts"), "")
	}
	play.Items = append(play.Items, lib, saves)
	runs := e("runs", "Runs")
	if in.ActiveRuns > 0 {
		runs = with(runs, fmt.Sprintf("%d active", in.ActiveRuns), "")
	}
	play.Items = append(play.Items, runs)
	play.Items = append(play.Items, e("plugins", "Plugins"))

	settings := e("settings", "Settings")
	if in.UpdateAvailable {
		settings = with(settings, "update", "")
	}
	if !in.Admin {
		return []navGroup{play, {Items: []navEntry{settings}}}
	}

	setup := navGroup{Eyebrow: "SETUP"}
	sys := e("systems", "Systems & Cores")
	if in.Firmware > 0 {
		sys = with(sys, plural(in.Firmware, "issue", "issues"), "error")
	}
	cl := e("clients", "Clients")
	if in.Pending > 0 {
		cl = with(cl, plural(in.Pending, "request", "requests"), "")
	}
	setup.Items = []navEntry{sys, cl, e("users", "Users")}

	admin := navGroup{Eyebrow: "ADMIN"}
	st := e("status", "Status")
	if in.StatusProblem != "" {
		st = with(st, in.StatusProblem, "")
	}
	rep := e("reports", "Reports")
	if in.NewReports > 0 {
		rep = with(rep, fmt.Sprintf("%d new", in.NewReports), "info")
	}
	admin.Items = []navEntry{st, rep, settings}
	return []navGroup{play, setup, admin}
}

// navActive returns the navigation id for a page path ("" when it is none of them).
func navActive(path string) string {
	for _, n := range navIDs {
		if path == "/"+n || strings.HasPrefix(path, "/"+n+"/") {
			return n
		}
	}
	return ""
}

// navActiveFromURL is navActive for the HX-Current-URL header.
func navActiveFromURL(raw string) string {
	if u, err := url.Parse(raw); err == nil {
		return navActive(u.Path)
	}
	return ""
}
