# Features as built

What the Hub, the protocol and the Player do today, by area. Status by version: [roadmap.md](roadmap.md). How to use them: [documentation index](README.md).

**FrameBeam Hub** (`server/`)

- Admin setup via `setup-admin` and via `/setup` in the web interface (loopback only).
- HTTPS with a self-signed certificate (or your own certificate); the fingerprint is logged at startup. The Hub renews its own certificate at startup when it is expired or expires within 30 days (`framebeam-hub renew-cert` does it on demand, or "Renew now" in Settings › Security without a restart; key and certificate are written atomically); the Settings page shows "Expires within 30 days". Own certificates are never modified.
- Web interface with login (admins only): Library (upload and "Rescan folder" import), Saves, Systems & Cores, Clients, Users, Settings.
- Pairing of new devices with Allow/Deny; issue and revoke tokens (Revoke); Delete removes a device entry for good (confirmation page, saves are kept).
- ROM upload in the web interface.
- ROM download via API with Range and ETag.
- Versioned saves per user and game (API tag `saves`, added in API 1.1.0, `protocol_version` stays 1, handshake feature `saves_v1`); the Hub stores the save as an opaque blob and never merges it.
- Web page "Saves" (admin): slots, conflicts ("Use Hub version" / "Adopt local save"), history with Download, badge in the navigation.
- Info endpoint `/.well-known/framebeam` and handshake with `protocol_version`.
- Sessions: session API (visibility Private / Hub users / Invite only, invites, viewers), WSS presence and signaling relay, revoke on visibility change; optional STUN servers via `-ice-servers` / `FRAMEBEAM_ICE_SERVERS`.
- Users and invites: Users page creates single-use invite codes (shown once, with "Copy link" to the public `/invite` page at creation), disable/enable users and delete them for good (confirmation page; removes their devices, invites, saves and save history, uploaded games stay), display names unique; Clients page assigns a pending device to a user and marks Players below the minimum protocol version as "Player too old".
- Settings: "Allow users to upload games" and Appearance (Light / Dark / System).
- Systems & Cores page: expected core version, reports from clients, firmware mode and firmware files per system (user-supplied, never shipped).
- Core package cache: the Hub fetches an Ed25519-signed core index and the packages from FrameBeam's GitHub Releases (startup, every 24 h, "Check source now") and serves them to Players. It needs internet access to github.com; offline use `framebeam-hub import-cores <dir>` (systemd: `install-hub.sh import-cores <dir>`). Extra trusted keys: `--core-trust-key`.
- Sessions over the internet: optional embedded STUN/TURN relay (`-turn`, `-public-host`; off by default) with short-lived credentials; Settings shows TURN status and the router port forwards (ADR 0012).
- Save comfort: retention/thinning (`-save-keep-recent`, `-save-keep-daily`, `-save-keep-weekly`), restore from history, manual snapshots, `save_updated` push over WSS; the Saves page can restore, snapshot, delete a snapshot and create a slot (0.7.x, `saves_v3`). Save upload (`saves_v4`): upload a save file into a slot via API or web (Saves slot detail, collapsible "Upload save" form on the Saves list, "Upload save" link per Library row); the file becomes the current checkpoint and the previous one goes to history as `before_upload`.
- Core index endpoints (`GET /api/v1/cores/index` and `.sig`, served byte-exact) so Players verify the signature themselves.
- Hub UI pass (0.7): sidebar navigation swaps only the content area; badges and tables update live (Server-Sent Events). Settings → Network edits port, embedded TURN, public host, relay range and save retention in the browser; these web settings win over `hub.env`, and changes that need it are applied by a restart the Hub triggers itself (ports below 1024 only via `install-hub.sh --port`).
- Hardening for internet exposure: TURN relay refuses internal-range peers (except connected Players) and limits allocations; the web port requires CSRF tokens on multipart forms, limits sign-in attempts, uses `__Host-` cookies with TLS and needs passwords of at least 12 characters; the Player API rate limits WebSocket messages, pairing and invite redemption ([hub-configuration.md](guides/hub-configuration.md#turn-security)).
- Ships as a Debian package and with a systemd installer script for Linux / Raspberry Pi (`packaging/linux/`).

**Protocol** (`protocol/`)

- OpenAPI 3.0.3 for `/api/v1` and WSS message schemas; OpenAPI spec version 1.8.0, `protocol_version` is 1; handshake features `saves_v1`, `sessions_v1`, `users_v1`, `uploads_v1`, `firmware_v1`, `cores_v1`, `saves_v2`, `saves_v3`, `saves_v4`, `cores_index_v1` and, with TURN on, `turn_v1`. See [reference/protocol.md](reference/protocol.md).

**FrameBeam Player** (`client/`, [ADR 0003](adr/0003-player-phase2.md))

- Connection screen with Hub profiles and auto-connect.
- Hub identification with fingerprint confirmation on first contact (TOFU); on mismatch the connection is blocked and no credential is sent. After a Hub certificate change the Hub card shows the stored and the presented fingerprint; "Trust new certificate" and then "Yes, trust this certificate" re-pin it and keep the credential (CLI: `--accept-fingerprint <sha256>`).
- Settings → Hubs: switch, remove (with confirmation), auto-connect; the current Hub is marked.
- Pairing via approval request, token renewal and Revoke.
- Library with search and filter.
- Hash-verified ROM cache with resumable download.
- Launch NDS games locally with melonDS DS: video, audio via Qt Multimedia, keyboard, touch via mouse.
- OpenGL hardware rendering for libretro cores (0.5): melonDS DS offers an OpenGL renderer and an internal resolution on the Emulation page (applied on next start; default Software). Hardware frames are read back to the CPU, so Sessions work unchanged. `FRAMEBEAM_DISABLE_HW_RENDER=1` forces the software path; the CLI has no hardware rendering.
- GPU-direct encoding (0.7.x, [ADR 0019](adr/0019-gpu-direct-nvenc.md)): on NVIDIA, a shared hardware-rendered Session hands the core's OpenGL texture to NVENC through CUDA-GL interop, with no readback or CPU conversion for the encoder. Any problem falls back to the readback path without stopping the Session; `FRAMEBEAM_DISABLE_GPU_ENCODE=1` turns it off, and the diagnostics host line shows `· GPU-direct` or `· readback (GPU-direct off)`. Verified only locally on a real NVIDIA GPU.
- Player UI pass (0.6): Library filter chips with live core state, Settings with Hub switch/edit/remove (address and port), in-game layout switch, fullscreen (F11/Esc), Multiview PiP / Side-by-Side / Grid 2×2, diagnostics overlay split into Emulation and Streaming (F3).
- Hotkeys (0.7.x): Controllers → Hotkeys configures fullscreen (F11), diagnostics (F3), snapshot (F5) and speed-up (Space); Esc is fixed. Built-in controller profiles are read-only; "Duplicate to edit" copies one.
- Background game (0.7.x): "← Library" pauses the running game and keeps it loaded (a shared Session stays shared); the Library shows "Now running" with Resume and Quit game, and starting another game asks first.
- Saves in the running game: "Manage saves" in the game panel shows slots and history live (snapshot, delete, restore, upload) and resolves conflicts inline; restore and upload apply as a game restart.
- Speed-up / fast-forward (0.7.x, [ADR 0018](adr/0018-player-speed-up.md)): Space or header button, speed 1.5× to 8× (default 2×), for libretro cores that allow it; Emulation settings for speed, speed-up on start and audio; usable while the Session is shared.
- Save sync with the Hub: sync before launch, auto checkpoint while playing (12 s after the last change, at most every 60 s), final sync on pause, stop and exit; pending uploads are kept per Hub and user.
- Conflict dialog with "Keep both, decide later" as default; per-game badge Synced / Sync pending / Conflict.
- Player identity: the FrameBeam logo is the window/taskbar icon, the Windows executable icon and the installer icon; the sidebar user block shows the Hub user name (handshake `user`, stored per Hub profile) above the device name.
- CLI: `saves list`, `save push`, `save pull`, `save resolve`.
- Share a running game as a Session (Private / Hub users / Invite only with invites, Join/Decline); "Sessions on this Hub" in the library.
- Watch a Session over direct WebRTC (H.264 + Opus); watch-only mode without a running game.
- Multiview: up to 4 surfaces (local game plus up to 3 remote Sessions, or 4 remote Sessions) as side-by-side, 2 x 2 grid or PiP; exactly one audible surface ("Audio here"); the picker lists all Sessions; diagnostics tab with RTT (negotiated DataChannel `fb-diag`). Encoding runs on a worker thread; under load the oldest pending video frame is dropped.
- Windows: the FFmpeg build enables the NVENC, QSV and AMF H.264 encoders (selected at runtime, software H.264 as fallback). A Windows test checks they are compiled in; opening them needs a GPU and is verified only locally.
- Diagnostics show the connection type (direct / relay) and the target bitrate; the host adapts the encoder bitrate to the worst viewer (AIMD). `FRAMEBEAM_FORCE_RELAY=1` (CLI `--force-relay`) forces the relay for testing.
- Save comfort: save history with Restore and manual snapshots, slot picker per game, live "save changed on another device" notice; "Upload save file…" in the game's save section (needs `saves_v4`; blocked while the game runs or the slot has pending changes; the local save is backed up first); the Player verifies the signed core index itself and marks a core "untrusted" otherwise.
- CLI: `session-share --synthetic`, `session-watch`.
- Credentials in the Credential Manager on Windows, in memory only on Linux (new pairing after restart).
- Redeem an invite code to join a Hub as a new user (no password).
- Upload ROMs to the Hub from the Player when the Hub allows user uploads.
- Firmware path for NDS: in native mode the Player fetches your firmware files from the Hub and shows "Firmware required" / "Firmware missing" instead of launching without them.
- Cores come from the Hub: the Player downloads the core for a system on demand (size and SHA-256 checked) into `cache/cores/<core-id>/<version>/<platform>/`; installers ship no cores. CLI: `fetch-core <system-id>`.
- Emulation page: core options per global/system level (locked options are hidden).
- Controllers page: SDL3 gamepads, built-in and user profiles, remapping, input test.
- Settings page: Appearance (Dark / Light / System).
- Windows installer (Inno Setup; the MSI with WiX is planned in 0.9), CI artifact `framebeam-player-windows-x64-setup`.
