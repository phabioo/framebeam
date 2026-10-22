#!/usr/bin/env bash
# Installs apt packages on a CI runner with bounded time: each attempt
# is limited (update 120 s, install 480 s), apt retries/timeouts are set, and
# up to 3 attempts are made so a hanging mirror cannot block a job for long.
# A timed-out apt/dpkg is killed hard (timeout -k) and dpkg is repaired (dpkg --configure -a) before the next attempt.
# After a failed attempt the runner's Azure mirror is swapped for archive.ubuntu.com, because a slow or hanging mirror
# outlasts all retries against the same host. Downloaded packages stay in apt's cache, so a retry continues.
# Usage: scripts/ci-apt-install.sh <packages...>
set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "usage: $0 <packages...>" >&2
  exit 2
fi

opts=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30)
attempts=3

log="$(mktemp)"

# Runs one apt step with a time limit; on failure prints which step failed, its exit code (124 = timed out) and the
# end of its output, so a hanging source is named in the job log.
run_step() {
  local limit="$1" name="$2"
  shift 2
  local rc=0
  timeout -k 10 "$limit" "$@" >"$log" 2>&1 || rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "apt step '${name}' failed with exit code ${rc} (124 = timed out after ${limit} s); last output:"
    tail -n 25 "$log"
  fi
  return "$rc"
}

attempt_once() {
  run_step 120 update sudo apt-get update "${opts[@]}" &&
    run_step 480 install sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q "${opts[@]}" "$@"
}

switch_mirror() {
  local f
  # GitHub runners list the mirror in apt-mirrors.txt (sources use mirror+file:), older images in the sources.
  for f in /etc/apt/apt-mirrors.txt /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do
    [ -f "$f" ] || continue
    if grep -q 'azure\.archive\.ubuntu\.com' "$f"; then
      sudo sed -i 's#azure\.archive\.ubuntu\.com#archive.ubuntu.com#g' "$f"
      echo "switched apt mirror to archive.ubuntu.com in $f"
    fi
  done
  grep -rhoE 'https?://[^ ]+' /etc/apt/sources.list /etc/apt/sources.list.d/ 2>/dev/null | sort -u | sed 's/^/apt source: /' || true
}

for ((i = 1; i <= attempts; i++)); do
  if attempt_once "$@"; then
    exit 0
  fi
  echo "::warning::apt update/install failed (attempt ${i}/${attempts})"
  if [ "$i" -lt "$attempts" ]; then
    # A killed apt may have left dpkg half-configured ("dpkg was interrupted"); without this the retries cannot succeed.
    timeout -k 10 120 sudo DEBIAN_FRONTEND=noninteractive dpkg --configure -a || true
    switch_mirror
    sleep 10
  fi
done

echo "::error::apt install failed after ${attempts} attempts"
exit 1
