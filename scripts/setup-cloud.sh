#!/usr/bin/env bash
# Setup script for OpenAI Codex cloud environments (`bash scripts/setup-cloud.sh`) and other non-Claude cloud agents.
# Runs the session-start hook (vcpkg, Go modules, Qt/FFmpeg apt packages) and then prebuilds the client
# dependencies (libdatachannel, SDL3, melonDS DS core) so `make check-client` works without internet.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! FRAMEBEAM_CLOUD_SETUP=1 bash "$ROOT/.claude/hooks/session-start.sh"; then
  echo "setup-cloud: prerequisites failed (see warnings above)" >&2
  exit 1
fi

for target in fetch-deps fetch-sdl3 fetch-core; do
  log="$(mktemp)"
  if make --no-print-directory "$target" >"$log" 2>&1; then
    echo "setup-cloud: $target ok"
  else
    echo "setup-cloud: step '$target' failed" >&2
    tail -n 20 "$log" >&2
    rm -f "$log"
    exit 1
  fi
  rm -f "$log"
done
