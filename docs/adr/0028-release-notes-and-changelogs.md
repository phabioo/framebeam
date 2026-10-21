# ADR 0028: Release notes and changelogs

- Status: Accepted
- Date: 2026-10-21
- Deciders: Fabio

## Context

Until 0.15 every beta release had the same fixed text ("Automatic beta build of commit ..."), `promote.yml` kept it for the stable release, and there was no `CHANGELOG.md`. The update index only carried `notes_url`, so the Player had nothing to show after an update. The plugin repositories use `gh release create --generate-notes`, which lists PR titles only. `main` is protected by a ruleset (changes only through PRs with required checks) and PRs auto-merge, so any scheme that writes files per PR would conflict. Roadmap 0.16 ([roadmap.md](../roadmap.md)) asks for real notes per beta, a summary per stable release, an archive and the data for a "What's new" screen.

## Decisions

### D1 Changelog section in the PR description

Every PR description has a `## Changelog` section ([PR template](../../.github/pull_request_template.md)): bullets under `### Added`, `### Changed`, `### Fixed`, `### Removed` or `### Security`, one user-visible change per line, English. A bullet may start with `Hub:` or `Player:` when only one product is affected. CI, docs, test and refactoring-only PRs write `none` alone. The text lives in the PR, not in the repository, so there are no merge conflicts.

### D2 Required check

Workflow `pr-changelog.yml` ("Changelog", job "Changelog section") runs `scripts/changelog.py check` on PRs against `main` and fails when the section is missing, empty or malformed. Bot PRs (Dependabot and similar) are exempt. It is a required check of the `main` ruleset (set by hand, see Consequences). `workflow_dispatch` with input `pr` re-checks a PR whose events started no workflows.

### D3 Beta notes

`release.yml` runs `scripts/changelog.py collect` over `git log --first-parent` from the previous plain `vX.Y.Z` release (prerelease or not; none: the newest stable release) to the built commit, maps the commits to PRs and builds the notes from their sections, grouped by category with PR links, plus a compare link. Fallbacks: a PR without a section appears under Changed with its title, bot PRs under Dependencies. If collection fails as a whole, the release gets the old fixed text and the index no `notes`; a release is never blocked by its notes.

### D4 Stable notes

`promote.yml` collects over the range from the previous stable release to the promoted beta ("Changes since X.Y.Z"), so all betas in between appear once. The result replaces the beta text of the GitHub release. The optional input `highlights` (Markdown) goes above it. The same range, filtered per product, becomes the index `notes` of the stable entries and replaces the beta value.

### D5 CHANGELOG.md through a PR

`promote.yml` opens the PR "docs: changelog for X.Y.Z" on branch `changelog/vX.Y.Z` that adds the section (newest first, stable releases only) to `CHANGELOG.md`, with auto-merge. A direct push is not possible (ruleset). A failure in this step only warns and never undoes the promotion; the section is then added by hand. Events caused by `GITHUB_TOKEN` start no workflows, so the workflow dispatches `ci.yml` and `pr-changelog.yml` on the branch; `ci.yml` treats a `workflow_dispatch` on a non-`main` ref like a push to a branch, so a docs-only change skips the build jobs. The merge of this PR builds one more beta with the text "No user-visible changes."

### D6 Index field `notes`

The signed updates index gets an optional per-entry field `notes`: short plain text (groups `### Added`, bullets `- line`), at most 4000 bytes, product-filtered (D3, D4), format in [update-index.md](../reference/update-index.md). `framebeam-sign release-add` and the Hub reject invalid values; the Player drops an invalid value and keeps the release. The Hub passes `notes` on in its update status. The Player keeps it for a later "What's new" screen; no UI exists yet and the screen waits for the Claude Design handoff. Old clients ignore the field.

### D7 Tooling

`scripts/changelog.py` (Python standard library only, run with `python3 -I`) implements `check` and `collect`. It has no FrameBeam-specific logic apart from the product prefixes and can be copied unchanged into the plugin repositories (their adoption is a separate package). Unit tests are in `scripts/tests` and run in `make check-hub` (step `script-tests` of `scripts/check.sh`).

## Rejected

- **Changelog fragment files in the repository** (one file per PR, collected at release): merge conflicts and extra rebases with auto-merge, and files that must be cleaned up afterwards.
- **GitHub `--generate-notes`**: lists PR titles only, which are written for developers; no categories, no per-product text, nothing to put in the index.
- **Direct push of `CHANGELOG.md` to `main`**: the ruleset requires PRs with checks, and a bypass for a workflow token would weaken it.

## Consequences

- Every PR author writes one short section; the check fails otherwise. Pure CI/docs PRs write `none`.
- Manual steps for Fabio: add "Changelog section" to the required checks of the `main` ruleset; turn on the repository setting "Allow GitHub Actions to create and approve pull requests", otherwise the promote summary shows a warning and the `CHANGELOG.md` section is added by hand.
- The changelog PR adds one extra beta build per promotion ("No user-visible changes.").
- Betas carry real notes; the stable release carries the whole range since the previous stable release.
- The Hub and the Player have the data for "What's new"; the Player screen is open (design handoff).
- The plugin repositories still use `--generate-notes` until their package is done.
