package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"
	"time"

	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/hub"
	"github.com/phabioo/uzume/server/internal/updates"
)

// Update rollback on the command line (ADR 0029 D4):
//
//	uzume-server rollback --list [--json]            the earlier versions and what going back does to the data
//	uzume-server rollback <version> [flags]          go back to one of them (run as the service user, like `update stage`)
//	uzume-server rollback --resume                   resume automatic updates after going back
const rollbackUsage = "usage: uzume-server rollback --list [--json] | rollback <version> [--yes] [--confirm-data-loss] | rollback --resume"

type rollbackVersionJSON struct {
	Version       string     `json:"version"`
	Channel       string     `json:"channel"`
	PublishedAt   time.Time  `json:"published_at"`
	FirstUsed     *time.Time `json:"first_used,omitempty"`
	LastUsed      *time.Time `json:"last_used,omitempty"`
	SchemaVersion int        `json:"schema_version"`
	Data          string     `json:"data"` // keep, restore or refuse
	Reason        string     `json:"reason,omitempty"`
	DBBackup      string     `json:"db_backup,omitempty"`
	LostSince     *time.Time `json:"lost_since,omitempty"`
	ManualCommand string     `json:"manual_command,omitempty"`
}

type rollbackListJSON struct {
	Current       string                `json:"current"`
	CurrentSchema int                   `json:"current_schema"`
	CanRollback   bool                  `json:"can_rollback"`
	Unavailable   string                `json:"unavailable,omitempty"`
	InContainer   bool                  `json:"in_container"`
	Paused        *updates.Pause        `json:"paused,omitempty"`
	Skipped       string                `json:"skipped,omitempty"`
	BackupFolder  string                `json:"backup_folder"`
	Versions      []rollbackVersionJSON `json:"versions"`
}

func timePtr(t time.Time) *time.Time {
	if t.IsZero() {
		return nil
	}
	return &t
}

func rollbackListBody(info hub.RollbackInfo) rollbackListJSON {
	out := rollbackListJSON{Current: info.Current, CurrentSchema: info.CurrentSchema, CanRollback: info.CanRollback, Unavailable: info.Unavailable,
		InContainer: info.InContainer, Paused: info.Paused, Skipped: info.Skipped, BackupFolder: info.BackupFolder, Versions: []rollbackVersionJSON{}}
	for _, v := range info.Versions {
		out.Versions = append(out.Versions, rollbackVersionJSON{Version: v.Version, Channel: v.Channel, PublishedAt: v.PublishedAt,
			FirstUsed: timePtr(v.FirstUsed), LastUsed: timePtr(v.LastUsed), SchemaVersion: v.SchemaVersion, Data: string(v.Action), Reason: v.Reason,
			DBBackup: v.DBBackup, LostSince: timePtr(v.LostSince), ManualCommand: v.ManualCommand})
	}
	return out
}

func runRollback(args []string, in io.Reader, out io.Writer) error {
	fs := flag.NewFlagSet("rollback", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	list := fs.Bool("list", false, "list the earlier versions")
	asJSON := fs.Bool("json", false, "with --list: print one JSON object")
	yes := fs.Bool("yes", false, "do not ask for confirmation")
	confirmLoss := fs.Bool("confirm-data-loss", false, "accept that the database is replaced by the backup made before the update")
	resume := fs.Bool("resume", false, "resume automatic updates after going back")
	target, rest := splitArg(args)
	if err := fs.Parse(rest); err != nil {
		return err
	}
	if fs.NArg() > 0 || (target != "" && (*list || *resume)) || (target == "" && !*list && !*resume) {
		return errors.New(rollbackUsage)
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 45*time.Minute)
	defer cancel()
	svc, closeFn, err := openService(ctx, cfg, false)
	if err != nil {
		return err
	}
	defer closeFn()

	switch {
	case *resume:
		if err := svc.ResumeUpdates(ctx); err != nil {
			return err
		}
		fmt.Fprintln(out, "Automatic updates are on again.")
		return nil
	case *list:
		info, err := svc.RollbackVersions(ctx)
		if err != nil {
			return err
		}
		if *asJSON {
			return json.NewEncoder(out).Encode(rollbackListBody(info))
		}
		printRollbackList(out, info)
		return nil
	}

	info, err := svc.RollbackVersions(ctx)
	if err != nil {
		return err
	}
	var chosen *hub.RollbackVersion
	for i := range info.Versions {
		if info.Versions[i].Version == target {
			chosen = &info.Versions[i]
		}
	}
	if chosen == nil {
		printRollbackList(out, info)
		return fmt.Errorf("%s is not one of the earlier versions that can be installed", target)
	}
	if info.InContainer {
		return hub.ErrInContainer
	}
	if !info.CanRollback {
		return errors.New(info.Unavailable)
	}
	if chosen.Action == updates.SchemaRefuse {
		return errors.New(chosen.Reason)
	}
	fmt.Fprintf(out, "Go back from %s to %s.\n", info.Current, chosen.Version)
	fmt.Fprintf(out, "  First: a safety backup without ROMs goes to %s.\n", info.BackupFolder)
	switch chosen.Action {
	case updates.SchemaKeep:
		fmt.Fprintln(out, "  Database: kept as it is; the older version understands it.")
	case updates.SchemaRestore:
		fmt.Fprintf(out, "  Database: replaced by the automatic backup made before the update (%s).\n", chosen.DBBackup)
		fmt.Fprintf(out, "  Changes since %s are lost from the running server; they are kept in the safety backup.\n", chosen.LostSince.Local().Format("2 Jan 2006 15:04"))
		if !*confirmLoss {
			return errors.New("this loses changes made since that time; run again with --confirm-data-loss to go on")
		}
	}
	fmt.Fprintln(out, "  Saves, states and ROMs are not changed. The server restarts (about 1 minute); automatic updates are paused until you resume them.")
	if info.ConnectedPlayers > 0 {
		fmt.Fprintf(out, "  %d connected app(s) disconnect for about a minute.\n", info.ConnectedPlayers)
	}
	if !*yes {
		fmt.Fprint(out, "Go back now? [y/N] ")
		line, _ := bufio.NewReader(in).ReadString('\n')
		if a := strings.ToLower(strings.TrimSpace(line)); a != "y" && a != "yes" {
			return errors.New("canceled")
		}
	}
	res, err := svc.Rollback(ctx, hub.RollbackRequest{Version: target, By: "cli", ConfirmDataLoss: *confirmLoss})
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "Safety backup written: %s/%s\n", res.SafetyDir, res.Safety)
	fmt.Fprintf(out, "Staged %s; the installer was requested. The server restarts in a moment.\n", res.Staged.Version)
	return nil
}

func printRollbackList(out io.Writer, info hub.RollbackInfo) {
	fmt.Fprintf(out, "Running %s (database schema %d)\n", info.Current, info.CurrentSchema)
	if info.Paused != nil {
		fmt.Fprintf(out, "Automatic updates are paused since you went back from %s on %s. Resume with: uzume-server rollback --resume\n",
			info.Paused.From, info.Paused.At.Local().Format("2 Jan 15:04"))
	}
	if info.Skipped != "" {
		fmt.Fprintf(out, "Skipped: %s (never installed automatically; install it by hand with `uzume-server update stage`)\n", info.Skipped)
	}
	if info.Unavailable != "" {
		fmt.Fprintln(out, info.Unavailable)
	}
	if len(info.Versions) == 0 {
		fmt.Fprintln(out, "No earlier version can be installed.")
		return
	}
	fmt.Fprintln(out, "Earlier versions:")
	for _, v := range info.Versions {
		used := ""
		if !v.FirstUsed.IsZero() {
			used = " · used " + v.FirstUsed.Local().Format("2 Jan") + " - " + v.LastUsed.Local().Format("2 Jan")
		}
		fmt.Fprintf(out, "  %-12s %-6s%s\n", v.Version, v.Channel, used)
		switch v.Action {
		case updates.SchemaKeep:
			fmt.Fprintln(out, "               data is kept")
		case updates.SchemaRestore:
			fmt.Fprintf(out, "               needs the database backup %s: changes since %s are lost (kept in the safety backup)\n",
				v.DBBackup, v.LostSince.Local().Format("2 Jan 2006 15:04"))
		default:
			fmt.Fprintf(out, "               not possible: %s\n", v.Reason)
		}
		if !info.CanRollback && v.ManualCommand != "" {
			fmt.Fprintf(out, "               manual: %s\n", v.ManualCommand)
		}
	}
}
