package web

import (
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
)

// Reports page (docs/design/hub.md 6d-hub): problem reports sent from apps. Plain on purpose: the REST API
// (/api/v1/admin/reports) is the primary interface, admin moves into the app. Opening a report does not mark it
// seen; "Mark as seen" and Delete are explicit actions.

type reportFilter struct {
	Label string
	On    bool
	URL   string
}

type reportItem struct {
	ID, Title, Sub, URL, StatusLabel, StatusClass string
	Selected, New                                 bool
}

type reportFileView struct {
	Name, Size, URL string
}

type reportDetail struct {
	ID                                           int64
	Title, Description, StatusLabel, StatusClass string
	Sent                                         When
	From, Player, Linked                         string
	Size                                         string
	Files                                        []reportFileView
	New                                          bool
}

type reportsBody struct {
	Self    string
	HasAny  bool
	Filters []reportFilter
	Items   []reportItem
	Detail  *reportDetail
}

func reportURL(id int64, filter string) string {
	u := "/reports"
	if id != 0 {
		u += "/" + strconv.FormatInt(id, 10)
	}
	if filter != "" && filter != "all" {
		u += "?filter=" + url.QueryEscape(filter)
	}
	return u
}

func (s *Server) reportsGet(w http.ResponseWriter, r *http.Request, sess *session) {
	s.renderReports(w, r, sess)
}

func (s *Server) renderReports(w http.ResponseWriter, r *http.Request, sess *session) {
	ctx := r.Context()
	filter := r.URL.Query().Get("filter")
	if filter != hub.ReportNew && filter != hub.ReportSeen {
		filter = "all"
	}
	var selected int64
	if v := r.PathValue("id"); v != "" {
		id, err := strconv.ParseInt(v, 10, 64)
		if err != nil {
			http.Redirect(w, r, "/reports?err=noreport", http.StatusSeeOther)
			return
		}
		selected = id
	}
	status := filter
	if filter == "all" {
		status = ""
	}
	list, counts, err := s.svc.ListReports(ctx, status)
	if err != nil {
		s.fail(w, r, err)
		return
	}
	var detail *hub.Report
	if selected != 0 {
		x, err := s.svc.GetReport(ctx, selected)
		if errors.Is(err, hub.ErrNotFound) {
			http.Redirect(w, r, "/reports?err=noreport", http.StatusSeeOther)
			return
		} else if err != nil {
			s.fail(w, r, err)
			return
		}
		detail = &x
	} else if len(list) > 0 {
		selected = list[0].ID
		x, err := s.svc.GetReport(ctx, selected)
		if err != nil {
			s.fail(w, r, err)
			return
		}
		detail = &x
	}

	now := s.svc.Now()
	body := reportsBody{Self: reportURL(selected, filter), HasAny: counts.All > 0}
	if selected == 0 {
		body.Self = reportURL(0, filter)
	}
	for _, f := range []struct {
		key, label string
		n          int
	}{{"all", "All", counts.All}, {hub.ReportNew, "New", counts.New}, {hub.ReportSeen, "Seen", counts.Seen}} {
		u := reportURL(0, f.key)
		body.Filters = append(body.Filters, reportFilter{Label: fmt.Sprintf("%s · %d", f.label, f.n), On: f.key == filter, URL: u})
	}
	for _, x := range list {
		it := reportItem{ID: strconv.FormatInt(x.ID, 10), Title: x.Title, URL: reportURL(x.ID, filter), Selected: x.ID == selected, New: x.Status == hub.ReportNew}
		it.StatusLabel, it.StatusClass = reportStatus(x.Status)
		sub := x.User + " · " + fmtWhen(x.CreatedAt, now).Text + " · " + humanBytes(x.Size)
		if x.EventTitle != "" {
			sub += " · linked to “" + x.EventTitle + "”"
		}
		it.Sub = sub
		body.Items = append(body.Items, it)
	}
	if detail != nil {
		body.Detail = reportDetailView(*detail, now)
	}

	d := s.base(r, sess, "reports", "Reports")
	d.Body = body
	s.render(w, http.StatusOK, "reports", "layout", d)
}

func reportStatus(st string) (label, class string) {
	if st == hub.ReportNew {
		return "New", "info"
	}
	return "Seen", ""
}

func reportDetailView(x hub.Report, now time.Time) *reportDetail {
	v := &reportDetail{ID: x.ID, Title: x.Title, Description: x.Description, Sent: fmtWhen(x.CreatedAt, now),
		From: x.User + " · " + x.Device, Size: humanBytes(x.Size), New: x.Status == hub.ReportNew}
	v.StatusLabel, v.StatusClass = reportStatus(x.Status)
	v.Player = x.PlayerVersion
	if x.OS != "" {
		if v.Player != "" {
			v.Player += " · "
		}
		v.Player += x.OS
	}
	if x.EventTitle != "" {
		v.Linked = x.EventTitle
		if x.EventTime != nil {
			v.Linked += " · " + x.EventTime.Local().Format("15:04")
		}
	}
	for _, f := range x.Files {
		v.Files = append(v.Files, reportFileView{Name: f.Name, Size: humanBytes(f.Size),
			URL: "/reports/" + strconv.FormatInt(x.ID, 10) + "/files/" + url.PathEscape(f.Name)})
	}
	return v
}

func reportID(r *http.Request) (int64, bool) {
	id, err := strconv.ParseInt(r.PathValue("id"), 10, 64)
	return id, err == nil && id > 0
}

func (s *Server) reportSeen(w http.ResponseWriter, r *http.Request, _ *session) {
	id, ok := reportID(r)
	if !ok {
		http.Redirect(w, r, "/reports?err=noreport", http.StatusSeeOther)
		return
	}
	if _, err := s.svc.MarkReportSeen(r.Context(), id); errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/reports?err=noreport", http.StatusSeeOther)
		return
	} else if err != nil {
		s.fail(w, r, err)
		return
	}
	http.Redirect(w, r, reportURL(id, r.URL.Query().Get("filter"))+sep(r.URL.Query().Get("filter"))+"ok=reportseen", http.StatusSeeOther)
}

// sep is the separator before an added query parameter.
func sep(filter string) string {
	if filter != "" && filter != "all" {
		return "&"
	}
	return "?"
}

func (s *Server) reportDeleteConfirm(w http.ResponseWriter, r *http.Request, sess *session) {
	id, ok := reportID(r)
	if !ok {
		http.Redirect(w, r, "/reports?err=noreport", http.StatusSeeOther)
		return
	}
	x, err := s.svc.GetReport(r.Context(), id)
	if errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/reports?err=noreport", http.StatusSeeOther)
		return
	} else if err != nil {
		s.fail(w, r, err)
		return
	}
	d := s.base(r, sess, "reports", "Delete report")
	d.Body = confirmBody{
		Heading: fmt.Sprintf("Delete report #%d?", x.ID),
		Intro:   "“" + x.Title + "” from " + x.User + " and its attachments are removed from this server. This cannot be undone.",
		Action:  fmt.Sprintf("/reports/%d/delete", x.ID), Button: "Delete report", Cancel: reportURL(x.ID, ""),
	}
	s.render(w, http.StatusOK, "confirm", "layout", d)
}

func (s *Server) reportDelete(w http.ResponseWriter, r *http.Request, _ *session) {
	id, ok := reportID(r)
	if !ok {
		http.Redirect(w, r, "/reports?err=noreport", http.StatusSeeOther)
		return
	}
	if err := s.svc.DeleteReport(r.Context(), id); errors.Is(err, hub.ErrNotFound) {
		http.Redirect(w, r, "/reports?err=noreport", http.StatusSeeOther)
		return
	} else if err != nil {
		s.fail(w, r, err)
		return
	}
	http.Redirect(w, r, "/reports?ok=reportdeleted", http.StatusSeeOther)
}

func (s *Server) reportFile(w http.ResponseWriter, r *http.Request, _ *session) {
	id, ok := reportID(r)
	if !ok {
		http.NotFound(w, r)
		return
	}
	name := r.PathValue("name")
	f, size, err := s.svc.OpenReportFile(r.Context(), id, name)
	if errors.Is(err, hub.ErrNotFound) {
		http.NotFound(w, r)
		return
	} else if err != nil {
		s.fail(w, r, err)
		return
	}
	defer f.Close()
	h := w.Header()
	h.Set("Content-Type", "application/octet-stream")
	h.Set("Content-Disposition", `attachment; filename="`+name+`"`)
	h.Set("Content-Length", strconv.FormatInt(size, 10))
	io.Copy(w, f)
}

func (s *Server) reportArchive(w http.ResponseWriter, r *http.Request, _ *session) {
	id, ok := reportID(r)
	if !ok {
		http.NotFound(w, r)
		return
	}
	if _, err := s.svc.GetReport(r.Context(), id); errors.Is(err, hub.ErrNotFound) {
		http.NotFound(w, r)
		return
	} else if err != nil {
		s.fail(w, r, err)
		return
	}
	h := w.Header()
	h.Set("Content-Type", "application/zip")
	h.Set("Content-Disposition", fmt.Sprintf(`attachment; filename="uzume-report-%d.zip"`, id))
	if err := s.svc.WriteReportArchive(r.Context(), id, w); err != nil {
		s.log.Warn("report archive", "id", id, "err", err)
	}
}
