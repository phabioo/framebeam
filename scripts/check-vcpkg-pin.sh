#!/usr/bin/env bash
# Pin check for the vcpkg baseline (CI-5, ADR 0008 amendment 2026-10-19).
#   scripts/check-vcpkg-pin.sh            offline: format and single source of truth (part of make check-hub)
#   scripts/check-vcpkg-pin.sh --online   additionally: the baseline commit exists upstream and is a release tag; warns when
#                                         a newer vcpkg release tag exists (used by the weekly dependency workflow)
# The only pin is "builtin-baseline" in client/vcpkg.json. scripts/bootstrap-vcpkg.sh reads it from there, so Linux
# (bootstrap) and Windows (manifest mode) always use the same vcpkg revision, and a Dependabot bump of vcpkg.json is enough.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$root/client/vcpkg.json"
bootstrap="$root/scripts/bootstrap-vcpkg.sh"

baseline="$(sed -n 's/.*"builtin-baseline"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$manifest" | head -n 1)"
[[ "$baseline" =~ ^[0-9a-f]{40}$ ]] || { echo "ERROR vcpkg pin: builtin-baseline in client/vcpkg.json must be a 40-character lowercase hex commit (got '${baseline}')" >&2; exit 1; }

# No second copy of the commit anywhere: the bootstrap script must not carry its own 40-hex literal.
if grep -nE '[0-9a-f]{40}' "$bootstrap" >&2; then
  echo "ERROR vcpkg pin: $bootstrap contains a commit literal; it must read builtin-baseline from client/vcpkg.json" >&2
  exit 1
fi
grep -q 'client/vcpkg.json' "$bootstrap" || { echo "ERROR vcpkg pin: bootstrap-vcpkg.sh does not read client/vcpkg.json" >&2; exit 1; }

# Other places that must not hard-code the baseline.
if grep -rnE "builtin-baseline[\"']?[[:space:]]*[:=][[:space:]]*[\"']?[0-9a-f]{40}" "$root/.github" "$root/scripts" "$root/docs" 2>/dev/null | grep -v '^.*check-vcpkg-pin.sh'; then
  echo "ERROR vcpkg pin: the baseline is hard-coded outside client/vcpkg.json" >&2
  exit 1
fi

if [ "${1:-}" = "--online" ]; then
  tags="$(git ls-remote --tags https://github.com/microsoft/vcpkg 'refs/tags/20*' | sed 's|refs/tags/||')"
  [ -n "$tags" ] || { echo "ERROR vcpkg pin: could not list vcpkg tags" >&2; exit 1; }
  tag="$(awk -v b="$baseline" '$1 == b {print $2}' <<<"$tags" | sed 's/\^{}$//' | head -n 1)"
  [ -n "$tag" ] || { echo "ERROR vcpkg pin: baseline $baseline is not a vcpkg release tag" >&2; exit 1; }
  newest="$(awk '{print $2}' <<<"$tags" | sed 's/\^{}$//' | sort -u | tail -n 1)"
  if [ "$tag" != "$newest" ]; then
    echo "::warning title=vcpkg baseline behind::client/vcpkg.json is at $tag, newest vcpkg release is $newest (bump per docs/development.md)"
  fi
  echo "vcpkg baseline $tag (newest release $newest)"
fi
echo "ok  vcpkg pin: ${baseline:0:10}"
