#!/usr/bin/env bash
# Quiet check steps: output buffered, on failure only the last 40 lines.
# Usage: scripts/check.sh hub-fmt|hub-vet|hub-test|hub-codegen|hub-build|generate|client|hub|all
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

case "${1:-all}" in
  hub-fmt)     step "hub: fmt" hub_fmt ;;
  hub-vet)     step "hub: vet" hub_vet ;;
  hub-test)    step "hub: test" hub_test ;;
  hub-codegen) step "hub: codegen" hub_codegen ;;
  hub)   step "hub: fmt" hub_fmt; step "hub: vet" hub_vet; step "hub: test" hub_test; step "hub: codegen" hub_codegen ;;
  hub-build) step "hub: build linux/amd64+arm64" hub_build ;;
  generate)  step "hub: generate" generate ;;
  client)    client ;;
  all)       "$0" hub || status=1; "$0" client || status=1 ;;
  *) echo "unknown step: $1" >&2; exit 2 ;;
esac
exit "$status"
