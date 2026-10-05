#!/usr/bin/env bash
# E2E: framebeam_player_cli gegen einen echten FrameBeam Hub (lokal gebaut, TLS selbstsigniert, Loopback).
# Leise: nur "ok  <Schritt>" / "FAIL <Schritt>" + Log-Ende. Dummy-Daten, keine echten ROMs.
# Aufruf: scripts/e2e-player-hub.sh [pfad/zu/framebeam_player_cli]
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

die() { # die <Label> [Logdatei...]
  echo "FAIL $1"
  shift
  for f in "$@"; do [ -f "$f" ] && tail -n 15 "$f"; done
  exit 1
}
ok() { echo "ok  $1"; }
# wait_for <Sekunden> <Befehl...>: wartet, bis der Befehl 0 liefert.
wait_for() { local n=$(( $1 * 10 )); shift; while [ "$n" -gt 0 ]; do "$@" >/dev/null 2>&1 && return 0; sleep 0.1; n=$((n-1)); done; return 1; }

[ -x "$CLI" ] || die "CLI nicht gefunden: $CLI"

# 1. Hub bauen
(cd "$ROOT/server" && go build -o "$TMP/framebeam-hub" ./cmd/framebeam-hub) >"$TMP/build.log" 2>&1 || die "hub build" "$TMP/build.log"
ok "hub build"

# 2. Admin anlegen, Hub starten (TLS selbstsigniert, freier Loopback-Port)
export FRAMEBEAM_DATA_DIR="$TMP/hub"
printf '%s\n' "$PW" | "$TMP/framebeam-hub" setup-admin -username admin >"$TMP/setup.log" 2>&1 || die "setup-admin" "$TMP/setup.log"
PORT="$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')"
ADDR="127.0.0.1:$PORT"
"$TMP/framebeam-hub" -listen "$ADDR" >"$TMP/hub.log" 2>&1 &
HUB_PID=$!
wait_for 15 curl -fsk "https://$ADDR/.well-known/framebeam" || die "hub start" "$TMP/hub.log"
ok "hub start $ADDR"

# 3. Webinterface: Login (CSRF), Upload einer Dummy-ROM
JAR="$TMP/jar"
CURL=(curl -sk -b "$JAR" -c "$JAR")
csrf=$("${CURL[@]}" "https://$ADDR/login" | sed -n 's/.*name="_csrf" value="\([^"]*\)".*/\1/p' | head -n1)
[ -n "$csrf" ] || die "web login (CSRF-Token fehlt)" "$TMP/hub.log"
code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' --data-urlencode "_csrf=$csrf" --data-urlencode "username=admin" \
  --data-urlencode "password=$PW" "https://$ADDR/login")
[ "$code" = "303" ] || die "web login (HTTP $code)" "$TMP/hub.log"
page=$("${CURL[@]}" "https://$ADDR/library")
tok=$(printf '%s' "$page" | sed -n 's/.*"X-CSRF-Token": "\([^"]*\)".*/\1/p' | head -n1)
[ -n "$tok" ] || die "web login (Session-CSRF fehlt)" "$TMP/hub.log"
ok "web login"

head -c 4096 /dev/urandom >"$TMP/e2e-dummy.nds"
SHA="$(sha256sum "$TMP/e2e-dummy.nds" | cut -d' ' -f1)"
code=$("${CURL[@]}" -o /dev/null -w '%{http_code}' -F "_csrf=$tok" -F "title=E2E Dummy" -F "file=@$TMP/e2e-dummy.nds" "https://$ADDR/library/upload")
[ "$code" = "303" ] || die "web upload (HTTP $code)" "$TMP/hub.log"
ok "web upload dummy-ROM sha256=${SHA:0:12}..."

# 4. Player-CLI: pair + games + fetch-rom + wait-revoked, im Hintergrund
PDIR="$TMP/player"
QT_QPA_PLATFORM=offscreen "$CLI" pair "https://$ADDR" --accept-fingerprint --data-dir "$PDIR" games fetch-rom "$SHA" wait-revoked \
  >"$TMP/cli.out" 2>"$TMP/cli.err" &
CLI_PID=$!
wait_for 20 grep -q "AwaitingApproval" "$TMP/cli.err" || die "cli pair (keine Pending Request)" "$TMP/cli.out" "$TMP/cli.err"
ok "cli: Erstkontakt bestaetigt, Pairing-Anfrage gestellt"

# 5. Allow im Webinterface (Zuordnung zum Admin)
clients=$("${CURL[@]}" "https://$ADDR/clients")
rid=$(printf '%s' "$clients" | sed -n 's#.*/clients/requests/\([^/"]*\)/allow.*#\1#p' | head -n1)
uid=$(printf '%s' "$clients" | sed -n 's#.*<option value="\([^"]*\)".*#\1#p' | head -n1)
[ -n "$rid" ] && [ -n "$uid" ] || die "web: Pending Request nicht gefunden" "$TMP/hub.log"
body=$("${CURL[@]}" -H "X-CSRF-Token: $tok" -H "HX-Request: true" --data-urlencode "user_id=$uid" "https://$ADDR/clients/requests/$rid/allow")
printf '%s' "$body" | grep -q "Gerät erlaubt" || die "web allow" "$TMP/hub.log"
ok "web: Pending Request erlaubt"

# 6. CLI-Ergebnis pruefen
wait_for 40 grep -q "READY-FOR-REVOKE" "$TMP/cli.out" || die "cli: Ablauf nicht abgeschlossen" "$TMP/cli.out" "$TMP/cli.err"
grep -q "^\[Connected\]" "$TMP/cli.err" && ok "cli: Connected" || die "cli: nicht Connected" "$TMP/cli.err"
grep -q "E2E Dummy.*$SHA" "$TMP/cli.out" && ok "cli: Spiel in der Library" || die "cli: Spiel fehlt" "$TMP/cli.out"
ROM="$PDIR/cache/roms/$SHA.nds"
[ -f "$ROM" ] && [ "$(sha256sum "$ROM" | cut -d' ' -f1)" = "$SHA" ] && ok "rom im Cache, SHA-256 korrekt" || die "rom im Cache" "$TMP/cli.out" "$TMP/cli.err"
hubfp=$(sed -n 's/.*sha256_fingerprint=\([0-9A-F:]*\).*/\1/p' "$TMP/hub.log" | head -n1)
[ -n "$hubfp" ] && grep -q "$hubfp" "$TMP/cli.out" && ok "fingerprint stimmt mit Hub-Log ueberein" || die "fingerprint-Vergleich" "$TMP/cli.out" "$TMP/hub.log"
! grep -rq "fbd_\|fba_\|fbp_" "$PDIR" && ok "keine Tokens in Player-Dateien" || die "Token in Player-Datei"

# 7. Revoke im Webinterface -> CLI (gleicher Lauf) faellt auf NeedsPairing
clients=$("${CURL[@]}" "https://$ADDR/clients")
did=$(printf '%s' "$clients" | sed -n 's#.*/clients/devices/\([^/"]*\)/revoke.*#\1#p' | head -n1)
[ -n "$did" ] || die "web: Geraet nicht gefunden" "$TMP/hub.log"
body=$("${CURL[@]}" -H "X-CSRF-Token: $tok" -H "HX-Request: true" -X POST "https://$ADDR/clients/devices/$did/revoke")
printf '%s' "$body" | grep -q "widerrufen" || die "web revoke" "$TMP/hub.log"
ok "web: Zugriff widerrufen"
wait_for 20 grep -q "Widerrufen:" "$TMP/cli.out" || die "cli: kein NeedsPairing nach Revoke" "$TMP/cli.out" "$TMP/cli.err"
wait "$CLI_PID"; rc=$?; CLI_PID=""
[ "$rc" = "0" ] && grep -q "device_revoked" "$TMP/cli.out" && ok "cli: nach Revoke NeedsPairing (device_revoked)" || die "cli nach Revoke (Exit $rc)" "$TMP/cli.out" "$TMP/cli.err"

# 8. Erneuter Lauf mit demselben Datenverzeichnis darf nicht Connected werden.
# Linux: Credential-Store nur im Speicher -> kein Credential mehr; das Mass ist hier "kein Connected, Exit != 0".
QT_QPA_PLATFORM=offscreen "$CLI" games --data-dir "$PDIR" >"$TMP/cli2.out" 2>"$TMP/cli2.err"; rc=$?
if [ "$rc" != "0" ] && ! grep -q "^\[Connected\]" "$TMP/cli2.err"; then
  ok "zweiter Lauf: nicht Connected (Exit $rc; Linux ohne persistenten Credential-Store)"
else
  die "zweiter Lauf wurde Connected" "$TMP/cli2.out" "$TMP/cli2.err"
fi
