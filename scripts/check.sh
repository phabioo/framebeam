#!/usr/bin/env bash
# Quiet check steps: output buffered, on failure only the last 40 lines.
# Usage: scripts/check.sh hub-fmt|hub-vet|hub-staticcheck|hub-test|hub-codegen|hub-build|generate|client|packaging|hub|all
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CGO_ENABLED=0 GOTOOLCHAIN=local
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/.cache/framebeam/vcpkg}"
status=0

# step <label> <command...>: runs it, reports "ok  label" or "FAIL label" + log tail.
step() {
  local label="$1"; shift
  local log; log="$(mktemp)"
  if "$@" >"$log" 2>&1; then
    echo "ok  $label"
  else
    echo "FAIL $label"
    tail -n "${STEP_TAIL:-40}" "$log"
    status=1
  fi
  rm -f "$log"
}

hub_fmt() {
  local out; out="$(cd "$ROOT/server" && gofmt -l .)" || return 1
  [ -z "$out" ] || { echo "not gofmt-formatted:"; echo "$out"; return 1; }
}
hub_vet()  { (cd "$ROOT/server" && go vet ./...); }
STATICCHECK_VERSION="2025.1.1"
# Pinned staticcheck (installed into GOBIN/GOPATH on demand); findings in server/internal/api/api.gen.go are excluded.
hub_staticcheck() {
  local bin
  bin="$(go env GOBIN)"; [ -n "$bin" ] || bin="$(go env GOPATH)/bin"
  if ! "$bin/staticcheck" -version 2>/dev/null | grep -q "$STATICCHECK_VERSION"; then
    go install "honnef.co/go/tools/cmd/staticcheck@$STATICCHECK_VERSION" || return 1
  fi
  local out rc=0
  out="$(cd "$ROOT/server" && "$bin/staticcheck" ./... 2>&1)" || rc=$?
  # rc 1 = findings; drop those in the generated api.gen.go (no "Code generated" header). Other output (rc >= 2, build errors) stays.
  out="$(printf '%s\n' "$out" | grep -v 'api\.gen\.go:' || true)"
  if [ "$rc" -ge 2 ] || [ -n "$(printf '%s' "$out" | tr -d '[:space:]')" ]; then printf '%s\n' "$out"; return 1; fi
}
hub_test() { (cd "$ROOT/server" && go test ./...); }
generate() { (cd "$ROOT/server" && go generate ./...); }
hub_codegen() {
  local before after
  before="$(cd "$ROOT/server/internal/api" && cat ./*.gen.go | sha256sum)"
  generate || return 1
  after="$(cd "$ROOT/server/internal/api" && cat ./*.gen.go | sha256sum)"
  [ "$before" = "$after" ] || { echo "Generated code is stale: run 'make generate' and commit"; return 1; }
  (cd "$ROOT/server" && go mod tidy -diff) || { echo "go.mod/go.sum not tidy: run 'go mod tidy'"; return 1; }
}
hub_build() {
  local arch
  mkdir -p "$ROOT/server/dist"
  for arch in amd64 arm64; do
    (cd "$ROOT/server" && GOOS=linux GOARCH="$arch" go build -trimpath \
      -ldflags "-s -w -X github.com/phabioo/framebeam/server/internal/version.Version=${HUB_VERSION:-dev}" \
      -o "dist/framebeam-hub-linux-$arch" ./cmd/framebeam-hub) || return 1
  done
}
CORE_PATH=""
client_core() { CORE_PATH="$("$ROOT/scripts/fetch-melonds-ds.sh")" && [ -f "$CORE_PATH" ]; }
DDC_PATH=""
client_libdatachannel() { DDC_PATH="$("$ROOT/scripts/fetch-libdatachannel.sh")" && [ -d "$DDC_PATH" ]; }
SDL3_PATH=""
client_sdl3() { SDL3_PATH="$("$ROOT/scripts/fetch-sdl3.sh")" && [ -d "$SDL3_PATH" ]; }
# ctest with the output of failed tests (QtTest FAIL!/QWARN/QFATAL with context) so the assertion is not lost.
client_test() {
  local p="$1" out rc=0
  out="$(mktemp)"
  (cd "$ROOT/client" && ctest --preset "$p" --output-on-failure >"$out" 2>&1) || rc=$?
  if [ "$rc" -ne 0 ]; then
    grep -E '^\s*[0-9]+/[0-9]+ Test\s+#[0-9]+: .*\*\*\*|tests failed|The following tests FAILED|^\s+[0-9]+ - ' "$out" | head -n 20
    echo "--- failed test output (FAIL!/QWARN/QFATAL, context) ---"
    grep -E -B2 -A4 'FAIL!|QFATAL|QWARN|Exception|Segmentation' "$out" | head -n 110
  fi
  rm -f "$out"
  return "$rc"
}
client() {
  local p="${CLIENT_PRESET:-linux-debug}"
  local -a core_arg=()
  # libdatachannel (pinned, cached under ~/.cache/framebeam/deps): mandatory, built on first use.
  step "client: libdatachannel" client_libdatachannel
  [ -n "$DDC_PATH" ] && [ -d "$DDC_PATH" ] || return 1
  # SDL3 (pinned, gamepad only, cached under ~/.cache/framebeam/deps): mandatory, built on first use.
  step "client: sdl3" client_sdl3
  [ -n "$SDL3_PATH" ] && [ -d "$SDL3_PATH" ] || return 1
  core_arg=("-DCMAKE_PREFIX_PATH=$DDC_PATH" "-DSDL3_ROOT=$SDL3_PATH")
  # Core first (idempotent, cached); if it fails, tests with NEEDS_CORE run as SKIP.
  step "client: core" client_core
  [ -n "$CORE_PATH" ] && [ -f "$CORE_PATH" ] && core_arg+=("-DFRAMEBEAM_MELONDS_DS_CORE=$CORE_PATH")
  step "client: configure" bash -c "cd '$ROOT/client' && cmake --preset $p ${core_arg[*]:-}"
  step "client: build"     bash -c "cd '$ROOT/client' && cmake --build --preset $p"
  STEP_TAIL=150 step "client: test" client_test "$p"
}

# packaging: syntax, shellcheck, unit verification and an install/uninstall smoke test.
packaging() {
  local dir="$ROOT/packaging/linux" tmp rc=0
  tmp="$(mktemp -d)"
  bash -n "$dir/install-hub.sh" || rc=1
  if command -v shellcheck >/dev/null 2>&1; then
    shellcheck "$dir/install-hub.sh" || rc=1
  else
    echo "skip shellcheck (not installed)"
  fi
  if command -v systemd-analyze >/dev/null 2>&1; then
    sed 's#^ExecStart=.*#ExecStart=/bin/true#; s#^User=.*#User=root#; s#^Group=.*#Group=root#' \
      "$dir/framebeam-hub.service" >"$tmp/framebeam-hub.service"
    # Complaints naming the temp unit are real and fail; anything else (e.g. no
    # systemd manager in a container) is tolerated.
    if ! systemd-analyze verify "$tmp/framebeam-hub.service" >"$tmp/verify.log" 2>&1 \
       && grep -q "$tmp/framebeam-hub.service" "$tmp/verify.log"; then
      grep "$tmp/framebeam-hub.service" "$tmp/verify.log" | head -n 5
      rc=1
    fi
  else
    echo "skip systemd-analyze (not installed)"
  fi
  printf '#!/bin/sh\necho "framebeam-hub 0.0.0-smoke"\n' >"$tmp/dummy-hub"
  chmod +x "$tmp/dummy-hub"
  local root="$tmp/root"
  FRAMEBEAM_INSTALL_ROOT="$root" "$dir/install-hub.sh" install --binary "$tmp/dummy-hub" \
    --port 8444 --no-start || rc=1
  local bad
  for bad in /etc /var/lib; do
    if FRAMEBEAM_INSTALL_ROOT="$tmp/bad" "$dir/install-hub.sh" install --binary "$tmp/dummy-hub" \
        --data-dir "$bad" --no-start >/dev/null 2>&1; then
      echo "install --data-dir $bad should have failed"; rc=1
    fi
  done
  grep -qx 'FRAMEBEAM_LISTEN=:8444' "$root/etc/framebeam/hub.env" \
    || { echo "hub.env lacks FRAMEBEAM_LISTEN=:8444"; rc=1; }
  [ -x "$root/usr/local/bin/framebeam-hub" ] || { echo "binary not installed"; rc=1; }
  [ -f "$root/etc/systemd/system/framebeam-hub.service" ] || { echo "unit not installed"; rc=1; }
  FRAMEBEAM_INSTALL_ROOT="$root" "$dir/install-hub.sh" uninstall --purge --yes || rc=1
  [ ! -e "$root/usr/local/bin/framebeam-hub" ] && [ ! -e "$root/etc/framebeam" ] \
    || { echo "uninstall --purge left files behind"; rc=1; }
  rm -rf "$tmp"
  return "$rc"
}

case "${1:-all}" in
  hub-fmt)     step "hub: fmt" hub_fmt ;;
  hub-vet)     step "hub: vet" hub_vet ;;
  hub-staticcheck) step "hub: staticcheck" hub_staticcheck ;;
  hub-test)    step "hub: test" hub_test ;;
  hub-codegen) step "hub: codegen" hub_codegen ;;
  packaging) step "packaging" packaging ;;
  hub)   step "hub: fmt" hub_fmt; step "hub: vet" hub_vet; step "hub: staticcheck" hub_staticcheck; step "hub: test" hub_test; step "hub: codegen" hub_codegen; step "packaging" packaging ;;
  hub-build) step "hub: build linux/amd64+arm64" hub_build ;;
  generate)  step "hub: generate" generate ;;
  client)    client ;;
  all)       "$0" hub || status=1; "$0" client || status=1 ;;
  *) echo "unknown step: $1" >&2; exit 2 ;;
esac
exit "$status"
