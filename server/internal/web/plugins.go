package web

import (
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"sort"
	"strings"
	"unicode"

	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/pluginpkg"
)

// permissionLabels are the plain-language names of the plugin permissions (docs/design/plugins.md).
var permissionLabels = map[string]string{
	"memory.read":  "Read game memory",
	"memory.write": "Change game memory",
	"rom.read":     "Read the game file",
	"ui":           "Show panels in game",
	"run.state":    "Share run progress with your Hub",
	"tool.exec":    "Run an external tool",
}

type permView struct{ ID, Label string }

func permViews(ids []string) []permView {
	out := make([]permView, 0, len(ids))
	for _, id := range ids {
		l := permissionLabels[id]
		if l == "" {
			l = id
		}
		out = append(out, permView{ID: id, Label: l})
	}
	return out
}

func permSummary(ids []string) string {
	if len(ids) == 0 {
		return "None"
	}
	var l []string
	for _, p := range permViews(ids) {
		l = append(l, p.Label)
	}
	return strings.Join(l, ", ")
}

type pluginRow struct {
	ID, Version, Name, Mono, Href string
	OriginLabel, OriginClass      string
	Perms                         string
	Rel                           string // "Needs X · permissions" / "Used by Y · permissions"
	Selected                      bool
}

// needView is one dependency in the detail panel.
type needView struct {
	Name, Mono, Range string
	Present           bool
}

type pluginDetail struct {
	pluginRow
	Meta, SHA256, Size, Description, Provides, Requires string
	Unverified                                          bool
	Needs                                               []needView
	UsedBy                                              []string
	ShortSHA                                            string
	Perms                                               []permView
	PermsTitle                                          string
	DownloadHref, RemoveHref                            string
}

type catalogCard struct {
	ID, Version, Name, Mono, Description string
	State, InstalledVersion              string
	Install, Update, Installed           bool
}

type catalogView struct {
	Configured   bool
	LastErr      string
	Cards        []catalogCard
	CheckedLine  string // "signed · checked today 17:52 · 2 plugins"
	CheckedTitle string
	Failed       bool // the last check failed: the error variant of the status line
	ErrLine      string
	NoCatalogYet bool
}

type reviewView struct {
	Stage, Name, Version, Mono string
	Perms                      []permView
	OriginLine, Heading        string
	Official, Signed           bool
	SigProblem                 string
	Notice                     string // already offered / conflict
	Err                        string
	CanOffer                   bool
}

type pluginsBody struct {
	Rows       []pluginRow
	Sel        *pluginDetail
	Catalog    catalogView
	Review     *reviewView
	UploadOpen bool
}

func originLabel(p hub.Plugin) (label, class string) {
	switch {
	case p.Origin == hub.PluginOriginOfficial:
		return "✓ Official", "ok"
	case p.Source == hub.PluginSourceImport:
		return "Hub import · unverified", "amber"
	}
	return "Hub upload · unverified", "amber"
}

func pluginHref(id, version string) string {
	return "/plugins/" + url.PathEscape(id) + "/" + url.PathEscape(version)
}

func toRow(p hub.Plugin) pluginRow {
	l, c := originLabel(p)
	return pluginRow{ID: p.PluginID, Version: p.Version, Name: p.Name, Mono: monogram(p.Name), Href: pluginHref(p.PluginID, p.Version),
		OriginLabel: l, OriginClass: c, Perms: permSummary(p.Permissions())}
}

// monogram is the two-letter tile of a plugin: initials of the first two words that contain a letter or digit
// ("SoulLink & Nuzlocke" is "SN"), else the first two letters of the name.
func monogram(name string) string {
	var words []string
	for _, w := range strings.Fields(name) {
		if strings.IndexFunc(w, func(r rune) bool { return unicode.IsLetter(r) || unicode.IsDigit(r) }) >= 0 {
			words = append(words, w)
		}
	}
	if len(words) >= 2 {
		return initial(words[0]) + initial(words[1])
	}
	var r []rune
	for _, c := range strings.ToUpper(name) {
		if unicode.IsLetter(c) || unicode.IsDigit(c) {
			r = append(r, c)
		}
	}
	if len(r) >= 2 {
		return string(r[:2])
	}
	return initial(name)
}

// rangeText turns a manifest version range into words: "^0.3" and ">=0.3" read "0.3 or newer".
func rangeText(r string) string {
	r = strings.TrimSpace(r)
	for _, p := range []string{"^", ">=", "~"} {
		if strings.HasPrefix(r, p) {
			return strings.TrimSpace(strings.TrimPrefix(r, p)) + " or newer"
		}
	}
	if r == "" || r == "*" {
		return "any version"
	}
	return r
}

type pluginsOpts struct {
	status     int
	sel        *hub.Plugin
	review     *reviewView
	err        string
	uploadOpen bool
}

func (s *Server) renderPlugins(w http.ResponseWriter, r *http.Request, sess *session, o pluginsOpts) {
	ctx := r.Context()
	ps, err := s.svc.ListPlugins(ctx)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	st, err := s.svc.PluginIndexStatus(ctx)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	b := pluginsBody{Review: o.review, UploadOpen: o.uploadOpen}
	// Dependencies between the installed plugins: the newest version of each plugin id speaks for it.
	newest := map[string]hub.Plugin{}
	for _, p := range ps { // oldest first: the last one wins
		newest[p.PluginID] = p
	}
	usedBy := map[string][]string{}
	for _, p := range newest {
		for id := range p.Manifest.Requires {
			usedBy[id] = append(usedBy[id], p.Name)
		}
	}
	for _, l := range usedBy {
		sort.Strings(l)
	}
	relOf := func(p hub.Plugin) string {
		var parts []string
		var needs []string
		for id := range p.Manifest.Requires {
			if n, ok := newest[id]; ok {
				needs = append(needs, n.Name)
			} else {
				needs = append(needs, id)
			}
		}
		sort.Strings(needs)
		if len(needs) > 0 {
			parts = append(parts, "Needs "+strings.Join(needs, ", "))
		}
		if u := usedBy[p.PluginID]; len(u) > 0 {
			parts = append(parts, "Used by "+strings.Join(u, ", "))
		}
		parts = append(parts, permSummary(p.Permissions()))
		return strings.Join(parts, " · ")
	}
	// Newest version first within a plugin.
	sort.SliceStable(ps, func(i, j int) bool { return ps[i].PluginID < ps[j].PluginID })
	for i := 0; i < len(ps); {
		j := i
		for j < len(ps) && ps[j].PluginID == ps[i].PluginID {
			j++
		}
		for k := j - 1; k >= i; k-- { // ListPlugins is oldest first
			row := toRow(ps[k])
			row.Rel = relOf(ps[k])
			row.Selected = o.sel != nil && o.sel.PluginID == ps[k].PluginID && o.sel.Version == ps[k].Version
			b.Rows = append(b.Rows, row)
		}
		i = j
	}
	if o.sel != nil {
		p := *o.sel
		d := &pluginDetail{pluginRow: toRow(p), SHA256: p.SHA256, Size: humanBytes(p.Size), Description: p.Description,
			Unverified: p.Origin != hub.PluginOriginOfficial, Perms: permViews(p.Permissions()),
			DownloadHref: pluginHref(p.PluginID, p.Version) + "/download", RemoveHref: pluginHref(p.PluginID, p.Version) + "/remove"}
		d.PermsTitle = "Permissions"
		if d.Unverified {
			d.PermsTitle = "Permissions · confirmed by admin"
		}
		date := when(p.CreatedAt, s.svc.Now()).Text
		switch {
		case p.Source == hub.PluginSourceIndex:
			d.Meta = p.Version + " · from the plugin index · " + date
		case p.Source == hub.PluginSourceImport:
			d.Meta = p.Version + " · imported from a folder · " + date
		case p.UploadedByName != "":
			d.Meta = p.Version + " · uploaded by " + p.UploadedByName + " · " + date
		default:
			d.Meta = p.Version + " · uploaded · " + date
		}
		d.Provides = strings.Join(p.Manifest.Provides, ", ")
		var ids []string
		for id := range p.Manifest.Requires {
			ids = append(ids, id)
		}
		sort.Strings(ids)
		for _, id := range ids {
			n := needView{Name: id, Mono: monogram(id), Range: rangeText(p.Manifest.Requires[id])}
			if o, ok := newest[id]; ok {
				n.Name, n.Mono, n.Present = o.Name, monogram(o.Name), true
			}
			d.Needs = append(d.Needs, n)
		}
		d.UsedBy = usedBy[p.PluginID]
		d.ShortSHA = shortHash(p.SHA256)
		b.Sel = d
	}
	b.Catalog = catalogView{Configured: st.Configured, LastErr: st.LastError}
	if st.LastCheck != nil {
		w := whenPtr(st.LastCheck, s.svc.Now())
		b.Catalog.CheckedTitle = w.Title
		if st.LastError != "" {
			b.Catalog.Failed = true
			b.Catalog.ErrLine = "Could not load the catalog · " + strings.TrimSuffix(st.LastError, ".") + " · " + w.Text
		} else {
			b.Catalog.CheckedLine = fmt.Sprintf("signed · checked %s · %s", w.Text, plural(len(st.Catalog), "plugin", "plugins"))
		}
	} else if st.Configured {
		b.Catalog.CheckedLine = "not checked yet"
		if st.LastError != "" {
			b.Catalog.Failed = true
			b.Catalog.ErrLine = "Could not load the catalog · " + strings.TrimSuffix(st.LastError, ".")
		}
	}
	b.Catalog.NoCatalogYet = false
	for _, c := range st.Catalog {
		b.Catalog.Cards = append(b.Catalog.Cards, catalogCard{ID: c.ID, Version: c.Version, Name: c.Name, Mono: monogram(c.Name), Description: c.Description,
			State: c.State, InstalledVersion: c.InstalledVersion, Install: c.State == hub.CatalogInstall, Update: c.State == hub.CatalogUpdate,
			Installed: c.State == hub.CatalogInstalled})
	}
	d := s.base(r, sess, "plugins", "Plugins")
	d.Body = b
	if o.err != "" {
		d.Error = o.err
	}
	if o.status == 0 {
		o.status = http.StatusOK
	}
	s.render(w, o.status, "plugins", "layout", d)
}

func (s *Server) pluginsGet(w http.ResponseWriter, r *http.Request, sess *session) {
	var sel *hub.Plugin
	if id, v := r.PathValue("id"), r.PathValue("version"); id != "" {
		p, err := s.svc.GetPlugin(r.Context(), id, v)
		if errors.Is(err, hub.ErrNotFound) {
			http.Redirect(w, r, "/plugins?err=noplugin", http.StatusSeeOther)
			return
		}
		if err != nil {
			s.fail(w, r, err)
			return
		}
		sel = &p
	}
	s.renderPlugins(w, r, sess, pluginsOpts{sel: sel})
}

func reviewOf(st hub.PluginStaged) *reviewView {
	v := &reviewView{Stage: st.StageID, Name: st.Name, Version: st.Version, Mono: monogram(st.Name), Perms: permViews(st.Permissions),
		Official: st.Official, Signed: st.Signed, SigProblem: st.SignatureProblem, CanOffer: !st.Conflict}
	switch {
	case st.Official:
		v.Heading = "Offer " + st.Name + " " + st.Version + " to Players?"
		v.OriginLine = "Signed by FrameBeam. Players see it as “Official”."
	case st.Signed:
		v.Heading = "Offer " + st.Name + " " + st.Version + " to Players?"
		v.OriginLine = "The signature of this file is not valid (" + st.SignatureProblem + "). FrameBeam has not checked this file. Players will see it as “Hub upload · unverified”."
	default:
		v.Heading = "Offer " + st.Name + " " + st.Version + " to Players?"
		v.OriginLine = "FrameBeam has not checked this file. Players will see it as “Hub upload · unverified”."
	}
	switch {
	case st.Conflict:
		v.Notice = "A different file with this version is already offered. Remove it first or change the version."
	case st.AlreadyOffered:
		v.Notice = "This exact file is already offered to Players."
	}
	return v
}

// pluginUpload reads the file, validates it and shows the review step. Nothing is offered before the admin ticks
// every permission and confirms. Admin only (guard), CSRF checked before the file part is read.
func (s *Server) pluginUpload(w http.ResponseWriter, r *http.Request, sess *session) {
	r.Body = http.MaxBytesReader(w, r.Body, pluginpkg.MaxCompressedBytes+multipartSlack)
	mr, err := r.MultipartReader()
	if err != nil {
		http.Error(w, "Invalid request", http.StatusBadRequest)
		return
	}
	csrfOK := r.Context().Value(keyCSRFChecked) == true
	var staged *hub.PluginStaged
	for {
		part, err := mr.NextPart()
		if err == io.EOF {
			break
		}
		if err != nil {
			s.pluginUploadError(w, r, sess, err)
			return
		}
		switch part.FormName() {
		case "_csrf":
			if hub.TokenEqual(readField(part), sess.CSRFToken) {
				csrfOK = true
			}
		case "file":
			if !csrfOK {
				http.Error(w, "CSRF check failed", http.StatusForbidden)
				return
			}
			if part.FileName() == "" {
				continue
			}
			st, err := s.svc.StagePluginUpload(r.Context(), part)
			if err != nil {
				s.pluginUploadError(w, r, sess, err)
				return
			}
			staged = &st
		}
		part.Close()
	}
	if !csrfOK {
		http.Error(w, "CSRF check failed", http.StatusForbidden)
		return
	}
	if staged == nil {
		s.renderPlugins(w, r, sess, pluginsOpts{status: http.StatusBadRequest, err: "Please select a plugin file.", uploadOpen: true})
		return
	}
	s.renderPlugins(w, r, sess, pluginsOpts{review: reviewOf(*staged)})
}

func (s *Server) pluginUploadError(w http.ResponseWriter, r *http.Request, sess *session, err error) {
	var mbe *http.MaxBytesError
	var he *hub.Error
	switch {
	case errors.As(err, &mbe), errors.Is(err, hub.ErrPayloadTooLarge):
		s.renderPlugins(w, r, sess, pluginsOpts{status: http.StatusRequestEntityTooLarge, err: "The file is too large (maximum 32 MiB).", uploadOpen: true})
	case errors.As(err, &he) && he.Code == hub.CodeBadRequest:
		s.renderPlugins(w, r, sess, pluginsOpts{status: http.StatusBadRequest, err: "This is not a valid plugin file: " + he.Message + ".", uploadOpen: true})
	default:
		s.fail(w, r, err)
	}
}

// pluginOffer is the confirmation: every permission the plugin asks for must be ticked (checked on the server).
func (s *Server) pluginOffer(w http.ResponseWriter, r *http.Request, sess *session) {
	stage := r.PostFormValue("stage")
	_, err := s.svc.OfferStagedPlugin(r.Context(), stage, r.PostForm["perm"], sess.User.ID)
	var he *hub.Error
	switch {
	case err == nil:
		http.Redirect(w, r, "/plugins?ok=pluginoffered", http.StatusSeeOther)
	case errors.Is(err, hub.ErrConflict):
		st, serr := s.svc.StagedPlugin(r.Context(), stage)
		if serr != nil {
			s.renderPlugins(w, r, sess, pluginsOpts{status: http.StatusConflict, err: errMessageOf(err)})
			return
		}
		rv := reviewOf(st)
		rv.Err = errMessageOf(err)
		s.renderPlugins(w, r, sess, pluginsOpts{status: http.StatusConflict, review: rv})
	case errors.As(err, &he) && he.Code == hub.CodeBadRequest:
		st, serr := s.svc.StagedPlugin(r.Context(), stage)
		if serr != nil {
			s.renderPlugins(w, r, sess, pluginsOpts{status: http.StatusBadRequest, err: he.Message, uploadOpen: true})
			return
		}
		rv := reviewOf(st)
		rv.Err = he.Message
		s.renderPlugins(w, r, sess, pluginsOpts{status: http.StatusBadRequest, review: rv})
	default:
		s.fail(w, r, err)
	}
}

func errMessageOf(err error) string {
	var he *hub.Error
	if errors.As(err, &he) {
		return he.Message
	}
	return err.Error()
}

func (s *Server) pluginDiscard(w http.ResponseWriter, r *http.Request, _ *session) {
	s.svc.DiscardStagedPlugin(r.PostFormValue("stage"))
	http.Redirect(w, r, "/plugins", http.StatusSeeOther)
}

func (s *Server) pluginDownload(w http.ResponseWriter, r *http.Request, _ *session) {
	f, p, err := s.svc.OpenPluginFile(r.Context(), r.PathValue("id"), r.PathValue("version"))
	if errors.Is(err, hub.ErrNotFound) {
		http.NotFound(w, r)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	defer f.Close()
	w.Header().Set("Content-Type", "application/zip")
	w.Header().Set("Content-Disposition", `attachment; filename="`+p.PluginID+"-"+p.Version+`.fbplugin"`)
	w.Header().Set("ETag", `"`+p.SHA256+`"`)
	http.ServeContent(w, r, "", p.CreatedAt, f)
}

func (s *Server) pluginRemoveConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	p, err := s.svc.GetPlugin(r.Context(), r.PathValue("id"), r.PathValue("version"))
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/plugins?err=noplugin", http.StatusSeeOther)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	s.renderPluginRemove(w, r, sess, p, http.StatusOK, "")
}

func (s *Server) renderPluginRemove(w http.ResponseWriter, r *http.Request, sess *session, p hub.Plugin, status int, errText string) {
	d := s.base(r, sess, "plugins", "Remove plugin")
	d.Error = errText
	d.Body = confirmBody{
		Heading: "Remove “" + p.Name + " " + p.Version + "” from the Hub?",
		Intro:   "Players can no longer download this version from this Hub. The following changes:",
		Removes: []string{
			"The file and its entry are deleted from this Hub",
			"Players keep their copy until they uninstall it",
		},
		Note:   "A version that a run still uses (not finished or abandoned) cannot be removed.",
		Action: pluginHref(p.PluginID, p.Version) + "/remove", Button: "Remove from Hub", Cancel: pluginHref(p.PluginID, p.Version),
	}
	s.render(w, status, "confirm", "layout", d)
}

func (s *Server) pluginRemove(w http.ResponseWriter, r *http.Request, sess *session) {
	id, v := r.PathValue("id"), r.PathValue("version")
	p, gerr := s.svc.GetPlugin(r.Context(), id, v)
	err := s.svc.RemovePlugin(r.Context(), id, v)
	switch {
	case err == nil:
		http.Redirect(w, r, "/plugins?ok=pluginremoved", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/plugins?err=noplugin", http.StatusSeeOther)
	case errors.Is(err, hub.ErrConflict) && gerr == nil:
		s.renderPluginRemove(w, r, sess, p, http.StatusConflict, errMessageOf(err))
	default:
		s.fail(w, r, err)
	}
}

func (s *Server) pluginIndexRefresh(w http.ResponseWriter, r *http.Request, _ *session) {
	err := s.svc.RefreshPluginIndex(r.Context())
	switch {
	case errors.Is(err, hub.ErrPluginIndexOff):
		http.Redirect(w, r, "/plugins?err=noindex", http.StatusSeeOther)
	case err != nil:
		// The reason is stored and shown in the catalog section.
		http.Redirect(w, r, "/plugins?err=indexfailed", http.StatusSeeOther)
	default:
		http.Redirect(w, r, "/plugins?ok=pluginindex", http.StatusSeeOther)
	}
}

func (s *Server) pluginIndexInstall(w http.ResponseWriter, r *http.Request, sess *session) {
	p, err := s.svc.InstallIndexPlugin(r.Context(), r.PostFormValue("id"), r.PostFormValue("version"))
	switch {
	case err == nil:
		http.Redirect(w, r, pluginHref(p.PluginID, p.Version)+"?ok=plugininstalled", http.StatusSeeOther)
	case errors.Is(err, hub.ErrPluginIndexOff):
		http.Redirect(w, r, "/plugins?err=noindex", http.StatusSeeOther)
	case errors.Is(err, hub.ErrNotFound):
		http.Redirect(w, r, "/plugins?err=noplugin", http.StatusSeeOther)
	case errors.Is(err, hub.ErrConflict):
		s.renderPlugins(w, r, sess, pluginsOpts{status: http.StatusConflict, err: errMessageOf(err)})
	default:
		// Download, checksum or signature problems are shown to the admin; details stay in the Hub log.
		s.log.Warn("plugin install from the index failed", "plugin", r.PostFormValue("id"), "version", r.PostFormValue("version"), "err", err)
		s.renderPlugins(w, r, sess, pluginsOpts{status: http.StatusBadGateway, err: "The plugin could not be installed: " + err.Error()})
	}
}
