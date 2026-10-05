#!/usr/bin/env bash
# Quiet check steps: output buffered, on failure only the last 40 lines.
# Usage: scripts/check.sh hub-fmt|hub-vet|hub-test|hub-codegen|hub-build|generate|client|packaging|hub|all
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
    tail -n 40 "$log"
    status=1
  fi
  rm -f "$log"
}

hub_fmt() {
  local out; out="$(cd "$ROOT/server" && gofmt -l .)" || return 1
  [ -z "$out" ] || { echo "not gofmt-formatted:"; echo "$out"; return 1; }
}
hub_vet()  { (cd "$ROOT/server" && go vet ./...); }
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
client() {
  local p="${CLIENT_PRESET:-linux-debug}"
  local -a core_arg=()
  # Core first (idempotent, cached); if it fails, tests with NEEDS_CORE run as SKIP.
  step "client: core" client_core
  [ -n "$CORE_PATH" ] && [ -f "$CORE_PATH" ] && core_arg=("-DFRAMEBEAM_MELONDS_DS_CORE=$CORE_PATH")
  step "client: configure" bash -c "cd '$ROOT/client' && cmake --preset $p ${core_arg[*]:-}"
  step "client: build"     bash -c "cd '$ROOT/client' && cmake --build --preset $p"
  step "client: test"      bash -c "cd '$ROOT/client' && ctest --preset $p"
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
  hub-test)    step "hub: test" hub_test ;;
  hub-codegen) step "hub: codegen" hub_codegen ;;
  packaging) step "packaging" packaging ;;
  hub)   step "hub: fmt" hub_fmt; step "hub: vet" hub_vet; step "hub: test" hub_test; step "hub: codegen" hub_codegen; step "packaging" packaging ;;
  hub-build) step "hub: build linux/amd64+arm64" hub_build ;;
  generate)  step "hub: generate" generate ;;
  client)    client ;;
  all)       "$0" hub || status=1; "$0" client || status=1 ;;
  *) echo "unknown step: $1" >&2; exit 2 ;;
esac
exit "$status"
