package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/phabioo/framebeam/server/internal/config"
	"github.com/phabioo/framebeam/server/internal/hub"
	"github.com/phabioo/framebeam/server/internal/updates"
	"github.com/phabioo/framebeam/server/internal/version"
)

// versionInfo is the machine-readable version (S1): the same keys as the Player's --version-json.
type versionInfo struct {
	Product            string `json:"product"`
	Version            string `json:"version"`
	Channel            string `json:"channel"`
	Commit             string `json:"commit"`
	ProtocolVersion    int    `json:"protocol_version"`
	MinProtocolVersion int    `json:"min_protocol_version"`
}

func currentVersionInfo() versionInfo {
	return versionInfo{Product: "hub", Version: version.Version, Channel: version.Channel, Commit: version.Commit,
		ProtocolVersion: hub.ProtocolVersion, MinProtocolVersion: hub.MinProtocolVersion}
}

// runVersion prints the version, or with --json one JSON object.
func runVersion(args []string, out io.Writer) error {
	fs := flag.NewFlagSet("version", flag.ContinueOnError)
	asJSON := fs.Bool("json", false, "print one JSON object")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() > 0 {
		return errors.New("usage: framebeam-hub version [--json]")
	}
	if !*asJSON {
		fmt.Fprintln(out, version.String())
		return nil
	}
	return json.NewEncoder(out).Encode(currentVersionInfo())
}

const updateUsage = "usage: framebeam-hub update check|stage|apply-staged [flags]"

func runUpdate(args []string, out io.Writer) error {
	if len(args) == 0 {
		return errors.New(updateUsage)
	}
	switch args[0] {
	case "check":
		return runUpdateCheck(args[1:], out, false)
	case "stage":
		return runUpdateCheck(args[1:], out, true)
	case "apply-staged":
		return runApplyStaged(args[1:], out, execRunner)
	}
	return errors.New(updateUsage)
}

type updateArtifactJSON struct {
	Name   string `json:"name"`
	Size   int64  `json:"size"`
	SHA256 string `json:"sha256"`
	URL    string `json:"url"`
}

type updateAvailableJSON struct {
	Version            string             `json:"version"`
	Channel            string             `json:"channel"`
	PublishedAt        time.Time          `json:"published_at"`
	NotesURL           string             `json:"notes_url,omitempty"`
	ProtocolVersion    int                `json:"protocol_version"`
	MinProtocolVersion int                `json:"min_protocol_version"`
	Artifact           updateArtifactJSON `json:"artifact"`
}

type updateCheckJSON struct {
	Current   string               `json:"current"`
	Channel   string               `json:"channel"`
	Platform  string               `json:"platform"`
	UpToDate  bool                 `json:"up_to_date"`
	Available *updateAvailableJSON `json:"available"`
	Skipped   int                  `json:"skipped"`
	Staged    *updates.Staged      `json:"staged,omitempty"`
	Request   string               `json:"request_file,omitempty"`
}

// runUpdateCheck prints the selection as JSON; with stage it also downloads, verifies and stages the update and
// creates the request file (run it as the service user, which owns the data directory).
func runUpdateCheck(args []string, out io.Writer, stage bool) error {
	name := "check"
	if stage {
		name = "stage"
	}
	fs := flag.NewFlagSet("update "+name, flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	channel := fs.String("channel", "", "channel stable or beta (default: the Hub setting, else its default channel, else the build channel)")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() > 0 {
		return errors.New("usage: framebeam-hub update " + name + " [-channel stable|beta] [flags]")
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	keys, err := cfg.TrustedCoreKeys()
	if err != nil {
		return err
	}
	ch := *channel
	if ch == "" {
		ch = updates.NormalizeChannel(version.Channel) // without a Hub database the build channel applies
	}
	if _, serr := os.Stat(filepath.Join(cfg.DataDir, "framebeam.db")); *channel == "" && serr == nil {
		ctx := context.Background()
		svc, closeFn, err := openService(ctx, cfg)
		if err != nil {
			return err
		}
		set, err := svc.UpdateSettings(ctx)
		closeFn()
		if err != nil {
			return err
		}
		ch = set.Channel
	}
	if !updates.ValidSelectableChannel(ch) {
		return errors.New("updates are off for this build; pass -channel stable|beta")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 45*time.Minute)
	defer cancel()
	platform := updates.LinuxPlatform()
	f, err := updates.Source{IndexURL: cfg.UpdateIndexURL}.LoadIndex(ctx, keys)
	if err != nil {
		return err
	}
	sel, err := updates.Select(f.Index, updates.Query{Product: updates.ProductHub, Channel: ch, Platform: platform,
		Kind: updates.KindDeb, Current: version.Version})
	if err != nil {
		return err
	}
	res := updateCheckJSON{Current: version.Version, Channel: ch, Platform: platform, UpToDate: sel.Release == nil, Skipped: len(f.Skipped)}
	if sel.Release != nil {
		r, a := sel.Release, sel.Artifact
		res.Available = &updateAvailableJSON{Version: r.Version, Channel: r.Channel, PublishedAt: r.PublishedAt, NotesURL: r.NotesURL,
			ProtocolVersion: r.ProtocolVersion, MinProtocolVersion: r.MinProtocolVersion,
			Artifact: updateArtifactJSON{Name: a.Name, Size: a.Size, SHA256: a.SHA256, URL: a.URL}}
		if stage {
			if !updates.RequestDirWritable(cfg.UpdateRequestDir) {
				return fmt.Errorf("request directory %s does not exist or is not writable: not a packaged install?", cfg.UpdateRequestDir)
			}
			st, err := updates.Stage(ctx, f, *r, *a, cfg.DataDir)
			if err != nil {
				return err
			}
			if err := updates.WriteRequest(cfg.UpdateRequestDir, st.Version); err != nil {
				return err
			}
			res.Staged, res.Request = &st, updates.RequestPath(cfg.UpdateRequestDir)
		}
	}
	return json.NewEncoder(out).Encode(res)
}

// execRunner runs a command and returns its combined output.
func execRunner(ctx context.Context, name string, args ...string) ([]byte, error) {
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.Env = append(os.Environ(), "DEBIAN_FRONTEND=noninteractive")
	return cmd.CombinedOutput()
}

// runApplyStaged is the root helper behind the update unit. It trusts nothing in the data directory except what
// the signature covers; see updates.ApplyStaged.
func runApplyStaged(args []string, out io.Writer, run updates.Runner) error {
	fs := flag.NewFlagSet("update apply-staged", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	if err := fs.Parse(args); err != nil {
		return err
	}
	keys, err := cfg.TrustedCoreKeys()
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Minute)
	defer cancel()
	res, err := updates.ApplyStaged(ctx, updates.ApplyOptions{DataDir: cfg.DataDir, RequestFile: updates.RequestPath(cfg.UpdateRequestDir),
		Keys: keys, CurrentVersion: version.Version, Run: run})
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "Installed FrameBeam Hub %s\n", strings.TrimSpace(res.Version))
	return nil
}
