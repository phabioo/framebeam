#!/usr/bin/env bash
# SessionStart hook (synchronous): prepares cloud sessions. Quiet, idempotent, never blocks.
# Only active in remote environments. Installs Qt 6.4 (apt) for the Player; the core build is only available via make.
[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0
set -uo pipefail

ROOT="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/.cache/framebeam/vcpkg}"
export GOTOOLCHAIN=local

warn() { echo "session-start: $1" >&2; }
run() {
  local label="$1"; shift
  local log; log="$(mktemp)"
  "$@" >"$log" 2>&1 || { warn "$label failed"; tail -n 10 "$log" >&2; }
  rm -f "$log"
}

run "vcpkg bootstrap" "$ROOT/scripts/bootstrap-vcpkg.sh"
# Go: scripts pin GOTOOLCHAIN=local, so a system go older than the "go" directive in server/go.mod cannot build.
# Fetch the toolchain named in go.mod's "toolchain" line into the module cache and put its bin dir first on PATH.
ensure_go_toolchain() {
  command -v go >/dev/null || return 0
  local gomod="$ROOT/server/go.mod" need tc have newest dir
  need="$(awk '$1=="go"{print $2; exit}' "$gomod")"
  tc="$(awk '$1=="toolchain"{print $2; exit}' "$gomod")"
  have="$(GOTOOLCHAIN=local go env GOVERSION)"; have="${have#go}"
  [ -n "$need" ] && [ -n "$tc" ] || return 0
  newest="$(printf '%s\n%s\n' "$need" "$have" | sort -V | tail -n1)"
  [ "$newest" = "$have" ] && return 0
  (cd "$ROOT/server" && GOTOOLCHAIN="$tc" go version) >/dev/null 2>&1 || { warn "go toolchain $tc download failed"; return 0; }
  dir="$(GOTOOLCHAIN=local go env GOMODCACHE)/golang.org/toolchain@v0.0.1-$tc.$(GOTOOLCHAIN=local go env GOOS)-$(GOTOOLCHAIN=local go env GOARCH)/bin"
  [ -x "$dir/go" ] || { warn "go toolchain $tc not found at $dir"; return 0; }
  export PATH="$dir:$PATH"
  if [ -n "${CLAUDE_ENV_FILE:-}" ] && ! grep -qsF "$dir" "$CLAUDE_ENV_FILE"; then
    echo "export PATH=\"$dir:\$PATH\"" >> "$CLAUDE_ENV_FILE"
  fi
}
ensure_go_toolchain

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
exit 0
