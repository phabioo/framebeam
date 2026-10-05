#!/usr/bin/env bash
# SessionStart-Hook (synchron): bereitet Cloud-Sessions vor. Leise, idempotent, blockiert nie.
# Nur in Remote-Umgebungen aktiv. Installiert Qt 6.4 (apt) fuer den Player; den Core-Build gibt es nur ueber make.
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
# Qt-6.4-Pakete (Ubuntu noble) + Xvfb, nur wenn eines fehlt; Core-Build bewusst nicht hier (zu lang).
QT_PKGS="qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-window qml6-module-qtqml-workerscript qml6-module-qtquick-templates libgl-dev libepoxy-dev xvfb"
missing=0
for p in $QT_PKGS; do dpkg -s "$p" >/dev/null 2>&1 || missing=1; done
if [ "$missing" = 1 ]; then
  if command -v apt-get >/dev/null; then
    SUDO=""; [ "$(id -u)" = 0 ] || SUDO="sudo"
    # shellcheck disable=SC2086
    run "Qt-apt-Pakete" bash -c "$SUDO apt-get update -qq && DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -qq $QT_PKGS"
  else
    warn "apt-get fehlt (Qt nicht installiert)"
  fi
fi
for t in cmake ninja; do
  command -v "$t" >/dev/null || warn "$t fehlt (make check-client nicht lauffaehig)"
done
exit 0
