package httpapi

import (
	"context"
	"time"

	"github.com/phabioo/uzume/server/internal/adminhost"
	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/health"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/statusinfo"
)

// Admin endpoints (0.17): the server health view and the metrics setting for the admin in the app. They use the
// same data builder (statusinfo) as the app status view. Admin tokens only: 401 without a token, 403 for users.

// WithStatus gives the admin endpoints their data source; without it they answer 404.
func WithStatus(src *statusinfo.Source) Option { return func(s *Server) { s.status = src } }

// adminSource returns the data source for an admin call, or the error to answer.
func (s *Server) adminSource(ctx context.Context) (*statusinfo.Source, error) {
	if principal(ctx).User.Role != hub.RoleAdmin {
		return nil, hub.ErrForbidden
	}
	if s.status == nil {
		return nil, hub.ErrNotFound
	}
	return s.status, nil
}

func (s *Server) GetAdminStatus(ctx context.Context, _ api.GetAdminStatusRequestObject) (api.GetAdminStatusResponseObject, error) {
	src, err := s.adminSource(ctx)
	if err != nil {
		return nil, err
	}
	host, _ := ctx.Value(keyHost).(string)
	st, err := src.Collect(ctx, host)
	if err != nil {
		return nil, err
	}
	return api.GetAdminStatus200JSONResponse(adminStatusBody(st)), nil
}

func adminStatusBody(st statusinfo.Status) api.AdminStatus {
	out := api.AdminStatus{HubName: st.HubName, HubVersion: st.Version, UptimeSeconds: int64(st.Uptime.Seconds()), PublicAddress: st.Address,
		Time: st.Now, MetricsEnabled: st.MetricsOn, Viewers: st.Viewers,
		Problems: []api.AdminProblem{}, Sessions: []api.AdminStatusSession{}, Players: []api.AdminStatusPlayer{}, Errors: []api.AdminStatusError{}}
	for _, p := range st.Problems {
		sev := api.AdminProblemSeverityWarn
		if p.Severity == health.Error {
			sev = api.AdminProblemSeverityError
		}
		out.Problems = append(out.Problems, api.AdminProblem{Id: p.ID, Title: p.Title, Detail: p.Detail, Severity: sev,
			ActionLabel: strPtr(p.ActionLabel)})
	}
	if st.CPUOK {
		c := float32(st.CPUPercent)
		out.CpuPercent = &c
	}
	out.Memory.ProcessBytes = int64(st.Memory.Process)
	if st.Memory.SystemOK {
		t, u := int64(st.Memory.SystemTotal), int64(st.Memory.SystemUsed)
		out.Memory.SystemTotalBytes, out.Memory.SystemUsedBytes = &t, &u
	}
	if st.DiskOK {
		out.Disk = &struct {
			FreeBytes  int64 `json:"free_bytes"`
			TotalBytes int64 `json:"total_bytes"`
		}{FreeBytes: int64(st.DiskFree), TotalBytes: int64(st.DiskTotal)}
	}
	if st.Backup.FolderSet {
		bd := &api.AdminStatusBackupDisk{Folder: st.Backup.Folder}
		if st.Backup.OK {
			t, f := int64(st.Backup.Total), int64(st.Backup.Free)
			bd.TotalBytes, bd.FreeBytes = &t, &f
		}
		out.BackupDisk = bd
	}
	for _, x := range st.Sessions {
		out.Sessions = append(out.Sessions, api.AdminStatusSession{GameTitle: x.GameTitle, Owner: x.Owner, DeviceName: x.DeviceName,
			Visibility: api.AdminStatusSessionVisibility(x.Visibility), StartedAt: x.Since, Viewers: x.Viewers})
	}
	if st.TURN != nil {
		out.Turn = &struct {
			Allocations int `json:"allocations"`
			RelayPorts  int `json:"relay_ports"`
		}{Allocations: st.TURN.Allocations, RelayPorts: st.TURN.RelayPorts}
	}
	for _, p := range st.Players {
		out.Players = append(out.Players, api.AdminStatusPlayer{DeviceName: p.DeviceName, User: p.User, Version: p.Version,
			State: api.AdminStatusPlayerState(p.State), Outdated: p.Outdated})
	}
	for _, e := range st.Errors {
		lvl := api.AdminStatusErrorLevelWarn
		if statusinfo.IsError(e) {
			lvl = api.AdminStatusErrorLevelError
		}
		out.Errors = append(out.Errors, api.AdminStatusError{Time: e.Time, Level: lvl, Message: e.Message})
	}
	return out
}

func strPtr(s string) *string {
	if s == "" {
		return nil
	}
	return &s
}

func (s *Server) GetAdminStatusLog(ctx context.Context, _ api.GetAdminStatusLogRequestObject) (api.GetAdminStatusLogResponseObject, error) {
	src, err := s.adminSource(ctx)
	if err != nil {
		return nil, err
	}
	name := `attachment; filename="uzume-server-log-` + time.Now().UTC().Format("20060102-150405") + `.txt"`
	return api.GetAdminStatusLog200TextResponse{Body: string(src.LogSnapshot()),
		Headers: api.GetAdminStatusLog200ResponseHeaders{ContentDisposition: &name}}, nil
}

func (s *Server) metricsState(ctx context.Context, token string) api.AdminMetrics {
	m := api.AdminMetrics{Enabled: s.svc.MetricsEnabled(ctx), TokenSet: s.svc.MetricsTokenSet(ctx)}
	if token != "" {
		m.Token = &token
	}
	return m
}

func (s *Server) GetAdminMetrics(ctx context.Context, _ api.GetAdminMetricsRequestObject) (api.GetAdminMetricsResponseObject, error) {
	if _, err := s.adminSource(ctx); err != nil {
		return nil, err
	}
	return api.GetAdminMetrics200JSONResponse(s.metricsState(ctx, "")), nil
}

func (s *Server) SetAdminMetrics(ctx context.Context, req api.SetAdminMetricsRequestObject) (api.SetAdminMetricsResponseObject, error) {
	if _, err := s.adminSource(ctx); err != nil {
		return nil, err
	}
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	tok, err := s.svc.SetMetricsEnabled(ctx, req.Body.Enabled)
	if err != nil {
		return nil, err
	}
	s.log.Info("metrics endpoint switched", "enabled", req.Body.Enabled, "admin", principal(ctx).User.Username)
	return api.SetAdminMetrics200JSONResponse(s.metricsState(ctx, tok)), nil
}

func (s *Server) CreateAdminMetricsToken(ctx context.Context, _ api.CreateAdminMetricsTokenRequestObject) (api.CreateAdminMetricsTokenResponseObject, error) {
	if _, err := s.adminSource(ctx); err != nil {
		return nil, err
	}
	tok, err := s.svc.RegenerateMetricsToken(ctx)
	if err != nil {
		return nil, err
	}
	s.log.Info("metrics token regenerated", "admin", principal(ctx).User.Username)
	return api.CreateAdminMetricsToken200JSONResponse(api.AdminMetricsToken{Token: tok}), nil
}

// WithHost gives the admin settings and library endpoints the process-level facts (the server host
// implements it); without it they answer 404.
func WithHost(h adminhost.Host) Option { return func(s *Server) { s.host = h } }

// adminHost returns the host for an admin call (admin check first), or the error to answer.
func (s *Server) adminHost(ctx context.Context) (adminhost.Host, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if s.host == nil {
		return nil, hub.ErrNotFound
	}
	return s.host, nil
}
