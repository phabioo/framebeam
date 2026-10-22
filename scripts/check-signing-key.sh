#!/usr/bin/env bash
# Checks that the signing seed in env UZUME_SIGNING_KEY matches a public key compiled into the server
# (DefaultTrustedKeys in server/internal/corepkg/keys.go) and that a throwaway updates index (built with release-add,
# the code path of update-index.sh), signed with it, verifies.
# Usage: scripts/check-signing-key.sh <path/to/uzume-sign>
# Never prints the seed. Used by release.yml and promote.yml before the updates-index is signed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIGN="${1:?usage: check-signing-key.sh <uzume-sign>}"
KEYS_FILE="${UZUME_KEYS_GO:-$ROOT/server/internal/corepkg/keys.go}"

[ -n "${UZUME_SIGNING_KEY:-}" ] || { echo "::error title=Signing key missing::Secret UZUME_SIGNING_KEY of the environment release (main only) is empty or not set." >&2; exit 1; }

pub="$("$SIGN" pubkey | sed -n 's/^public_key=//p' | head -n 1)"
[ -n "$pub" ] || { echo "::error::could not derive the public key from UZUME_SIGNING_KEY" >&2; exit 1; }
if ! grep -qF "\"$pub\"" "$KEYS_FILE"; then
  echo "::error title=Signing key not trusted::The public key of UZUME_SIGNING_KEY ($pub) is not in DefaultTrustedKeys ($KEYS_FILE); servers would reject the index." >&2
  exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat >"$tmp/release.json" <<'JSON'
{"product":"hub","channel":"beta","version":"0.0.0","commit":"0000000000000000000000000000000000000000",
 "published_at":"2000-01-01T00:00:00Z","protocol_version":1,"min_protocol_version":1,
 "artifacts":[{"platform":"linux-amd64","kind":"deb","name":"uzume-server_0.0.0_amd64.deb","size":1,
   "sha256":"0000000000000000000000000000000000000000000000000000000000000000","url":"https://example.invalid/uzume-server_0.0.0_amd64.deb"}]}
JSON
"$SIGN" release-add -index "$tmp/updates-index.json" -release "$tmp/release.json" -keep 5 >/dev/null
"$SIGN" sign -index "$tmp/updates-index.json" -out "$tmp/updates-index.json.sig" -bundle "$tmp/updates-index.json.signed"
"$SIGN" verify -index "$tmp/updates-index.json" -sig "$tmp/updates-index.json.sig" -pub "$pub"
"$SIGN" verify -index "$tmp/updates-index.json" -bundle "$tmp/updates-index.json.signed" -pub "$pub"
echo "signing key ok: public key is trusted by the Hub and a throwaway updates index verifies"
