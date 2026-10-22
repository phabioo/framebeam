package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"

	"github.com/phabioo/uzume/server/internal/config"
	"github.com/phabioo/uzume/server/internal/diag"
)

// runDiagnostics writes the diagnostics bundle to a file without starting the server, for a server whose web interface
// does not come up. It never opens the database (a running server is not disturbed), so the bundle holds the logs and
// the configuration from server.env/flags only, not counts, cores or apps.
func runDiagnostics(args []string, out io.Writer) error {
	// The output file may come before or after the flags.
	path := ""
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		path, args = args[0], args[1:]
	}
	fs := flag.NewFlagSet("diagnostics", flag.ContinueOnError)
	cfg := config.Register(fs, os.Getenv)
	if err := fs.Parse(args); err != nil {
		return err
	}
	if path == "" && fs.NArg() == 1 {
		path = fs.Arg(0)
	} else if fs.NArg() > 0 {
		return errors.New("usage: uzume-server diagnostics <out.zip> [flags]")
	}
	if path == "" {
		return errors.New("usage: uzume-server diagnostics <out.zip> [flags]")
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	o := diag.Options{Config: *cfg}
	ctx := context.Background()
	info, err := diag.Collect(ctx, nil, o)
	if err != nil {
		return err
	}
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600) // never overwrite an existing file
	if err != nil {
		return err
	}
	if err := diag.Write(ctx, f, info, o); err != nil {
		f.Close()
		os.Remove(path)
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}
	fmt.Fprintf(out, "Wrote %s. It contains IP addresses and host names, no passwords or tokens.\n", path)
	return nil
}
