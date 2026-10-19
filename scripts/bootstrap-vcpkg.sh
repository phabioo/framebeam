#!/usr/bin/env bash
# Clones and bootstraps vcpkg (pinned, shallow). Idempotent and quiet.
# Target: ${VCPKG_ROOT:-$HOME/.cache/framebeam/vcpkg}
set -euo pipefail

# The pin is "builtin-baseline" in client/vcpkg.json (single source of truth, shared with the Windows manifest build;
# scripts/check-vcpkg-pin.sh checks that no second copy exists).
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VCPKG_COMMIT="$(sed -n 's/.*"builtin-baseline"[[:space:]]*:[[:space:]]*"\([0-9a-f]\{40\}\)".*/\1/p' "$here/../client/vcpkg.json" | head -n 1)"
[ -n "$VCPKG_COMMIT" ] || { echo "ERROR bootstrap-vcpkg: no 40-hex builtin-baseline in client/vcpkg.json" >&2; exit 1; }
VCPKG_REPO="https://github.com/microsoft/vcpkg"
root="${VCPKG_ROOT:-$HOME/.cache/framebeam/vcpkg}"

if [ -x "$root/vcpkg" ] && [ "$(git -C "$root" rev-parse HEAD 2>/dev/null)" = "$VCPKG_COMMIT" ]; then
  exit 0
fi

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
fail() { echo "ERROR bootstrap-vcpkg: $1" >&2; tail -n 40 "$log" >&2; exit 1; }

if [ ! -d "$root/.git" ]; then
  mkdir -p "$root"
  git -C "$root" init -q >"$log" 2>&1 || fail "git init"
  git -C "$root" remote add origin "$VCPKG_REPO" >>"$log" 2>&1 || fail "remote add"
fi
if [ "$(git -C "$root" rev-parse HEAD 2>/dev/null || true)" != "$VCPKG_COMMIT" ]; then
  git -C "$root" fetch -q --depth 1 origin "$VCPKG_COMMIT" >>"$log" 2>&1 || fail "git fetch"
  git -C "$root" checkout -q --force "$VCPKG_COMMIT" >>"$log" 2>&1 || fail "git checkout"
fi
"$root/bootstrap-vcpkg.sh" -disableMetrics >>"$log" 2>&1 || fail "bootstrap"
echo "ok  vcpkg: $root @ ${VCPKG_COMMIT:0:10}"
