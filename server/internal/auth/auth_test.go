package auth

import (
	"strings"
	"testing"
)

var fast = Params{Time: 1, MemoryKiB: 64, Threads: 1, KeyLen: 32, SaltLen: 16}

func TestPasswordRoundTrip(t *testing.T) {
	h, err := HashPassword("correct horse", fast)
	if err != nil || !strings.HasPrefix(h, "$argon2id$v=19$m=64,t=1,p=1$") {
		t.Fatalf("hash: %q %v", h, err)
	}
	if ok, err := VerifyPassword("correct horse", h); !ok || err != nil {
		t.Fatalf("verify: %v %v", ok, err)
	}
	if ok, _ := VerifyPassword("wrong", h); ok {
		t.Fatal("falsches Passwort akzeptiert")
	}
	h2, _ := HashPassword("correct horse", fast)
	if h == h2 {
		t.Fatal("Salt fehlt")
	}
	if _, err := VerifyPassword("x", "garbage"); err == nil {
		t.Fatal("Format nicht geprüft")
	}
	if _, err := VerifyPassword("x", "$argon2id$v=19$m=999999999,t=1,p=1$AAAA$AAAA"); err == nil {
		t.Fatal("absurde Parameter akzeptiert")
	}
}

func TestDefaultParamsRoundTrip(t *testing.T) {
	h, _ := HashPassword("pw", DefaultParams)
	if ok, _ := VerifyPassword("pw", h); !ok {
		t.Fatal("default params")
	}
}

func TestTokens(t *testing.T) {
	a, _ := NewToken(PrefixAccess)
	b, _ := NewToken(PrefixAccess)
	if a == b || !strings.HasPrefix(a, "fba_") || len(a) != 4+43 {
		t.Fatalf("token: %q", a)
	}
	if !EqualHash(HashToken(a), HashToken(a)) || EqualHash(HashToken(a), HashToken(b)) || len(HashToken(a)) != 64 {
		t.Fatal("hash")
	}
}
