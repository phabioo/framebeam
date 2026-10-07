package corepkg

import (
	"crypto/ed25519"
	"fmt"
)

// DefaultIndexURL is the fixed trusted source of the signed core index (FrameBeam's own GitHub Releases).
// The signature lives at the same URL + ".sig".
const DefaultIndexURL = "https://github.com/phabioo/framebeam/releases/download/cores-index/cores-index.json"

// DefaultTrustedKeys are the compiled-in trusted signing keys (base64 std of the 32-byte Ed25519 public key).
// The FrameBeam release public key goes here once it has been generated (`framebeam-sign keygen`). Until then
// the list is empty and the Hub needs an explicit --core-trust-key to accept any index.
var DefaultTrustedKeys = []string{}

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
