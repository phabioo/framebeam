# ADR 0016: Release numbering and promotion

- Status: Accepted (Fabio, 2026-10-08)
- Date: 2026-10-08
- Deciders: Fabio

## Context

[ADR 0011](0011-automatic-updates.md) D1/D3 numbered betas `X.Y.Z-beta.<run>` and made a stable release a rebuild from a `vX.Y.Z` tag, plus a PR that bumped `VERSION`. In practice the numbers drifted: promoting 0.7.1 proposed 0.8.0 as the next version although no 0.8 work had started, `VERSION` bumps depended on a PR that had to merge, and the run number in the beta suffix told users nothing. One number per build, and a promotion that does not build again, is simpler to reason about.

## Decisions

### D1 One plain number per beta

- Every push to `main` (every merged PR) builds a beta with a plain version `X.Y.Z`, published as GitHub prerelease tag `vX.Y.Z` and listed in the update index under channel `beta`. No `-beta.N` suffix any more.
- PR and branch builds stay `X.Y.Z-dev.<run>`, channel `dev`, and are never published.
- Pushing a `v*` tag builds and releases nothing.

### D2 Number assignment

- The root file `VERSION` is the floor for the next beta. CI uses `max(VERSION, highest existing plain tag vX.Y.* of that X.Y line + 1)`. After `v0.7.1` the next merge is 0.7.2, then 0.7.3.
- Only a milestone PR that starts a new minor line raises `VERSION` to `X.(Y+1).0` (for example 0.8.0). Otherwise nobody touches `VERSION`.
- The CI version job reserves the number by creating the tag on the commit before building. A failed or re-run build reuses or leaves its number, so gaps are possible.
- CI runs on `main` no longer cancel each other.

### D3 Promote to release

- `.github/workflows/promote.yml` ("Promote to release", manual). Input `version` is optional; empty means the newest beta prerelease.
- No rebuild, no new number, no new tag, no `VERSION` bump PR. The workflow adds the same artifacts to the signed `updates-index` under channel `stable` (from the `index-hub.json` / `index-player.json` assets stored on each beta release) and flips the GitHub release from prerelease to latest stable.
- Stable numbers therefore skip (0.7.1 -> 0.8.3 -> 0.8.6), as with Chrome.
- Betas built before this change (`vX.Y.Z-beta.N`) cannot be promoted. `v0.7.1` is the last stable made the old way.

### D4 Pruning

- The newest 5 beta prereleases are kept; legacy `-beta.N` prereleases age out in the same pool. Promoted releases are never pruned.

### D5 Default update channel

- Release builds of Hub and Player are compiled with channel `beta`. Dev builds stay off.
- With no channel selected, the first successful index load stores a resolved default: `update_channel_default` (Hub: `settings` table; Player: key in `player.json`). It is `stable` if the index lists the running version as stable, else `beta`.
- It is never re-resolved automatically: a beta install stays on beta after its version is promoted, a fresh install of a stable release follows stable. An explicit channel selection always wins.
- Automatic install default: on for beta, off for stable (unchanged).

## Rejected

- Keep `X.Y.Z-beta.N` and only fix the promote bump: the number still changes between beta and stable.
- Rebuild on the release tag: the shipped binary would differ from the tested beta.
- Compile `stable` into promoted builds: needs a rebuild (see above); D5 resolves the channel at runtime instead.

## Consequences

- The tested beta binary is the stable binary; its embedded channel is `beta`, so the channel is resolved at runtime (D5).
- Beta numbers are dense but may have gaps; stable numbers are sparse. Users see plain `X.Y.Z` everywhere.
- Debian versions no longer need `~beta.N` for published builds; `~` stays for `-dev` and legacy pre-release versions.
- Needs `index-hub.json` / `index-player.json` assets on every beta release.

## Supersedes in ADR 0011

- D1: the `X.Y.Z-beta.<run>` beta version, the tag-to-stable rule and "tag must equal `VERSION`".
- D3: the tag-triggered stable release and the `v*-beta.*` pruning rule.
- The "milestone PR raises `VERSION`" wording and promote's `VERSION` bump PR.
- The compiled default channel logic of D5 (now D5 here).
