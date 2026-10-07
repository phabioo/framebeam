#!/usr/bin/env bash
# E2E (phase 4, 0.4): two framebeam_player_cli devices against a real FrameBeam Hub (built locally, self-signed TLS,
# loopback): one shares a Session with synthetic frames and a tone, the other watches it over WebRTC.
# Round 1: direct (host candidates). Round 2 (ADR 0012 D5): Hub with the embedded TURN server on loopback, both
# Players with --force-relay; the viewer has to report `relay (udp)`. Round 2 is skipped when the Hub binary has no
# -turn flag. Quiet: only "ok  <step>" / "FAIL <step>" + log tail. Dummy data, no real ROMs.
# Usage: scripts/e2e-session.sh [path/to/framebeam_player_cli]
#   Default: client/build/linux-debug/network/framebeam_player_cli
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI="${1:-$ROOT/client/build/linux-debug/network/framebeam_player_cli}"
export CGO_ENABLED=0 GOTOOLCHAIN=local
PW="e2e-dummy-password"
TMP="$(mktemp -d)"
HUB_PID="" SHARE_PID="" WATCH_PID=""
PREFIX=""
cleanup() {
  [ -n "$SHARE_PID" ] && kill "$SHARE_PID" 2>/dev/null
  [ -n "$WATCH_PID" ] && kill "$WATCH_PID" 2>/dev/null
  [ -n "$HUB_PID" ] && kill "$HUB_PID" 2>/dev/null
  wait 2>/dev/null
  rm -rf "$TMP"
}
trap cleanup EXIT

die() { # die <label> [logfile...]
  echo "FAIL ${PREFIX}$1"
  shift
  for f in "$@"; do [ -f "$f" ] && tail -n 15 "$f"; done
  exit 1
}
ok() { echo "ok  ${PREFIX}$1"; }
wait_for() { local n=$(( $1 * 10 )); shift; while [ "$n" -gt 0 ]; do "$@" >/dev/null 2>&1 && return 0; sleep 0.1; n=$((n-1)); done; return 1; }

# free_port: a port that is free for TCP and UDP on loopback.
free_port() {
  python3 - <<'PY'
import socket
while True:
    t = socket.socket(); t.bind(("127.0.0.1", 0)); p = t.getsockname()[1]
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        u.bind(("127.0.0.1", p)); print(p); break
    except OSError:
        pass
    finally:
        t.close(); u.close()
PY
}
# free_udp_range <count>: "first-last" of <count> consecutive UDP ports that are free on loopback.
free_udp_range() {
  python3 - "$1" <<'PY'
import random, socket, sys
n = int(sys.argv[1])
while True:
    base = random.randint(30000, 60000)
    socks = []
    try:
        for p in range(base, base + n):
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", p)); socks.append(s)
        print(f"{base}-{base + n - 1}"); break
    except OSError:
        pass
    finally:
        for s in socks: s.close()
PY
}

[ -x "$CLI" ] || die "CLI not found: $CLI"

(cd "$ROOT/server" && go build -o "$TMP/framebeam-hub" ./cmd/framebeam-hub) >"$TMP/build.log" 2>&1 || die "hub build" "$TMP/build.log"
JAR="" tok="" ADDR=""
CURL=()

# start_device <label> <name> <out> <cli args after pair...>: pairs a new device (own data dir, credentials are
# memory-only on Linux, so the Session command runs as a follow-up in the same process), approves it in the web
# interface.
start_device() {
  local label="$1" name="$2" out="$3"; shift 3
  QT_QPA_PLATFORM=offscreen "$CLI" pair "https://$ADDR" --accept-fingerprint --data-dir "$TMP/dev-$label-$name" "$@" \
    >"$out" 2>"$out.err" &
  DEV_PID=$!
  wait_for 20 grep -q "AwaitingApproval" "$out.err" || die "device $name did not request pairing" "$out" "$out.err"
  local cl rid u b
  cl=$("${CURL[@]}" "https://$ADDR/clients")
  rid=$(printf '%s' "$cl" | sed -n 's#.*/clients/requests/\([^/"]*\)/allow.*#\1#p' | head -n1)
  u=$(printf '%s' "$cl" | sed -n 's#.*<option value="\([^"]*\)".*#\1#p' | head -n1)
  [ -n "$rid" ] && [ -n "$u" ] || die "pending request of $name not found" "$TMP/$label.hub.log"
  b=$("${CURL[@]}" -H "X-CSRF-Token: $tok" -H "HX-Request: true" --data-urlencode "user_id=$u" "https://$ADDR/clients/requests/$rid/allow")
  printf '%s' "$b" | grep -q "Device allowed" || die "allow $name" "$TMP/$label.hub.log"
}

# run_round <label> <relay: 0|1> [extra Hub flags...]: Hub start, admin, dummy game, share + watch, assertions.
run_round() {
  local label="$1" relay="$2"; shift 2
  local hublog="$TMP/$label.hub.log" share="$TMP/$label.share.out" watch="$TMP/$label.watch.out"
  local cliopts=()
  [ "$relay" = "1" ] && cliopts=(--force-relay)

  # 1. Hub: admin, start
  export FRAMEBEAM_DATA_DIR="$TMP/hub-$label"
  printf '%s\n' "$PW" | "$TMP/framebeam-hub" setup-admin -username admin >"$TMP/$label.setup.log" 2>&1 || die "setup-admin" "$TMP/$label.setup.log"
  local port
  port="$(free_port)"
  ADDR="127.0.0.1:$port"
  "$TMP/framebeam-hub" -listen "$ADDR" "$@" >"$hublog" 2>&1 &
  HUB_PID=$!
  wait_for 15 curl -fsk "https://$ADDR/.well-known/framebeam" || die "hub start" "$hublog"
  ok "hub build + start $ADDR"

  # 2. Web interface: login (CSRF), upload a dummy game
  JAR="$TMP/jar-$label"
  CURL=(curl -sk -b "$JAR" -c "$JAR")
  local csrf code page
  csrf=$("${CURL[@]}" "https://$ADDR/login" | sed -n 's/.*name="_csrf" value="\([^"]*\)".*/\1/p' | head -n1)
  [ -n "$csrf" ] || die "web login (CSRF token missing)" "$hublog"
  code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' --data-urlencode "_csrf=$csrf" --data-urlencode "username=admin" \
    --data-urlencode "password=$PW" "https://$ADDR/login")
  [ "$code" = "303" ] || die "web login (HTTP $code)" "$hublog"
  page=$("${CURL[@]}" "https://$ADDR/library")
  tok=$(printf '%s' "$page" | sed -n 's/.*"X-CSRF-Token": "\([^"]*\)".*/\1/p' | head -n1)
  [ -n "$tok" ] || die "web login (session CSRF missing)" "$hublog"
  head -c 4096 /dev/urandom >"$TMP/e2e-dummy.nds"
  code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' -F "_csrf=$tok" -F "title=E2E Dummy" -F "file=@$TMP/e2e-dummy.nds" "https://$ADDR/library/upload")
  [ "$code" = "303" ] || die "web upload (HTTP $code)" "$hublog"
  ok "web login + dummy game uploaded"

  # 3. Device A shares (synthetic frames + 440 Hz tone) for 40 s
  start_device "$label" share "$share" session-share --synthetic --visibility hub_users --seconds 40 ${cliopts[@]+"${cliopts[@]}"}
  SHARE_PID=$DEV_PID
  wait_for 30 grep -q "^SHARING session=" "$share" || die "share: Session not published" "$share" "$share.err" "$hublog"
  ok "device A published a Session ($(sed -n 's/^SHARING session=\([^ ]*\).*/\1/p' "$share" | head -n1 | cut -c1-8)...)"

  # 4. Device B watches the first Session for 8 s; exit 0 = >= 30 decoded frames and audio arrived
  start_device "$label" watch "$watch" session-watch --first --seconds 8 ${cliopts[@]+"${cliopts[@]}"}
  WATCH_PID=$DEV_PID
  wait "$WATCH_PID"; local rc=$?
  WATCH_PID=""
  [ "$rc" = "0" ] && grep -q "^WATCH-OK " "$watch" || die "watch (exit $rc)" "$watch" "$watch.err" "$share" "$share.err" "$hublog"
  ok "device B watched: $(grep '^WATCH-OK ' "$watch" | head -n1)"
  grep -q "^VIEWER-CONNECTED " "$share" && ok "device A: viewer connected over WebRTC" || die "share: no viewer connected" "$share"
  grep -q "^STATS host viewers=1 encoder=[a-z0-9_]* fps=[1-9]" "$share" && ok "device A: encoder running with a viewer ($(grep -m1 '^STATS host viewers=1 encoder=[a-z0-9_]* fps=[1-9]' "$share" | sed 's/^STATS host //'))" \
    || die "share: no stats with a viewer" "$share"
  grep -q "^STATS host viewers=1 .* target_kbps=[1-9][0-9]*" "$share" && ok "device A: target bitrate reported" \
    || die "share: no target bitrate in the stats" "$share"

  # Connection type of the selected candidate pair (viewer: `CONNECTION <type>` at the end of the watch)
  if [ "$relay" = "1" ]; then
    grep -qx "CONNECTION relay (udp)" "$watch" && ok "device B: connection type relay (udp)" \
      || die "watch: connection type is not relay (udp): $(grep '^CONNECTION ' "$watch")" "$watch" "$watch.err" "$hublog"
  else
    grep -q "^CONNECTION direct (" "$watch" && ok "device B: connection type $(sed -n 's/^CONNECTION //p' "$watch" | head -n1)" \
      || die "watch: connection type is not direct: $(grep '^CONNECTION ' "$watch")" "$watch"
  fi

  # 5. After the viewer left, the encoder stops (viewers=0, no encoder)
  wait_for 15 bash -c "grep '^STATS host' '$share' | tail -n 1 | grep -q '^STATS host viewers=0 encoder=- '" && ok "device A: encoder stopped after the viewer left" \
    || die "share: encoder still running without viewers" "$share"

  # 6. Sharer ends the Session at the end of its run
  wait "$SHARE_PID"; rc=$?
  SHARE_PID=""
  [ "$rc" = "0" ] && grep -q "^SHARE-DONE ended" "$share" && ok "device A ended the Session (exit 0)" || die "share end (exit $rc)" "$share" "$share.err"
  if [ "$relay" = "1" ]; then
    grep -qx "CONNECTION relay (udp)" "$share" && ok "device A: connection type relay (udp)" \
      || die "share: connection type is not relay (udp): $(grep '^CONNECTION ' "$share")" "$share"
  fi

  kill "$HUB_PID" 2>/dev/null
  wait "$HUB_PID" 2>/dev/null
  HUB_PID=""
}

run_round direct 0

# Round 2: embedded TURN server on loopback (flags of the Hub, ADR 0012 D2), everything forced through the relay.
HUB_HELP="$("$TMP/framebeam-hub" -h 2>&1 || true)"
if grep -q -- -turn <<<"$HUB_HELP"; then
  PREFIX="[relay] "
  TURN_PORT="$(free_port)"
  RELAY_RANGE="$(free_udp_range 16)"
  run_round relay 1 -turn -public-host 127.0.0.1 -turn-port "$TURN_PORT" -turn-relay-ports "$RELAY_RANGE" -turn-relay-ip 127.0.0.1
  PREFIX=""
else
  echo "skip TURN round: this Hub binary has no -turn flag"
fi

! grep -rq "fbd_\|fba_\|fbp_" "$TMP"/dev-* && ok "no tokens in Player files" || die "token in Player file"
