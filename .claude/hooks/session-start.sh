#!/usr/bin/env bash
# SessionStart hook (synchronous): prepares cloud sessions. Quiet, idempotent, never blocks.
# Only active in remote environments. Installs Qt 6.4 (apt) for the Player; the core build is only available via make.
[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || [ "${FRAMEBEAM_CLOUD_SETUP:-}" = "1" ] || exit 0
set -uo pipefail

ROOT="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/.cache/framebeam/vcpkg}"
export GOTOOLCHAIN=local

FAILED=0
warn() { echo "session-start: $1" >&2; FAILED=1; }
run() {
  local label="$1"; shift
  local log; log="$(mktemp)"
  "$@" >"$log" 2>&1 || { warn "$label failed"; tail -n 10 "$log" >&2; }
  rm -f "$log"
}

run "vcpkg bootstrap" "$ROOT/scripts/bootstrap-vcpkg.sh"
if command -v go >/dev/null; then
  # "go mod download" without arguments downloads all modules in the build list, including tool dependencies.
  run "go mod download" bash -c "cd '$ROOT/server' && go mod download"
else
  warn "go missing"
fi
# Qt 6.4 packages (Ubuntu noble) + Xvfb + media dev packages (FFmpeg, Opus, OpenSSL for libdatachannel), only if one
# is missing; core, libdatachannel and SDL3 builds deliberately not here (too long; scripts/check.sh builds libdatachannel and SDL3, no apt packages needed for SDL3).
QT_PKGS="qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-window qml6-module-qtqml-workerscript qml6-module-qtquick-templates libgl-dev libepoxy-dev xvfb libavcodec-dev libswscale-dev libopus-dev qt6-websockets-dev libssl-dev pkg-config"
missing=0
for p in $QT_PKGS; do dpkg -s "$p" >/dev/null 2>&1 || missing=1; done
if [ "$missing" = 1 ]; then
  if command -v apt-get >/dev/null; then
    SUDO=""; [ "$(id -u)" = 0 ] || SUDO="sudo"
    # shellcheck disable=SC2086
    run "Qt apt packages" bash -c "$SUDO apt-get update -qq && DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -qq $QT_PKGS"
  else
    warn "apt-get missing (Qt not installed)"
  fi
fi
for t in cmake ninja; do
  command -v "$t" >/dev/null || warn "$t missing (make check-client cannot run)"
done
# Only the generic cloud setup (scripts/setup-cloud.sh) treats failures as fatal; Claude sessions never block.
[ "$FAILED" = 0 ] || [ "${FRAMEBEAM_CLOUD_SETUP:-}" != "1" ] || exit 1
exit 0
