#!/usr/bin/env bash
# Checks that the signing seed in env FRAMEBEAM_SIGNING_KEY matches a public key compiled into the Hub
# (DefaultTrustedKeys in server/internal/corepkg/keys.go) and that a throwaway index signed with it verifies.
# Usage: scripts/check-core-signing-key.sh <path/to/framebeam-sign>
# Never prints the seed. Used by release.yml and promote.yml before the updates-index is signed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIGN="${1:?usage: check-core-signing-key.sh <framebeam-sign>}"
KEYS_FILE="${FRAMEBEAM_KEYS_GO:-$ROOT/server/internal/corepkg/keys.go}"

[ -n "${FRAMEBEAM_SIGNING_KEY:-}" ] || { echo "::error title=Signing key missing::Repository secret FRAMEBEAM_SIGNING_KEY is empty or not set." >&2; exit 1; }

pub="$("$SIGN" pubkey | sed -n 's/^public_key=//p' | head -n 1)"
[ -n "$pub" ] || { echo "::error::could not derive the public key from FRAMEBEAM_SIGNING_KEY" >&2; exit 1; }
if ! grep -qF "\"$pub\"" "$KEYS_FILE"; then
  echo "::error title=Signing key not trusted::The public key of FRAMEBEAM_SIGNING_KEY ($pub) is not in DefaultTrustedKeys ($KEYS_FILE); Hubs would reject the index." >&2
  exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat >"$tmp/package.json" <<'JSON'
{"core_id":"check_key","version":"0.0.0","platform":"linux-x64","license":"none","source_url":"https://example.invalid/",
 "source_ref":"check","origin":"signing key check",
 "files":[{"name":"lib.so","role":"library","size":1,"sha256":"0000000000000000000000000000000000000000000000000000000000000000","url":"https://example.invalid/lib.so"}]}
JSON
"$SIGN" add -index "$tmp/cores-index.json" -package "$tmp/package.json"
"$SIGN" sign -index "$tmp/cores-index.json" -out "$tmp/cores-index.json.sig"
"$SIGN" verify -index "$tmp/cores-index.json" -sig "$tmp/cores-index.json.sig" -pub "$pub"
echo "signing key ok: public key is trusted by the Hub and a throwaway index verifies"
