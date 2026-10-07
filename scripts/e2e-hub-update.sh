#!/usr/bin/env bash
# E2E of the Hub packaging and self-update on a real systemd host (CI runner; destructive, installs a service):
#   1. install with packaging/linux/install-hub.sh (script install)
#   2. install the .deb over it (migration keeps data and hub.env, service stays up)
#   3. update round trip: second .deb with a higher version, throwaway signing key, local signed updates index
#      with file:// URLs, `framebeam-hub update stage` as the service user, the path unit applies it.
# Skips (exit 0) with a message when root/sudo or systemd is missing. Env: FRAMEBEAM_E2E_PORT (default 8447),
# FRAMEBEAM_E2E_CLEANUP=0 keeps the installation afterwards.
# Usage: scripts/e2e-hub-update.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${FRAMEBEAM_E2E_PORT:-8447}"
V1="0.3.0-e2e.1"
V2="0.3.0-e2e.2"
SHARE=/srv/framebeam-e2e-updates   # not /tmp: the Hub unit has PrivateTmp
DATA=/var/lib/framebeam

say()  { echo "e2e: $*"; }
fail() { echo "e2e FAIL: $*" >&2; dump_logs; exit 1; }
dump_logs() {
  $SUDO journalctl -u framebeam-hub -u framebeam-hub-update --no-pager -n 40 -o cat 2>/dev/null | tail -n 40 >&2 || true
}

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then SUDO="sudo"; else
    say "SKIP: needs root or passwordless sudo"; exit 0
  fi
fi
[ -d /run/systemd/system ] || { say "SKIP: systemd is not running on this host"; exit 0; }
for t in go dpkg-deb curl; do
  command -v "$t" >/dev/null 2>&1 || { say "SKIP: $t not found"; exit 0; }
done
if $SUDO systemctl is-active --quiet framebeam-hub 2>/dev/null; then
  fail "framebeam-hub is already active on this host; refusing to touch an existing installation"
fi

for p in "$DATA" /etc/framebeam /usr/local/bin/framebeam-hub /usr/bin/framebeam-hub; do
  if $SUDO test -e "$p"; then fail "$p exists: this test needs a host without a FrameBeam Hub installation (it removes everything afterwards)"; fi
done
ARCH="$(dpkg --print-architecture)"
case "$ARCH" in amd64|arm64) ;; *) say "SKIP: unsupported architecture $ARCH"; exit 0 ;; esac
WORK="$(mktemp -d)"
cleanup() {
  local rc=$?
  if [ "${FRAMEBEAM_E2E_CLEANUP:-1}" != 0 ]; then
    $SUDO systemctl stop framebeam-hub-update.path framebeam-hub-update.service framebeam-hub 2>/dev/null || true
    $SUDO dpkg --purge framebeam-hub >/dev/null 2>&1 || true
    $SUDO rm -rf "$SHARE" "$DATA" /etc/framebeam /etc/systemd/system/framebeam-hub.service* /usr/local/bin/framebeam-hub
    $SUDO systemctl daemon-reload 2>/dev/null || true
  fi
  rm -rf "$WORK"
  return "$rc"
}
trap cleanup EXIT

build_hub() { # VERSION OUT
  (cd "$ROOT/server" && CGO_ENABLED=0 GOTOOLCHAIN=local go build -trimpath \
    -ldflags "-s -w -X github.com/phabioo/framebeam/server/internal/version.Version=$1 -X github.com/phabioo/framebeam/server/internal/version.Channel=stable" \
    -o "$2" ./cmd/framebeam-hub)
}
wait_api() { # up to 60 s
  for _ in $(seq 1 60); do
    curl -fsk "https://127.0.0.1:$PORT/.well-known/framebeam" >/dev/null 2>&1 && return 0
    sleep 1
  done
  return 1
}
installed_version() { /usr/bin/framebeam-hub version --json 2>/dev/null | grep -o '"version":"[^"]*"' | head -n1 | cut -d'"' -f4; }

say "building Hub $V1 and $V2 ($ARCH) and framebeam-sign"
build_hub "$V1" "$WORK/hub-v1"
build_hub "$V2" "$WORK/hub-v2"
(cd "$ROOT/server" && CGO_ENABLED=0 GOTOOLCHAIN=local go build -o "$WORK/framebeam-sign" ./cmd/framebeam-sign)
DEB1="$("$ROOT/packaging/linux/build-deb.sh" --binary "$WORK/hub-v1" --arch "$ARCH" --version "$V1" --out "$WORK/deb")"
DEB2="$("$ROOT/packaging/linux/build-deb.sh" --binary "$WORK/hub-v2" --arch "$ARCH" --version "$V2" --out "$WORK/deb")"

# 1. script install
say "1/3 install with install-hub.sh"
$SUDO "$ROOT/packaging/linux/install-hub.sh" install --binary "$WORK/hub-v1" --port "$PORT" --name e2e >/dev/null
wait_api || fail "Hub does not answer after install-hub.sh"
echo "keep-me" | $SUDO tee "$DATA/e2e-marker" >/dev/null
$SUDO chown framebeam:framebeam "$DATA/e2e-marker"
echo "FRAMEBEAM_E2E_MARK=1" | $SUDO tee -a /etc/framebeam/hub.env >/dev/null

# 2. .deb over the script install
say "2/3 install the .deb over it (migration)"
$SUDO dpkg -i "$DEB1" >/dev/null
[ ! -e /usr/local/bin/framebeam-hub ] || fail "old /usr/local/bin/framebeam-hub still present"
$SUDO test -f /etc/framebeam/framebeam-hub.service.pre-deb || fail "old unit was not kept as framebeam-hub.service.pre-deb"
[ ! -e /etc/systemd/system/framebeam-hub.service ] || fail "old unit still shadows the packaged one"
$SUDO grep -qx 'FRAMEBEAM_E2E_MARK=1' /etc/framebeam/hub.env || fail "hub.env was modified"
$SUDO grep -qx "FRAMEBEAM_LISTEN=:$PORT" /etc/framebeam/hub.env || fail "hub.env lost the listen address"
[ "$($SUDO cat "$DATA/e2e-marker")" = keep-me ] || fail "data marker lost"
wait_api || fail "Hub does not answer after the .deb install"
$SUDO systemctl is-active --quiet framebeam-hub || fail "framebeam-hub is not active after the .deb install"
$SUDO systemctl is-active --quiet framebeam-hub-update.path || fail "update path unit is not active"
[ "$(installed_version)" = "$V1" ] || fail "installed version is '$(installed_version)', expected $V1"

# 3. update round trip
say "3/3 update $V1 -> $V2 through a signed local index"
$SUDO install -d -m 0755 "$SHARE"
$SUDO cp "$DEB2" "$SHARE/"
deb2_name="$(basename "$DEB2")"
"$WORK/framebeam-sign" keygen -out "$WORK/seed" >"$WORK/keygen.out"
PUB="$(FRAMEBEAM_SIGNING_KEY="$(cat "$WORK/seed")" "$WORK/framebeam-sign" pubkey | sed -n 's/^public_key=//p' | head -n1)"
[ -n "$PUB" ] || fail "could not derive the throwaway public key"
cat >"$WORK/release.json" <<JSON
{"product":"hub","channel":"stable","version":"$V2","commit":"0000000000000000000000000000000000000000",
 "published_at":"$(date -u +%Y-%m-%dT%H:%M:%SZ)","protocol_version":1,"min_protocol_version":1,
 "artifacts":[{"platform":"linux-$ARCH","kind":"deb","name":"$deb2_name","size":$(stat -c %s "$DEB2"),
   "sha256":"$(sha256sum "$DEB2" | cut -d' ' -f1)","url":"file://$SHARE/$deb2_name"}]}
JSON
"$WORK/framebeam-sign" release-add -allow-file-urls -index "$WORK/updates-index.json" -release "$WORK/release.json"
FRAMEBEAM_SIGNING_KEY="$(cat "$WORK/seed")" "$WORK/framebeam-sign" sign -allow-file-urls -index "$WORK/updates-index.json" -out "$WORK/updates-index.json.sig"
"$WORK/framebeam-sign" verify -index "$WORK/updates-index.json" -sig "$WORK/updates-index.json.sig" -pub "$PUB" >/dev/null
$SUDO install -m 0644 "$WORK/updates-index.json" "$WORK/updates-index.json.sig" "$SHARE/"
{
  echo "FRAMEBEAM_HUB_UPDATE_INDEX_URL=file://$SHARE/updates-index.json"
  echo "FRAMEBEAM_HUB_CORE_TRUST_KEYS=$PUB"
} | $SUDO tee -a /etc/framebeam/hub.env >/dev/null
$SUDO systemctl restart framebeam-hub
wait_api || fail "Hub does not answer after the restart with the update settings"

$SUDO runuser -u framebeam -- bash -c 'set -a; . /etc/framebeam/hub.env; exec /usr/bin/framebeam-hub update stage' \
  || fail "framebeam-hub update stage failed"

ok=0
for _ in $(seq 1 120); do
  if [ "$(installed_version)" = "$V2" ] && $SUDO systemctl is-active --quiet framebeam-hub \
     && curl -fsk "https://127.0.0.1:$PORT/.well-known/framebeam" >/dev/null 2>&1; then ok=1; break; fi
  sleep 1
done
[ "$ok" -eq 1 ] || fail "update was not applied within 120 s (installed: $(installed_version))"
[ ! -e /run/framebeam/update-request ] || fail "update request file was not consumed"
$SUDO grep -qx 'FRAMEBEAM_E2E_MARK=1' /etc/framebeam/hub.env || fail "hub.env lost after the update"
[ "$($SUDO cat "$DATA/e2e-marker")" = keep-me ] || fail "data marker lost after the update"
$SUDO systemctl is-active --quiet framebeam-hub-update.path || fail "update path unit not active after the update"
$SUDO grep -q '"ok" *: *true' "$DATA/updates/last-result.json" \
  || fail "last-result.json missing or not ok"
say "OK: installed $V2, service active, data and hub.env kept"
