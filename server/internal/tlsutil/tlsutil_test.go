package tlsutil

import (
	"crypto/ecdsa"
	"crypto/x509"
	"os"
	"path/filepath"
	"testing"
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
		t.Fatal("kein ECDSA")
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
	c2, err := EnsureSelfSigned(dir) // zweiter Start lädt dasselbe Zertifikat
	if err != nil || Fingerprint(c1) != Fingerprint(c2) || len(Fingerprint(c1)) != 95 {
		t.Fatalf("fingerprint %q %v", Fingerprint(c2), err)
	}
}
