# Design specification

Purpose: token-efficient preparation of the UI prototype, so that implementation agents read only the necessary part per screen. The prototype is a mock-up; where it contradicts, the architecture applies (`../architecture/09-ui-and-navigation.md`), deviations see below. Mock values (e.g. core version "1.2.0", user names, device names, versions, addresses) are illustrative; the seeded registry expects melonDS DS 1.4.0. Hosts in the mock are generic placeholders (`hub.example.com`, `hub.local`, `lena.example.net`).

Source: Claude Design handoff "FrameBeam Designs v3" (second round, accepted by Fabio on 2026-10-07; the diagnostics screens 3t-3y and the updated 3g/3h come from a later handoff that Fabio has not accepted yet); `source/framebeam-designs-v3.dc.html` is the current state, older versions are not carried over. Two further sources belong to it: `source/framebeam-logo.dc.html` (logo sheet) and `source/framebeam-mark.dc.html` (mark component), see [logo.md](logo.md). The files stay unchanged and do not render standalone (runtime `support.js` is missing from the repository). 25 screens (3a-3y) at 1440 x 900 plus an intro block for 3t-3y (line 669); mock data in the script from line 1362 (`renderVals` at line 1626; the diagnostics mock data are in `dgVals` at line 1573).

Language note: the source texts are English throughout (the earlier German texts and labels are gone). Screen labels in the source are `data-screen-label` values; the heading names in the specification files may be shortened (3l appears in the source as "Hub Systems and Cores", 3q as "Hub Settings revised").

Reading note: read only the necessary file or section. Open the source HTML only for details in the given line range (lines = screen container from `data-screen-label` to the end; anchors `#3x` = block ID in the source). The screens appear in the source in this order: 3a-3f, 3p, 3g, 3h, 3r, 3i, 3t-3y, 3j, 3k, 3s, 3l, 3m, 3n, 3o, 3q.

- `tokens.md`: colors, typography, spacing, radii, shadows, layout dimensions (Player dark, Hub light).
- `player.md`: screens 3a-3i, 3p, 3r, 3t-3y (FrameBeam Player, Qt 6 QML).
- `hub.md`: screens 3j-3o, 3q, 3s (FrameBeam Hub web interface, `html/template` + htmx).
- `logo.md`: logo concept, tones, sizes, wordmark.

## Screens

| Screen | Specification | Source (anchor, lines) | Phase / version |
|---|---|---|---|
| 3a Player Connection | [player.md](player.md#3a-player-connection) | `#3a`, 31-53 | 2 |
| 3b Player Pairing | [player.md](player.md#3b-player-pairing) | `#3b`, 59-73 | 2 |
| 3c Player Library | [player.md](player.md#3c-player-library) | `#3c`, 79-178 | 2; filter chips and pills in the 0.6 Player UI pass |
| 3d Player Save Conflict | [player.md](player.md#3d-player-save-conflict) | `#3d`, 184-212 | 3 |
| 3e Player Emulation | [player.md](player.md#3e-player-emulation) | `#3e`, 218-294 | 5; rebuilt: 0.6 Player UI pass |
| 3f Player Controllers | [player.md](player.md#3f-player-controllers) | `#3f`, 300-374 | 5; rebuilt: 0.6 Player UI pass |
| 3p Player Settings (new) | [player.md](player.md#3p-player-settings) | `#3p`, 380-441 | 0.6 Player UI pass; hub edit (address, port): 0.4 feature, UI in 0.6 |
| 3g Player Session | [player.md](player.md#3g-player-session) | `#3g`, 447-518 | 4; layout switch, fullscreen: 0.6 Player UI pass; save slot and snapshot, Direct/Relayed: 0.4 features, UI in 0.6; diagnostics overlay drawn in 3t-3y |
| 3h Player Side-by-Side | [player.md](player.md#3h-player-side-by-side) | `#3h`, 524-572 | 4; per-tile connection type: 0.4 feature, UI in 0.6; diagnostics panel drawn in 3x |
| 3r Player Multiview Grid (new) | [player.md](player.md#3r-player-multiview-grid) | `#3r`, 578-616 | 0.4 feature (up to 4 tiles), UI in 0.6 |
| 3i Player PiP | [player.md](player.md#3i-player-pip) | `#3i`, 622-666 | 4 |
| 3t Player Diagnostics Software (new) | [player.md](player.md#3t-player-diagnostics-software) | `#3t`, 680-699 | 0.6 Player UI pass |
| 3u Player Diagnostics OpenGL (new) | [player.md](player.md#3u-player-diagnostics-opengl) | `#3u`, 703-722 | 0.6 Player UI pass |
| 3v Player Diagnostics Session (new) | [player.md](player.md#3v-player-diagnostics-session) | `#3v`, 726-745 | 0.6 Player UI pass |
| 3w Player Diagnostics Fallback (new) | [player.md](player.md#3w-player-diagnostics-fallback) | `#3w`, 749-768 | 0.6 Player UI pass |
| 3x Player Multiview Diagnostics (new) | [player.md](player.md#3x-player-multiview-diagnostics) | `#3x`, 772-780 | 0.6 Player UI pass |
| 3y Player Fullscreen Diagnostics (new) | [player.md](player.md#3y-player-fullscreen-diagnostics) | `#3y`, 784-810 | 0.6 Player UI pass |
| 3j Hub Library | [hub.md](hub.md#3j-hub-library) | `#3j`, 816-864 | 1 |
| 3k Hub Saves | [hub.md](hub.md#3k-hub-saves) | `#3k`, 870-954 | 3; slots, snapshot markers: 0.4 features, UI implemented in 0.7 |
| 3s Hub Saves Slots (new) | [hub.md](hub.md#3s-hub-saves-slots) | `#3s`, 960-1018 | 0.4 feature (slots, snapshots, restore, retention), UI implemented in 0.7 |
| 3l Hub Systems and Cores | [hub.md](hub.md#3l-hub-systems-and-cores) | `#3l`, 1024-1132 | 5; rebuilt: 0.7 Hub UI pass (implemented) |
| 3m Hub Clients | [hub.md](hub.md#3m-hub-clients) | `#3m`, 1138-1174 | 1 |
| 3n Hub Users | [hub.md](hub.md#3n-hub-users) | `#3n`, 1180-1217 | 5 |
| 3o Hub Settings (superseded by 3q) | [hub.md](hub.md#3o-hub-settings-superseded-by-3q) | `#3o`, 1223-1251 | 1 (parts), 5; reference only |
| 3q Hub Settings revised (new) | [hub.md](hub.md#3q-hub-settings-revised) | `#3q`, 1257-1356 | 0.7 Hub UI pass (implemented); Network section (relay, public address): 0.4 feature, UI implemented in 0.7 |

Phases per `../workflow.md` (phase plan); 0.4-0.8 are the versions of the roadmap (`../roadmap.md`). Borderline cases: 3o (reference only) phase 1 covered only Hub name, address, transport and certificate, admin account; appearance (dark/light) and "Allow users to upload games" phase 5 (requires users); these now live in 3q. 3m is phase 1 (Allow/Decline, Revoke); the assignment to an existing user (architecture 10) belongs to it as well, but requires created users (until phase 5 only admin). 3k and 3d both belong to phase 3, although 3k is a Hub page. 3l phase 5 (firmware path); nav badges of the Hub shell ("1 conflict", "2 issues", "1 request", Settings "1 update") follow the respective phases. The Hub shell (sidebar, page header, tables) is created with the first Hub page in phase 1. "UI in the 0.6/0.7 pass" means: the feature (protocol, storage, diagnostics) arrives in 0.4, the screen in the named UI pass.

## Decisions (Fabio, 2026-10-07)

1. Accent is Signal Cyan: `#3cbfd8` (Player dark) and `#1a7f96` (Hub light; hover `#0f5a6b`, selection `#eef8fa`, chip `#ddf1f6`, border `#a6d9e6`; Player chip surface `#15262b`). The old amber `#e9b44c` is gone ([tokens.md](tokens.md)).
2. Cyan is intentionally also the warning color; there is no separate warning color. Warnings (conflict, hub too old, awaiting approval, relayed) therefore read as "needs attention", not as danger; errors keep their red.
3. 3q "Hub Settings revised" supersedes 3o. 3o stays in the source only as reference and is marked superseded in the table.
4. Logo: concept 4a "Frame & Beam", palette "signal" ([logo.md](logo.md)).
5. Requested by Fabio (2026-10-07), not yet accepted as a design: split the in-game diagnostics into emulator diagnostics and streaming diagnostics, shown separately. The handoff draws this as 3t-3y; the specification in `player.md` is a proposal until Fabio accepts it.

## Deviations from the architecture

Noted against the architecture; the status after the PoC is given per item. Checked against `09-ui-and-navigation.md`, `03-saves.md` and `10-identity-pairing-tls.md`; other files not read (points there: unclear).

Resolved history (first handoff round):

- Fingerprint confirmation (3a) and "Certificate trusted" without confirmation (3b): the Player shows both fingerprints and requires confirmation (ADR 0003, ADR 0009). The architecture applies.
- Appearance "Light | Dark | System" (3o/3q): implemented in Hub and Player; the second palette per product is derived, not designed ([tokens.md](tokens.md), ADR 0007 D7).
- Revision numbering "Rev N" (checkpoint) vs "vN" (history) and "Restore" in the Hub: numbered separately by ADR 0005; "Restore" was not in phase 3 and is now drawn in 3k/3s (0.4 feature, confirmation drawn).
- Terms: UI texts use "Session"; "Stream" does not occur. Labels such as "Allow users to upload games", "Invite only", "Current Checkpoint" are taken from the source.
- "Needs attention" count in 3c: the chip counts now add up (All 42 = Ready 37 + Needs attention 2 + Not downloaded 3); the 8 sample tiles do not mirror them (three attention states, two downloads), which is mock data only. What counts as "needs attention" stays open.
- Diagnostics as an equal-ranking tab next to Session and Multiview (3g-3i): accepted as drawn (ADR 0006 D6); the architecture only requires it to be optional/collapsible, which the collapsible panel in 3g satisfies. The newer handoff splits it into two separately collapsible sections, Emulation and Streaming (3t-3y; see Decisions item 5 and open points m-t).
- Not drawn but named by the architecture: Hub switcher dialog, admin setup and login in the Hub, metadata actions in the library entry (later), firmware block states in the library detail (3c shows only the success case). Settings → Hubs is now drawn in 3p (previously open); the multiview picker "Add to multiview" is now drawn in 3r.

Open points (new handoff):

a. (decided) In-game layout switch vs Emulation default: 3g-3i have a layout switch (Stacked / Side by side / Top only) and 3e has the option "DS screen layout" as default. Decided by Fabio on 2026-10-07: the in-game switch applies only to the running game; global defaults live in the settings (Emulation page). In Multiview the layout applies to all tiles.
b. "Side by side" and "Top only" are DS-specific. For 3DS (0.8) the screens differ in size; the available layouts must come from the system manifest, not be hard-wired.
c. 3e options "Renderer" and "Internal resolution" need OpenGL hardware rendering, which the Player does not have until the OpenGL hardware rendering of 0.5. Only options the core reports are shown (the screen says so); until 0.5 these two are not offered or shown as unavailable (open: exact presentation).
d. Esc exits fullscreen (toolbar "Exit fullscreen · F11 · Esc"). Esc may collide with game hotkeys (3f has a Hotkeys tab, not drawn); open. The code has only "Fullscreen on start" today (no button, no F11).
e. (decided) Relay port range: decided by Fabio on 2026-10-07: the range follows the code, UDP 49160-49199 (ADR 0012); the design value 49160-49200 (3q) is a mock deviation. The port field is UDP 3478.
f. Mock values to ignore: melonDS DS "1.2.0" (the registry expects 1.4.0); "Bundled with Windows Player" in 3l (cores come from the Hub since 0.2, ADR 0010); GBA and SNES are example systems only (the second real system is 3DS with Azahar); the 3l toggle "Show example future systems" in the source script; "Last install failed ... HTTP 404" in 3q is mock text (a real update failure was observed separately, not part of the design).
g. Emulation (3e) no longer shows where an inherited value comes from (ADR 0007 inheritance chain Global → System/Core → Game). Only "changed vs default" (dot, "Reset", "Reset N changed") is shown. Decide whether that is enough; per-game overrides are "coming later".
h. 3p: the second column (Updates, Hubs, Appearance, Diagnostics) and the main area show all sections below each other. Open whether the column is only jump anchors or real sub-pages (in 3q they are real sub-pages); should be uniform.
i. Logo light accent is `#1a8aa3`, the Hub UI accent is `#1a7f96`: small inconsistency, one of them should win (open).
j. (partly resolved in 0.7, ADR 0014) The Hub retention text is now built from the real rules and the restore text states only what the service does. Remaining unverified mock texts in 3q and 3s: certificate "renews automatically 30 days before expiry", retention (all of the last 48 hours, then last per day for 30 days, then one per month; snapshots kept until deleted), "Players load the restored save on their next start", "not possible while a session of this game is running". Deleting a snapshot is mentioned but not drawn.
k. The logo sheet shows a favicon "FrameBeam Player · Web"; there is no web player. The Hotkeys tab (3f) and the "+" for a new save slot (3g, 3k, 3s) are drawn but have no screens.
l. "Default Multiview" in 3e offers only "Picture-in-Picture" and "Side-by-Side", while 3r adds the mode "Grid 2×2" (up to four tiles); whether the default can be the grid is open.

Open points (diagnostics handoff, 3t-3y; checked against `client/media/mediastats.h`, `sessionhost.h`, `sessionviewer.h`, `videodecoder.cpp`, `videoencoder.cpp`, `client/emulation/hw_render.h`, `client/ui/audiooutput.cpp`, `SessionPanel.qml`, ADR 0006 D6 and ADR 0013):

m. Measured today vs new: `SessionStats` has fps, size, encoder name, codec, video/audio bitrate, RTT, connection type, target bitrate, loss (viewer only); `AvInfo.fps` gives the core's target fps. Not measured anywhere: actual emulation fps (the runner only emits a frame number), frame time with emu/readback split (`HwRenderContext::readback` is not timed), audio buffer fill and underruns (`audiooutput.cpp` only sets the buffer size and logs state changes). The GPU/driver line exists only as `HwRenderContext::glInfo()` ("vendor / renderer / version", for logs). All of these are new measurements for 0.6.
n. Remote viewer rows in 3g/3v/3x ("Decoder on her Player", bitrate, fps, loss for Lena/Jonas): each viewer already sends loss and received video kbit/s once per second over `fb-diag` (`SessionViewer::sendRxReport`, ADR 0012 D5); the host receives them in `SessionHost::onRxReport` and emits `rxReportReceived`, but `ViewerLinkStats` (state, RTT, connection type) does not keep them yet. 0.6 should retain and expose that report instead of adding new telemetry; only the viewer's decoded fps (and decoder name) needs an extra field in the report. Not decided.
o. Encoder/decoder names: `videoencoder.cpp` tries `h264_nvenc`, `h264_qsv`, `h264_amf`, `libopenh264`, `libx264`; the Windows vcpkg FFmpeg enables `nvcodec`, `qsv` and `amf`, Linux uses the distro FFmpeg (HW encoders depend on it); a HW encoder opening on a real GPU is verified only locally by Fabio. `videodecoder.cpp` uses `avcodec_find_decoder(H264)` (software); the drawn "Decoder D3D11VA" (3v/3x) does not exist today and `SessionStats` has no decoder name.
p. Connection pill: `connectionType` is "direct (host|srflx|prflx)", "relay (udp|tcp)" or empty. "Local" (own row) is not a value; "Direct" does not tell LAN from internet; the TCP relay variant and "unknown" (empty) have no drawn pill.
q. Fallback hint "OpenGL requested · fell back to software (GL 4.3 not available)" (3t-3y): ADR 0013 D1/D4 make 3.3 the tested minimum and fall back on missing QGuiApplication, `FRAMEBEAM_DISABLE_HW_RENDER=1` or failed context/FBO creation, today only logged. Per ADR 0013 D5 only melonDS DS's "compute" renderer needs GL 4.3; the OpenGL renderer does not. Open: which version and reason text to show per cause, and the line "3× requested · needs OpenGL" (the requested value must be exposed from the core options to QML).
r. F3 for diagnostics: no F3 (or F11) binding exists in `client/ui` today; F5 is drawn for "Save snapshot". F3 may collide with game hotkeys like Esc (point d); the Hotkeys tab (3f) is not drawn and which keys the keyboard mapping forwards to the core is not specified.
s. Persistence of "open/closed per section": today `SessionPanel.showDiagnostics` is a non-persisted QML property (default hidden, one panel). Player settings live in `settings/player.json` (`PlayerSettings`) and Session settings in `player-settings.json`; where the three states (overlay, Emulation, Streaming) are stored, and whether window, multiview and fullscreen share them, is open. The initial states in the mock (e.g. 3g: Emulation collapsed, Streaming open) are mock defaults.
t. Target bitrate: the Streaming rows show only the measured bitrate and "adaptive bitrate", not the current target, although the roadmap (0.6, "From 0.4") requires connection type and target bitrate to be shown clearly. `SessionStats` has `videoBitrateKbps` and `targetBitrateKbps` separately; the host line must show both (for example "6.0 / target 6.5 Mbit/s"); exact presentation open.
