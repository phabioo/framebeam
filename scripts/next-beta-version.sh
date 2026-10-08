#!/usr/bin/env bash
# Next beta version for a push to main (ADR 0011).
#   next-beta-version.sh <VERSION-file-content> <commit-sha> < "git ls-remote --tags" output
# VERSION (X.Y.Z) is the floor. In the X.Y line of VERSION the result is max(VERSION, highest plain tag vX.Y.N + 1).
# If a plain tag of that line already points at <commit-sha> (re-run of the same commit), that version is reused.
# Suffixed tags (v0.7.1-beta.335) and tags of other lines are ignored. Prints X.Y.Z.
#   next-beta-version.sh --self-test
set -euo pipefail

compute() {
  local floor="$1" sha="$2" major minor patch
  [[ "$floor" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || { echo "VERSION must be X.Y.Z, found '$floor'" >&2; return 1; }
  major="${BASH_REMATCH[1]}"; minor="${BASH_REMATCH[2]}"; patch="${BASH_REMATCH[3]}"
  [[ "$sha" =~ ^[0-9a-f]{40}$ ]] || { echo "commit must be a 40 digit sha, found '$sha'" >&2; return 1; }
  awk -v major="$major" -v minor="$minor" -v floor="$patch" -v sha="$sha" '
    { ref = $2; sub(/\^\{\}$/, "", ref); sub(/^refs\/tags\//, "", ref); tag[ref] = $1 }  # peeled (^{}) lines come later and win
    END {
      max = -1; reuse = -1
      for (t in tag) {
        n = split(t, p, ".")
        if (n != 3 || p[1] != "v" major || p[2] !~ /^[0-9]+$/ || p[3] !~ /^[0-9]+$/ || p[2] + 0 != minor + 0) continue
        if (p[3] + 0 > max) max = p[3] + 0
        if (tag[t] == sha && p[3] + 0 > reuse) reuse = p[3] + 0
      }
      next_patch = (max + 1 > floor + 0) ? max + 1 : floor + 0
      if (reuse >= 0) next_patch = reuse
      printf "%s.%s.%d\n", major, minor, next_patch
    }'
}

self_test() {
  local rc=0 a="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" b="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
  t() { # expected floor sha tags...
    local want="$1" floor="$2" sha="$3" got tags="" x
    shift 3
    for x in "$@"; do tags+="${x%%=*}	refs/tags/${x#*=}"$'\n'; done
    got="$(printf '%s' "$tags" | compute "$floor" "$sha")"
    if [ "$got" = "$want" ]; then echo "ok   $floor + [$*] -> $got"; else echo "FAIL $floor + [$*] -> $got (want $want)"; rc=1; fi
  }
  t 0.7.2  0.7.1 "$a" "$b=v0.7.1" "$b=v0.7.1-beta.335"
  t 0.8.0  0.8.0 "$a" "$b=v0.7.5"
  t 0.7.11 0.7.1 "$a" "$b=v0.7.1" "$b=v0.7.2" "$b=v0.7.10"
  t 0.7.5  0.7.5 "$a" "$b=v0.7.2"
  t 0.7.1  0.7.1 "$a"
  t 0.7.2  0.7.1 "$a" "$b=v0.7.1" "$a=v0.7.2"          # re-run of the same commit reuses its tag
  t 0.7.2  0.7.1 "$a" "$b=v0.7.1" "$b=v0.7.2" "$a=v0.7.2^{}"
  t 0.7.3  0.7.1 "$a" "$b=v0.7.1" "$b=v0.7.2"
  t 0.7.2  0.7.1 "$a" "$b=v0.7.1" "$b=v0.70.9" "$b=v0.7.1.1" "$b=v1.7.9"
  return "$rc"
}

if [ "${1:-}" = "--self-test" ]; then self_test; exit $?; fi
[ $# -eq 2 ] || { echo "usage: $0 <VERSION> <sha>  (tags on stdin)  |  --self-test" >&2; exit 2; }
compute "$(printf '%s' "$1" | tr -d '[:space:]')" "$2"
