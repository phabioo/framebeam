#!/usr/bin/env bash
# Builds the libretro core melonDS DS (pinned tag, from source via git clone) into
# $HOME/.cache/framebeam/cores/melondsds/<tag>/linux-<arch>/ and prints the .so path (stdout).
# Idempotent: already built = done immediately. All messages go to stderr.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/melonds-ds.pin"
CACHE="${FRAMEBEAM_CACHE_DIR:-$HOME/.cache/framebeam}"
OUT="$CACHE/cores/melondsds/$MELONDS_DS_TAG/linux-$(uname -m)"
CORE="$OUT/melondsds_libretro.so"

if [ -f "$CORE" ]; then echo "$CORE"; exit 0; fi

SRC="$CACHE/src/melonds-ds-$MELONDS_DS_TAG"
BUILD="$SRC/build"
if [ ! -d "$SRC/.git" ]; then
  rm -rf "$SRC"; mkdir -p "$(dirname "$SRC")"
  git clone -q --depth 1 --branch "$MELONDS_DS_TAG" https://github.com/JesseTG/melonds-ds "$SRC" >&2
fi
[ "$(git -C "$SRC" rev-parse HEAD)" = "$MELONDS_DS_COMMIT" ] \
  || { echo "melonDS DS: commit of $MELONDS_DS_TAG differs from the pin" >&2; exit 1; }

cmake -S "$SRC" -B "$BUILD" -G Ninja -Wno-deprecated -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DENABLE_LTO_RELEASE=OFF >&2
cmake --build "$BUILD" --parallel "$(nproc)" >&2

built="$(find "$BUILD" -name 'melondsds_libretro.so' -type f | head -n1)"
[ -n "$built" ] || { echo "melonDS DS: melondsds_libretro.so not found" >&2; exit 1; }
mkdir -p "$OUT"
cp "$built" "$CORE"
cp "$SRC/LICENSE" "$OUT/LICENSE-melonDS-DS.txt"
echo "$CORE"
