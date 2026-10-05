// Package version provides the hub version, settable via -ldflags.
package version

// Version is set at build time:
//
//	-ldflags "-X github.com/phabioo/framebeam/server/internal/version.Version=1.2.3"
var Version = "dev"

// String returns the current hub version.
func String() string { return Version }
