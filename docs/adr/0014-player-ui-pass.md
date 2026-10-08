# ADR 0014: Player UI pass (0.6)

- Status: accepted (diagnostics design 3t-3y accepted by Fabio on 2026-10-08)
- Date: 2026-10-07
- Decided by: Fabio (proposal by the orchestrator; open design points answered with defaults, accepted with the merge)

## Context

Roadmap 0.6 (`docs/roadmap.md`) reworks the FrameBeam Player UI to the v4 design handoff (`docs/design/`): cyan tokens, clearer screens, live updates, and the in-game diagnostics split into Emulation and Streaming (design 3t-3y). The handoff left open points (`docs/design/README.md`, b, d, h, l, m-t). This ADR fixes the defaults the 0.6 code implements. Before 0.6 the Player had amber tokens, a single diagnostics panel, no fullscreen button and no way to edit a saved Hub.

## Decisions

### D1 Tokens

Signal Cyan `#3cbfd8` replaces amber everywhere in the Player. Warning uses the accent (warn background `#15262b`). The light palette stays derived (accent on light `#1a7f96`).

### D2 Settings page (open point h)

The section column of 3p is a jump list. The main area is one scrolling page with all sections. Clicking a section scrolls to it and highlights the item. No sub-pages.

### D3 Editing a saved Hub

Editing changes only host and port. `hubId`, pinned fingerprint, credential and Hub user stay. Validation rules and messages follow `docs/design/player.md` 3p; the validator is C++ and unit-tested. Editing the active Hub reconnects to the new address. A different certificate at the new address goes through the existing "certificate changed" flow and is never accepted silently.

### D4 Keys (open points d, r)

F11 toggles fullscreen. Esc leaves fullscreen when fullscreen, otherwise keeps its current meaning (pause). F3 toggles the diagnostics overlay. F5 saves a snapshot. These keys are handled by the Player and never forwarded to the core. The Hotkeys tab of 3f is not built (open).

### D5 Diagnostics persistence (open point s)

Three booleans in `PlayerSettings` (`settings/player.json`): overlay open (default false), Emulation section open (default true), Streaming section open (default true). Shared by window, multiview and fullscreen. Fullscreen never shows Streaming.

### D6 New measurements (open point m)

Actual emulation fps (frames over a sliding 1 s window); frame time split into emu (`retro_run`) and readback (hardware readback, 0 for software); a 5 s frame-time history for the sparkline; audio buffer fill in ms and underrun count; GPU/driver string from `HwRenderContext::glInfo()`; renderer in use and fallback reason.

### D7 Viewer report (open point n)

The existing `fb-diag` rx report gains the optional fields `fps` (decoded fps) and `dec` (decoder name). The host keeps the latest report per viewer (loss, kbit/s, fps, decoder) in `ViewerLinkStats`. The change is backward compatible: both fields are optional, older Players omit them and the host shows "—". `protocol_version` stays 1.

### D8 Codec names (open point o)

The UI shows the real FFmpeg codec name in use (for example `h264`, `h264_nvenc`, `libopenh264`), never a name that is not in use.

### D9 Connection pill (open point p)

Own row "Local" (neutral). "direct ..." shows "Direct" (ok). "relay (udp)" shows "Relayed (TURN)" and "relay (tcp)" shows "Relayed (TURN/TCP)" (both warn). Empty shows "Connecting" (neutral).

### D10 Fallback hint (open point q)

Shown when the core requested OpenGL but the software path runs. Reason per cause: "disabled by FRAMEBEAM_DISABLE_HW_RENDER", "no OpenGL 3.3 context", "framebuffer creation failed", "core asked for an unsupported context". The Resolution subline "N× requested · needs OpenGL" appears only when the core option value is above 1×.

### D11 Target bitrate (open point t)

The host line reads "6.0 / target 6.5 Mbit/s" while adaptive bitrate is active.

### D12 Layouts (open point b)

The in-game layout switch offers the layouts derived from the system manifest (screen count): Stacked / Side by side / Top only for 2-screen systems; 1-screen systems show no switch. The switch applies to the running game only; the default stays in Settings → Emulation (decision a, 2026-10-07).

### D13 Default Multiview (open point l)

The default gains "Grid 2×2".

### D14 Library "Needs attention" chip

Covers save conflict, hash mismatch, core missing or incompatible, and firmware missing. "Sync pending" is not attention. Chips (All / Ready / Needs attention / Not downloaded) combine with the search field.

### D15 Core state refresh

After a core download finishes (or the core cache changes), Library tiles and the detail pane re-evaluate the core state without a restart.

## Rejected

- Real sub-pages in Settings (3q style): one scrolling page with a jump list is simpler and keeps all sections visible.
- Editing the Hub fingerprint or credential together with the address: a new certificate must always go through the explicit "certificate changed" flow.
- A new `protocol_version` for the rx report: optional fields suffice.
- Forwarding F3/F5/F11 to the core: Player keys must not depend on core input.

## Consequences

- Screens, tokens and transitions follow the v4 design; screenshot tests (`FRAMEBEAM_SCREENSHOT_DIR`, `ctest -R screenshots`) render every screen at 1440x900. They produce the "after" set; the PR carries it. There is no "before" set.
- The host Player keeps the latest viewer report per viewer in memory; the Hub is unchanged and persists nothing new.
- Open: the Player does not know the Hub user's display name (the sidebar shows the device name); per-game settings are still "coming later"; built-in controller profiles are read-only; the Hotkeys tab is not built.
- Deferred without a milestone: the `PlayerController` split (moved from 0.1.1 to 0.6, not done in 0.6).
- Verification of look and feel, GPU values and real sessions is done locally by Fabio.
