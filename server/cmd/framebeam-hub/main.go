// Command framebeam-hub ist der FrameBeam Hub (Phase 0: nur Gerüst).
package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/phabioo/framebeam/server/internal/version"
)

func main() {
	showVersion := flag.Bool("version", false, "Version ausgeben und beenden")
	flag.Parse()

	if *showVersion {
		fmt.Println(version.String())
		return
	}
	fmt.Fprintf(os.Stderr, "FrameBeam Hub %s: noch keine Server-Logik (Phase 0)\n", version.String())
}
