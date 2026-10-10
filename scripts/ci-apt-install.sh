#!/usr/bin/env bash
# Installs apt packages on a CI runner with bounded time: each attempt
# is limited (update 120 s, install 240 s, max 6 min), apt retries/timeouts are set, and
# up to 3 attempts are made so a hanging mirror cannot block a job for long.
# A timed-out apt/dpkg is killed hard (timeout -k) and dpkg is repaired (dpkg --configure -a) before the next attempt.
# Usage: scripts/ci-apt-install.sh <packages...>
set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "usage: $0 <packages...>" >&2
  exit 2
fi

opts=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30)
attempts=3

attempt_once() {
  timeout -k 10 120 sudo apt-get update -qq "${opts[@]}" &&
    timeout -k 10 240 sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${opts[@]}" "$@"
}

for ((i = 1; i <= attempts; i++)); do
  if attempt_once "$@"; then
    exit 0
  fi
  echo "::warning::apt update/install failed (attempt ${i}/${attempts})"
  if [ "$i" -lt "$attempts" ]; then
    # A killed apt may have left dpkg half-configured ("dpkg was interrupted"); without this the retries cannot succeed.
    timeout -k 10 120 sudo DEBIAN_FRONTEND=noninteractive dpkg --configure -a || true
    sleep 10
  fi
done

echo "::error::apt install failed after ${attempts} attempts"
exit 1
