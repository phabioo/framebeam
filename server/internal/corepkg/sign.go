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

// Signing contexts (domain separation, ADR 0016 amendment 2026-10-19). A signature made with one context never
// verifies for another document type, even when the JSON shapes overlap.
const (
	ContextUpdatesIndex = "framebeam-updates-index-v2"
	ContextPluginIndex  = "framebeam-plugin-index-v2"
	ContextCoresIndex   = "framebeam-cores-index-v2"
)

const ctxSigTag = "ed25519ctx"

func ctxMessage(context string, index []byte) []byte {
	msg := make([]byte, 0, len(context)+1+len(index))
	msg = append(msg, context...)
	msg = append(msg, '\n')
	return append(msg, index...)
}

// SignContext signs context + "\n" + index and returns the line "ed25519ctx <key_id> <context> <base64 sig>\n".
func SignContext(context string, index, seed []byte) ([]byte, error) {
	if context == "" || strings.ContainsAny(context, " \r\n\t") {
		return nil, errors.New("invalid signing context")
	}
	pub, err := PublicFromSeed(seed)
	if err != nil {
		return nil, err
	}
	sig := ed25519.Sign(ed25519.NewKeyFromSeed(seed), ctxMessage(context, index))
	return []byte(ctxSigTag + " " + KeyID(pub) + " " + context + " " + base64.StdEncoding.EncodeToString(sig) + "\n"), nil
}

// IsContextSig reports whether the signature line is a domain-separated one.
func IsContextSig(sig []byte) bool {
	f := strings.Fields(strings.TrimRight(string(sig), "\r\n"))
	return len(f) > 0 && f[0] == ctxSigTag
}

// VerifyContext checks a signature line for the document type context. A domain-separated line must carry exactly
// that context. A legacy line ("ed25519 <key_id> <sig>", raw bytes) is still accepted while legacy is true (the
// transition of the ADR 0016 amendment 2026-10-19); with legacy false only domain-separated lines pass.
func VerifyContext(context string, index, sig []byte, keys []ed25519.PublicKey, legacy bool) error {
	if !IsContextSig(sig) {
		if !legacy {
			return errors.New("signature has no signing context")
		}
		return Verify(index, sig, keys)
	}
	if len(keys) == 0 {
		return ErrNoTrustedKey
	}
	line := strings.TrimRight(string(sig), "\r\n")
	if strings.ContainsAny(line, "\r\n") {
		return errors.New("signature file must be a single line")
	}
	f := strings.Fields(line)
	if len(f) != 4 {
		return errors.New("signature line must be: ed25519ctx <key_id> <context> <base64 signature>")
	}
	if f[2] != context {
		return fmt.Errorf("signature is for %q, not %q", f[2], context)
	}
	signature, err := base64.StdEncoding.DecodeString(f[3])
	if err != nil || len(signature) != ed25519.SignatureSize {
		return errors.New("signature is not valid base64 of 64 bytes")
	}
	for _, k := range keys {
		if KeyID(k) == f[1] {
			if !ed25519.Verify(k, ctxMessage(context, index), signature) {
				return errors.New("signature does not match the index")
			}
			return nil
		}
	}
	return fmt.Errorf("signing key %s is not trusted", f[1])
}

// Bundle is the single-file form of a signed index: the signature line, then the exact index bytes. One file
// cannot be read as a mismatched pair (ADR 0016 amendment 2026-10-19).
func Bundle(sigLine, index []byte) []byte {
	line := strings.TrimRight(string(sigLine), "\r\n")
	return append([]byte(line+"\n"), index...)
}

// SplitBundle splits a bundle into the signature line and the index bytes.
func SplitBundle(b []byte) (sig, index []byte, err error) {
	head := b
	if len(head) > 4096 {
		head = head[:4096]
	}
	i := strings.IndexByte(string(head), '\n')
	if i < 0 {
		return nil, nil, errors.New("bundle has no signature line")
	}
	return b[:i+1], b[i+1:], nil
}
