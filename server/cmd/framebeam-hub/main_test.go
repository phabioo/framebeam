package main

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/phabioo/framebeam/server/internal/hub/hubtest"
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

func TestImportCores(t *testing.T) {
	src := hubtest.NewCoreSource(t)
	src.AddPackage(t, "melonds_ds", "1.4.0", "linux-x64", bytes.Repeat([]byte{7}, 64))
	in, data := t.TempDir(), t.TempDir()
	idx, sig := src.Index()
	os.WriteFile(filepath.Join(in, "cores-index.json"), idx, 0o644)
	os.WriteFile(filepath.Join(in, "cores-index.json.sig"), sig, 0o644)
	os.WriteFile(filepath.Join(in, "melonds_ds-1.4.0-linux-x64-melonds_ds.so"), bytes.Repeat([]byte{7}, 64), 0o644)
	os.WriteFile(filepath.Join(in, "LICENSE.txt"), []byte("license of melonds_ds"), 0o644)

	var out strings.Builder
	// Without the test key: refused (only the compiled-in release key is trusted).
	if err := runImportCores([]string{in, "-data-dir", data}, &out); err == nil || !strings.Contains(err.Error(), "is not trusted") {
		t.Fatalf("err %v", err)
	}
	// Directory before or after the flags.
	for _, args := range [][]string{{in, "-data-dir", data, "-core-trust-key", src.PublicKeyB64()},
		{"-data-dir", data, "-core-trust-key", src.PublicKeyB64(), in}} {
		out.Reset()
		if err := runImportCores(args, &out); err != nil {
			t.Fatal(err)
		}
		if !strings.Contains(out.String(), "1 package(s)") {
			t.Fatalf("summary %q", out.String())
		}
	}
	if !strings.Contains(out.String(), "2 already cached") {
		t.Fatalf("second import: %q", out.String())
	}
	if _, err := os.Stat(filepath.Join(data, "cores", "melonds_ds", "1.4.0", "linux-x64", "melonds_ds.so")); err != nil {
		t.Fatal(err)
	}
	if err := runImportCores([]string{"-data-dir", data}, &out); err == nil {
		t.Fatal("missing directory accepted")
	}
	if err := runImportCores([]string{in, "-data-dir", data, "-core-trust-key", "bad"}, &out); err == nil {
		t.Fatal("bad key accepted")
	}
}
