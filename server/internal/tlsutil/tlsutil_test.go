package tlsutil

import (
	"crypto/ecdsa"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestEnsureSelfSigned(t *testing.T) {
	dir := t.TempDir()
	c1, err := EnsureSelfSigned(dir)
	if err != nil {
		t.Fatal(err)
	}
	leaf, err := x509.ParseCertificate(c1.Certificate[0])
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := leaf.PublicKey.(*ecdsa.PublicKey); !ok || leaf.PublicKeyAlgorithm != x509.ECDSA {
		t.Fatal("not ECDSA")
	}
	if err := leaf.VerifyHostname("localhost"); err != nil {
		t.Fatal(err)
	}
	if err := leaf.VerifyHostname("127.0.0.1"); err != nil {
		t.Fatal(err)
	}
	if err := leaf.VerifyHostname("::1"); err != nil {
		t.Fatal(err)
	}
	st, _ := os.Stat(filepath.Join(dir, "tls", "key.pem"))
	if st.Mode().Perm() != 0o600 {
		t.Fatalf("key mode %v", st.Mode())
	}
	c2, err := EnsureSelfSigned(dir) // second start loads the same certificate
	if err != nil || Fingerprint(c1) != Fingerprint(c2) || len(Fingerprint(c1)) != 95 {
		t.Fatalf("fingerprint %q %v", Fingerprint(c2), err)
	}
}

func leafOf(t *testing.T, c tls.Certificate) *x509.Certificate {
	t.Helper()
	l, err := x509.ParseCertificate(c.Certificate[0])
	if err != nil {
		t.Fatal(err)
	}
	return l
}

// ageCert creates a self-signed certificate as if it had been generated `age` ago.
func ageCert(t *testing.T, dir string, age time.Duration) tls.Certificate {
	t.Helper()
	c, err := create(dir, time.Now().Add(-age))
	if err != nil {
		t.Fatal(err)
	}
	return c
}

func TestRenewalDecision(t *testing.T) {
	day := 24 * time.Hour
	year := 365 * day
	cases := []struct {
		name  string
		age   time.Duration
		renew bool
	}{
		{"expired", 11 * year, true},
		{"expires in about 10 days", 10*year - 10*day, true},
		{"fine", 1 * year, false},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			dir := t.TempDir()
			old := ageCert(t, dir, tc.age)
			c, ren, err := EnsureSelfSignedRenewing(dir, time.Now())
			if err != nil {
				t.Fatal(err)
			}
			if (ren != nil) != tc.renew {
				t.Fatalf("renewed=%v, want %v", ren != nil, tc.renew)
			}
			if !tc.renew {
				if Fingerprint(c) != Fingerprint(old) {
					t.Fatal("certificate changed")
				}
				if _, err := os.Stat(filepath.Join(dir, "tls", "cert.pem.prev")); err == nil {
					t.Fatal("unexpected backup")
				}
				return
			}
			if ren.OldFingerprint != Fingerprint(old) || ren.NewFingerprint != Fingerprint(c) || ren.OldFingerprint == ren.NewFingerprint {
				t.Fatalf("fingerprints wrong: %+v", ren)
			}
			if leafOf(t, c).NotAfter.Before(time.Now().AddDate(9, 11, 0)) {
				t.Fatal("new certificate validity too short")
			}
			prev, err := Load(filepath.Join(dir, "tls", "cert.pem.prev"), filepath.Join(dir, "tls", "key.pem.prev"))
			if err != nil {
				t.Fatalf("backup unusable: %v", err)
			}
			if Fingerprint(prev) != Fingerprint(old) {
				t.Fatal("backup is not the old certificate")
			}
			// Second start: nothing left to renew.
			if _, ren2, err := EnsureSelfSignedRenewing(dir, time.Now()); err != nil || ren2 != nil {
				t.Fatalf("second start renewed: %v %v", ren2, err)
			}
		})
	}
}

func TestRenewSelfSignedForced(t *testing.T) {
	dir := t.TempDir()
	if _, _, err := RenewSelfSigned(dir, time.Now()); err == nil {
		t.Fatal("renewing without certificate must fail")
	}
	old := ageCert(t, dir, time.Hour)
	c, ren, err := RenewSelfSigned(dir, time.Now())
	if err != nil || ren == nil || Fingerprint(c) == Fingerprint(old) || ren.OldFingerprint != Fingerprint(old) {
		t.Fatalf("forced renewal failed: %+v %v", ren, err)
	}
}

func TestOwnCertFilesNeverTouched(t *testing.T) {
	// An own cert/key lives outside <dataDir>/tls; the renewal functions only ever read and
	// write <dataDir>/tls, so an expired own certificate stays byte-identical.
	own, data := t.TempDir(), t.TempDir()
	ageCert(t, own, 11*365*24*time.Hour)
	certB, _ := os.ReadFile(filepath.Join(own, "tls", "cert.pem"))
	keyB, _ := os.ReadFile(filepath.Join(own, "tls", "key.pem"))
	if _, _, err := EnsureSelfSignedRenewing(data, time.Now()); err != nil {
		t.Fatal(err)
	}
	certA, _ := os.ReadFile(filepath.Join(own, "tls", "cert.pem"))
	keyA, _ := os.ReadFile(filepath.Join(own, "tls", "key.pem"))
	if string(certA) != string(certB) || string(keyA) != string(keyB) {
		t.Fatal("own cert/key modified")
	}
}

func TestExpiresSoon(t *testing.T) {
	now := time.Now()
	if !ExpiresSoon(now.Add(29*24*time.Hour), now) || !ExpiresSoon(now.Add(-time.Hour), now) || ExpiresSoon(now.Add(31*24*time.Hour), now) {
		t.Fatal("ExpiresSoon wrong")
	}
}

// mkPair writes a fresh pair into dir/<name>.pem + suffix and returns its fingerprint.
func mkPair(t *testing.T, dataDir, suffix string) string {
	t.Helper()
	_, certFile, keyFile := selfSignedPaths(dataDir)
	if err := os.MkdirAll(filepath.Dir(certFile), 0o700); err != nil {
		t.Fatal(err)
	}
	k, c, err := generate(time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if err := writeFileAtomic(keyFile+suffix, k, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := writeFileAtomic(certFile+suffix, c, 0o644); err != nil {
		t.Fatal(err)
	}
	tc, err := Load(certFile+suffix, keyFile+suffix)
	if err != nil {
		t.Fatal(err)
	}
	return Fingerprint(tc)
}

func TestRenewLeavesNoTempFiles(t *testing.T) {
	dir := t.TempDir()
	if _, err := EnsureSelfSigned(dir); err != nil {
		t.Fatal(err)
	}
	if _, _, err := RenewSelfSigned(dir, time.Now()); err != nil {
		t.Fatal(err)
	}
	ents, _ := os.ReadDir(filepath.Join(dir, "tls"))
	var names []string
	for _, e := range ents {
		names = append(names, e.Name())
	}
	want := map[string]bool{"cert.pem": true, "key.pem": true, "cert.pem.prev": true, "key.pem.prev": true}
	if len(names) != len(want) {
		t.Fatalf("unexpected files %v", names)
	}
	for _, n := range names {
		if !want[n] {
			t.Fatalf("unexpected file %s", n)
		}
	}
	st, _ := os.Stat(filepath.Join(dir, "tls", "key.pem"))
	if st.Mode().Perm() != 0o600 {
		t.Fatalf("key mode %v", st.Mode())
	}
}

func TestRecoverOnlyNewPresent(t *testing.T) {
	// Crash after moving the old pair to .prev, before the .new pair was renamed in.
	dir := t.TempDir()
	prevFP := mkPair(t, dir, ".prev")
	newFP := mkPair(t, dir, ".new")
	c, ren, err := EnsureSelfSignedRenewing(dir, time.Now())
	if err != nil || ren != nil {
		t.Fatalf("err=%v ren=%v", err, ren)
	}
	if Fingerprint(c) != newFP || newFP == prevFP {
		t.Fatalf("expected the staged pair, got %s", Fingerprint(c))
	}
	_, certFile, keyFile := selfSignedPaths(dir)
	if exists(certFile+".new") || exists(keyFile+".new") {
		t.Fatal(".new files should be consumed")
	}
}

func TestRecoverMismatchedCurrentUsesNew(t *testing.T) {
	dir := t.TempDir()
	if _, err := EnsureSelfSigned(dir); err != nil {
		t.Fatal(err)
	}
	_, certFile, keyFile := selfSignedPaths(dir)
	// Replace the key with a foreign one: mismatched current pair.
	k, _, _ := generate(time.Now())
	if err := writeFileAtomic(keyFile, k, 0o600); err != nil {
		t.Fatal(err)
	}
	newFP := mkPair(t, dir, ".new")
	c, _, err := RenewSelfSigned(dir, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	// RenewSelfSigned repairs first, then renews: the result is neither the broken pair nor the staged one.
	if Fingerprint(c) == newFP {
		t.Fatal("renew should create another certificate after repairing")
	}
	got, _ := Load(certFile+".prev", keyFile+".prev")
	if Fingerprint(got) != newFP {
		t.Fatal("repaired .new pair should be kept as .prev")
	}
}

func TestRecoverMismatchedCurrentRestoresPrev(t *testing.T) {
	dir := t.TempDir()
	prevFP := mkPair(t, dir, ".prev")
	cur := mkPair(t, dir, "")
	_ = cur
	_, _, keyFile := selfSignedPaths(dir)
	k, _, _ := generate(time.Now())
	if err := writeFileAtomic(keyFile, k, 0o600); err != nil { // mismatch
		t.Fatal(err)
	}
	c, _, err := EnsureSelfSignedRenewing(dir, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if Fingerprint(c) != prevFP {
		t.Fatal("expected .prev to be restored")
	}
}

func TestRecoverIncompleteNewIsIgnored(t *testing.T) {
	dir := t.TempDir()
	prevFP := mkPair(t, dir, ".prev")
	_, certFile, keyFile := selfSignedPaths(dir)
	k, _, _ := generate(time.Now())
	if err := writeFileAtomic(keyFile+".new", k, 0o600); err != nil { // cert.new missing
		t.Fatal(err)
	}
	c, _, err := EnsureSelfSignedRenewing(dir, time.Now())
	if err != nil || Fingerprint(c) != prevFP {
		t.Fatalf("expected restore of .prev, err=%v", err)
	}
	_ = certFile
}

func TestRecoverUnrecoverable(t *testing.T) {
	dir := t.TempDir()
	mkPair(t, dir, "")
	_, _, keyFile := selfSignedPaths(dir)
	k, _, _ := generate(time.Now())
	if err := writeFileAtomic(keyFile, k, 0o600); err != nil {
		t.Fatal(err)
	}
	if _, _, err := EnsureSelfSignedRenewing(dir, time.Now()); err == nil || errors.Is(err, os.ErrNotExist) {
		t.Fatalf("want clear error, got %v", err)
	}
}

func TestStaleNewIgnoredWhenCurrentOK(t *testing.T) {
	dir := t.TempDir()
	c1, _ := EnsureSelfSigned(dir)
	mkPair(t, dir, ".new")
	c2, _, err := EnsureSelfSignedRenewing(dir, time.Now())
	if err != nil || Fingerprint(c1) != Fingerprint(c2) {
		t.Fatalf("current pair must win: %v", err)
	}
}

func TestLoneKeyOrCertIsFirstStart(t *testing.T) {
	for _, which := range []string{"key", "cert"} {
		dir := t.TempDir()
		mkPair(t, dir, "")
		_, certFile, keyFile := selfSignedPaths(dir)
		rm := keyFile
		if which == "key" {
			rm = certFile // keep only the key
		}
		if err := os.Remove(rm); err != nil {
			t.Fatal(err)
		}
		c, ren, err := EnsureSelfSignedRenewing(dir, time.Now())
		if err != nil || ren != nil || Fingerprint(c) == "" {
			t.Fatalf("lone %s: err=%v", which, err)
		}
		if _, err := Load(certFile, keyFile); err != nil {
			t.Fatalf("lone %s: not regenerated: %v", which, err)
		}
	}
}
