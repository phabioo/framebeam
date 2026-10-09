# ADR 0020: Cores from the libretro buildbot (0.8)

- Status: accepted (Fabio, 2026-10-09, in the project thread "Core-Discovery")
- Date: 2026-10-09
- Decided by: Fabio (proposal by the orchestrator)
- Supersedes: [ADR 0010](0010-cores-from-the-hub.md) D1 to D3 and D6 (source, signed index, package origin, licenses) and the Player-side index check of [ADR 0012](0012-internet-sessions-and-save-comfort.md) D6. ADR 0010 D4 (Hub cache) and D5 (Player cache, download on demand) stay.

## Context

ADR 0010 made FrameBeam's own signed GitHub releases the only core source, and the 0.8 plan (2026-10-07) wanted CI to mirror buildbot builds with a pinned SHA-256, the FrameBeam signature and the corresponding GPL source. Every new core and every core update would need a pin bump, a CI run and source archives. Fabio decided on 2026-10-09 to drop the mirror and to work like RetroArch's core downloader instead: FrameBeam defines which **systems** it supports, not which cores, and offers every libretro core for a system that the libretro buildbot provides. Players keep getting their cores only from their Hub.

## Decisions

### D1 Source: the libretro buildbot, through the Hub

- The Hub downloads cores from the libretro buildbot, nightly channel: base URL `https://buildbot.libretro.com/nightly` (flag `-core-buildbot-url`, env `FRAMEBEAM_HUB_CORE_BUILDBOT_URL`).
- Core metadata comes from the libretro core info files: `https://buildbot.libretro.com/assets/frontend/info.zip` (flag `-core-info-url`, env `FRAMEBEAM_HUB_CORE_INFO_URL`), the same archive RetroArch's "Update Core Info Files" uses.
- Available builds per platform come from `<base>/<os>/<arch>/latest/.index-extended`, one line per file: `<YYYY-MM-DD> <crc32 hex> <file>`, e.g. `2026-10-09 1a2b3c4d desmume_libretro.dll.zip`.
- Platform mapping (FrameBeam platform → buildbot path, library suffix): `windows-x64` → `windows/x86_64` (`.dll`), `linux-x64` → `linux/x86_64` (`.so`). Other FrameBeam platforms are not offered until the Player exists for them (0.12); the mapping is a table in the Hub.
- Players never contact the buildbot. The Player downloads cores from its Hub as before (ADR 0010 D5). A Player that wants cores "directly from the internet" uses a Hub on the same PC (planned with the combined installer).
- Stable channel (`/stable/<RetroArch version>/`) is not used: it is only rebuilt with RetroArch releases and lags behind.

### D2 Trust and pinning

- The buildbot signs nothing. Integrity of a download: HTTPS, the CRC32 from `.index-extended` must match the zip, and the zip must hold exactly one library file `<core>_libretro.<suffix>` (no path components, bounded size; zip slip and zip bombs are rejected).
- Pin at install: the Hub computes the SHA-256 of the extracted library when it installs a build and serves exactly that file with that hash to Players (trust on first download). Players check size and SHA-256 as today and trust the Hub through the pinned TLS fingerprint.
- No signature check on cores anymore, neither on the Hub nor on the Player. The Ed25519 release key and `framebeam-sign` stay for the update index (ADR 0011).
- The UI says where a core comes from ("libretro buildbot, nightly 2026-10-09").

### D3 Identity and versions

- Core id = the libretro core name, i.e. the core info file name without `_libretro.info` (`melondsds`, `desmume`, `noods`, `azahar`). Pattern `^[a-z0-9][a-z0-9_-]{0,63}$`. The legacy id `melonds_ds` (packages from the old signed source) is an alias of `melondsds`.
- Most cores report no usable version (`display_version` is `Git` or `SVN`). The package version is therefore the Hub's build id: the UTC date of the newest upstream build at install, `YYYY.MM.DD`, with `.N` appended when a second build is installed on the same day (`2026.10.09`, `2026.10.09.2`). It sorts with the existing numeric dot-segment compare.
- Per platform the package records the upstream date and CRC32 (`source_ref` = `<YYYY-MM-DD> <crc32>`, `source_url` = the zip URL, origin `libretro-buildbot`).
- A system has one installed build per core. The Player always provisions exactly the version the Hub serves for the chosen core, so all Players of a Hub run the same build. ADR 0017 D1 (major version block) only applies where both sides report a semver version; otherwise it only warns.

### D4 Systems, not cores

- The Hub's systems registry keeps the systems FrameBeam supports (`nds` today) with the libretro system ids that belong to them (`nds` → `nds`; later `3ds` → `3ds`).
- Discovery: every core whose info file has a matching `systemid` and that has a build for at least one served platform is offered for that system. Shown per core: name, license, required graphics API (`required_hw_api`), firmware notes, supported extensions, build date, and whether FrameBeam has a core profile for it (D6).
- The admin installs, updates and removes cores per system on "Systems & Cores" and picks the **default core** of the system. Installed = enabled for Players. Nothing installs or updates by itself; the Hub refreshes the catalog (info.zip and `.index-extended`) at start, every 24 h and via "Check source now", and shows "Update available" when the buildbot has a newer build. Updating keeps the old build until the new one is fully cached, then removes it.
- Existing installations keep working: a `melonds_ds` package from the old signed source stays installed and stays the default until the admin installs or picks another core.

### D5 Protocol

- Handshake feature `cores_v2`. `SystemInfo` gains `default_core_id` and `cores` (one entry per installed core: `core_id`, `display_name`, `version`, `license`, `experimental`, `required_hw_api`, `origin`). `preferred_core_id`, `expected_core_version` and `core_package_version` keep describing the default core, so Players without `cores_v2` keep working.
- `cores_index_v1` and `/api/v1/cores/index{,.sig}` are removed; Players already handle Hubs without the feature.
- Package and file endpoints (`/api/v1/cores/{core_id}/packages/...`) are unchanged; `core_id` follows the D3 pattern. Packages from the buildbot carry no `license` file; the license name comes from the core info.
- OpenAPI 1.9.0; `protocol_version` stays 1.

### D6 Player: system manifest and core profiles

- The built-in manifest is split: a **system manifest** (`manifests/systems/<system>.json`: display and screens, input profile, labels, extensions, firmware files) and **core profiles** (`manifests/cores/<core_id>.json`: library basename, display name, option defaults, locked options, always-shown options, firmware option mapping, legacy aliases).
- The system's screen layout and touch mapping only work when the core outputs the layout FrameBeam composes from; the core profile locks the options that guarantee this. Profiles exist for `melondsds` and `desmume`.
- A core without a profile is marked **experimental**: it runs with no option defaults or locks, its framebuffer is shown as one screen as the core renders it, touch is off, and firmware is only placed into the system directory.
- Core choice: the Emulation page offers the cores the Hub serves for a system, at system and game level (game > system > Hub default). The choice is stored locally like the other emulation settings. Core options stay per core (their keys are core-specific anyway).
- The Player-side signature check of core packages (ADR 0012 D6) is removed; size and SHA-256 against the Hub stay.

### D7 Saves across cores

- The Player records per game which core id and version last wrote the local save. When a game starts with another core or another build, the Player first creates a manual snapshot on the Hub (`saves_v2`, label `Before core change: <old> → <new>`) and shows a one-time notice. A failed snapshot blocks the start with a clear message (never risk a save silently).
- Raw cartridge saves of DS cores are usually interchangeable but not guaranteed; the existing SAVE_RAM size check still refuses mismatching saves. Save states (later) never cross cores.

### D8 Offline Hub

- `framebeam-hub import-cores <dir>` imports buildbot zips laid out as `<dir>/<platform>/<core>_libretro.<suffix>.zip` plus an optional `<dir>/info.zip`; the same zip checks as D2 apply (CRC32 only when an `.index-extended` is in the platform folder), the SHA-256 is pinned at import.

### D9 Licenses

- FrameBeam no longer redistributes cores; the Hub downloads them for its own Players, like RetroArch does per device. No mirrored source archives are needed.
- The license name from the core info is shown on the Hub and in the Player. Cores whose license field marks them non-commercial are shown with that note; they are not hidden.

### D10 CI and repository

- `.github/workflows/cores.yml` and the signed core index (release `cores-index`) are retired; the existing releases stay untouched so older Hubs keep working.
- `scripts/fetch-melonds-ds.*` stay as the CI test dependency. CI additionally fetches DeSmuME from the buildbot (`scripts/fetch-buildbot-core.sh`, Linux and Windows) so the core tests run against both profiled cores.

## Rejected

- **Signed FrameBeam mirror (ADR 0010, 0.8 plan of 2026-10-07):** constant maintenance per core and per update, plus GPL source archives for every mirrored binary.
- **Players downloading from the buildbot themselves:** different Players would run different builds against the same central saves, and Players without internet access would have no cores. A Hub on the same PC covers the standalone case.
- **Automatic core updates:** a nightly build changes daily; updates happen only by an admin action, so saves are never exposed to an unnoticed core change.

## Consequences

- Adding a core for a supported system is an admin click; a core profile in the Player makes it a first-class core.
- Trust in cores equals trust in the libretro buildbot over HTTPS, the same as for RetroArch users. A compromised buildbot build reaches a Hub only when its admin installs or updates.
- The Hub needs HTTPS egress to `buildbot.libretro.com` (or `import-cores`).
