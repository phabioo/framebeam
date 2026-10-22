package main

import (
	"bufio"
	"os"
	"runtime"
	"strings"
)

// The Linux service gets its settings from /etc/uzume/server.env through systemd (EnvironmentFile). A command typed in
// a shell does not, so without help `uzume-server status` would look at the default data folder and ignore a custom
// UZUME_DATA_DIR, port or public host. loadServerEnv reads the UZUME_* keys of that file into the process environment
// for the commands, never overriding a variable that is already set (flags still win over both).

// defaultServerEnvFile is the file the Linux packages create; UZUME_SERVER_ENV_FILE names another one ("none" = off).
const defaultServerEnvFile = "/etc/uzume/server.env"

// loadServerEnv loads the env file for a command. A missing or unreadable file is not an error (a server started
// by hand, a container, Windows): the commands then use the environment they have.
func loadServerEnv() {
	if runtime.GOOS == "windows" {
		return
	}
	path := os.Getenv("UZUME_SERVER_ENV_FILE")
	switch path {
	case "none":
		return
	case "":
		path = defaultServerEnvFile
	}
	applyEnvFile(path, os.LookupEnv, os.Setenv)
}

// applyEnvFile sets the UZUME_* keys of an EnvironmentFile-style file that lookup does not know yet.
func applyEnvFile(path string, lookup func(string) (string, bool), set func(k, v string) error) {
	f, err := os.Open(path)
	if err != nil {
		return
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 0, 64*1024), 1<<20)
	for sc.Scan() {
		k, v, ok := parseEnvLine(sc.Text())
		if !ok || !strings.HasPrefix(k, "UZUME_") || k == "UZUME_SERVER_ENV_FILE" {
			continue
		}
		if cur, set := lookup(k); set && cur != "" {
			continue
		}
		_ = set(k, v)
	}
}

// parseEnvLine parses one KEY=VALUE line of a systemd EnvironmentFile: comments (# and ;) and blank lines are
// skipped, the value may be quoted with " (with \" and \\ escapes) or ' (literal); an unquoted value is trimmed.
func parseEnvLine(line string) (key, val string, ok bool) {
	line = strings.TrimSpace(line)
	if line == "" || line[0] == '#' || line[0] == ';' {
		return "", "", false
	}
	k, v, found := strings.Cut(line, "=")
	k = strings.TrimSpace(k)
	if !found || k == "" || strings.ContainsAny(k, " \t") {
		return "", "", false
	}
	v = strings.TrimSpace(v)
	switch {
	case len(v) >= 2 && v[0] == '"' && v[len(v)-1] == '"':
		v = strings.NewReplacer(`\"`, `"`, `\\`, `\`).Replace(v[1 : len(v)-1])
	case len(v) >= 2 && v[0] == '\'' && v[len(v)-1] == '\'':
		v = v[1 : len(v)-1]
	}
	return k, v, true
}
