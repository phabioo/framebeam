package tlsutil

import (
	"crypto/ecdsa"
	"crypto/tls"
	"crypto/x509"
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
