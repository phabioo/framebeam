#!/usr/bin/env bash
# E2E (phase 4): two framebeam_player_cli devices against a real FrameBeam Hub (built locally, self-signed TLS,
# loopback): one shares a Session with synthetic frames and a tone, the other watches it over WebRTC.
# Quiet: only "ok  <step>" / "FAIL <step>" + log tail. Dummy data, no real ROMs.
# Usage: scripts/e2e-session.sh [path/to/framebeam_player_cli]
#   Default: client/build/linux-debug/network/framebeam_player_cli
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI="${1:-$ROOT/client/build/linux-debug/network/framebeam_player_cli}"
export CGO_ENABLED=0 GOTOOLCHAIN=local
PW="e2e-dummy-password"
TMP="$(mktemp -d)"
HUB_PID="" SHARE_PID="" WATCH_PID=""
cleanup() {
  [ -n "$SHARE_PID" ] && kill "$SHARE_PID" 2>/dev/null
  [ -n "$WATCH_PID" ] && kill "$WATCH_PID" 2>/dev/null
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
wait_for() { local n=$(( $1 * 10 )); shift; while [ "$n" -gt 0 ]; do "$@" >/dev/null 2>&1 && return 0; sleep 0.1; n=$((n-1)); done; return 1; }

[ -x "$CLI" ] || die "CLI not found: $CLI"

# 1. Hub: build, admin, start
(cd "$ROOT/server" && go build -o "$TMP/framebeam-hub" ./cmd/framebeam-hub) >"$TMP/build.log" 2>&1 || die "hub build" "$TMP/build.log"
export FRAMEBEAM_DATA_DIR="$TMP/hub"
printf '%s\n' "$PW" | "$TMP/framebeam-hub" setup-admin -username admin >"$TMP/setup.log" 2>&1 || die "setup-admin" "$TMP/setup.log"
PORT="$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')"
ADDR="127.0.0.1:$PORT"
"$TMP/framebeam-hub" -listen "$ADDR" >"$TMP/hub.log" 2>&1 &
HUB_PID=$!
wait_for 15 curl -fsk "https://$ADDR/.well-known/framebeam" || die "hub start" "$TMP/hub.log"
ok "hub build + start $ADDR"

# 2. Web interface: login (CSRF), upload a dummy game
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
head -c 4096 /dev/urandom >"$TMP/e2e-dummy.nds"
code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' -F "_csrf=$tok" -F "title=E2E Dummy" -F "file=@$TMP/e2e-dummy.nds" "https://$ADDR/library/upload")
[ "$code" = "303" ] || die "web upload (HTTP $code)" "$TMP/hub.log"
ok "web login + dummy game uploaded"

# start_device <name> <out> <cli args after pair...>: pairs a new device (own data dir, credentials are memory-only
# on Linux, so the Session command runs as a follow-up in the same process), approves it in the web interface.
start_device() {
  local name="$1" out="$2"; shift 2
  QT_QPA_PLATFORM=offscreen "$CLI" pair "https://$ADDR" --accept-fingerprint --data-dir "$TMP/dev-$name" "$@" \
    >"$out" 2>"$out.err" &
  DEV_PID=$!
  wait_for 20 grep -q "AwaitingApproval" "$out.err" || die "device $name did not request pairing" "$out" "$out.err"
  local cl rid u b
  cl=$("${CURL[@]}" "https://$ADDR/clients")
  rid=$(printf '%s' "$cl" | sed -n 's#.*/clients/requests/\([^/"]*\)/allow.*#\1#p' | head -n1)
  u=$(printf '%s' "$cl" | sed -n 's#.*<option value="\([^"]*\)".*#\1#p' | head -n1)
  [ -n "$rid" ] && [ -n "$u" ] || die "pending request of $name not found" "$TMP/hub.log"
  b=$("${CURL[@]}" -H "X-CSRF-Token: $tok" -H "HX-Request: true" --data-urlencode "user_id=$u" "https://$ADDR/clients/requests/$rid/allow")
  printf '%s' "$b" | grep -q "Device allowed" || die "allow $name" "$TMP/hub.log"
}

# 3. Device A shares (synthetic frames + 440 Hz tone) for 40 s
start_device share "$TMP/share.out" session-share --synthetic --visibility hub_users --seconds 40
SHARE_PID=$DEV_PID
wait_for 30 grep -q "^SHARING session=" "$TMP/share.out" || die "share: Session not published" "$TMP/share.out" "$TMP/share.out.err" "$TMP/hub.log"
ok "device A published a Session ($(sed -n 's/^SHARING session=\([^ ]*\).*/\1/p' "$TMP/share.out" | head -n1 | cut -c1-8)...)"

# 4. Device B watches the first Session for 8 s; exit 0 = >= 30 decoded frames and audio arrived
start_device watch "$TMP/watch.out" session-watch --first --seconds 8
WATCH_PID=$DEV_PID
wait "$WATCH_PID"; rc=$?
WATCH_PID=""
[ "$rc" = "0" ] && grep -q "^WATCH-OK " "$TMP/watch.out" || die "watch (exit $rc)" "$TMP/watch.out" "$TMP/watch.out.err" "$TMP/share.out" "$TMP/share.out.err"
ok "device B watched: $(grep '^WATCH-OK ' "$TMP/watch.out" | head -n1)"
grep -q "^VIEWER-CONNECTED " "$TMP/share.out" && ok "device A: viewer connected over WebRTC" || die "share: no viewer connected" "$TMP/share.out"
grep -q "^STATS host viewers=1 encoder=[a-z0-9_]* fps=[1-9]" "$TMP/share.out" && ok "device A: encoder running with a viewer ($(grep -m1 '^STATS host viewers=1 encoder=[a-z0-9_]* fps=[1-9]' "$TMP/share.out" | sed 's/^STATS host //'))" \
  || die "share: no stats with a viewer" "$TMP/share.out"

# 5. After the viewer left, the encoder stops (viewers=0, no encoder)
wait_for 15 bash -c "tail -n 1 '$TMP/share.out' | grep -q '^STATS host viewers=0 encoder=- '" && ok "device A: encoder stopped after the viewer left" \
  || die "share: encoder still running without viewers" "$TMP/share.out"

# 6. Sharer ends the Session at the end of its run
wait "$SHARE_PID"; rc=$?
SHARE_PID=""
[ "$rc" = "0" ] && grep -q "^SHARE-DONE ended" "$TMP/share.out" && ok "device A ended the Session (exit 0)" || die "share end (exit $rc)" "$TMP/share.out" "$TMP/share.out.err"
! grep -rq "fbd_\|fba_\|fbp_" "$TMP"/dev-* && ok "no tokens in Player files" || die "token in Player file"
