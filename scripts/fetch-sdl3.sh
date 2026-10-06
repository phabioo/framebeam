#!/usr/bin/env bash
# Builds SDL3 (pinned tag, from source) as a shared library for gamepad/joystick use only (no video, audio,
# render, camera; no X11/Wayland/ALSA dev packages needed) and installs it to
# $HOME/.cache/framebeam/deps/sdl3/<tag>-<arch>; prints the install prefix (stdout).
# Idempotent: already installed = done immediately. All messages go to stderr.
# Parallelism: CMAKE_BUILD_PARALLEL_LEVEL (default 3).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/sdl3.pin"
CACHE="${FRAMEBEAM_CACHE_DIR:-$HOME/.cache/framebeam}"
PREFIX="$CACHE/deps/sdl3/$SDL3_TAG-$(uname -m)"

if [ -f "$PREFIX/.complete" ]; then echo "$PREFIX"; exit 0; fi

SRC="$CACHE/src/sdl3-$SDL3_TAG"
BUILD="$SRC/build"
if [ ! -d "$SRC/.git" ]; then
  rm -rf "$SRC"; mkdir -p "$(dirname "$SRC")"
  git clone -q --depth 1 --branch "$SDL3_TAG" https://github.com/libsdl-org/SDL.git "$SRC" >&2
fi
[ "$(git -C "$SRC" rev-parse HEAD)" = "$SDL3_COMMIT" ] \
  || { echo "SDL3: commit of $SDL3_TAG differs from the pin" >&2; exit 1; }

cmake -S "$SRC" -B "$BUILD" -G Ninja -Wno-deprecated -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DSDL_SHARED=ON -DSDL_STATIC=OFF \
  -DSDL_VIDEO=OFF -DSDL_AUDIO=OFF -DSDL_RENDER=OFF -DSDL_GPU=OFF -DSDL_CAMERA=OFF \
  -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF >&2
cmake --build "$BUILD" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}" >&2
cmake --install "$BUILD" >&2
touch "$PREFIX/.complete"
echo "$PREFIX"
