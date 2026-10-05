#!/usr/bin/env bash
# SessionStart-Hook (synchron): bereitet Cloud-Sessions vor. Leise, idempotent, blockiert nie.
# Nur in Remote-Umgebungen aktiv. Qt wird bewusst noch nicht installiert.
[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0
set -uo pipefail

ROOT="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/.cache/framebeam/vcpkg}"
export GOTOOLCHAIN=local

warn() { echo "session-start: $1" >&2; }
run() {
  local label="$1"; shift
  local log; log="$(mktemp)"
  "$@" >"$log" 2>&1 || { warn "$label fehlgeschlagen"; tail -n 10 "$log" >&2; }
  rm -f "$log"
}

run "vcpkg-Bootstrap" "$ROOT/scripts/bootstrap-vcpkg.sh"
if command -v go >/dev/null; then
  # "go mod download" ohne Argumente laedt alle Module der Build-Liste inkl. Tool-Abhaengigkeiten.
  run "go mod download" bash -c "cd '$ROOT/server' && go mod download"
else
  warn "go fehlt"
fi
for t in cmake ninja; do
  command -v "$t" >/dev/null || warn "$t fehlt (make check-client nicht lauffaehig)"
done
exit 0
