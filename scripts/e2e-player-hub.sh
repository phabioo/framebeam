#!/usr/bin/env bash
# E2E: framebeam_player_cli against a real FrameBeam Hub (built locally, self-signed TLS, loopback).
# Quiet: only "ok  <step>" / "FAIL <step>" + log tail. Dummy data, no real ROMs.
# Usage: scripts/e2e-player-hub.sh [path/to/framebeam_player_cli]
#   Default: client/build/linux-debug/network/framebeam_player_cli
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI="${1:-$ROOT/client/build/linux-debug/network/framebeam_player_cli}"
export CGO_ENABLED=0 GOTOOLCHAIN=local
PW="e2e-dummy-password"
TMP="$(mktemp -d)"
HUB_PID="" CLI_PID=""
cleanup() {
  [ -n "$CLI_PID" ] && kill "$CLI_PID" 2>/dev/null
  [ -n "$HUB_PID" ] && kill "$HUB_PID" 2>/dev/null
  wait 2>/dev/null
  rm -rf "$TMP"
}
trap cleanup EXIT

die() { # die <label> [logfile...]
  echo "FAIL $1"
  shift
  for f in "$@"; do [ -f "$f" ] && tail -n 15 "$f"; done
  exit 1
}
ok() { echo "ok  $1"; }
# wait_for <seconds> <command...>: waits until the command returns 0.
wait_for() { local n=$(( $1 * 10 )); shift; while [ "$n" -gt 0 ]; do "$@" >/dev/null 2>&1 && return 0; sleep 0.1; n=$((n-1)); done; return 1; }

[ -x "$CLI" ] || die "CLI not found: $CLI"

# 1. Build the Hub
(cd "$ROOT/server" && go build -o "$TMP/framebeam-hub" ./cmd/framebeam-hub) >"$TMP/build.log" 2>&1 || die "hub build" "$TMP/build.log"
ok "hub build"

# 1b. Core import (ADR 0020): dummy library (random bytes, no real core) zipped like the libretro buildbot does,
#     plus an info.zip with a minimal .info (systemid nds) and an .index-extended. Imported offline before the Hub
#     starts (no signing); the Hub's buildbot URL is unreachable on purpose.
case "$(uname -m)" in aarch64|arm64) CORE_PLATFORM=linux-arm64 ;; *) CORE_PLATFORM=linux-x64 ;; esac
CDIR="$TMP/cores-in"
mkdir -p "$CDIR/$CORE_PLATFORM"
head -c 8192 /dev/urandom >"$TMP/melondsds_libretro.so"
CORE_SHA="$(sha256sum "$TMP/melondsds_libretro.so" | cut -d' ' -f1)"
printf 'display_name = "melonDS DS (E2E dummy)"\ncorename = "melonDS DS"\nsystemid = "nds"\nlicense = "GPLv3"\n' >"$TMP/melondsds_libretro.info"
python3 -I - "$TMP" "$CDIR" "$CORE_PLATFORM" <<'PY' || die "core: building the import directory"
import sys, zipfile, zlib
tmp, cdir, plat = sys.argv[1:4]
with zipfile.ZipFile(f"{cdir}/info.zip", "w") as z:
    z.write(f"{tmp}/melondsds_libretro.info", "melondsds_libretro.info")
name = "melondsds_libretro.so.zip"
with zipfile.ZipFile(f"{cdir}/{plat}/{name}", "w") as z:
    z.write(f"{tmp}/melondsds_libretro.so", "melondsds_libretro.so")
crc = zlib.crc32(open(f"{tmp}/melondsds_libretro.so", "rb").read()) & 0xFFFFFFFF
open(f"{cdir}/{plat}/.index-extended", "w").write(f"2026-10-09 {crc:08x} {name}\n")
PY
ok "core: import directory built (melondsds $CORE_PLATFORM)"

# 2. Create admin, start Hub (self-signed TLS, free loopback port)
export FRAMEBEAM_DATA_DIR="$TMP/hub"
printf '%s\n' "$PW" | "$TMP/framebeam-hub" setup-admin -username admin >"$TMP/setup.log" 2>&1 || die "setup-admin" "$TMP/setup.log"
"$TMP/framebeam-hub" import-cores "$CDIR" >"$TMP/import.log" 2>&1 || die "core: import-cores" "$TMP/import.log"
ok "core: import-cores"
PORT="$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')"
ADDR="127.0.0.1:$PORT"
"$TMP/framebeam-hub" -listen "$ADDR" -core-buildbot-url "https://127.0.0.1:1/nightly" >"$TMP/hub.log" 2>&1 &
HUB_PID=$!
wait_for 15 curl -fsk "https://$ADDR/.well-known/framebeam" || die "hub start" "$TMP/hub.log"
ok "hub start $ADDR"

# 3. Web interface: login (CSRF), upload a dummy ROM
JAR="$TMP/jar"
CURL=(curl -sk -b "$JAR" -c "$JAR")
csrf=$("${CURL[@]}" "https://$ADDR/login" | sed -n 's/.*name="_csrf" value="\([^"]*\)".*/\1/p' | head -n1)
[ -n "$csrf" ] || die "web login (CSRF token missing)" "$TMP/hub.log"
code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' --data-urlencode "_csrf=$csrf" --data-urlencode "username=admin" \
  --data-urlencode "password=$PW" "https://$ADDR/login")
[ "$code" = "303" ] || die "web login (HTTP $code)" "$TMP/hub.log"
page=$("${CURL[@]}" "https://$ADDR/library")
tok=$(printf '%s' "$page" | sed -n 's/.*"X-CSRF-Token": "\([^"]*\)".*/\1/p' | head -n1)
[ -n "$tok" ] || die "web login (session CSRF missing)" "$TMP/hub.log"
ok "web login"

head -c 4096 /dev/urandom >"$TMP/e2e-dummy.nds"
SHA="$(sha256sum "$TMP/e2e-dummy.nds" | cut -d' ' -f1)"
code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' -F "_csrf=$tok" -F "title=E2E Dummy" -F "file=@$TMP/e2e-dummy.nds" "https://$ADDR/library/upload")
[ "$code" = "303" ] || die "web upload (HTTP $code)" "$TMP/hub.log"
ok "web upload dummy ROM sha256=${SHA:0:12}..."

# 4. Player CLI: pair + games + fetch-rom + wait-revoked, in the background
PDIR="$TMP/player"
QT_QPA_PLATFORM=offscreen "$CLI" pair "https://$ADDR" --accept-fingerprint --data-dir "$PDIR" games fetch-rom "$SHA" wait-revoked \
  >"$TMP/cli.out" 2>"$TMP/cli.err" &
CLI_PID=$!
wait_for 20 grep -q "AwaitingApproval" "$TMP/cli.err" || die "cli pair (no pending request)" "$TMP/cli.out" "$TMP/cli.err"
ok "cli: first contact confirmed, pairing request submitted"

# 5. Allow in the web interface (assign to admin)
clients=$("${CURL[@]}" "https://$ADDR/clients")
rid=$(printf '%s' "$clients" | sed -n 's#.*/clients/requests/\([^/"]*\)/allow.*#\1#p' | head -n1)
uid=$(printf '%s' "$clients" | sed -n 's#.*<option value="\([^"]*\)".*#\1#p' | head -n1)
[ -n "$rid" ] && [ -n "$uid" ] || die "web: pending request not found" "$TMP/hub.log"
body=$("${CURL[@]}" -H "X-CSRF-Token: $tok" -H "HX-Request: true" --data-urlencode "user_id=$uid" "https://$ADDR/clients/requests/$rid/allow")
printf '%s' "$body" | grep -q "Device allowed" || die "web allow" "$TMP/hub.log"
ok "web: pending request allowed"

# 6. Check CLI result
wait_for 40 grep -q "READY-FOR-REVOKE" "$TMP/cli.out" || die "cli: flow did not complete" "$TMP/cli.out" "$TMP/cli.err"
grep -q "^\[Connected\]" "$TMP/cli.err" && ok "cli: Connected" || die "cli: not Connected" "$TMP/cli.err"
grep -q "E2E Dummy.*$SHA" "$TMP/cli.out" && ok "cli: game in the library" || die "cli: game missing" "$TMP/cli.out"
ROM="$PDIR/cache/roms/$SHA.nds"
[ -f "$ROM" ] && [ "$(sha256sum "$ROM" | cut -d' ' -f1)" = "$SHA" ] && ok "ROM in cache, SHA-256 correct" || die "ROM in cache" "$TMP/cli.out" "$TMP/cli.err"
hubfp=$(sed -n 's/.*sha256_fingerprint=\([0-9A-F:]*\).*/\1/p' "$TMP/hub.log" | head -n1)
[ -n "$hubfp" ] && grep -q "$hubfp" "$TMP/cli.out" && ok "fingerprint matches Hub log" || die "fingerprint comparison" "$TMP/cli.out" "$TMP/hub.log"
! grep -rq "fbd_\|fba_\|fbp_" "$PDIR" && ok "no tokens in Player files" || die "token in Player file"

# 7. Revoke in the web interface -> CLI (same run) falls back to NeedsPairing
clients=$("${CURL[@]}" "https://$ADDR/clients")
did=$(printf '%s' "$clients" | sed -n 's#.*/clients/devices/\([^/"]*\)/revoke.*#\1#p' | head -n1)
[ -n "$did" ] || die "web: device not found" "$TMP/hub.log"
body=$("${CURL[@]}" -H "X-CSRF-Token: $tok" -H "HX-Request: true" -X POST "https://$ADDR/clients/devices/$did/revoke")
printf '%s' "$body" | grep -q "Access revoked" || die "web revoke" "$TMP/hub.log"
ok "web: access revoked"
wait_for 20 grep -q "Revoked:" "$TMP/cli.out" || die "cli: no NeedsPairing after revoke" "$TMP/cli.out" "$TMP/cli.err"
wait "$CLI_PID"; rc=$?; CLI_PID=""
[ "$rc" = "0" ] && grep -q "device_revoked" "$TMP/cli.out" && ok "cli: NeedsPairing after revoke (device_revoked)" || die "cli after revoke (exit $rc)" "$TMP/cli.out" "$TMP/cli.err"

# 8. A second run with the same data directory must not become Connected.
# Linux: credential store is memory-only -> no credential left; the criterion here is "not Connected, exit != 0".
QT_QPA_PLATFORM=offscreen "$CLI" games --data-dir "$PDIR" >"$TMP/cli2.out" 2>"$TMP/cli2.err"; rc=$?
if [ "$rc" != "0" ] && ! grep -q "^\[Connected\]" "$TMP/cli2.err"; then
  ok "second run: not Connected (exit $rc; Linux without persistent credential store)"
else
  die "second run became Connected" "$TMP/cli2.out" "$TMP/cli2.err"
fi

# 9. Save round trip (D3): push (base 0) -> pull -> stale push from a second device -> CONFLICT (exit 3)
#    -> resolve use_local -> pull shows the local bytes -> resolve again -> STALE (exit 4). Dummy bytes only.
GID=$(sed -n "s/^E2E Dummy.*$SHA\t\(.*\)$/\1/p" "$TMP/cli.out" | head -n1)
[ -n "$GID" ] || die "save: game id not in the games output" "$TMP/cli.out"

# pair_run <name> <out-file> <cli args after pair...>: pairs a new device (own data dir), approves it in the web
# interface and runs the follow-up commands in the same process (credentials are memory-only on Linux).
pair_run() {
  local name="$1" out="$2"; shift 2
  QT_QPA_PLATFORM=offscreen "$CLI" pair "https://$ADDR" --accept-fingerprint --data-dir "$TMP/dev-$name" "$@" \
    >"$out" 2>"$out.err" &
  local pid=$!
  CLI_PID=$pid
  wait_for 20 grep -q "AwaitingApproval" "$out.err" || { die "save: device $name did not request pairing" "$out" "$out.err"; }
  local cl rid u b
  cl=$("${CURL[@]}" "https://$ADDR/clients")
  rid=$(printf '%s' "$cl" | sed -n 's#.*/clients/requests/\([^/"]*\)/allow.*#\1#p' | head -n1)
  # The Hub preselects the request's user (admin for plain pairing); fall back to the first option.
  u=$(printf '%s' "$cl" | sed -n 's#.*<option value="\([^"]*\)" selected.*#\1#p' | head -n1)
  [ -n "$u" ] || u=$(printf '%s' "$cl" | sed -n 's#.*<option value="\([^"]*\)".*#\1#p' | head -n1)
  [ -n "$rid" ] && [ -n "$u" ] || die "save: pending request of $name not found" "$TMP/hub.log"
  b=$("${CURL[@]}" -H "X-CSRF-Token: $tok" -H "HX-Request: true" --data-urlencode "user_id=$u" "https://$ADDR/clients/requests/$rid/allow")
  printf '%s' "$b" | grep -q "Device allowed" || die "save: allow $name" "$TMP/hub.log"
  wait "$pid"; PAIR_RC=$?
  CLI_PID=""
}

printf 'e2e-save-A-dummy-bytes' >"$TMP/saveA.bin"
printf 'e2e-save-B-dummy-bytes-longer' >"$TMP/saveB.bin"
pair_run devA "$TMP/save1.out" games save push "$GID" "$TMP/saveA.bin" --base 0 save pull "$GID" "$TMP/pullA.bin"
[ "$PAIR_RC" = "0" ] && grep -q "^OK revision=1 " "$TMP/save1.out" || die "save: push base 0 (exit $PAIR_RC)" "$TMP/save1.out" "$TMP/save1.out.err"
cmp -s "$TMP/saveA.bin" "$TMP/pullA.bin" && ok "save: push (base 0) -> revision 1, pull returns identical bytes" || die "save: pulled bytes differ" "$TMP/save1.out"

pair_run devB "$TMP/save2.out" games save push "$GID" "$TMP/saveB.bin" --base 0
CID=$(sed -n 's/^CONFLICT id=\([^ ]*\) .*/\1/p' "$TMP/save2.out" | head -n1)
[ "$PAIR_RC" = "3" ] && [ -n "$CID" ] && ok "save: stale push from a second device -> exit 3, CONFLICT id=$CID" || die "save: stale push (exit $PAIR_RC)" "$TMP/save2.out" "$TMP/save2.out.err"

pair_run devC "$TMP/save3.out" games save pull "$GID" "$TMP/pullHub.bin" \
  save resolve "$GID" "$CID" use_local --expected 1 save pull "$GID" "$TMP/pullLocal.bin" \
  save resolve "$GID" "$CID" use_local --expected 2
[ "$PAIR_RC" = "4" ] && grep -q "^STALE" "$TMP/save3.out" || die "save: second resolve should be STALE/exit 4 (exit $PAIR_RC)" "$TMP/save3.out" "$TMP/save3.out.err"
cmp -s "$TMP/saveA.bin" "$TMP/pullHub.bin" && ok "save: Hub bytes unchanged by the conflicting upload" || die "save: hub bytes changed by conflict"
grep -q "^OK revision=2 " "$TMP/save3.out" && cmp -s "$TMP/saveB.bin" "$TMP/pullLocal.bin" \
  && ok "save: resolve use_local (expected 1) -> revision 2, pull shows the local bytes" || die "save: resolve use_local" "$TMP/save3.out" "$TMP/save3.out.err"
ok "save: second resolve of the same conflict -> STALE (exit 4)"

# 10. Phase 5: onboarding invite (redeem), ROM upload (CLI), firmware state "required but missing". Dummy data only.
mkinvite() { # mkinvite -> prints the invite code (shown once in the Users page response)
  local b
  b=$("${CURL[@]}" -H "X-CSRF-Token: $tok" -H "HX-Request: true" --data-urlencode "expiry=1h" --data-urlencode "authorize=1" "https://$ADDR/users/invites")
  printf '%s' "$b" | grep -o 'FB-[0-9A-Z]\{4\}-[0-9A-Z]\{4\}' | head -n1
}
INV="$(mkinvite)"
[ -n "$INV" ] || die "invite: no code in the Users page response" "$TMP/hub.log"
head -c 2048 /dev/urandom >"$TMP/up-user.nds"
QT_QPA_PLATFORM=offscreen "$CLI" redeem-invite "https://$ADDR" --accept-fingerprint --data-dir "$TMP/dev-inv" \
  --code "$INV" --name "E2E Anna" games game upload "$TMP/up-user.nds" >"$TMP/inv1.out" 2>"$TMP/inv1.err"; rc=$?
grep -q "^\[Connected\]" "$TMP/inv1.err" && ! grep -q "AwaitingApproval" "$TMP/inv1.err" \
  && ok "invite: redeemed, device authorized directly (no approval step)" || die "invite: redeem (exit $rc)" "$TMP/inv1.out" "$TMP/inv1.err"
[ "$rc" = "1" ] && grep -q "uploads_disabled" "$TMP/inv1.err" \
  && ok "upload: a plain user without upload permission gets uploads_disabled (exit 1)" || die "upload: user without permission (exit $rc)" "$TMP/inv1.out" "$TMP/inv1.err"
QT_QPA_PLATFORM=offscreen "$CLI" redeem-invite "https://$ADDR" --accept-fingerprint --data-dir "$TMP/dev-inv2" \
  --code "$INV" --name "E2E Bert" >"$TMP/inv2.out" 2>"$TMP/inv2.err"; rc=$?
[ "$rc" = "1" ] && grep -q "invite_invalid" "$TMP/inv2.err" && ok "invite: reused code -> invite_invalid (exit 1)" \
  || die "invite: reused code (exit $rc)" "$TMP/inv2.out" "$TMP/inv2.err"
INV2="$(mkinvite)"
QT_QPA_PLATFORM=offscreen "$CLI" redeem-invite "https://$ADDR" --accept-fingerprint --data-dir "$TMP/dev-inv3" \
  --code "$INV2" --name "e2e anna" >"$TMP/inv3.out" 2>"$TMP/inv3.err"; rc=$?
[ "$rc" = "1" ] && grep -q "display_name_taken" "$TMP/inv3.err" && ok "invite: display name taken (case-insensitive) -> display_name_taken" \
  || die "invite: taken name (exit $rc)" "$TMP/inv3.out" "$TMP/inv3.err"

head -c 4096 /dev/urandom >"$TMP/cli-up.nds"
UPSHA="$(sha256sum "$TMP/cli-up.nds" | cut -d' ' -f1)"
# Firmware mode native for nds: no firmware files were provided, so the required files are missing on the Hub.
body=$("${CURL[@]}" -o /dev/null -w '%{http_code}' -H "X-CSRF-Token: $tok" -H "HX-Request: true" --data-urlencode "mode=native" \
  "https://$ADDR/systems/nds/firmware-mode")
case "$body" in 200|303) ;; *) die "firmware: set mode native (HTTP $body)" "$TMP/hub.log";; esac
# One admin device for upload + systems (the Hub allows only 5 pairing requests per IP and minute).
pair_run devU "$TMP/up1.out" games game upload "$TMP/cli-up.nds" --title "CLI Upload" game upload "$TMP/cli-up.nds" systems fetch-core nds fetch-core nds
UPID=$(sed -n 's/^OK game_id=\([^ ]*\) sha256=.*/\1/p' "$TMP/up1.out" | head -n1)
[ "$PAIR_RC" = "0" ] && [ -n "$UPID" ] && grep -q "^OK game_id=$UPID sha256=$UPSHA" "$TMP/up1.out" \
  && ok "upload: admin CLI upload -> game created (streamed, sha256 matches)" || die "upload: CLI upload (exit $PAIR_RC)" "$TMP/up1.out" "$TMP/up1.out.err"
grep -q "^DUPLICATE existing_game_id=$UPID" "$TMP/up1.out" && ok "upload: same ROM again -> DUPLICATE with the existing game id" \
  || die "upload: duplicate" "$TMP/up1.out" "$TMP/up1.out.err"
grep -Pq '^nds\tmode=native\t' "$TMP/up1.out" && grep -Pq '^  bios7\trequired=true\tpresent=false' "$TMP/up1.out" \
  && ok "firmware: mode native, required files missing on the Hub (Player: Firmware required/missing, launch blocked)" \
  || die "firmware: systems output" "$TMP/up1.out" "$TMP/up1.out.err"

# 11. Cores from the Hub (0.2): the devU run above also ran fetch-core nds twice (the Hub limits pairing requests
#     per IP, so no extra device). First call downloads the imported dummy core, the second is a cache hit.
mapfile -t CPATHS < <(sed -n 's/^core_path=//p' "$TMP/up1.out")
mapfile -t CSRC < <(sed -n 's/^core_source=//p' "$TMP/up1.out")
[ "${#CPATHS[@]}" = "2" ] && [ "${CPATHS[0]}" = "${CPATHS[1]}" ] && [ -f "${CPATHS[0]}" ] \
  || die "core: expected two identical core_path lines" "$TMP/up1.out" "$TMP/up1.out.err"
[ "$(sha256sum "${CPATHS[0]}" | cut -d' ' -f1)" = "$CORE_SHA" ] \
  && ok "core: fetch-core nds -> library in the cache, SHA-256 matches the imported library" || die "core: sha256 of the fetched library" "$TMP/up1.out"
[ "${CSRC[0]:-}" = "download" ] && [ "${CSRC[1]:-}" = "cache" ] \
  && ok "core: first fetch-core downloads, second is a cache hit (core_source=download, then cache)" || die "core: core_source sequence: ${CSRC[*]:-none}" "$TMP/up1.out"
case "${CPATHS[0]}" in "$TMP/dev-devU"/*) ok "core: library below the Player data dir" ;; *) die "core: path outside the data dir: ${CPATHS[0]}" ;; esac
CVER="$(sed -n 's/^core_version=//p' "$TMP/up1.out" | head -n1)"
[ -n "$CVER" ] && ok "core: fetch-core reports the Hub build id as version ($CVER)" || die "core: core_version missing" "$TMP/up1.out"
