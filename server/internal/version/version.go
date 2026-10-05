// Package version stellt die per -ldflags setzbare Hub-Version bereit.
package version

// Version wird beim Build gesetzt:
//
//	-ldflags "-X github.com/phabioo/framebeam/server/internal/version.Version=1.2.3"
var Version = "dev"

// String liefert die aktuelle Hub-Version.
func String() string { return Version }
