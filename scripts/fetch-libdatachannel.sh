#!/usr/bin/env bash
# Builds libdatachannel (pinned tag, from source via git clone with submodules) with media ON and
# its own WebSocket OFF (OpenSSL) and installs it to $HOME/.cache/framebeam/deps/libdatachannel/<tag>;
# prints the install prefix (stdout). Idempotent: already installed = done immediately.
# All messages go to stderr. Parallelism: CMAKE_BUILD_PARALLEL_LEVEL (default 3).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/libdatachannel.pin"
CACHE="${FRAMEBEAM_CACHE_DIR:-$HOME/.cache/framebeam}"
PREFIX="$CACHE/deps/libdatachannel/$LIBDATACHANNEL_TAG-$(uname -m)"

if [ -f "$PREFIX/.complete" ]; then echo "$PREFIX"; exit 0; fi

SRC="$CACHE/src/libdatachannel-$LIBDATACHANNEL_TAG"
BUILD="$SRC/build"
if [ ! -d "$SRC/.git" ]; then
  rm -rf "$SRC"; mkdir -p "$(dirname "$SRC")"
  git clone -q --depth 1 --branch "$LIBDATACHANNEL_TAG" --recurse-submodules --shallow-submodules \
    https://github.com/paullouisageneau/libdatachannel.git "$SRC" >&2
fi
[ "$(git -C "$SRC" rev-parse HEAD)" = "$LIBDATACHANNEL_COMMIT" ] \
  || { echo "libdatachannel: commit of $LIBDATACHANNEL_TAG differs from the pin" >&2; exit 1; }

cmake -S "$SRC" -B "$BUILD" -G Ninja -Wno-deprecated -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DUSE_GNUTLS=OFF -DUSE_NICE=OFF -DNO_MEDIA=OFF -DNO_WEBSOCKET=ON \
  -DNO_EXAMPLES=ON -DNO_TESTS=ON -DBUILD_SHARED_LIBS=ON >&2
cmake --build "$BUILD" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}" >&2
cmake --install "$BUILD" >&2
touch "$PREFIX/.complete"
echo "$PREFIX"
