package web

import (
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"strings"

	"github.com/phabioo/framebeam/server/internal/hub"
)

// maxFirmwareForm bounds the firmware upload form (the largest firmware image is 512 KiB).
const maxFirmwareForm = 1 << 20

type fwView struct {
	ID, Name, Requirement, Expected, Present, Size string
	State, StateLabel, Class                       string
	Pinned                                         bool
	PinFull                                        string
	HasFile                                        bool
	Sizes                                          string
	Note                                           string
}

type clientView struct {
	Device, Platform, Versions, Status, StatusText string
	OK                                             bool
	Blocking                                       bool // only an incompatible protocol blocks launching (ADR 0007 D3)
}

type versionOption struct {
	Value, Label string
	Selected     bool
}

type coreRowView struct {
	CoreID, Version, Platform, Size, License string
	Cached                                   string
	Class                                    string
}

type coreSourceView struct {
	URL, LastCheck, LastSuccess, LastError string
	Skipped                                int
	Rows                                   []coreRowView
}

type systemView struct {
	Versions                                                      []versionOption
	ID, Name, CoreID, CoreName, Expected, Provisioning, Platforms string
	Extensions, InputProfile, DisplayProfile, Mode                string
	Native                                                        bool
	Firmware                                                      []fwView
	Clients                                                       []clientView
	// List chip and summary chip (ready / firmware problems / clients that differ), tab counters.
	ChipText, ChipClass, SummaryText, SummaryClass string
	FwBad, ClientsBad, ClientsBlocking             int
	Href                                           string
	Selected                                       bool
	FirmwareHint                                   string
}

// Tabs of the system detail.
const (
	tabFirmware = "firmware"
	tabClients  = "clients"
	tabCore     = "core"
)

type systemsBody struct {
	Systems []systemView // matching the search
	Sel     *systemView
	Tab     string
	Query   string
	Total   int // systems in the registry
	Cores   coreSourceView
	// ListURL and DetailURL are the URLs the live regions reload themselves from; OOB marks the list as part of a detail fragment.
	ListURL, DetailURL string
	TabHrefs           map[string]string
	OOB                bool
}

// systemsURL builds a Systems URL (selection, tab, search).
func systemsURL(sys, tab, q string) string {
	v := url.Values{}
	if sys != "" {
		v.Set("sys", sys)
	}
	if tab != "" && tab != tabFirmware {
		v.Set("tab", tab)
	}
	if q != "" {
		v.Set("q", q)
	}
	if len(v) == 0 {
		return "/systems"
	}
	return "/systems?" + v.Encode()
}

func statusView(f hub.FirmwareFile) (label, class string) {
	switch f.State {
	case hub.FirmwareValid:
		return "✓ Valid", "ok"
	case hub.FirmwareMismatch:
		return "✕ Hash mismatch", "error"
	case hub.FirmwareMissing:
		return "○ Missing", "error dashed"
	}
	return "○ Optional", ""
}

func (s *Server) systemsBodyData(r *http.Request) (systemsBody, error) {
	reg, err := s.svc.ListRegistry(r.Context())
	if err != nil {
		return systemsBody{}, err
	}
	var b systemsBody
	for _, e := range reg {
		v := systemView{ID: e.ID, Name: e.Name, CoreID: e.CoreID, CoreName: e.CoreName, Expected: e.ExpectedCoreVersion,
			Provisioning: e.Provisioning, Platforms: strings.Join(e.Platforms, ", "), Extensions: strings.Join(e.Extensions, ", "),
			InputProfile: e.InputProfile, DisplayProfile: e.DisplayProfile, Mode: string(e.FirmwareMode),
			Native: e.FirmwareMode == hub.FirmwareNative}
		vers, err := s.svc.CoreVersions(r.Context(), e.CoreID)
		if err != nil {
			return systemsBody{}, err
		}
		v.Versions = append(v.Versions, versionOption{Value: "", Label: "any version", Selected: e.ExpectedCoreVersion == ""})
		known := false
		for _, ver := range vers {
			known = known || ver == e.ExpectedCoreVersion
			v.Versions = append(v.Versions, versionOption{Value: ver, Label: ver, Selected: ver == e.ExpectedCoreVersion})
		}
		if e.ExpectedCoreVersion != "" && !known {
			v.Versions = append(v.Versions, versionOption{Value: e.ExpectedCoreVersion, Label: e.ExpectedCoreVersion + " (not in source)", Selected: true})
		}
		for _, f := range e.Firmware {
			fv := fwView{ID: f.ID, Name: f.DisplayName, Requirement: "optional", Expected: "—", Present: "—", State: string(f.State),
				Pinned: f.Pinned != "", HasFile: f.Present, Sizes: strings.ReplaceAll(sizeList(f.Sizes), " or ", ", ")}
			if f.Required {
				fv.Requirement = "required"
			}
			if f.Pinned != "" {
				fv.PinFull = f.Pinned
				fv.Expected = shortHash(f.Pinned)
			}
			if f.Present {
				fv.Present = shortHash(f.SHA256) + " · " + humanBytes(f.Size)
			}
			fv.StateLabel, fv.Class = statusView(f)
			switch {
			case f.State == hub.FirmwareMismatch:
				fv.Note = "The file does not match the expected SHA-256."
				if f.Pinned != "" {
					fv.Note = "The file does not match the expected hash (" + shortHash(f.Pinned) + ")."
				}
			case f.State == hub.FirmwareMissing && f.Required:
				fv.Note = "Games for this system cannot start until this file is provided."
			}
			if f.Required && (f.State == hub.FirmwareMissing || f.State == hub.FirmwareMismatch) {
				v.FwBad++
			}
			v.Firmware = append(v.Firmware, fv)
		}
		reports, err := s.svc.ListClientReports(r.Context(), e)
		if err != nil {
			return systemsBody{}, err
		}
		for _, c := range reports {
			cv := clientView{Device: c.DeviceName, Platform: c.Platform + "-" + c.Arch, Status: string(c.Status)}
			if c.Status != hub.ClientCompatible {
				v.ClientsBad++
			}
			if c.Status == hub.ClientPlayerTooOld {
				v.ClientsBlocking++
				cv.Blocking = true
			}
			core := c.CoreVersion
			if core == "" {
				core = "not installed"
			}
			cv.Versions = "Player " + c.PlayerVersion + " · " + e.CoreName + " " + core
			switch c.Status {
			case hub.ClientCompatible:
				cv.OK, cv.StatusText = true, "compatible"
			case hub.ClientCoreMismatch:
				cv.StatusText = "Core version mismatch · " + c.Expected + " expected"
			case hub.ClientCoreMissing:
				cv.StatusText = "Core missing"
			case hub.ClientPlayerTooOld:
				cv.StatusText = "Player too old · protocol v" + c.Expected + " required"
			}
			v.Clients = append(v.Clients, cv)
		}
		v.chips()
		b.Systems = append(b.Systems, v)
	}
	q := r.URL.Query()
	if q.Get("sys") == "" && isHX(r, "systems-list") {
		// A search request carries only q; the selection comes from the page URL.
		if cur, err := url.Parse(r.Header.Get("HX-Current-URL")); err == nil {
			cq := cur.Query()
			q.Set("sys", cq.Get("sys"))
			q.Set("tab", cq.Get("tab"))
		}
	}
	s.selectSystem(&b, q)
	now := s.svc.Now()
	src, err := s.svc.CoreSource(r.Context())
	if err != nil {
		return systemsBody{}, err
	}
	b.Cores = coreSourceView{URL: src.URL, LastCheck: "never", LastSuccess: "never", LastError: src.LastError, Skipped: src.Skipped}
	if src.LastCheck != nil {
		b.Cores.LastCheck = ago(*src.LastCheck, now)
	}
	if src.LastSuccess != nil {
		b.Cores.LastSuccess = ago(*src.LastSuccess, now)
	}
	pkgs, err := s.svc.ListCorePackages(r.Context())
	if err != nil {
		return systemsBody{}, err
	}
	for _, p := range pkgs {
		row := coreRowView{CoreID: p.CoreID, Version: p.Version, Platform: p.Platform, Size: humanBytes(p.TotalSize()), License: p.License}
		switch n := p.CachedFiles(); {
		case n == len(p.Files):
			row.Cached, row.Class = "yes", "ok"
		case n == 0:
			row.Cached = "no"
		default:
			row.Cached, row.Class = fmt.Sprintf("partial (%d/%d)", n, len(p.Files)), "error dashed"
		}
		b.Cores.Rows = append(b.Cores.Rows, row)
	}
	return b, nil
}

// chips derives the list chip, the summary chip and the firmware hint of a system.
func (v *systemView) chips() {
	switch {
	case v.FwBad > 0:
		v.ChipText, v.ChipClass = plural(v.FwBad, "firmware", "firmware"), "error"
		v.SummaryText, v.SummaryClass = fmt.Sprintf("✕ Not ready · %s missing or invalid", plural(v.FwBad, "file", "files")), "error"
	case v.ClientsBlocking > 0:
		v.ChipText, v.ChipClass = plural(v.ClientsBad, "client", "clients"), "error"
		v.SummaryText, v.SummaryClass = fmt.Sprintf("✕ %s cannot start games: Player too old", plural(v.ClientsBlocking, "client", "clients")), "error"
	case v.ClientsBad > 0:
		v.ChipText, v.ChipClass = plural(v.ClientsBad, "client", "clients"), "warn"
		v.SummaryText, v.SummaryClass = fmt.Sprintf("▲ %s differ from the registry · launching stays allowed", plural(v.ClientsBad, "client", "clients")), "warn"
	default:
		v.ChipText, v.ChipClass = "● Ready", "ok"
		v.SummaryText, v.SummaryClass = "● Ready", "ok"
	}
	switch n := len(v.Firmware); {
	case n == 0:
		v.FirmwareHint = "none"
	default:
		v.FirmwareHint = plural(n, "file", "files") + " · see the Firmware tab"
	}
}

// selectSystem applies the search and the selection (sys, tab, q) of the request to the body.
func (s *Server) selectSystem(b *systemsBody, q url.Values) {
	all := b.Systems
	b.Total, b.Systems = len(all), nil
	b.Query = strings.TrimSpace(q.Get("q"))
	b.Tab = q.Get("tab")
	if b.Tab != tabClients && b.Tab != tabCore {
		b.Tab = tabFirmware
	}
	needle := strings.ToLower(b.Query)
	sel := -1
	for i := range all {
		if all[i].ID == q.Get("sys") {
			sel = i
		}
	}
	for i := range all {
		v := all[i]
		if needle != "" && !strings.Contains(strings.ToLower(v.Name+" "+v.ID+" "+v.CoreName+" "+v.CoreID), needle) {
			continue
		}
		b.Systems = append(b.Systems, v)
	}
	if sel < 0 && len(all) > 0 {
		sel = 0 // default: the first system of the registry (also while searching)
	}
	for i := range b.Systems {
		b.Systems[i].Href = systemsURL(b.Systems[i].ID, b.Tab, b.Query)
		b.Systems[i].Selected = sel >= 0 && b.Systems[i].ID == all[sel].ID
	}
	if sel >= 0 {
		b.Sel = &all[sel]
		b.TabHrefs = map[string]string{}
		for _, t := range []string{tabFirmware, tabClients, tabCore} {
			b.TabHrefs[t] = systemsURL(all[sel].ID, t, b.Query)
		}
		b.DetailURL = systemsURL(all[sel].ID, b.Tab, b.Query)
	}
	b.ListURL = systemsURL("", "", b.Query)
	if b.Sel != nil {
		b.ListURL = systemsURL(b.Sel.ID, b.Tab, b.Query)
	}
}

func sizeList(sizes []int64) string {
	parts := make([]string, len(sizes))
	for i, n := range sizes {
		parts[i] = humanBytes(n)
	}
	return strings.Join(parts, " or ")
}

func (s *Server) renderSystems(w http.ResponseWriter, r *http.Request, sess *session, status int, errMsg string) {
	body, err := s.systemsBodyData(r)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	d := s.base(r, sess, "systems", "Systems & Cores")
	d.Body = body
	if errMsg != "" {
		d.Error = errMsg
	}
	switch {
	case isHX(r, "systems-list"):
		d.Fragment = true
		s.render(w, status, "systems", "systems-list", d)
	case isHX(r, "systems-detail") && body.Sel != nil:
		d.Fragment = true
		body.OOB = true
		d.Body = body
		s.render(w, status, "systems", "systems-detail-fragment", d)
	default:
		s.render(w, status, "systems", "layout", d)
	}
}

func (s *Server) systemsGet(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderSystems(w, r, sess, http.StatusOK, "")
}

// systemResult redirects on success (keeping the selected system and tab) and renders an error message for business errors.
func (s *Server) systemResult(w http.ResponseWriter, r *http.Request, sess *session, err error, ok, tab string) {
	var he *hub.Error
	sys := r.PathValue("id")
	to := func(key, val string) string {
		return "/systems?sys=" + url.QueryEscape(sys) + "&tab=" + tab + "&" + key + "=" + val
	}
	switch {
	case err == nil:
		http.Redirect(w, r, to("ok", ok), http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, to("err", "nofile"), http.StatusSeeOther)
	case errors.As(err, &he) && he.Code == hub.CodeBadRequest:
		q := r.URL.Query()
		q.Set("sys", sys)
		q.Set("tab", tab)
		r.URL.RawQuery = q.Encode()
		s.renderSystems(w, r, sess, http.StatusBadRequest, he.Message+".")
	default:
		s.fail(w, r, err)
	}
}

func (s *Server) systemVersion(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.SetExpectedCoreVersion(r.Context(), r.PathValue("id"), r.PostFormValue("version")), "version", tabCore)
}

func (s *Server) systemFirmwareMode(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.SetFirmwareMode(r.Context(), r.PathValue("id"), hub.FirmwareMode(r.PostFormValue("mode"))), "fwmode", tabFirmware)
}

func (s *Server) firmwarePin(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.SetFirmwarePin(r.Context(), r.PathValue("id"), r.PathValue("file"), r.PostFormValue("sha256")), "fwpin", tabFirmware)
}

func (s *Server) firmwareRemove(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.RemoveFirmware(r.Context(), r.PathValue("id"), r.PathValue("file")), "fwremoved", tabFirmware)
}

// firmwareUpload takes a multipart form (_csrf, file). The body is bounded; the bytes are never logged.
func (s *Server) firmwareUpload(w http.ResponseWriter, r *http.Request, sess *session) {
	r.Body = http.MaxBytesReader(w, r.Body, maxFirmwareForm)
	if err := r.ParseMultipartForm(maxFirmwareForm); err != nil {
		s.renderSystems(w, r, sess, http.StatusBadRequest, "The file is too large or the form is invalid.")
		return
	}
	if r.Context().Value(keyCSRFChecked) != true && !hub.TokenEqual(r.PostFormValue("_csrf"), sess.CSRFToken) {
		http.Error(w, "CSRF check failed", http.StatusForbidden)
		return
	}
	f, _, err := r.FormFile("file")
	if err != nil {
		s.renderSystems(w, r, sess, http.StatusBadRequest, "Please select a file.")
		return
	}
	defer f.Close()
	_, err = s.svc.ProvideFirmware(r.Context(), r.PathValue("id"), r.PathValue("file"), f)
	s.systemResult(w, r, sess, err, "fwfile", tabFirmware)
}

// coresSync triggers a sync of the core source in the background ("Check source now").
func (s *Server) coresSync(w http.ResponseWriter, r *http.Request, _ *session) {
	s.svc.TriggerCoreSync()
	to := "/systems?ok=coresync"
	if sys := r.PostFormValue("sys"); sys != "" {
		to = "/systems?sys=" + url.QueryEscape(sys) + "&tab=core&ok=coresync"
	}
	http.Redirect(w, r, to, http.StatusSeeOther)
}
