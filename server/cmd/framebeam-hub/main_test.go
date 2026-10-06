package main

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/tlsutil"
)

func TestSetupAdminTwice(t *testing.T) {
	dir := t.TempDir()
	var out bytes.Buffer
	if err := runSetupAdmin([]string{"-data-dir", dir, "-username", "fabio"}, strings.NewReader("secret-1234\n"), &out); err != nil {
		t.Fatal(err)
	}
	if strings.Contains(out.String(), "secret") {
		t.Fatal("password in output")
	}
	err := runSetupAdmin([]string{"-data-dir", dir, "-username", "second"}, strings.NewReader("secret-1234\n"), &out)
	if err == nil {
		t.Fatal("second admin must be refused")
	}
	if err := runSetupAdmin([]string{"-data-dir", dir}, strings.NewReader("x\n"), &out); err == nil {
		t.Fatal("missing -username must fail")
	}
}

func TestRenewCert(t *testing.T) {
	dir := t.TempDir()
	var out strings.Builder
	if err := runRenewCert([]string{"-data-dir", dir}, &out); err == nil {
		t.Fatal("renew without an existing certificate must fail")
	}
	if _, err := tlsutil.EnsureSelfSigned(dir); err != nil {
		t.Fatal(err)
	}
	if err := runRenewCert([]string{"-data-dir", dir}, &out); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "New SHA-256 fingerprint: ") {
		t.Fatalf("output: %q", out.String())
	}
	if _, err := os.Stat(filepath.Join(dir, "tls", "cert.pem.prev")); err != nil {
		t.Fatal("backup missing")
	}
}

func TestRenewCertRefusesOwnCert(t *testing.T) {
	dir, own := t.TempDir(), t.TempDir()
	certFile, keyFile := filepath.Join(own, "c.pem"), filepath.Join(own, "k.pem")
	if err := os.WriteFile(certFile, []byte("own-cert"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(keyFile, []byte("own-key"), 0o600); err != nil {
		t.Fatal(err)
	}
	var out strings.Builder
	if err := runRenewCert([]string{"-data-dir", dir, "-tls-cert", certFile, "-tls-key", keyFile}, &out); err == nil {
		t.Fatal("must refuse with own cert/key")
	}
	if b, _ := os.ReadFile(certFile); string(b) != "own-cert" {
		t.Fatal("own cert modified")
	}
	if b, _ := os.ReadFile(keyFile); string(b) != "own-key" {
		t.Fatal("own key modified")
	}
	if _, err := os.Stat(filepath.Join(dir, "tls")); err == nil {
		t.Fatal("tls dir created")
	}
}
