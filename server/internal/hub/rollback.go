package hub

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/phabioo/framebeam/server/internal/backup"
	"github.com/phabioo/framebeam/server/internal/store"
	"github.com/phabioo/framebeam/server/internal/updates"
)

// Update rollback (ADR 0029 D4, finishes ADR 0011 D8): go back to one of the last five releases from the signed
// index. Candidates and the schema rule live in internal/updates; this file holds the service: the candidate list
// with what going back does to the data, the safety backup, the staging for the root helper, and the pause.

// Errors of the rollback functions.
var (
	// ErrInContainer: a container is rolled back by pulling the older image tag.
	ErrInContainer = errors.New("this Hub runs in a container; pull the older image tag and recreate the container instead")
	// ErrRollbackConfirm: the rollback replaces the database; the caller has to confirm the loss first.
	ErrRollbackConfirm = errors.New("going back replaces the database with the backup made before the update; confirm to go on")
)

// pauseStaleAfter is how long a pause whose rollback was never applied is kept before it is dropped.
const pauseStaleAfter = 30 * time.Minute

// recordRunning notes the running version in the local history of installed versions (first and last used) and
// returns the history. It writes only when something changed.
func (s *Service) recordRunning(ctx context.Context) []updates.HistoryEntry {
	v, _, err := s.getSetting(ctx, updates.SettingHistory)
	if err != nil {
		return nil
	}
	h := updates.ParseHistory(v)
	nh, changed := updates.RecordVersion(h, s.hubVer, store.KnownSchemaVersion(), s.now())
	if changed {
		_ = s.setSetting(ctx, updates.SettingHistory, updates.MarshalHistory(nh))
	}
	return nh
}

// ---- pause and skip ----

// UpdatePause returns the pause after going back, or nil. A pause whose rollback did not happen (the Hub still
// runs the version it was meant to leave) is ended: at once when the helper reported a failure, after
// pauseStaleAfter otherwise.
func (s *Service) UpdatePause(ctx context.Context) *updates.Pause {
	v, _, err := s.getSetting(ctx, updates.SettingPaused)
	if err != nil {
		return nil
	}
	p := updates.ParsePause(v)
	if p == nil {
		return nil
	}
	if c, err := updates.CompareVersions(s.hubVer, p.From); err != nil || c < 0 {
		return p // running an older version than the one left: paused
	}
	if updates.RequestPending(s.updCfg.requestDir) {
		return nil
	}
	failed := false
	if r, _ := updates.ReadResult(s.dataDir); r != nil && r.Time.After(p.At.Add(-time.Second)) && !r.OK {
		failed = true
	}
	if failed || s.now().Sub(p.At) > pauseStaleAfter {
		s.endPause(ctx, p, true)
	}
	return nil
}

// endPause puts `update_auto` back and clears the pause (and with dropSkip the skipped version).
func (s *Service) endPause(ctx context.Context, p *updates.Pause, dropSkip bool) {
	_ = s.setSetting(ctx, updates.SettingAuto, p.PrevAuto)
	_ = s.setSetting(ctx, updates.SettingPaused, "")
	if dropSkip {
		_ = s.setSetting(ctx, updates.SettingSkip, "")
	}
}

// ResumeUpdates ends the pause: automatic updates are on again as before going back. The skipped version stays
// skipped (it is not installed automatically); a newer one is.
func (s *Service) ResumeUpdates(ctx context.Context) (err error) {
	defer s.publishOK(&err, TopicUpdates)
	s.upd.mu.Lock()
	p := s.UpdatePause(ctx)
	if p == nil {
		s.upd.mu.Unlock()
		return &Error{Code: CodeConflict, Message: "Automatic updates are not paused"}
	}
	s.endPause(ctx, p, false)
	s.upd.mu.Unlock()
	s.TriggerUpdateCheck()
	return nil
}

// UpdateSkipped returns the version that going back skipped ("" = none). It is dropped once a newer version runs.
func (s *Service) UpdateSkipped(ctx context.Context) string {
	v, _, err := s.getSetting(ctx, updates.SettingSkip)
	if err != nil || v == "" {
		return ""
	}
	if c, err := updates.CompareVersions(s.hubVer, v); err != nil || c > 0 {
		_ = s.setSetting(ctx, updates.SettingSkip, "")
		return ""
	}
	return v
}

// skippedVersion reports whether version is the skipped one (or older): automatic install ignores it.
func skippedVersion(version, skip string) bool {
	if skip == "" {
		return false
	}
	c, err := updates.CompareVersions(version, skip)
	return err != nil || c <= 0
}

// UpdatePausedProblem is the banner text "Running X · automatic updates paused", or nil.
func (s *Service) UpdatePausedProblem(ctx context.Context) *BackupProblem {
	p := s.UpdatePause(ctx)
	if p == nil {
		return nil
	}
	d := fmt.Sprintf("Running %s · automatic updates paused until you resume them. Went back from %s %s", s.hubVer, p.From, p.At.Local().Format("2 Jan 15:04"))
	if p.By != "" {
		d += " by " + p.By
	}
	d += "."
	if p.Backup != "" {
		d += " The safety backup " + p.Backup + " has the data from before."
	}
	return &BackupProblem{Title: "Updates paused", Detail: d}
}

// ---- candidates ----

// RollbackVersion is an earlier version that can be installed.
type RollbackVersion struct {
	Version     string
	Channel     string
	PublishedAt time.Time
	NotesURL    string
	// FirstUsed and LastUsed come from the local history (zero: this version never ran on this Hub).
	FirstUsed, LastUsed time.Time
	// SchemaVersion is the database schema the version uses (0 = unknown).
	SchemaVersion int
	// Action is what going back does to the data: keep, restore or refuse.
	Action updates.SchemaAction
	Reason string // why it is refused
	// DBBackup and LostSince describe a restore: the automatic pre-update backup that replaces the database and
	// the time after which changes are only in the safety backup.
	DBBackup  string
	LostSince time.Time
	Artifact  string
	Size      int64
	URL       string
	// ManualCommand is what an admin does by hand when the Hub cannot go back itself.
	ManualCommand string
}

// RollbackInfo is everything the Updates page and the API show for "Earlier versions".
type RollbackInfo struct {
	Current       string
	CurrentSchema int
	// CanRollback: this Hub can go back itself (packaged install, not a container, updates on).
	CanRollback bool
	// Unavailable says why not (shown instead of the buttons).
	Unavailable string
	InContainer bool
	Versions    []RollbackVersion
	Paused      *updates.Pause
	// Skipped is the version going back skipped; SkippedAvailable that it is still available.
	Skipped          string
	SkippedAvailable *UpdateAvailable
	// BackupFolder is where the safety backup is written (the backup folder, else <data>/backups).
	BackupFolder     string
	ConnectedPlayers int
	ActiveSessions   int
}

// rollbackContext loads the index and computes the candidates.
func (s *Service) rollbackContext(ctx context.Context) (*updates.Fetched, []updates.Candidate, []updates.HistoryEntry, UpdateSettings, error) {
	set, err := s.UpdateSettings(ctx)
	if err != nil {
		return nil, nil, nil, set, err
	}
	q := updates.Query{Product: updates.ProductHub, Channel: set.Channel, Platform: s.updCfg.platform, Kind: updates.KindFor(s.updCfg.platform), Current: s.hubVer}
	if _, err := updates.Select(updates.Index{}, q); err != nil {
		return nil, nil, nil, set, fmt.Errorf("%w: %v", ErrUpdatesOff, err)
	}
	f, err := s.updateSource().LoadIndex(ctx, s.cores.keys)
	if err != nil {
		return nil, nil, nil, set, err
	}
	hist := s.recordRunning(ctx)
	cands := updates.RollbackCandidates(f.Index, updates.RollbackQuery{Product: updates.ProductHub, Platform: q.Platform, Kind: q.Kind,
		Current: s.hubVer, Channel: set.Channel, Protocol: ProtocolVersion, History: hist})
	return f, cands, hist, set, nil
}

func (s *Service) safetyBackupDir(ctx context.Context) string {
	if dir := s.backupSettings(ctx).Folder; dir != "" {
		return dir
	}
	return filepath.Join(s.dataDir, updates.DBBackupDir)
}

func (s *Service) currentSchema() int {
	v, err := store.SchemaVersion(s.db)
	if err != nil {
		return 0
	}
	return v
}

// RollbackVersions lists the earlier versions (at most the last five that speak the running protocol version)
// and what going back would do to the data, and the pause state. It fetches the signed index.
func (s *Service) RollbackVersions(ctx context.Context) (RollbackInfo, error) {
	info := RollbackInfo{Current: s.hubVer, CurrentSchema: s.currentSchema(), InContainer: updates.InContainer()}
	info.Paused = s.UpdatePause(ctx)
	info.Skipped = s.UpdateSkipped(ctx)
	info.BackupFolder = s.safetyBackupDir(ctx)
	info.ConnectedPlayers = len(s.clients())
	info.ActiveSessions = s.countActiveSessions(ctx)
	if info.Skipped != "" {
		if a := s.storedAvailable(ctx); a != nil && skippedVersion(a.Version, info.Skipped) {
			a.Skipped = true
			info.SkippedAvailable = a
		}
	}
	f, cands, hist, _, err := s.rollbackContext(ctx)
	if err != nil {
		return info, err
	}
	_ = f
	backups := updates.ListDBBackups(s.dataDir)
	switch {
	case info.InContainer:
		info.Unavailable = "This Hub runs in a container. Pull the older image tag and recreate the container."
	case !s.packaged():
		info.Unavailable = "This Hub was not installed from the package, so it cannot go back by itself. Use the manual steps below."
	default:
		info.CanRollback = true
	}
	for _, c := range cands {
		plan := updates.PlanSchema(updates.SchemaKnown(c, hist), info.CurrentSchema, backups)
		v := RollbackVersion{Version: c.Release.Version, Channel: c.Release.Channel, PublishedAt: c.Release.PublishedAt, NotesURL: c.Release.NotesURL,
			FirstUsed: c.First, LastUsed: c.Last, SchemaVersion: plan.TargetSchema, Action: plan.Action, Reason: plan.Reason,
			Artifact: c.Artifact.Name, Size: c.Artifact.Size, URL: c.Artifact.URL, ManualCommand: manualRollbackCommand(s.updCfg.platform, c)}
		if plan.Backup != nil {
			v.DBBackup, v.LostSince = plan.Backup.Name, plan.Backup.Time
		}
		info.Versions = append(info.Versions, v)
	}
	return info, nil
}

// manualRollbackCommand is what an admin does by hand for one version.
func manualRollbackCommand(platform string, c updates.Candidate) string {
	if updates.InContainer() {
		return "docker pull ghcr.io/phabioo/framebeam-hub:" + c.Release.Version + " and recreate the container"
	}
	if updates.KindFor(platform) == updates.KindMSI {
		return "Download " + c.Artifact.URL + " and run " + c.Artifact.Name + " as Administrator."
	}
	return "curl -fLO " + shellQuote(c.Artifact.URL) + " && sudo apt install --allow-downgrades ./" + c.Artifact.Name
}

// ---- going back ----

// RollbackRequest asks to go back to one version.
type RollbackRequest struct {
	Version string
	// By is the admin's name (shown in the banner), "cli" for the command line.
	By string
	// ConfirmDataLoss is required when going back replaces the database with the backup from before the update.
	ConfirmDataLoss bool
}

// RollbackResult describes a started rollback.
type RollbackResult struct {
	Staged updates.Staged
	Plan   updates.SchemaPlan
	// Safety is the safety backup written first, SafetyDir its folder.
	Safety, SafetyDir string
	Pause             updates.Pause
}

// Rollback goes back to req.Version: it checks the version against the candidates, applies the schema rule (a
// restore needs ConfirmDataLoss; no way to keep the data is a refusal), writes a safety backup without ROMs,
// stages the older package, pauses automatic updates and asks the root helper. The Hub restarts a moment later
// with the older version.
func (s *Service) Rollback(ctx context.Context, req RollbackRequest) (res RollbackResult, err error) {
	defer s.publishOK(&err, TopicUpdates)
	if updates.InContainer() {
		return res, ErrInContainer
	}
	s.upd.mu.Lock()
	defer s.upd.mu.Unlock()
	if !s.packaged() {
		return res, ErrNotPackaged
	}
	f, cands, hist, set, err := s.rollbackContext(ctx)
	if err != nil {
		return res, err
	}
	var cand *updates.Candidate
	for i := range cands {
		if cands[i].Release.Version == req.Version {
			cand = &cands[i]
		}
	}
	if cand == nil {
		return res, badRequest("%s is not one of the earlier versions that can be installed", req.Version)
	}
	res.Plan = updates.PlanSchema(updates.SchemaKnown(*cand, hist), s.currentSchema(), updates.ListDBBackups(s.dataDir))
	switch res.Plan.Action {
	case updates.SchemaRefuse:
		return res, &Error{Code: CodeConflict, Message: res.Plan.Reason}
	case updates.SchemaRestore:
		if !req.ConfirmDataLoss {
			return res, fmt.Errorf("%w: changes since %s are lost from the running Hub and kept in the safety backup", ErrRollbackConfirm, res.Plan.Backup.Time.Local().Format("2 Jan 15:04"))
		}
	}

	// 1. Safety backup (no ROMs), before anything changes.
	dir := s.safetyBackupDir(ctx)
	if err = os.MkdirAll(dir, 0o750); err != nil {
		return res, fmt.Errorf("safety backup folder: %w", err)
	}
	j, jctx, err := s.begin("backup", req.By)
	if err != nil {
		return res, err
	}
	bset := s.backupSettings(ctx)
	bset.Folder, bset.IncludeROMs = dir, false
	safety, err := backup.Create(jctx, s.backupOptions(bset, true, j.report))
	s.end(j)
	if err != nil {
		return res, fmt.Errorf("safety backup failed, nothing was changed: %w", err)
	}
	res.Safety, res.SafetyDir = safety.Name, dir

	// 2. The update state the older Hub finds: automatic install off, the pause, the skipped version, the history.
	prevAuto, _, _ := s.getSetting(ctx, updates.SettingAuto)
	pause := updates.Pause{From: s.hubVer, To: cand.Release.Version, At: s.now().UTC().Truncate(time.Second), By: req.By, Backup: safety.Name, PrevAuto: prevAuto}
	restoreDB := ""
	if res.Plan.Action == updates.SchemaRestore {
		restoreDB = res.Plan.Backup.Name
		pause.RestoredDB, pause.LostSince = restoreDB, res.Plan.Backup.Time
	}
	res.Pause = pause
	state := map[string]string{updates.SettingAuto: "0", updates.SettingPaused: updates.MarshalPause(pause),
		updates.SettingSkip: s.hubVer, updates.SettingHistory: updates.MarshalHistory(hist)}
	restore := func() {
		_ = s.setSetting(ctx, updates.SettingAuto, prevAuto)
		_ = s.setSetting(ctx, updates.SettingPaused, "")
	}
	prevSkip, _, _ := s.getSetting(ctx, updates.SettingSkip)
	// 3. Stage, write the state into the live database, request the root helper.
	st, err := updates.StageRollback(ctx, f, cand.Release, cand.Artifact, s.dataDir, restoreDB, state)
	if err != nil {
		return res, err
	}
	res.Staged = st
	for k, v := range state {
		if k == updates.SettingHistory {
			continue
		}
		if err = s.setSetting(ctx, k, v); err != nil {
			restore()
			_ = s.setSetting(ctx, updates.SettingSkip, prevSkip)
			return res, err
		}
	}
	_ = set
	if err = updates.WriteRequest(s.updCfg.requestDir, st.Version); err != nil {
		restore()
		_ = s.setSetting(ctx, updates.SettingSkip, prevSkip)
		return res, err
	}
	return res, nil
}
