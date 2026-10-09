package corepkg

import (
	"crypto/ed25519"
	"fmt"
)

// DefaultTrustedKeys are the compiled-in trusted signing keys (base64 std of the 32-byte Ed25519 public key).
// The private counterpart of the FrameBeam release key lives only in the GitHub secret FRAMEBEAM_SIGNING_KEY.
var DefaultTrustedKeys = []string{
	"XNEQ9t7C2AtGzhfsg+oZwDKp8G2SG0iLxAQGg1u86PI=", // FrameBeam release key (2026-10-07)
}

// TrustedKeys parses DefaultTrustedKeys plus the extra base64 public keys (Hub flag/env).
func TrustedKeys(extra []string) ([]ed25519.PublicKey, error) {
	var out []ed25519.PublicKey
	for _, group := range [][]string{DefaultTrustedKeys, extra} {
		for _, k := range group {
			pk, err := ParsePublicKey(k)
			if err != nil {
				return nil, fmt.Errorf("trusted key: %w", err)
			}
			out = append(out, pk)
		}
	}
	return out, nil
}
