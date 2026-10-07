package corepkg

import (
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"strings"
)

// ErrNoTrustedKey is reported when no trusted signing key is configured.
var ErrNoTrustedKey = errors.New("no trusted signing key configured")

// KeyID is the first 16 hex characters of the SHA-256 over the 32-byte public key.
func KeyID(pub ed25519.PublicKey) string {
	h := sha256.Sum256(pub)
	return hex.EncodeToString(h[:])[:16]
}

// ParsePublicKey parses a base64 (std) Ed25519 public key of 32 bytes.
func ParsePublicKey(b64 string) (ed25519.PublicKey, error) {
	b, err := base64.StdEncoding.DecodeString(strings.TrimSpace(b64))
	if err != nil || len(b) != ed25519.PublicKeySize {
		return nil, errors.New("public key must be base64 of 32 bytes")
	}
	return ed25519.PublicKey(b), nil
}

// ParseSeed parses a base64 (std) Ed25519 private key seed of 32 bytes. The error never contains the input.
func ParseSeed(b64 string) ([]byte, error) {
	b, err := base64.StdEncoding.DecodeString(strings.TrimSpace(b64))
	if err != nil || len(b) != ed25519.SeedSize {
		return nil, errors.New("signing key must be base64 of a 32-byte seed")
	}
	return b, nil
}

// PublicFromSeed derives the public key of a seed.
func PublicFromSeed(seed []byte) (ed25519.PublicKey, error) {
	if len(seed) != ed25519.SeedSize {
		return nil, errors.New("seed must be 32 bytes")
	}
	return ed25519.NewKeyFromSeed(seed).Public().(ed25519.PublicKey), nil
}

// Sign signs the exact bytes of the index and returns the signature line "ed25519 <key_id> <base64 sig>\n".
func Sign(index, seed []byte) ([]byte, error) {
	pub, err := PublicFromSeed(seed)
	if err != nil {
		return nil, err
	}
	sig := ed25519.Sign(ed25519.NewKeyFromSeed(seed), index)
	return []byte("ed25519 " + KeyID(pub) + " " + base64.StdEncoding.EncodeToString(sig) + "\n"), nil
}

// ParseSigLine parses a signature line into key id and signature bytes.
func ParseSigLine(sig []byte) (keyID string, signature []byte, err error) {
	line := strings.TrimRight(string(sig), "\r\n")
	if strings.ContainsAny(line, "\r\n") {
		return "", nil, errors.New("signature file must be a single line")
	}
	f := strings.Fields(line)
	if len(f) != 3 || f[0] != "ed25519" {
		return "", nil, errors.New("signature line must be: ed25519 <key_id> <base64 signature>")
	}
	signature, err = base64.StdEncoding.DecodeString(f[2])
	if err != nil || len(signature) != ed25519.SignatureSize {
		return "", nil, errors.New("signature is not valid base64 of 64 bytes")
	}
	return f[1], signature, nil
}

// Verify checks the signature line against the trusted keys (the key is selected by its id).
func Verify(index, sig []byte, keys []ed25519.PublicKey) error {
	if len(keys) == 0 {
		return ErrNoTrustedKey
	}
	id, signature, err := ParseSigLine(sig)
	if err != nil {
		return err
	}
	for _, k := range keys {
		if KeyID(k) == id {
			if !ed25519.Verify(k, index, signature) {
				return errors.New("signature does not match the index")
			}
			return nil
		}
	}
	return fmt.Errorf("signing key %s is not trusted", id)
}
