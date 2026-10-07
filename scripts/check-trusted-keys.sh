#!/usr/bin/env bash
# The Hub (server/internal/corepkg/keys.go, DefaultTrustedKeys) and the Player (compiled-in key list under client/)
# must trust the same release keys. Fails when a Hub key is missing in the Player sources, or when the Player file
# holding the list contains a key the Hub does not know.
# Usage: scripts/check-trusted-keys.sh   (part of scripts/check.sh packaging)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KEYS_FILE="${FRAMEBEAM_KEYS_GO:-$ROOT/server/internal/corepkg/keys.go}"
CLIENT_DIR="${FRAMEBEAM_CLIENT_DIR:-$ROOT/client}"
KEY_RE='"[A-Za-z0-9+/]{43}="'

hub_keys="$(awk '/DefaultTrustedKeys *=/ {on=1} on {print} on && /^}/ {exit}' "$KEYS_FILE" | grep -oE "$KEY_RE" | tr -d '"' | sort -u || true)"
[ -n "$hub_keys" ] || { echo "no keys found in DefaultTrustedKeys ($KEYS_FILE)" >&2; exit 1; }

rc=0
files=""
while IFS= read -r k; do
  found="$(grep -rlF --exclude-dir=build --exclude-dir=.git --exclude-dir=vcpkg_installed -- "\"$k\"" "$CLIENT_DIR" 2>/dev/null || true)"
  if [ -z "$found" ]; then
    echo "Hub trusted key $k is not embedded in the Player sources under client/" >&2
    rc=1
  else
    files+="$found"$'\n'
  fi
done <<<"$hub_keys"

# Reverse direction, within the files that hold the Hub keys.
while IFS= read -r f; do
  [ -n "$f" ] || continue
  while IFS= read -r k; do
    [ -n "$k" ] || continue
    grep -qxF "$k" <<<"$hub_keys" || { echo "Player key $k (in ${f#"$ROOT"/}) is not in the Hub's DefaultTrustedKeys" >&2; rc=1; }
  done < <(grep -oE "$KEY_RE" "$f" | tr -d '"' | sort -u)
done < <(printf '%s' "$files" | sort -u)

[ "$rc" -eq 0 ] && echo "trusted keys ok: Hub and Player embed the same release key(s)"
exit "$rc"
