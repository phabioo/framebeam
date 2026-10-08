#!/usr/bin/env bash
# Quiet check steps: output buffered, on failure only the last 40 lines.
# Usage: scripts/check.sh hub-fmt|hub-vet|hub-staticcheck|hub-test|hub-codegen|hub-build|hub-deb|generate|client|packaging|hub|all
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
      -ldflags "-s -w -X github.com/phabioo/framebeam/server/internal/version.Version=${HUB_VERSION:-dev} -X github.com/phabioo/framebeam/server/internal/version.Channel=${HUB_CHANNEL:-dev} -X github.com/phabioo/framebeam/server/internal/version.Commit=${HUB_COMMIT:-}" \
      -o "dist/framebeam-hub-linux-$arch" ./cmd/framebeam-hub) || return 1
  done
}
# hub-deb: .deb packages from the binaries in server/dist (make build-hub first), version = HUB_VERSION.
hub_deb() {
  local arch v="${HUB_VERSION:-}"
  [ -n "$v" ] || { echo "HUB_VERSION is required (X.Y.Z[-pre])"; return 1; }
  for arch in amd64 arm64; do
    "$ROOT/packaging/linux/build-deb.sh" --binary "$ROOT/server/dist/framebeam-hub-linux-$arch" \
      --arch "$arch" --version "$v" --out "$ROOT/server/dist" || return 1
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
  # Version and channel of the build (CI computes them once, spec 0.3 S1); unset: CMake defaults (<VERSION>-dev).
  [ -z "${FRAMEBEAM_VERSION:-}" ] || core_arg+=("-DFRAMEBEAM_VERSION=$FRAMEBEAM_VERSION")
  [ -z "${FRAMEBEAM_CHANNEL:-}" ] || core_arg+=("-DFRAMEBEAM_CHANNEL=$FRAMEBEAM_CHANNEL")
  [ -z "${FRAMEBEAM_COMMIT:-}" ] || core_arg+=("-DFRAMEBEAM_COMMIT=$FRAMEBEAM_COMMIT")
  # Core first (idempotent, cached); if it fails, tests with NEEDS_CORE run as SKIP.
  step "client: core" client_core
  [ -n "$CORE_PATH" ] && [ -f "$CORE_PATH" ] && core_arg+=("-DFRAMEBEAM_MELONDS_DS_CORE=$CORE_PATH")
  step "client: configure" bash -c "cd '$ROOT/client' && cmake --preset $p ${core_arg[*]:-}"
  step "client: build"     bash -c "cd '$ROOT/client' && cmake --build --preset $p"
  STEP_TAIL=150 step "client: test" client_test "$p"
}

# packaging_deb TMP: builds the Hub .deb for both architectures from a dummy binary and checks control and contents;
# the update/hub units are verified when systemd-analyze exists.
packaging_deb() {
  local tmp="$1" dir="$ROOT/packaging/linux" arch deb rc=0 listing info
  for arch in amd64 arm64; do
    deb="$("$dir/build-deb.sh" --binary "$tmp/dummy-hub" --arch "$arch" --version 0.3.0-beta.7 --out "$tmp/deb")" \
      || { echo "build-deb.sh failed for $arch"; return 1; }
    [ "$(basename "$deb")" = "framebeam-hub_0.3.0-beta.7_${arch}.deb" ] || { echo "unexpected deb name: $deb"; rc=1; }
    info="$(dpkg-deb -f "$deb")"
    grep -qx 'Package: framebeam-hub' <<<"$info" || { echo "$arch: control lacks Package"; rc=1; }
    grep -qx 'Version: 0.3.0~beta.7' <<<"$info" || { echo "$arch: control Version is not 0.3.0~beta.7"; rc=1; }
    grep -qx "Architecture: $arch" <<<"$info" || { echo "$arch: control Architecture wrong"; rc=1; }
    listing="$(dpkg-deb -c "$deb")"
    local p
    for p in ./usr/bin/framebeam-hub ./lib/systemd/system/framebeam-hub.service \
             ./lib/systemd/system/framebeam-hub-update.path ./lib/systemd/system/framebeam-hub-update.service; do
      grep -q " $p\$" <<<"$listing" || { echo "$arch: $p missing from the package"; rc=1; }
    done
    # Nothing outside usr/bin and the unit directory: no data, no config.
    if grep -E ' \./' <<<"$listing" | grep -vE ' \./(usr/(bin/(framebeam-hub)?)?|lib/(systemd/(system/(framebeam-hub[a-z.-]*)?)?)?)?$' | grep -q .; then
      echo "$arch: unexpected paths in the package"; rc=1
    fi
    local x="$tmp/x-$arch"
    mkdir -p "$x/ctrl"; dpkg-deb -R "$deb" "$x/pkg" >/dev/null
    grep -qx 'ExecStart=/usr/bin/framebeam-hub' "$x/pkg/lib/systemd/system/framebeam-hub.service" || { echo "$arch: unit ExecStart"; rc=1; }
    grep -qx 'PathExists=/run/framebeam/update-request' "$x/pkg/lib/systemd/system/framebeam-hub-update.path" || { echo "$arch: path unit"; rc=1; }
    grep -qx 'ExecStart=/usr/bin/framebeam-hub update apply-staged' "$x/pkg/lib/systemd/system/framebeam-hub-update.service" || { echo "$arch: update unit"; rc=1; }
    for p in postinst prerm postrm; do [ -x "$x/pkg/DEBIAN/$p" ] || { echo "$arch: $p not executable"; rc=1; }; done
  done
  if command -v actionlint >/dev/null 2>&1; then
    actionlint "$ROOT"/.github/workflows/*.yml || rc=1
  else
    echo "skip actionlint (not installed)"
  fi
  if command -v systemd-analyze >/dev/null 2>&1; then
    local u="$tmp/units"; mkdir -p "$u"
    dpkg-deb -x "$deb" "$tmp/xunits"
    cp "$tmp"/xunits/lib/systemd/system/*.* "$u/"
    sed -i 's#^ExecStart=.*#ExecStart=/bin/true#; s#^User=.*#User=root#; s#^Group=.*#Group=root#; /^EnvironmentFile=/d' "$u"/*.service
    systemd-analyze verify "$u"/framebeam-hub.service "$u"/framebeam-hub-update.service "$u"/framebeam-hub-update.path >"$tmp/verify2.log" 2>&1 || true
    if grep -E "$u/" "$tmp/verify2.log" | grep -viE 'not-found|Failed to (connect|create|open)|No such file' | grep -q .; then
      grep -E "$u/" "$tmp/verify2.log" | head -n 5; rc=1
    fi
  fi
  return "$rc"
}

# packaging: syntax, shellcheck, unit verification and an install/uninstall smoke test.
packaging() {
  local dir="$ROOT/packaging/linux" tmp rc=0
  tmp="$(mktemp -d)"
  local f
  for f in "$dir/install-hub.sh" "$dir/build-deb.sh" "$dir"/deb/* "$ROOT/scripts/check-trusted-keys.sh" "$ROOT/scripts/e2e-hub-update.sh" "$ROOT/scripts/next-beta-version.sh" "$ROOT/scripts/update-index.sh"; do
    bash -n "$f" || rc=1
  done
  # Beta version numbering of the CI version job (ADR 0011).
  "$ROOT/scripts/next-beta-version.sh" --self-test >"$tmp/beta-version.log" 2>&1 || { cat "$tmp/beta-version.log"; rc=1; }
  if command -v shellcheck >/dev/null 2>&1; then
    shellcheck "$dir/install-hub.sh" "$dir/build-deb.sh" "$ROOT/scripts/check-trusted-keys.sh" "$ROOT/scripts/e2e-hub-update.sh" || rc=1
    shellcheck -s sh "$dir"/deb/* || rc=1
    # Remaining scripts: warnings and errors only (the e2e scripts use A && ok || die on purpose).
    shellcheck -S warning "$ROOT"/scripts/*.sh "$ROOT/.claude/hooks/session-start.sh" || rc=1
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
  packaging_deb "$tmp" || rc=1
  "$ROOT/scripts/check-trusted-keys.sh" || rc=1
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
  local rc_out
  rc_out="$(FRAMEBEAM_INSTALL_ROOT="$root" "$dir/install-hub.sh" renew-cert 2>&1)" || { echo "renew-cert dry-run failed"; rc=1; }
  grep -q 'FRAMEBEAM_DATA_DIR=/var/lib/framebeam .*renew-cert' <<<"$rc_out" \
    || { echo "renew-cert dry-run lacks the runuser line"; rc=1; }
  mkdir -p "$tmp/cores-in"; : >"$tmp/cores-in/cores-index.json"; : >"$tmp/cores-in/cores-index.json.sig"
  rc_out="$(FRAMEBEAM_INSTALL_ROOT="$root" "$dir/install-hub.sh" import-cores "$tmp/cores-in" 2>&1)" || { echo "import-cores dry-run failed"; rc=1; }
  grep -q 'FRAMEBEAM_DATA_DIR=/var/lib/framebeam .*import-cores' <<<"$rc_out" \
    || { echo "import-cores dry-run lacks the runuser line"; rc=1; }
  if FRAMEBEAM_INSTALL_ROOT="$root" "$dir/install-hub.sh" import-cores >/dev/null 2>&1; then
    echo "import-cores without a directory must fail"; rc=1
  fi
  printf 'FRAMEBEAM_TLS_CERT=/x/c.pem\n' >>"$root/etc/framebeam/hub.env"
  if FRAMEBEAM_INSTALL_ROOT="$root" "$dir/install-hub.sh" renew-cert >/dev/null 2>&1; then
    echo "renew-cert must refuse with FRAMEBEAM_TLS_CERT set"; rc=1
  fi
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
  hub-deb) step "hub: deb packages" hub_deb ;;
  hub-build) step "hub: build linux/amd64+arm64" hub_build ;;
  generate)  step "hub: generate" generate ;;
  client)    client ;;
  all)       "$0" hub || status=1; "$0" client || status=1 ;;
  *) echo "unknown step: $1" >&2; exit 2 ;;
esac
exit "$status"
