package httpapi

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"mime/multipart"
	"net/http"
	"strconv"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Problem reports (0.17, feature reports_v1): any paired device sends one, admins read them. Admin tokens only for
// the admin endpoints: 401 without a token, 403 for users.

const reportMetaMax = 64 << 10 // the meta part is small JSON

// multipartFiles yields the "files" parts of a report upload one by one.
type multipartFiles struct{ mr *multipart.Reader }

func (m multipartFiles) Next() (string, io.Reader, error) {
	part, err := m.mr.NextPart()
	if err == io.EOF {
		return "", nil, io.EOF
	}
	if err != nil {
		return "", nil, err // a body over the cap surfaces as *http.MaxBytesError (413)
	}
	if part.FormName() != "files" || part.FileName() == "" {
		return "", nil, hub.ErrBadRequest
	}
	return part.FileName(), part, nil
}

func (s *Server) CreateReport(ctx context.Context, req api.CreateReportRequestObject) (api.CreateReportResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	part, err := req.Body.NextPart()
	var mbe *http.MaxBytesError
	if errors.As(err, &mbe) {
		return nil, err
	}
	if err != nil || part.FormName() != "meta" {
		return nil, &hub.Error{Code: hub.CodeBadRequest, Message: "The first part must be meta"}
	}
	raw, err := io.ReadAll(io.LimitReader(part, reportMetaMax+1))
	if err != nil {
		return nil, err
	}
	var meta api.ReportMeta
	if len(raw) > reportMetaMax || json.Unmarshal(raw, &meta) != nil {
		return nil, &hub.Error{Code: hub.CodeBadRequest, Message: "meta is not valid ReportMeta JSON"}
	}
	in := hub.ReportInput{Description: meta.Description}
	if meta.PlayerVersion != nil {
		in.PlayerVersion = *meta.PlayerVersion
	}
	if meta.Os != nil {
		in.OS = *meta.Os
	}
	if meta.LinkedEvent != nil {
		in.EventTitle = meta.LinkedEvent.Title
		in.EventTime = meta.LinkedEvent.Time
	}
	p := principal(ctx)
	if in.PlayerVersion == "" {
		in.PlayerVersion = p.Device.PlayerVersion
	}
	id, err := s.svc.CreateReport(ctx, p, in, multipartFiles{req.Body})
	if err != nil {
		return nil, err
	}
	s.log.Info("problem report received", "id", id, "device", p.Device.Name) // no text, no file names
	return api.CreateReport201JSONResponse{Id: id}, nil
}

func reportSummary(x hub.Report) api.AdminReportSummary {
	out := api.AdminReportSummary{Id: x.ID, Status: api.AdminReportSummaryStatus(x.Status), Title: x.Title, User: x.User, Device: x.Device,
		CreatedAt: x.CreatedAt, Size: x.Size, FileCount: len(x.Files), PlayerVersion: strPtr(x.PlayerVersion), Os: strPtr(x.OS),
		LinkedEventTitle: strPtr(x.EventTitle)}
	if x.HasConnectionCheck {
		out.HasConnectionCheck = ptr(true)
	}
	return out
}

func reportBody(x hub.Report) api.AdminReport {
	out := api.AdminReport{Id: x.ID, Status: api.AdminReportStatus(x.Status), Title: x.Title, User: x.User, Device: x.Device,
		CreatedAt: x.CreatedAt, Size: x.Size, FileCount: len(x.Files), PlayerVersion: strPtr(x.PlayerVersion), Os: strPtr(x.OS),
		LinkedEventTitle: strPtr(x.EventTitle), LinkedEventTime: x.EventTime, SeenAt: x.SeenAt, Description: x.Description,
		Files: []api.AdminReportFile{}}
	if x.HasConnectionCheck {
		out.HasConnectionCheck = ptr(true)
	}
	for _, f := range x.Files {
		out.Files = append(out.Files, api.AdminReportFile{Name: f.Name, Size: f.Size})
	}
	return out
}

func (s *Server) ListAdminReports(ctx context.Context, req api.ListAdminReportsRequestObject) (api.ListAdminReportsResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	status := ""
	if req.Params.Status != nil {
		status = string(*req.Params.Status)
	}
	rs, c, err := s.svc.ListReports(ctx, status)
	if err != nil {
		return nil, err
	}
	out := api.AdminReportList{Reports: make([]api.AdminReportSummary, 0, len(rs)), CountAll: c.All, CountNew: c.New, CountSeen: c.Seen}
	for _, x := range rs {
		out.Reports = append(out.Reports, reportSummary(x))
	}
	return api.ListAdminReports200JSONResponse(out), nil
}

func (s *Server) GetAdminReport(ctx context.Context, req api.GetAdminReportRequestObject) (api.GetAdminReportResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	x, err := s.svc.GetReport(ctx, req.Id)
	if err != nil {
		return nil, err
	}
	return api.GetAdminReport200JSONResponse(reportBody(x)), nil
}

func (s *Server) MarkAdminReportSeen(ctx context.Context, req api.MarkAdminReportSeenRequestObject) (api.MarkAdminReportSeenResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	x, err := s.svc.MarkReportSeen(ctx, req.Id)
	if err != nil {
		return nil, err
	}
	return api.MarkAdminReportSeen200JSONResponse(reportBody(x)), nil
}

func (s *Server) DeleteAdminReport(ctx context.Context, req api.DeleteAdminReportRequestObject) (api.DeleteAdminReportResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.DeleteReport(ctx, req.Id); err != nil {
		return nil, err
	}
	s.log.Info("problem report deleted", "id", req.Id, "admin", principal(ctx).User.Username)
	return api.DeleteAdminReport204Response{}, nil
}

func (s *Server) GetAdminReportFile(ctx context.Context, req api.GetAdminReportFileRequestObject) (api.GetAdminReportFileResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	f, size, err := s.svc.OpenReportFile(ctx, req.Id, req.Name)
	if err != nil {
		return nil, err
	}
	// The name passed the file-name rule when the report was stored, so it is safe in a header.
	cd := `attachment; filename="` + req.Name + `"`
	return api.GetAdminReportFile200ApplicationoctetStreamResponse{Body: f, ContentLength: size,
		Headers: api.GetAdminReportFile200ResponseHeaders{ContentDisposition: &cd, ContentLength: ptr(size)}}, nil
}

func (s *Server) GetAdminReportArchive(ctx context.Context, req api.GetAdminReportArchiveRequestObject) (api.GetAdminReportArchiveResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if _, err := s.svc.GetReport(ctx, req.Id); err != nil {
		return nil, err
	}
	pr, pw := io.Pipe()
	go func() { pw.CloseWithError(s.svc.WriteReportArchive(context.WithoutCancel(ctx), req.Id, pw)) }()
	cd := `attachment; filename="uzume-report-` + strconv.FormatInt(req.Id, 10) + `.zip"`
	return api.GetAdminReportArchive200ApplicationzipResponse{Body: pr, Headers: api.GetAdminReportArchive200ResponseHeaders{ContentDisposition: &cd}}, nil
}
