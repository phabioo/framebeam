// Package envcompat reads runtime environment variables under their UZUME_ names and falls back to the exact
// FRAMEBEAM_ name they had before the rename to Uzume. Every old name that was actually used is collected, so the
// server can log one deprecation hint per name at startup.
//
// Mapping: UZUME_<X> was FRAMEBEAM_<X>; inside the name SERVER was HUB, APP was PLAYER and LOCAL_SERVER was
// LOCAL_HUB (UZUME_SERVER_UPDATE_INDEX_URL was FRAMEBEAM_HUB_UPDATE_INDEX_URL).
package envcompat

import (
	"log/slog"
	"sort"
	"strings"
	"sync"
)

const (
	newPrefix = "UZUME_"
	oldPrefix = "FRAMEBEAM_"
)

var (
	mu     sync.Mutex
	used   = map[string]string{} // old name -> new name, for every fallback that supplied a value
	warned = map[string]bool{}
)

// LegacyName returns the pre-rename FRAMEBEAM_ name of a UZUME_ variable, or "" for any other name.
func LegacyName(name string) string {
	rest, ok := strings.CutPrefix(name, newPrefix)
	if !ok || rest == "" {
		return ""
	}
	switch {
	case strings.HasPrefix(rest, "SERVER_"):
		rest = "HUB_" + rest[len("SERVER_"):]
	case strings.HasPrefix(rest, "APP_"):
		rest = "PLAYER_" + rest[len("APP_"):]
	case strings.HasPrefix(rest, "LOCAL_SERVER"):
		rest = "LOCAL_HUB" + rest[len("LOCAL_SERVER"):]
	}
	return oldPrefix + rest
}

// Wrap returns a getenv that answers UZUME_ names: the new variable wins; when it is unset or empty the old
// FRAMEBEAM_ variable is used and remembered for WarnUsed. Other names pass through unchanged. Wrapping a wrapped
// function is harmless.
func Wrap(getenv func(string) string) func(string) string {
	return func(name string) string {
		if v := getenv(name); v != "" {
			return v
		}
		old := LegacyName(name)
		if old == "" {
			return ""
		}
		v := getenv(old)
		if v != "" {
			mu.Lock()
			used[old] = name
			mu.Unlock()
		}
		return v
	}
}

// Used returns the old names that supplied a value so far, sorted, mapped to the names to use instead.
func Used() map[string]string {
	mu.Lock()
	defer mu.Unlock()
	m := make(map[string]string, len(used))
	for k, v := range used {
		m[k] = v
	}
	return m
}

// WarnUsed logs one WARN per old name that is in use and was not reported before ("FRAMEBEAM_X is deprecated, use
// UZUME_Y"). It can be called again later; only names that appeared since are reported. It returns the names reported.
func WarnUsed(log *slog.Logger) []string {
	mu.Lock()
	var olds []string
	for old := range used {
		if !warned[old] {
			warned[old] = true
			olds = append(olds, old)
		}
	}
	news := make([]string, 0, len(olds))
	sort.Strings(olds)
	for _, o := range olds {
		news = append(news, used[o])
	}
	mu.Unlock()
	for i, o := range olds {
		log.Warn(o + " is deprecated, use " + news[i])
	}
	return olds
}

// Reset forgets collected names and reported state (for tests).
func Reset() {
	mu.Lock()
	used = map[string]string{}
	warned = map[string]bool{}
	mu.Unlock()
}
