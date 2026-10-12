package web

import (
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"sort"
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
	// Line is the mono line below the title ("sha256 24f6…9d1e · matches pin"); Provided is when the file arrived.
	Line     string
	Provided When
	PinOpen  bool
	SHA      string // full SHA-256 of the provided file
}

type clientView struct {
	Device, Platform, Versions, Status, StatusText string
	OK                                             bool
	Blocking                                       bool // only an incompatible protocol blocks launching (ADR 0007 D3)
}

type coreInstalledView struct {
	CoreID, Name, Version, License, Origin, Build, HWAPI, Platforms, UpdateDate string
	Default, Experimental, Update                                               bool
	// Updates column: pill label and class, the line below it, and whether the core is pinned (Auto/Off).
	UpdLabel, UpdClass, UpdSub, BuildTitle string
	Pinned                                 bool
}

type coreAvailView struct {
	CoreID, Name, License, HWAPI, Platforms, BuildDate, Extensions, Notes string
	Experimental, NonCommercial                                           bool
}

type coreSourceView struct {
	URL, LastCheck, LastSuccess, LastError string
	LastErrorShort                         string // one sentence; the raw error goes in a tooltip
	Count                                  int
	AutoUpdate                             bool
	AutoUpdateError                        string
}

type systemView struct {
	Installed                                                     []coreInstalledView
	Available                                                     []coreAvailView
	ID, Name, CoreID, CoreName, Expected, Provisioning, Platforms string
	Extensions, InputProfile, DisplayProfile, Mode, LibretroIDs   string
	Native, NoCore                                                bool
	Firmware                                                      []fwView
	Clients                                                       []clientView
	// List pill and header pill (Ready / n updates / Needs attention), tab counters.
	ChipText, ChipClass, SummaryText, SummaryClass string
	FwBad, ClientsBad, ClientsBlocking, Updates    int
	Href                                           string
	Selected                                       bool
	FirmwareHint                                   string
	// AutoMode is the per-system override ("" use the Hub default, "on", "off"); AutoEffective the result.
	AutoMode      string
	AutoEffective bool
	CoreLine      string // "Default core: melonDS DS"
	NCores        string // "3 cores", installed plus available of this system
}

// Tabs of the system detail (order of the tab bar: the core is what every system needs).
const (
	tabCore     = "core"
	tabFirmware = "firmware"
	tabClients  = "clients"
)

type systemsBody struct {
	Systems []systemView
	Sel     *systemView
	Tab     string
	Total   int // systems in the registry
	Cores   coreSourceView
	// ListURL and DetailURL are the URLs the live regions reload themselves from; OOB marks the list as part of a detail fragment.
	ListURL, DetailURL string
	TabHrefs           map[string]string
	OOB                bool
	// HubAutoLabel is "On" or "Off" for the Hub default (the first segment reads "Use Hub default (On)").
	HubAutoLabel string
}

// systemsURL builds a Systems URL (selection and tab; the Cores tab is the default and stays out of the URL).
func systemsURL(sys, tab string) string {
	v := url.Values{}
	if sys != "" {
		v.Set("sys", sys)
	}
	if tab != "" && tab != tabCore {
		v.Set("tab", tab)
	}
	if len(v) == 0 {
		return "/systems"
	}
	return "/systems?" + v.Encode()
}

// systemRank orders the systems by family and generation (design 3l-2): GB, GBC, GBA, DS, 3DS; unknown ids follow by name.
var systemRank = map[string]int{"gb": 1, "gbc": 2, "gba": 3, "nds": 4, "3ds": 5}

func sortSystems(v []systemView) {
	sort.SliceStable(v, func(i, j int) bool {
		ri, rj := systemRank[v[i].ID], systemRank[v[j].ID]
		switch {
		case ri != 0 && rj != 0:
			return ri < rj
		case ri != 0:
			return true
		case rj != 0:
			return false
		}
		return strings.ToLower(v[i].Name) < strings.ToLower(v[j].Name)
	})
}

func statusView(f hub.FirmwareFile) (label, class string) {
	switch f.State {
	case hub.FirmwareValid:
		return "Provided", "ok"
	case hub.FirmwareMismatch:
		return "Hash mismatch", "amber"
	case hub.FirmwareMissing:
		return "Missing", "amber"
	}
	return "Optional · not provided", ""
}

func (s *Server) systemsBodyData(r *http.Request) (systemsBody, error) {
	reg, err := s.svc.ListRegistry(r.Context())
	if err != nil {
		return systemsBody{}, err
	}
	devices, err := s.svc.LoadDeviceReports(r.Context()) // once for all systems
	if err != nil {
		return systemsBody{}, err
	}
	now := s.svc.Now()
	src, err := s.svc.CoreSource(r.Context())
	if err != nil {
		return systemsBody{}, err
	}
	var b systemsBody
	b.HubAutoLabel = onOff(src.AutoUpdate)
	for _, e := range reg {
		v := systemView{ID: e.ID, Name: e.Name, CoreID: e.CoreID, CoreName: e.CoreName, Expected: e.ExpectedCoreVersion,
			Provisioning: e.Provisioning, Platforms: strings.Join(e.Platforms, ", "), Extensions: strings.Join(e.Extensions, ", "),
			InputProfile: e.InputProfile, DisplayProfile: e.DisplayProfile, Mode: string(e.FirmwareMode),
			Native: e.FirmwareMode == hub.FirmwareNative, NoCore: e.CoreID == "", AutoMode: e.AutoUpdate}
		v.AutoEffective = src.AutoUpdate
		switch e.AutoUpdate {
		case hub.SystemAutoOn:
			v.AutoEffective = true
		case hub.SystemAutoOff:
			v.AutoEffective = false
		}
		v.LibretroIDs = strings.Join(e.LibretroIDs, ", ")
		sc, err := s.svc.SystemCores(r.Context(), e.ID)
		if err != nil {
			return systemsBody{}, err
		}
		for _, c := range sc.Installed {
			cv := coreInstalledView{CoreID: c.CoreID, Name: c.DisplayName, Version: c.Version, License: c.License, HWAPI: c.RequiredHWAPI,
				Default: c.Default, Experimental: c.Experimental, Update: c.UpdateAvailable, UpdateDate: c.UpdateDate,
				Platforms: strings.Join(c.Platforms, ", "), Build: c.BuildDate, Pinned: c.AutoUpdate == hub.CoreAutoOff}
			if c.Origin == hub.OriginBuildbot {
				cv.Origin = "Buildbot"
				cv.BuildTitle = "libretro buildbot · build " + c.BuildDate + " · nightly"
			} else {
				cv.Origin = c.Origin
				cv.BuildTitle = "build " + c.BuildDate
			}
			if cv.License == "" {
				cv.License = "unknown"
			}
			switch {
			case cv.Pinned:
				cv.UpdLabel, cv.UpdClass = "Off · pinned", "amber"
				cv.UpdSub = "pinned to this build"
			case v.AutoEffective:
				cv.UpdLabel, cv.UpdClass = "Auto", "ok"
				cv.UpdSub = "follows system"
			default:
				cv.UpdLabel, cv.UpdClass = "Auto", ""
				cv.UpdSub = "follows system"
			}
			if cv.Update {
				cv.UpdSub = cv.UpdateDate + " available"
				v.Updates++
			}
			v.Installed = append(v.Installed, cv)
		}
		for _, c := range sc.Available {
			av := coreAvailView{CoreID: c.CoreID, Name: c.DisplayName, License: c.License, HWAPI: c.RequiredHWAPI, BuildDate: c.BuildDate,
				Platforms: strings.Join(c.Platforms, ", "), Extensions: strings.Join(c.Extensions, ", "), Notes: strings.Join(c.Notes, " · "),
				Experimental: c.Experimental, NonCommercial: c.NonCommercial}
			if av.License == "" {
				av.License = "unknown"
			}
			v.Available = append(v.Available, av)
		}
		v.NCores = plural(len(v.Installed)+len(v.Available), "core", "cores")
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
				fv.SHA = f.SHA256
				fv.Provided = when(f.ProvidedAt, now)
				fv.Line = "sha256 " + shortHash(f.SHA256)
				switch {
				case f.Pinned == "":
					fv.Line += " · no pin"
				case f.State == hub.FirmwareMismatch:
					fv.Line += " · does not match the pin " + shortHash(f.Pinned)
				default:
					fv.Line += " · matches pin"
				}
			}
			fv.StateLabel, fv.Class = statusView(f)
			if f.Present && f.State != hub.FirmwareMismatch {
				fv.StateLabel, fv.Class = "Provided", "ok"
				if fv.Provided.Title != "" {
					fv.StateLabel += " · " + fv.Provided.Text
				}
			}
			fv.PinOpen = f.Pinned != "" || !f.Present
			switch {
			case f.State == hub.FirmwareMismatch:
				fv.Note = "The file does not match the expected SHA-256."
			case f.State == hub.FirmwareMissing && f.Required:
				fv.Note = "Games for this system cannot start until this file is provided."
			}
			if f.Required && (f.State == hub.FirmwareMissing || f.State == hub.FirmwareMismatch) {
				v.FwBad++
			}
			v.Firmware = append(v.Firmware, fv)
		}
		for _, c := range devices.ClientReports(e) {
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
			cv.Versions = "Player " + c.PlayerVersion + " · " + coreLabel(e.CoreName) + " " + core
			switch c.Status {
			case hub.ClientCompatible:
				cv.OK, cv.StatusText = true, "Compatible"
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
	sortSystems(b.Systems)
	s.selectSystem(&b, r.URL.Query())
	short := ""
	if e := src.LastError; e != "" {
		short = "Could not refresh the core catalog."
		for _, k := range []string{"dial tcp", "connection", "no such host", "timeout", "Get \"", "EOF"} {
			if strings.Contains(e, k) {
				short = "Could not reach the core source."
				break
			}
		}
	}
	b.Cores = coreSourceView{LastErrorShort: short, URL: strings.TrimPrefix(src.BuildbotURL, "https://"), LastCheck: "never", LastSuccess: "never", LastError: src.LastError, Count: src.Cores,
		AutoUpdate: src.AutoUpdate, AutoUpdateError: src.AutoUpdateError}
	if src.LastCheck != nil {
		b.Cores.LastCheck = ago(*src.LastCheck, now)
	}
	if src.LastSuccess != nil {
		b.Cores.LastSuccess = ago(*src.LastSuccess, now)
	}
	return b, nil
}

func onOff(on bool) string {
	if on {
		return "On"
	}
	return "Off"
}

// chips derives the list pill, the header pill and the firmware hint of a system. Decided: "Needs attention" is a
// missing core or missing (or mismatching) required firmware; an available update is information (cyan).
func (v *systemView) chips() {
	switch {
	case v.NoCore:
		v.ChipText, v.ChipClass = "Needs attention", "amber"
		v.SummaryText, v.SummaryClass = "Needs attention · no core installed", "amber"
	case v.FwBad > 0:
		v.ChipText, v.ChipClass = "Needs attention", "amber"
		v.SummaryText, v.SummaryClass = fmt.Sprintf("Needs attention · %s missing or invalid", plural(v.FwBad, "firmware file", "firmware files")), "amber"
	case v.Updates > 0:
		v.ChipText, v.ChipClass = plural(v.Updates, "update", "updates"), "info"
		v.SummaryText, v.SummaryClass = plural(v.Updates, "update", "updates")+" available", "info"
	default:
		v.ChipText, v.ChipClass = "Ready", "ok"
		v.SummaryText, v.SummaryClass = "Ready", "ok"
	}
	switch n := len(v.Firmware); {
	case n == 0:
		v.FirmwareHint = "none"
	default:
		v.FirmwareHint = plural(n, "file", "files") + " · see the Firmware tab"
	}
	if v.CoreName != "" {
		v.CoreLine = "Default core: " + v.CoreName
	} else {
		v.CoreLine = "No core installed"
	}
}

// selectSystem applies the selection (sys, tab) of the request to the body.
func (s *Server) selectSystem(b *systemsBody, q url.Values) {
	all := b.Systems
	b.Total = len(all)
	b.Tab = q.Get("tab")
	if b.Tab != tabClients && b.Tab != tabFirmware {
		b.Tab = tabCore
	}
	sel := -1
	for i := range all {
		if all[i].ID == q.Get("sys") {
			sel = i
		}
	}
	if sel < 0 && len(all) > 0 {
		// Default: the first system that needs attention, else the first with an update, else the first.
		sel = 0
		for _, pass := range []func(v systemView) bool{
			func(v systemView) bool { return v.NoCore || v.FwBad > 0 },
			func(v systemView) bool { return v.Updates > 0 },
		} {
			found := -1
			for i := range all {
				if pass(all[i]) {
					found = i
					break
				}
			}
			if found >= 0 {
				sel = found
				break
			}
		}
	}
	for i := range all {
		all[i].Href = systemsURL(all[i].ID, b.Tab)
		all[i].Selected = i == sel
	}
	b.Systems = all
	if sel >= 0 {
		b.Sel = &all[sel]
		b.TabHrefs = map[string]string{}
		for _, t := range []string{tabCore, tabFirmware, tabClients} {
			b.TabHrefs[t] = systemsURL(all[sel].ID, t)
		}
		b.DetailURL = systemsURL(all[sel].ID, b.Tab)
	}
	b.ListURL = systemsURL("", "")
	if b.Sel != nil {
		b.ListURL = systemsURL(b.Sel.ID, b.Tab)
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

func coreLabel(name string) string {
	if name == "" {
		return "core"
	}
	return name
}

// coreResult is systemResult for the core actions; an unknown system or core is reported as such.
func (s *Server) coreResult(w http.ResponseWriter, r *http.Request, sess *session, err error, ok string) {
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/systems?sys="+url.QueryEscape(r.PathValue("id"))+"&tab=core&err=nocore", http.StatusSeeOther)
		return
	}
	s.systemResult(w, r, sess, err, ok, tabCore)
}

func (s *Server) coreInstall(w http.ResponseWriter, r *http.Request, sess *session) {
	_, err := s.svc.InstallCore(r.Context(), r.PathValue("id"), r.PathValue("core"))
	s.coreResult(w, r, sess, err, "coreinstalled")
}

func (s *Server) coreUpdate(w http.ResponseWriter, r *http.Request, sess *session) {
	_, err := s.svc.UpdateCore(r.Context(), r.PathValue("id"), r.PathValue("core"))
	s.coreResult(w, r, sess, err, "coreupdated")
}

// coreRemoveConfirm asks before a core is removed (the destructive menu item ends with "…").
func (s *Server) coreRemoveConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	sysID, coreID := r.PathValue("id"), r.PathValue("core")
	e, err := s.svc.GetRegistryEntry(r.Context(), sysID)
	if err != nil {
		http.Redirect(w, r, "/systems?err=nocore", http.StatusSeeOther)
		return
	}
	name := ""
	for _, c := range e.Cores {
		if c.CoreID == coreID {
			name = c.DisplayName
		}
	}
	if name == "" {
		http.Redirect(w, r, "/systems?sys="+url.QueryEscape(sysID)+"&err=nocore", http.StatusSeeOther)
		return
	}
	d := s.base(r, sess, "systems", "Remove core")
	d.Body = confirmBody{
		Heading: "Remove " + name + " from " + e.Name + "?",
		Intro:   "Players can no longer download this core for " + e.Name + " from this Hub.",
		Removes: []string{"The core and its files are removed from this Hub", "Games for " + e.Name + " cannot start on a Player until a core is installed again"},
		Note:    "Players keep the copy they already downloaded. You can install the core again from the buildbot.",
		Action:  "/systems/" + url.PathEscape(sysID) + "/cores/" + url.PathEscape(coreID) + "/remove", Button: "Remove core",
		Cancel: systemsURL(sysID, tabCore),
	}
	s.render(w, http.StatusOK, "confirm", "layout", d)
}

func (s *Server) coreRemove(w http.ResponseWriter, r *http.Request, sess *session) {
	s.coreResult(w, r, sess, s.svc.RemoveCore(r.Context(), r.PathValue("id"), r.PathValue("core")), "coreremoved")
}

func (s *Server) coreDefault(w http.ResponseWriter, r *http.Request, sess *session) {
	s.coreResult(w, r, sess, s.svc.SetDefaultCore(r.Context(), r.PathValue("id"), r.PathValue("core")), "coredefault")
}

// systemAutoUpdate sets the per-system override of the automatic core update (mode default, on or off).
func (s *Server) systemAutoUpdate(w http.ResponseWriter, r *http.Request, sess *session) {
	mode := ""
	switch m := r.PostFormValue("mode"); m {
	case "default":
	case hub.SystemAutoOn, hub.SystemAutoOff:
		mode = m
	default:
		s.systemResult(w, r, sess, &hub.Error{Code: hub.CodeBadRequest, Message: "Unknown automatic update mode"}, "", tabCore)
		return
	}
	s.coreResult(w, r, sess, s.svc.SetSystemCoreAutoUpdate(r.Context(), r.PathValue("id"), mode), "coreautoupdate")
}

// coreAutoUpdate pins a core (mode off) or lets it follow its system again (mode auto).
func (s *Server) coreAutoUpdate(w http.ResponseWriter, r *http.Request, sess *session) {
	var pinned bool
	switch r.PostFormValue("mode") {
	case "auto":
	case "off":
		pinned = true
	default:
		s.systemResult(w, r, sess, &hub.Error{Code: hub.CodeBadRequest, Message: "Unknown automatic update mode"}, "", tabCore)
		return
	}
	s.coreResult(w, r, sess, s.svc.SetCoreAutoUpdatePin(r.Context(), r.PathValue("id"), r.PathValue("core"), pinned), "coreautoupdate")
}

func (s *Server) systemFirmwareMode(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.SetFirmwareMode(r.Context(), r.PathValue("id"), hub.FirmwareMode(r.PostFormValue("mode"))), "fwmode", tabFirmware)
}

func (s *Server) firmwarePin(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.SetFirmwarePin(r.Context(), r.PathValue("id"), r.PathValue("file"), r.PostFormValue("sha256")), "fwpin", tabFirmware)
}

// firmwareRemoveConfirm asks before a provided firmware file is removed.
func (s *Server) firmwareRemoveConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	sysID, fileID := r.PathValue("id"), r.PathValue("file")
	e, err := s.svc.GetRegistryEntry(r.Context(), sysID)
	if err != nil {
		http.Redirect(w, r, "/systems?err=nofile", http.StatusSeeOther)
		return
	}
	for _, f := range e.Firmware {
		if f.ID != fileID || !f.Present {
			continue
		}
		d := s.base(r, sess, "systems", "Remove firmware file")
		d.Body = confirmBody{
			Heading: "Remove " + f.DisplayName + " (" + e.Name + ")?",
			Intro:   "The file is deleted from this Hub.",
			Removes: []string{"The stored file (" + humanBytes(f.Size) + ", sha256 " + shortHash(f.SHA256) + ")", "Players that need it cannot get it from this Hub until you provide it again"},
			Note:    "A pinned SHA-256 stays. Players keep files they already have.",
			Action:  "/systems/" + url.PathEscape(sysID) + "/firmware/" + url.PathEscape(fileID) + "/remove", Button: "Remove file",
			Cancel: systemsURL(sysID, tabFirmware),
		}
		s.render(w, http.StatusOK, "confirm", "layout", d)
		return
	}
	http.Redirect(w, r, systemsURL(sysID, tabFirmware)+"&err=nofile", http.StatusSeeOther)
}

func (s *Server) firmwareRemove(w http.ResponseWriter, r *http.Request, sess *session) {
	s.systemResult(w, r, sess, s.svc.RemoveFirmware(r.Context(), r.PathValue("id"), r.PathValue("file")), "fwremoved", tabFirmware)
}

// firmwareDownload serves a provided firmware file to the admin ("Download file" in the row menu).
func (s *Server) firmwareDownload(w http.ResponseWriter, r *http.Request, _ *session) {
	f, def, err := s.svc.OpenFirmware(r.Context(), r.PathValue("id"), r.PathValue("file"))
	if errors.Is(err, hub.ErrNotFound) {
		http.NotFound(w, r)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	defer f.Close()
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", `attachment; filename="`+r.PathValue("id")+"-"+def.ID+`.bin"`)
	http.ServeContent(w, r, "", def.ProvidedAt, f)
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

// coresSync triggers a sync of the core source in the background ("Check now").
func (s *Server) coresSync(w http.ResponseWriter, r *http.Request, _ *session) {
	s.svc.TriggerCoreSync()
	to := "/systems?ok=coresync"
	if sys := r.PostFormValue("sys"); sys != "" {
		to = "/systems?sys=" + url.QueryEscape(sys) + "&tab=core&ok=coresync"
	}
	http.Redirect(w, r, to, http.StatusSeeOther)
}

// coresAutoUpdate switches the Hub default of the automatic core update on or off (field "on" = "1" or "0").
func (s *Server) coresAutoUpdate(w http.ResponseWriter, r *http.Request, _ *session) {
	on := r.PostFormValue("on") == "1"
	if err := s.svc.SetCoreAutoUpdate(r.Context(), on); err != nil {
		http.Error(w, "internal error", http.StatusInternalServerError)
		return
	}
	to := "/systems?ok=coreautoupdate"
	if sys := r.PostFormValue("sys"); sys != "" {
		to = "/systems?sys=" + url.QueryEscape(sys) + "&tab=core&ok=coreautoupdate"
	}
	http.Redirect(w, r, to, http.StatusSeeOther)
}
