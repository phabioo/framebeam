package web

import (
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"time"

	"github.com/phabioo/uzume/server/internal/hub"
)

// Settings > Backups (design 6b, kept minimal: the CLI and /api/v1/admin/backups are the main interfaces).

type backupRow struct {
	Name, Path, Size, Contents, Err string
	Created                         When
	Safety, Restored, ROMs          bool
}

type backupRunning struct {
	Title, Detail string
	Percent       int
	Show          bool // a bar is drawn
	Restore       bool
	Step          int
	CanCancel     bool
}

type backupResultCard struct {
	OK            bool
	Title, Detail string
	Safety        string
}

type restoreConfirm struct {
	Name, Date, Size, Contents string
	Lines                      []string
	Players                    string
	Folder                     string
	CanRestart                 bool
}

type weekdayOpt struct {
	N        int
	Label    string
	Selected bool
}

type backupsBody struct {
	Folder, WriteError       string
	FolderSet                bool
	Mode, Time               string
	Keep                     int
	ROMs                     bool
	Weekdays                 []weekdayOpt
	Running                  *backupRunning
	Result                   *backupResultCard
	StatusLine, StatusDetail string
	StatusClass              string // "", "warn"
	Pill                     string
	Next                     string
	Rows                     []backupRow
	Confirm                  *restoreConfirm
	Free                     string
	Poll                     bool
	CanRestart               bool
	TryAgain                 bool
	RestartPoll              bool
}

var weekdayLabels = []string{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}

func etaText(d time.Duration) string {
	switch {
	case d <= 0:
		return ""
	case d < 90*time.Second:
		return " · about 1 min left"
	}
	return fmt.Sprintf(" · about %d min left", int(d.Minutes()+.5))
}

// backupsData builds the Backups section.
func (s *Server) backupsData(r *http.Request) backupsBody {
	ctx := r.Context()
	now := time.Now()
	st := s.svc.BackupStatus(ctx)
	b := backupsBody{Folder: st.Settings.Folder, FolderSet: st.FolderSet, WriteError: st.WriteError, Mode: st.Settings.Mode,
		Time: st.Settings.Time, Keep: st.Settings.Keep, ROMs: st.Settings.IncludeROMs, CanRestart: st.CanRestart}
	for i, l := range weekdayLabels {
		b.Weekdays = append(b.Weekdays, weekdayOpt{N: i, Label: l, Selected: i == st.Settings.Weekday})
	}
	if st.DiskOK {
		b.Free = fmt.Sprintf("%s free", humanBytes(st.DiskFree))
	}

	// Status card.
	switch {
	case st.Running != nil:
		p := st.Running
		rb := &backupRunning{Restore: p.Kind == "restore", Step: p.Step, CanCancel: p.Kind == "backup" || p.Step <= 1}
		if rb.Restore {
			rb.Title = fmt.Sprintf("Restoring · step %d of 3", max(p.Step, 1))
			rb.Detail = [...]string{1: "Making a safety backup first.", 2: "Verifying and unpacking the backup.", 3: "Restarting the server. Apps reconnect by themselves; this page reloads when the server is back."}[min(max(p.Step, 1), 3)]
		} else {
			rb.Title = "Backing up · " + p.Phase
			rb.Detail = "Started by " + p.By + ". Apps keep playing; saves arriving now go into the next backup."
		}
		if p.Total > 0 && p.Step < 3 {
			rb.Show = true
			rb.Percent = int(float64(p.Done) / float64(p.Total) * 100)
			rb.Detail = fmt.Sprintf("%s of %s%s. %s", humanBytes(p.Done), humanBytes(p.Total), etaText(p.ETA), rb.Detail)
		}
		b.Running, b.Poll = rb, true
		b.RestartPoll = rb.Restore && rb.Step == 3
	case !st.FolderSet:
		b.Pill, b.StatusLine = "Off", "Choose a backup folder to turn backups on"
	case st.WriteError != "":
		b.Pill, b.StatusClass = "Cannot write", "warn"
		b.StatusLine = "The server runs as the user it was started with and needs write access to the backup folder."
		b.StatusDetail = st.WriteError + ". Check that the disk is mounted, or choose another folder."
	case st.Last != nil && !st.Last.OK:
		b.Pill, b.StatusClass = "Backup failed", "warn"
		b.StatusLine = "Backup failed " + fmtWhen(st.Last.Finished, now).Text
		b.StatusDetail = st.Last.Error
		if st.LastOK != nil {
			b.StatusDetail += " The last good backup is from " + fmtWhen(st.LastOK.Finished, now).Text + "."
		}
		b.TryAgain = true
	case st.LastOK != nil:
		b.Pill = "On"
		b.StatusLine = fmt.Sprintf("✓ Last backup ok %s · %s · took %s", fmtWhen(st.LastOK.Finished, now).Text, humanBytes(st.LastOK.Size),
			st.LastOK.Finished.Sub(st.LastOK.Started).Round(time.Second))
		if st.LastOK.PruneNote != "" {
			b.StatusDetail = st.LastOK.PruneNote
		}
	default:
		b.Pill, b.StatusLine = "On", "No backup yet"
	}
	if !st.Next.IsZero() && st.Running == nil {
		b.Next = "Next " + fmtWhen(st.Next, now).Text
	}
	if st.Restore != nil {
		rc := &backupResultCard{OK: st.Restore.OK, Safety: st.Restore.Safety}
		if st.Restore.OK {
			rc.Title = "✓ Restored · this server runs the backup from " + fmtWhen(st.Restore.BackupCreated, now).Text
			if st.Restore.Safety != "" {
				rc.Detail = "To undo, restore the safety backup " + st.Restore.Safety + "."
			}
		} else {
			rc.Title = "Not restored · nothing was changed"
			rc.Detail = st.Restore.Error
			if st.Restore.Safety != "" {
				rc.Detail += " The safety backup " + st.Restore.Safety + " is kept."
			}
		}
		b.Result = rc
	}

	// List.
	list, _ := s.svc.ListBackups(ctx)
	for _, e := range list {
		row := backupRow{Name: e.Name, Path: e.Path, Size: humanBytes(e.Size), Contents: e.Contents, Err: e.Err, Created: fmtWhen(e.Created, now),
			Safety: e.Safety, Restored: e.Restored, ROMs: e.IncludesROMs}
		b.Rows = append(b.Rows, row)
	}

	// Restore confirmation (6b-5).
	if name := r.URL.Query().Get("restore"); name != "" && st.Running == nil {
		if ri, err := s.svc.RestoreInfo(ctx, name); err == nil {
			c := &restoreConfirm{Name: name, Date: fmtWhen(ri.Entry.Created, now).Text, Size: humanBytes(ri.Entry.Size), Contents: ri.Entry.Contents,
				Folder: ri.SafetyFolder, CanRestart: st.CanRestart}
			line := "Database: users, devices, runs, plugin settings and server settings go back to this state."
			if ri.AddedUsers > 0 || ri.AddedDevices > 0 {
				line += fmt.Sprintf(" %s and %s added since then disappear.", plural(ri.AddedUsers, "user", "users"), plural(ri.AddedDevices, "device", "devices"))
			}
			c.Lines = []string{line, "Saves and snapshots, plugins, firmware and the certificate go back to this state."}
			if ri.Entry.IncludesROMs {
				c.Lines = append(c.Lines, "ROMs: the library goes back to this state.")
			} else {
				c.Lines = append(c.Lines, "ROMs: this backup has none; the ROM files now on the server stay.")
			}
			if ri.ConnectedPlayers > 0 {
				c.Players = fmt.Sprintf("%s connected; they disconnect for about a minute and running Sessions end.", plural(ri.ConnectedPlayers, "App is", "Apps are"))
			}
			b.Confirm = c
		}
	}
	return b
}

func (s *Server) backupsDone(w http.ResponseWriter, r *http.Request, sess *session, ok string, err error) {
	if err != nil {
		s.settingsDone(w, r, sess, "backups", settingsRes{Err: errorText(err)}, true)
		return
	}
	s.settingsDone(w, r, sess, "backups", settingsRes{OK: ok}, false)
}

// errorText is the message of a service error without its code prefix.
func errorText(err error) string {
	var he *hub.Error
	if errors.As(err, &he) {
		return he.Message
	}
	return err.Error()
}

func (s *Server) backupsSchedule(w http.ResponseWriter, r *http.Request, sess *session) {
	_ = r.ParseForm()
	u := hub.BackupSettingsUpdate{}
	if v := r.PostFormValue("mode"); v != "" {
		u.Mode = &v
	}
	if v := r.PostFormValue("weekday"); v != "" {
		if n, err := strconv.Atoi(v); err == nil {
			u.Weekday = &n
		}
	}
	if v := r.PostFormValue("time"); v != "" {
		u.Time = &v
	}
	if v := r.PostFormValue("keep"); v != "" {
		n, err := strconv.Atoi(v)
		if err != nil {
			s.backupsDone(w, r, sess, "", &hub.Error{Code: hub.CodeBadRequest, Message: "Keep must be a number"})
			return
		}
		u.Keep = &n
	}
	roms := r.PostFormValue("roms") == "1"
	u.IncludeROMs = &roms
	s.backupsDone(w, r, sess, "backupsaved", s.svc.SetBackupSettings(r.Context(), u))
}

func (s *Server) backupsFolder(w http.ResponseWriter, r *http.Request, sess *session) {
	f := r.PostFormValue("folder")
	s.backupsDone(w, r, sess, "backupsaved", s.svc.SetBackupSettings(r.Context(), hub.BackupSettingsUpdate{Folder: &f}))
}

func (s *Server) backupsRun(w http.ResponseWriter, r *http.Request, sess *session) {
	err := s.svc.StartBackup(r.Context(), sess.User.Username)
	if err == nil {
		s.log.Info("backup started", "admin", sess.User.Username)
	}
	s.backupsDone(w, r, sess, "backupstarted", err)
}

func (s *Server) backupsCancel(w http.ResponseWriter, r *http.Request, sess *session) {
	s.svc.CancelBackup()
	s.backupsDone(w, r, sess, "", nil)
}

func (s *Server) backupsRecheck(w http.ResponseWriter, r *http.Request, sess *session) {
	s.svc.RecheckBackupFolder(r.Context())
	s.backupsDone(w, r, sess, "", nil)
}

func (s *Server) backupsDismiss(w http.ResponseWriter, r *http.Request, sess *session) {
	s.svc.DismissRestoreResult()
	s.backupsDone(w, r, sess, "", nil)
}

func (s *Server) backupsDelete(w http.ResponseWriter, r *http.Request, sess *session) {
	name := r.PostFormValue("file")
	err := s.svc.DeleteBackup(r.Context(), name)
	if err == nil {
		s.log.Info("backup deleted", "file", name, "admin", sess.User.Username)
	}
	s.backupsDone(w, r, sess, "backupdeleted", err)
}

func (s *Server) backupsRestore(w http.ResponseWriter, r *http.Request, sess *session) {
	name := r.PostFormValue("file")
	err := s.svc.StartRestore(r.Context(), name, sess.User.Username)
	if err == nil {
		s.log.Warn("restore started", "file", name, "admin", sess.User.Username)
	}
	s.backupsDone(w, r, sess, "restorestarted", err)
}

// backupsDownload streams one backup file (admin only like every page; the file holds secrets).
func (s *Server) backupsDownload(w http.ResponseWriter, r *http.Request, sess *session) {
	f, st, err := s.svc.OpenBackup(r.Context(), r.URL.Query().Get("file"))
	if errors.Is(err, hub.ErrNotFound) {
		s.notFound(w, r)
		return
	}
	if err != nil {
		s.fail(w, r, err)
		return
	}
	defer f.Close()
	h := w.Header()
	h.Set("Content-Type", "application/x-tar")
	h.Set("Content-Disposition", `attachment; filename="`+st.Name()+`"`)
	h.Set("Cache-Control", "no-store")
	s.log.Info("backup downloaded", "file", st.Name(), "admin", sess.User.Username)
	http.ServeContent(w, r, st.Name(), st.ModTime(), f)
}
