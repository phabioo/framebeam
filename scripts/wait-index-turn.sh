#!/usr/bin/env bash
# Waits until it is this run's turn to update the signed updates index (release "updates-index"), CI-6.
#
# The index is read-modify-write, so Release and Promote runs must not overlap. A GitHub concurrency group serialises
# them but keeps only ONE pending run: with three main builds finishing while a publish runs, the middle run is cancelled,
# its version stays without prerelease and index entry and can never be promoted. Instead every run has its own
# concurrency group (nothing is cancelled) and queues here, oldest run id first, across release.yml, promote.yml and index-refresh.yml.
#
#   wait-index-turn.sh [max-wait-minutes]      (default 40)
# Needs gh (GH_TOKEN, GH_REPO) and GITHUB_RUN_ID. Exit 0 = go ahead, 1 = an older run did not finish in time.
set -euo pipefail

max_min="${1:-40}"
me="${GITHUB_RUN_ID:?GITHUB_RUN_ID is required}"
deadline=$(( $(date +%s) + max_min * 60 ))

# Ids of runs of the index-writing workflows that are not finished and are older than this one.
older_active() {
  local wf st out
  for wf in release.yml promote.yml index-refresh.yml; do
    for st in queued in_progress waiting requested pending; do
      # A workflow file that does not exist (yet) on the default branch has no runs: gh fails, which counts as "none".
      out="$(gh run list --workflow "$wf" --status "$st" --limit 100 --json databaseId --jq '.[].databaseId' 2>/dev/null)" || out=""
      printf '%s\n' "$out"
    done
  done | awk -v me="$me" 'NF && $1 < me' | sort -n | uniq
}

while :; do
  if ! ids="$(older_active)"; then
    echo "::warning::could not list workflow runs, retrying"
    ids="unknown"
  fi
  if [ -z "$ids" ]; then
    echo "No older index update is running; going ahead."
    exit 0
  fi
  if [ "$(date +%s)" -ge "$deadline" ]; then
    echo "::error title=Index queue::older run(s) still active after ${max_min} minutes: $(tr '\n' ' ' <<<"$ids"); not updating the index out of order" >&2
    exit 1
  fi
  echo "Waiting for older index update run(s): $(tr '\n' ' ' <<<"$ids")"
  sleep 30
done
