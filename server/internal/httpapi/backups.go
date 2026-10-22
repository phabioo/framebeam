package httpapi

import (
	"context"
	"net/http"
	"time"

	"github.com/phabioo/uzume/server/internal/api"
	"github.com/phabioo/uzume/server/internal/hub"
)

// Admin backup API (ADR 0029 D2): /api/v1/admin/backups. Every call needs the token of a device of an admin user.

func requireAdmin(ctx context.Context) error {
	if principal(ctx).User.Role != hub.RoleAdmin {
		return hub.ErrForbidden
	}
	return nil
}

func apiSettings(s hub.BackupSettings) api.AdminBackupSettings {
	return api.AdminBackupSettings{Mode: api.AdminBackupSettingsMode(s.Mode), Weekday: s.Weekday, Time: s.Time, Keep: s.Keep,
		IncludeRoms: s.IncludeROMs, Folder: s.Folder}
}

func apiRun(r *hub.BackupRun) *api.AdminBackupRun {
	if r == nil {
		return nil
	}
	o := &api.AdminBackupRun{Ok: r.OK, Started: r.Started, Finished: r.Finished}
	if r.Name != "" {
		o.Name = ptr(r.Name)
	}
	if r.Size != 0 {
		o.Size = ptr(r.Size)
	}
	if r.Error != "" {
		o.Error = ptr(r.Error)
	}
	if r.NoSpace {
		o.NoSpace = ptr(true)
	}
	if r.By != "" {
		o.By = ptr(r.By)
	}
	if r.Skipped != 0 {
		o.Skipped = ptr(r.Skipped)
	}
	if r.PruneNote != "" {
		o.PruneNote = ptr(r.PruneNote)
	}
	return o
}

func (s *Server) ListAdminBackups(ctx context.Context, _ api.ListAdminBackupsRequestObject) (api.ListAdminBackupsResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	list, err := s.svc.ListBackups(ctx)
	if err != nil {
		return nil, err
	}
	out := api.ListAdminBackups200JSONResponse{Backups: make([]api.AdminBackup, 0, len(list))}
	for _, e := range list {
		b := api.AdminBackup{Name: e.Name, Created: e.Created, Size: e.Size, Safety: e.Safety, IncludesRoms: e.IncludesROMs, Restored: e.Restored}
		if e.Contents != "" {
			b.Contents = ptr(e.Contents)
		}
		if e.HubVersion != "" {
			b.HubVersion = ptr(e.HubVersion)
		}
		if e.SchemaVersion != 0 {
			b.SchemaVersion = ptr(e.SchemaVersion)
		}
		if e.Err != "" {
			b.Error = ptr(e.Err)
		}
		out.Backups = append(out.Backups, b)
	}
	return out, nil
}

func (s *Server) StartAdminBackup(ctx context.Context, _ api.StartAdminBackupRequestObject) (api.StartAdminBackupResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.StartBackup(ctx, principal(ctx).User.Username); err != nil {
		return nil, err
	}
	s.log.Info("backup started", "admin", principal(ctx).User.Username)
	return api.StartAdminBackup202Response{}, nil
}

func (s *Server) GetAdminBackupStatus(ctx context.Context, _ api.GetAdminBackupStatusRequestObject) (api.GetAdminBackupStatusResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	st := s.svc.BackupStatus(ctx)
	out := api.AdminBackupStatus{Settings: apiSettings(st.Settings), FolderSet: st.FolderSet, CanRestore: st.CanRestart,
		Last: apiRun(st.Last), LastOk: apiRun(st.LastOK)}
	if st.WriteError != "" {
		out.WriteError = ptr(st.WriteError)
	}
	if st.DiskOK {
		out.DiskTotal, out.DiskFree = ptr(int64(st.DiskTotal)), ptr(int64(st.DiskFree))
	}
	if !st.Next.IsZero() {
		out.Next = ptr(st.Next)
	}
	if r := st.Running; r != nil {
		p := &api.AdminBackupProgress{Kind: api.AdminBackupProgressKind(r.Kind), Phase: r.Phase, DoneBytes: r.Done, TotalBytes: r.Total, Started: r.Started}
		if r.Step > 0 {
			p.Step = ptr(r.Step)
		}
		if r.By != "" {
			p.By = ptr(r.By)
		}
		if r.ETA > 0 {
			p.EtaSeconds = ptr(int(r.ETA / time.Second))
		}
		out.Running = p
	}
	if r := st.Restore; r != nil {
		rr := &api.AdminRestoreResult{Ok: r.OK, At: r.At, Backup: r.Backup, IncludesRoms: &r.IncludesROMs}
		if !r.BackupCreated.IsZero() {
			rr.BackupCreated = ptr(r.BackupCreated)
		}
		if r.Safety != "" {
			rr.Safety = ptr(r.Safety)
		}
		if r.Error != "" {
			rr.Error = ptr(r.Error)
		}
		out.Restore = rr
	}
	return api.GetAdminBackupStatus200JSONResponse(out), nil
}

func (s *Server) CancelAdminBackup(ctx context.Context, _ api.CancelAdminBackupRequestObject) (api.CancelAdminBackupResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if !s.svc.CancelBackup() {
		return nil, &hub.Error{Code: hub.CodeConflict, Message: "Nothing to cancel"}
	}
	return api.CancelAdminBackup204Response{}, nil
}

func (s *Server) GetAdminBackupSettings(ctx context.Context, _ api.GetAdminBackupSettingsRequestObject) (api.GetAdminBackupSettingsResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	return api.GetAdminBackupSettings200JSONResponse(apiSettings(s.svc.BackupSettings(ctx))), nil
}

func (s *Server) UpdateAdminBackupSettings(ctx context.Context, req api.UpdateAdminBackupSettingsRequestObject) (api.UpdateAdminBackupSettingsResponseObject, error) {
	if req.Body == nil {
		return nil, hub.ErrBadRequest
	}
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	b := req.Body
	u := hub.BackupSettingsUpdate{Weekday: b.Weekday, Time: b.Time, Keep: b.Keep, IncludeROMs: b.IncludeRoms, Folder: b.Folder}
	if b.Mode != nil {
		u.Mode = ptr(string(*b.Mode))
	}
	if err := s.svc.SetBackupSettings(ctx, u); err != nil {
		return nil, err
	}
	s.log.Info("backup settings changed", "admin", principal(ctx).User.Username)
	return api.UpdateAdminBackupSettings200JSONResponse(apiSettings(s.svc.BackupSettings(ctx))), nil
}

func (s *Server) DismissAdminRestoreResult(ctx context.Context, _ api.DismissAdminRestoreResultRequestObject) (api.DismissAdminRestoreResultResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	s.svc.DismissRestoreResult()
	return api.DismissAdminRestoreResult204Response{}, nil
}

func (s *Server) DeleteAdminBackup(ctx context.Context, req api.DeleteAdminBackupRequestObject) (api.DeleteAdminBackupResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.DeleteBackup(ctx, req.Name); err != nil {
		return nil, err
	}
	s.log.Info("backup deleted", "file", req.Name, "admin", principal(ctx).User.Username)
	return api.DeleteAdminBackup204Response{}, nil
}

// DownloadAdminBackup is not used by the strict handler (the route lives on the mux, see downloadAdminBackup).
func (s *Server) DownloadAdminBackup(context.Context, api.DownloadAdminBackupRequestObject) (api.DownloadAdminBackupResponseObject, error) {
	return nil, errNotImplemented
}

func (s *Server) RestoreAdminBackup(ctx context.Context, req api.RestoreAdminBackupRequestObject) (api.RestoreAdminBackupResponseObject, error) {
	if err := requireAdmin(ctx); err != nil {
		return nil, err
	}
	if err := s.svc.StartRestore(ctx, req.Name, principal(ctx).User.Username); err != nil {
		return nil, err
	}
	s.log.Warn("restore started", "file", req.Name, "admin", principal(ctx).User.Username)
	return api.RestoreAdminBackup202Response{}, nil
}

// downloadAdminBackup streams the archive with range support (the file can be many GB).
func (s *Server) downloadAdminBackup(w http.ResponseWriter, r *http.Request) {
	p, err := s.svc.Authenticate(r.Context(), bearer(r))
	if err == nil && p.User.Role != hub.RoleAdmin {
		err = hub.ErrForbidden
	}
	if err != nil {
		s.writeErr(w, r, err)
		return
	}
	f, st, err := s.svc.OpenBackup(r.Context(), r.PathValue("name"))
	if err != nil {
		s.writeErr(w, r, err)
		return
	}
	defer f.Close()
	w, done := rollingWrite(w)
	defer done()
	h := w.Header()
	h.Set("Content-Type", "application/x-tar")
	h.Set("Content-Disposition", `attachment; filename="`+st.Name()+`"`)
	h.Set("Cache-Control", "no-store")
	http.ServeContent(&jsonRangeErrWriter{ResponseWriter: w}, r, st.Name(), st.ModTime(), f)
}
