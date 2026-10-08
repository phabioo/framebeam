#!/usr/bin/env bash
# Builds the FrameBeam Hub .deb with dpkg-deb only (no debhelper): docs/guides/hub-install.md.
# Usage: build-deb.sh --binary PATH --arch amd64|arm64 --version X.Y.Z[-pre] --out DIR
# Control Version: SemVer with - replaced by ~ (0.3.0-beta.57 -> 0.3.0~beta.57); the file name keeps the SemVer. Prints the .deb path.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="" ARCH="" VERSION="" OUT=""

die() { echo "error: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --binary)  [ $# -ge 2 ] || die "--binary needs a value"; BINARY="$2"; shift ;;
    --arch)    [ $# -ge 2 ] || die "--arch needs a value"; ARCH="$2"; shift ;;
    --version) [ $# -ge 2 ] || die "--version needs a value"; VERSION="$2"; shift ;;
    --out)     [ $# -ge 2 ] || die "--out needs a value"; OUT="$2"; shift ;;
    -h|--help) sed -n '2,5p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) die "unknown argument: $1" ;;
  esac
  shift
done

[ -f "$BINARY" ] || die "--binary: file not found: '$BINARY'"
case "$ARCH" in amd64|arm64) ;; *) die "--arch must be amd64 or arm64" ;; esac
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || die "--version must be X.Y.Z or X.Y.Z-pre (got '$VERSION')"
[ -n "$OUT" ] || die "--out needs a value"
command -v dpkg-deb >/dev/null 2>&1 || die "dpkg-deb not found"

DEBVER="${VERSION//-/\~}"
DEB="$OUT/framebeam-hub_${VERSION}_${ARCH}.deb"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
chmod 0755 "$STAGE"

install -d -m 0755 "$STAGE/usr/bin" "$STAGE/lib/systemd/system" "$STAGE/DEBIAN" "$OUT"
install -m 0755 "$BINARY" "$STAGE/usr/bin/framebeam-hub"

# The script-install unit points at /usr/local/bin; the package unit runs /usr/bin and gets a runtime dir for the
# update request file (kept across Hub restarts so a request made right before a restart is not lost).
unit="$STAGE/lib/systemd/system/framebeam-hub.service"
sed -e 's#^ExecStart=/usr/local/bin/framebeam-hub$#ExecStart=/usr/bin/framebeam-hub#' \
    -e 's#^RestartSec=5$#RestartSec=5\nRuntimeDirectory=framebeam\nRuntimeDirectoryPreserve=yes#' \
    "$SCRIPT_DIR/framebeam-hub.service" >"$unit"
grep -qx 'ExecStart=/usr/bin/framebeam-hub' "$unit" || die "could not rewrite ExecStart in framebeam-hub.service"
grep -qx 'RuntimeDirectory=framebeam' "$unit" || die "could not add RuntimeDirectory to framebeam-hub.service"
chmod 0644 "$unit"
install -m 0644 "$SCRIPT_DIR/framebeam-hub-update.path" "$SCRIPT_DIR/framebeam-hub-update.service" \
  "$STAGE/lib/systemd/system/"

for s in postinst prerm postrm; do install -m 0755 "$SCRIPT_DIR/deb/$s" "$STAGE/DEBIAN/$s"; done

size_kb="$(du -sk "$STAGE/usr" "$STAGE/lib" | awk '{s+=$1} END {print s}')"
cat >"$STAGE/DEBIAN/control" <<CONTROL
Package: framebeam-hub
Version: $DEBVER
Section: net
Priority: optional
Architecture: $ARCH
Maintainer: FrameBeam <framebeam@users.noreply.github.com>
Installed-Size: $size_kb
Depends: systemd
Homepage: https://github.com/phabioo/framebeam
Description: FrameBeam Hub - self-hosted retro gaming library and session server
 Central ROM library, versioned saves and session coordination for FrameBeam
 Players. Runs as the systemd service framebeam-hub; configuration in
 /etc/framebeam/hub.env, data in /var/lib/framebeam (never removed by the
 package).
CONTROL

# Reproducible mtimes; xz for old dpkg versions (Raspberry Pi OS).
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$SCRIPT_DIR" log -1 --format=%ct 2>/dev/null || date +%s)}"
find "$STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$DEB" >/dev/null
echo "$DEB"
