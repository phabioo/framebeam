// Package version provides the hub version, channel and commit, settable via -ldflags.
package version

// Build information, set at build time:
//
//	-ldflags "-X github.com/phabioo/framebeam/server/internal/version.Version=1.2.3
//	          -X github.com/phabioo/framebeam/server/internal/version.Channel=stable
//	          -X github.com/phabioo/framebeam/server/internal/version.Commit=<sha>"
var (
	Version = "dev"
	// Channel is the compiled-in default update channel: stable, test or dev.
	Channel = "dev"
	// Commit is the source commit (may be empty).
	Commit = ""
)

// String returns the current hub version.
func String() string { return Version }
