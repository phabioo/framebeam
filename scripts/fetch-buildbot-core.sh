#!/usr/bin/env bash
# Downloads a libretro core from the nightly buildbot (ADR 0020 D10, CI test dependency) and prints its path (stdout, last line).
#   scripts/fetch-buildbot-core.sh <core>      e.g. desmume  ->  desmume_libretro.so
# Source: ${FRAMEBEAM_BUILDBOT_URL:-https://buildbot.libretro.com/nightly}/linux/x86_64/latest/<core>_libretro.so.zip
# The CRC32 of the extracted library is verified against ".index-extended" of the same directory; exactly one library is extracted
# to $HOME/.cache/framebeam/cores/buildbot/<core>/<date>-<crc>/ (FRAMEBEAM_CACHE_DIR overrides $HOME/.cache/framebeam).
# No pin on purpose: CI tests against the current nightly; date and CRC are logged (stderr).
# Idempotent: the same date+crc already cached = no zip download. Retries with backoff and timeouts.
# Env: FRAMEBEAM_FETCH_RETRIES (default 4), FRAMEBEAM_FETCH_BACKOFF seconds (default 2, doubled per attempt).
# http:// is accepted only for 127.0.0.1 (tests). All messages go to stderr.
set -euo pipefail

core="${1:-}"
[[ "$core" =~ ^[a-z0-9_]+$ ]] || { echo "usage: fetch-buildbot-core.sh <core>  (lower-case core name, e.g. desmume)" >&2; exit 2; }

BASE="${FRAMEBEAM_BUILDBOT_URL:-https://buildbot.libretro.com/nightly}"
BASE="${BASE%/}"
case "$BASE" in
  https://*|http://127.0.0.1|http://127.0.0.1:*|http://127.0.0.1/*) ;;
  *) echo "buildbot core: refusing URL '$BASE' (https required; http only for 127.0.0.1)" >&2; exit 2 ;;
esac
[ "$(uname -m)" = "x86_64" ] || { echo "buildbot core: only linux/x86_64 is supported (got $(uname -m))" >&2; exit 2; }
for t in curl unzip python3; do command -v "$t" >/dev/null || { echo "buildbot core: '$t' is required" >&2; exit 2; }; done

DIR_URL="$BASE/linux/x86_64/latest"
lib="${core}_libretro.so"
zipname="$lib.zip"
RETRIES="${FRAMEBEAM_FETCH_RETRIES:-4}"
BACKOFF="${FRAMEBEAM_FETCH_BACKOFF:-2}"
CACHE="${FRAMEBEAM_CACHE_DIR:-$HOME/.cache/framebeam}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# download URL DEST [max-seconds]
download() {
  curl -fsSL --proto '=https,http' --connect-timeout 15 --max-time "${3:-300}" -o "$2" "$1"
}
# retry DESCRIPTION CMD...: CMD up to $RETRIES times, exponential backoff.
retry() {
  local what="$1" n=1 wait="$BACKOFF"; shift
  until "$@"; do
    if [ "$n" -ge "$RETRIES" ]; then echo "buildbot core: $what failed after $n attempts" >&2; return 1; fi
    echo "buildbot core: $what failed (attempt $n/$RETRIES), retrying in ${wait}s" >&2
    sleep "$wait"; wait=$((wait * 2)); n=$((n + 1))
  done
}

retry "download of .index-extended" download "$DIR_URL/.index-extended" "$tmp/index" 120 \
  || { echo "buildbot core: cannot reach $DIR_URL/.index-extended" >&2; exit 1; }

# Line format: "<date> [<time>] <crc32 hex> [<size>] <file name>" (3 fields: crc is field 2, else field 3).
read -r date crc < <(awk -v n="$zipname" '$NF == n && NF >= 3 { print $1, (NF == 3 ? $2 : $3); exit }' "$tmp/index") || true
if [ -z "${date:-}" ] || ! [[ "$date" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || ! [[ "${crc:-}" =~ ^[0-9a-fA-F]{8}$ ]]; then
  echo "buildbot core: no valid entry for $zipname in $DIR_URL/.index-extended" >&2; exit 1
fi
crc="$(tr 'A-F' 'a-f' <<<"$crc")"
echo "buildbot core: $core nightly date=$date crc32=$crc" >&2

OUT="$CACHE/cores/buildbot/$core/$date-$crc"
if [ -f "$OUT/$lib" ]; then echo "buildbot core: cached $OUT/$lib" >&2; echo "$OUT/$lib"; exit 0; fi

# The index CRC32 is over the uncompressed library (like RetroArch's core updater), not over the zip.
fetch_verified() {
  download "$DIR_URL/$zipname" "$tmp/$zipname" 300 || return 1
  # Exactly one entry named like the library, no other content is extracted (no path from the archive is used).
  local -a entries
  mapfile -t entries < <(unzip -Z1 "$tmp/$zipname")
  if [ "${#entries[@]}" -ne 1 ] || [ "${entries[0]}" != "$lib" ]; then
    echo "buildbot core: unexpected zip content (expected only $lib): ${entries[*]}" >&2; return 1
  fi
  unzip -p "$tmp/$zipname" "$lib" >"$tmp/$lib" || return 1
  [ -s "$tmp/$lib" ] || { echo "buildbot core: extracted $lib is empty" >&2; return 1; }
  local actual
  actual="$(python3 -I -c 'import sys,zlib; print("%08x" % (zlib.crc32(open(sys.argv[1],"rb").read()) & 0xffffffff))' "$tmp/$lib")" || return 1
  if [ "$actual" != "$crc" ]; then
    rm -f "$tmp/$lib"
    echo "buildbot core: CRC32 mismatch for $lib: index says $crc, extracted library is $actual" >&2; return 1
  fi
}
retry "download/verification of $zipname" fetch_verified || exit 1

mkdir -p "$OUT"
chmod 0755 "$tmp/$lib"
mv "$tmp/$lib" "$OUT/$lib"
echo "buildbot core: installed $OUT/$lib" >&2
echo "$OUT/$lib"
