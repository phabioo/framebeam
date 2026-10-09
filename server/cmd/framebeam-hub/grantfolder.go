package main

import (
	"errors"
	"fmt"
	"io"
	"path/filepath"
)

// hubServiceAccount is the virtual account the Hub service runs as on Windows (MSI, contract 0.9).
const hubServiceAccount = `NT SERVICE\FrameBeamHub`

// errGrantUnsupported: grant-folder exists on Windows only; main exits with status 2.
var errGrantUnsupported = errors.New("not supported")

// runGrantFolder implements "framebeam-hub grant-folder <path>": the Hub service account may read and list the folder
// and everything below it (Windows, needs an administrator). Used by the Player's "Set up a Hub on this PC".
func runGrantFolder(args []string, out io.Writer) error {
	if len(args) != 1 || args[0] == "" {
		return errors.New("usage: framebeam-hub grant-folder <path>")
	}
	abs, err := filepath.Abs(args[0])
	if err != nil {
		return err
	}
	if err := grantFolder(abs); err != nil {
		return err
	}
	fmt.Fprintf(out, "Granted read access on %s to %s\n", abs, hubServiceAccount)
	return nil
}
