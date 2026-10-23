# shellcheck shell=bash
# Sourced by the e2e scripts: the server has no web pages, so the admin steps (sign in, upload a dummy game, allow
# or revoke a device, invites, firmware mode) go through the admin REST API with curl and python3.
# Needs ADDR (host:port of the server) and TMP; uses die/ok of the calling script.

# jget <dotted.path>: prints a field of the JSON on stdin ("requests.0.id"); fails when it is missing.
jget() {
  python3 -I -c '
import json, sys
v = json.load(sys.stdin)
for k in sys.argv[1].split("."):
    v = v[int(k)] if k.isdigit() else v[k]
print(v)' "$1"
}

ATOK="" ATOK_AT=0 ADMIN_UID="" ADMIN_DID="" ADMIN_CRED=""

# admin_token: mints an access token (15 minutes) from the device credential of the admin sign-in.
admin_token() {
  local resp
  resp=$(curl -sk -H 'Content-Type: application/json' \
    -d "{\"device_id\":\"$ADMIN_DID\",\"device_credential\":\"$ADMIN_CRED\"}" "https://$ADDR/api/v1/auth/token") || return 1
  ATOK=$(printf '%s' "$resp" | jget access_token) || return 1
  ATOK_AT=$SECONDS
}

# admin_login <password>: signs the user "admin" in with a device of its own (name e2e-admin); sets ATOK, ADMIN_UID.
admin_login() {
  local body resp
  ADMIN_DID=$(python3 -I -c 'import uuid; print(uuid.uuid4())')
  body=$(python3 -I -c '
import json, sys
print(json.dumps({"username": "admin", "password": sys.argv[2], "device_id": sys.argv[1], "device_name": "e2e-admin",
                  "platform": "linux", "arch": "x86_64", "app_version": "0.0.0", "protocol_version": 1}))' "$ADMIN_DID" "$1")
  resp=$(curl -sk -H 'Content-Type: application/json' -d "$body" "https://$ADDR/api/v1/auth/sign-in") || return 1
  ADMIN_UID=$(printf '%s' "$resp" | jget user_id) || return 1
  ADMIN_CRED=$(printf '%s' "$resp" | jget device_credential) || return 1
  admin_token
}

# api <curl args...>: curl with the admin bearer token (refreshed before it expires).
api() {
  [ $((SECONDS - ATOK_AT)) -gt 600 ] && admin_token
  curl -sk -H "Authorization: Bearer $ATOK" "$@"
}

# upload_game <file> <title>: uploads a dummy game as admin; prints the HTTP status.
upload_game() {
  local q
  q=$(python3 -I -c 'import sys, urllib.parse as u; print(u.urlencode({"filename": sys.argv[1], "title": sys.argv[2]}))' "$(basename "$1")" "$2")
  api -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/octet-stream' --data-binary "@$1" \
    "https://$ADDR/api/v1/games?$q"
}

# allow_pending: allows the oldest pending pairing request, assigned to the admin user (succeeds only when one exists).
allow_pending() {
  local rid code
  rid=$(api "https://$ADDR/api/v1/admin/pairing-requests" | jget requests.0.id) || return 1
  code=$(api -o /dev/null -w '%{http_code}' -X POST -H 'Content-Type: application/json' \
    -d "{\"user_id\":\"$ADMIN_UID\"}" "https://$ADDR/api/v1/admin/pairing-requests/$rid/allow")
  [ "$code" = "204" ]
}

# revoke_cli_device: revokes the trusted device that is not the admin sign-in device (the only CLI device).
revoke_cli_device() {
  local did code
  did=$(api "https://$ADDR/api/v1/admin/devices" | python3 -I -c '
import json, sys
for d in json.load(sys.stdin)["devices"]:
    if d["status"] == "trusted" and d["name"] != "e2e-admin":
        print(d["id"]); break')
  [ -n "$did" ] || return 1
  code=$(api -o /dev/null -w '%{http_code}' -X POST "https://$ADDR/api/v1/admin/devices/$did/revoke")
  [ "$code" = "200" ]
}

# mkinvite: prints a new invite code whose first device is trusted directly.
mkinvite() {
  api -X POST -H 'Content-Type: application/json' -d '{"expiry":"1h","authorize_device":true}' \
    "https://$ADDR/api/v1/admin/invites" | jget code.code
}

# set_firmware_mode <system> <builtin|native>: prints the HTTP status.
set_firmware_mode() {
  api -o /dev/null -w '%{http_code}' -X PATCH -H 'Content-Type: application/json' -d "{\"firmware_mode\":\"$2\"}" \
    "https://$ADDR/api/v1/admin/systems/$1"
}
