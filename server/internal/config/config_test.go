package config

import (
	"flag"
	"testing"
)

func parse(t *testing.T, env map[string]string, args ...string) *Config {
	t.Helper()
	fs := flag.NewFlagSet("t", flag.ContinueOnError)
	c := Register(fs, func(k string) string { return env[k] })
	if err := fs.Parse(args); err != nil {
		t.Fatal(err)
	}
	return c
}

func TestDefaultsEnvAndFlags(t *testing.T) {
	c := parse(t, nil)
	if c.DataDir != "/var/lib/framebeam" || c.Listen != ":8443" || c.Dev || !c.UseTLS() {
		t.Fatalf("defaults: %+v", c)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_LISTEN": ":9000", "FRAMEBEAM_DEV": "true"}, "-data-dir", "/x")
	if c.Listen != ":9000" || !c.Dev || c.DataDir != "/x" || c.UseTLS() {
		t.Fatalf("env/flag: %+v", c)
	}
	c = parse(t, map[string]string{"FRAMEBEAM_LISTEN": ":9000"}, "-listen", "127.0.0.1:1")
	if c.Listen != "127.0.0.1:1" || !c.ListenIsLoopback() {
		t.Fatalf("flag override: %+v", c)
	}
}

func TestValidate(t *testing.T) {
	if err := parse(t, nil, "-tls-cert", "a").Validate(); err == nil {
		t.Fatal("cert ohne key muss scheitern")
	}
	if err := parse(t, nil, "-dev", "-tls-cert", "a", "-tls-key", "b").Validate(); err == nil {
		t.Fatal("dev + tls muss scheitern")
	}
	if err := parse(t, nil).Validate(); err != nil {
		t.Fatal(err)
	}
}
