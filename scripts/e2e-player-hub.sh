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

# 2. Create admin, start Hub (self-signed TLS, free loopback port)
export FRAMEBEAM_DATA_DIR="$TMP/hub"
printf '%s\n' "$PW" | "$TMP/framebeam-hub" setup-admin -username admin >"$TMP/setup.log" 2>&1 || die "setup-admin" "$TMP/setup.log"
PORT="$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')"
ADDR="127.0.0.1:$PORT"
"$TMP/framebeam-hub" -listen "$ADDR" >"$TMP/hub.log" 2>&1 &
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
  u=$(printf '%s' "$cl" | sed -n 's#.*<option value="\([^"]*\)".*#\1#p' | head -n1)
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
