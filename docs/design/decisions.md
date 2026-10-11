# Design decisions and open points

Decisions on the design handoff, deviations from the architecture and the open points per handoff round. Index and screen table: [README.md](README.md).

## Decisions (Fabio, 2026-10-07)

1. Accent is Signal Cyan: `#3cbfd8` (Player dark) and `#1a7f96` (Hub light; hover `#0f5a6b`, selection `#eef8fa`, chip `#ddf1f6`, border `#a6d9e6`; Player chip surface `#15262b`). The old amber `#e9b44c` is gone ([tokens.md](tokens.md)).
2. Cyan is intentionally also the warning color; there is no separate warning color. Warnings (conflict, hub too old, awaiting approval, relayed) therefore read as "needs attention", not as danger; errors keep their red. Amended 2026-10-11: plugin states use the amber warning tone (item 6); the built Player and Hub states stay as they are.
3. 3q "Hub Settings revised" supersedes 3o. 3o stays in the source only as reference and is marked superseded in the table.
4. Logo: concept 4a "Frame & Beam", palette "signal" ([logo.md](logo.md)).
5. Requested by Fabio (2026-10-07), implemented in 0.6 per [ADR 0014](../adr/0014-player-ui-pass.md) (accepted): split the in-game diagnostics into emulator diagnostics and streaming diagnostics, shown separately. The handoff draws this as 3t-3y; Fabio accepted the design on 2026-10-08.

## Decisions (Fabio, 2026-10-11, plugin handoff)

Source: Claude Design handoff "Plugins + SoulLink" and its correction round 2, imported in [plugins.md](plugins.md). Design only; plugin API v1 is not implemented. Alignment with the architecture: [../architecture/12-plugins.md](../architecture/12-plugins.md) §16.

6. Amber is the warning tone for plugin states (unverified, paused, pending, manual, HP mid), dark `#e5b45a` on `#2a2414`, light `#8a5a00` on `#f7ecd6` ([tokens.md](tokens.md)). It supersedes decision 2 ("cyan only") for those states; plugins pick the tone "warning", never a hex.
7. Player colours (Max blue, Lena pink, Jonas lime, Sam violet) identify people only; they never carry status and are neither cyan nor near the danger red. Cyan stays for open, selection and focus.
8. A death is grey + ✕ everywhere, never red. No red border flash; the emphasis is neutral (the HUD widget expands for 3 s, off with Reduce motion).
9. In-game views (header, side panel, HUD, tiles, fullscreen) stay dark in both themes; the light theme applies to Library, Settings, the run screen and the Hub. The light variant of the SoulLink panel (5b) is removed.
10. HUD widgets sit in three corners (bottom left, bottom right, top left); the top right is reserved for diagnostics.
11. Remote Multiview tiles show one plugin strip; when several plugins offer one, the user picks it in the tile menu. Your own tile has none.
12. Ctrl+H means one thing: show or hide this plugin's HUD widget. Expanding a widget is a click or controller A.
13. F4 hides all HUD widgets. It is free today: the Player's default hotkeys are F3, F5, F11, Space and Esc (`client/core/controllerprofiles.cpp`).
14. Notices outside the game are part of the permission `ui` ("Show panels in game"), not a permission of their own.
15. "Change game memory" (`memory.write`) is asked once per game; every change is written to the run log that all players see; a run can forbid it.
16. Origin: a plugin signed by FrameBeam is "Official" wherever it was installed from; "Local" means an unsigned file (needs Developer mode). A local copy never replaces the Hub version used by a run.
17. In-game surfaces (tab tooltip, overflow row, HUD widget, toast, tile strip) show a small "Unverified" or "Local" marker on hover or focus for non-official plugins; Official shows none.
18. The component sheet is the draft block catalog: anchored popover, segmented control, radio list, wizard stepper, avatar, HUD widget container, tile strip container, location status card were added to the blocks the screens use; every block has a controller-focus state; the stream size set (13-44) is separate from the ui set. The final catalog stays open (§16.8).
19. Controllers → Hotkeys (3f-4) lists FrameBeam keys first, then one group per plugin with its origin badge; a conflicting plugin key stays unassigned and shows "Already used by …".

Deviations from the architecture (plugin handoff), aligned in §16 in the same change: notices under `ui` (§16.5), `memory.write` consent per game, run log event and run rule (§16.5), origin rule (§16.10), HUD corners and F4 (§16.8). Not in §16 and still design only: the building blocks of item 18, the Hub status "Starting" and the paused run status (open points as, ar).

## Deviations from the architecture

Noted against the architecture; the status after the PoC is given per item. Checked against `09-ui-and-navigation.md`, `03-saves.md` and `10-identity-pairing-tls.md`; other files not read (points there: unclear).

Resolved history (first handoff round):

- Fingerprint confirmation (3a) and "Certificate trusted" without confirmation (3b): the Player shows both fingerprints and requires confirmation (ADR 0003, ADR 0009). The architecture applies.
- Appearance "Light | Dark | System" (3o/3q): implemented in Hub and Player; the second palette per product is derived, not designed ([tokens.md](tokens.md), ADR 0007 D7).
- Revision numbering "Rev N" (checkpoint) vs "vN" (history) and "Restore" in the Hub: numbered separately by ADR 0005; "Restore" was not in phase 3 and is now drawn in 3k/3s (0.4 feature, confirmation drawn).
- Terms: UI texts use "Session"; "Stream" does not occur. Labels such as "Allow users to upload games", "Invite only", "Current Checkpoint" are taken from the source.
- "Needs attention" count in 3c: the chip counts now add up (All 42 = Ready 37 + Needs attention 2 + Not downloaded 3); the 8 sample tiles do not mirror them (three attention states, two downloads), which is mock data only. What counts as "needs attention" was decided in ADR 0014 D14 (save conflict, hash mismatch, core missing or incompatible, firmware missing).
- Diagnostics as an equal-ranking tab next to Session and Multiview (3g-3i): accepted as drawn (ADR 0006 D6); the architecture only requires it to be optional/collapsible, which the collapsible panel in 3g satisfies. The newer handoff splits it into two separately collapsible sections, Emulation and Streaming (3t-3y; see Decisions item 5 and open points m-t).
- Not drawn but named by the architecture: Hub switcher dialog, admin setup and login in the Hub, metadata actions in the library entry (later), firmware block states in the library detail (3c shows only the success case). Settings → Hubs is now drawn in 3p (previously open); the multiview picker "Add to multiview" is now drawn in 3r.

Open points (new handoff):

a. (decided) In-game layout switch vs Emulation default: 3g-3i have a layout switch (Stacked / Side by side / Top only) and 3e has the option "DS screen layout" as default. Decided by Fabio on 2026-10-07: the in-game switch applies only to the running game; global defaults live in the settings (Emulation page). In Multiview the layout applies to all tiles.
b. "Side by side" and "Top only" are DS-specific. For 3DS (0.10 since 2026-10-09) the screens differ in size; the available layouts must come from the system manifest, not be hard-wired. (0.6: decided by ADR 0014 D12: layouts come from the system manifest; no switch for 1-screen systems.)
c. 3e options "Renderer" and "Internal resolution" need OpenGL hardware rendering, which the Player does not have until the OpenGL hardware rendering of 0.5. Only options the core reports are shown (the screen says so); until 0.5 these two are not offered or shown as unavailable (open: exact presentation). (Done: OpenGL hardware rendering exists since 0.5, ADR 0013.)
d. Esc exits fullscreen (toolbar "Exit fullscreen · F11 · Esc"). Esc may collide with game hotkeys (3f has a Hotkeys tab, not drawn); open. The code has only "Fullscreen on start" today (no button, no F11). (0.6: decided by ADR 0014 D4: F11 toggles fullscreen, Esc leaves it (else pause), never forwarded to the core; Hotkeys tab not built; built in 0.7.x, PR #59.)
e. (decided) Relay port range: decided by Fabio on 2026-10-07: the range follows the code, UDP 49160-49199 (ADR 0012); the design value 49160-49200 (3q) is a mock deviation. The port field is UDP 3478.
f. Mock values to ignore: melonDS DS "1.2.0" (the registry expects 1.4.0); "Bundled with Windows Player" in 3l (cores come from the Hub since 0.2, ADR 0010); GBA and SNES are example systems only (the second real system is 3DS with Azahar); the 3l toggle "Show example future systems" in the source script; "Last install failed ... HTTP 404" in 3q is mock text (a real update failure was observed separately, not part of the design).
g. Emulation (3e) no longer shows where an inherited value comes from (ADR 0007 inheritance chain Global → System/Core → Game). Only "changed vs default" (dot, "Reset", "Reset N changed") is shown. Decide whether that is enough; per-game overrides are "coming later". (Per-game overrides shipped in 0.10, ADR 0022 D6.)
h. 3p: the second column (Updates, Hubs, Appearance, Diagnostics) and the main area show all sections below each other. Open whether the column is only jump anchors or real sub-pages (in 3q they are real sub-pages); should be uniform. (0.6: decided by ADR 0014 D2: jump list plus one scrolling page.)
i. (decided 2026-10-08) Both colors stay on purpose: the brand cyan on light is `#1a8aa3` (logo, app icon, favicon), the UI accent on light is `#1a7f96` (Hub `app.css`, Player `Theme.qml`) because it has better text contrast. No value changes.
j. (partly resolved in 0.7, ADR 0015) The Hub retention text is now built from the real rules and the restore text states only what the service does. Remaining unverified mock texts in 3q and 3s: certificate "renews automatically 30 days before expiry", retention (all of the last 48 hours, then last per day for 30 days, then one per month; snapshots kept until deleted), "Players load the restored save on their next start", "not possible while a session of this game is running". Deleting a snapshot was mentioned but not drawn; implemented in 0.7.x with an inline confirmation like the restore one.
k. The logo sheet shows a favicon "FrameBeam Player · Web"; there is no web player. The Hotkeys tab (3f) and the "+" for a new save slot (3g, 3k, 3s) are drawn but have no screens. (Done: the Hotkeys tab in 0.7.x, PR #59; "+ New slot" in the Hub and the Player saves view in 0.7.x, ADR 0015 and the 0.7.x UI revision.)
l. "Default Multiview" in 3e offers only "Picture-in-Picture" and "Side-by-Side", while 3r adds the mode "Grid 2×2" (up to four tiles); whether the default can be the grid is open. (0.6: decided by ADR 0014 D13: Default Multiview gains Grid 2×2.)

Open points (diagnostics handoff, 3t-3y; checked against `client/media/mediastats.h`, `sessionhost.h`, `sessionviewer.h`, `videodecoder.cpp`, `videoencoder.cpp`, `client/emulation/hw_render.h`, `client/ui/audiooutput.cpp`, `SessionPanel.qml`, ADR 0006 D6 and ADR 0013):

m. Measured today vs new: `SessionStats` has fps, size, encoder name, codec, video/audio bitrate, RTT, connection type, target bitrate, loss (viewer only); `AvInfo.fps` gives the core's target fps. Not measured anywhere: actual emulation fps (the runner only emits a frame number), frame time with emu/readback split (`HwRenderContext::readback` is not timed), audio buffer fill and underruns (`audiooutput.cpp` only sets the buffer size and logs state changes). The GPU/driver line exists only as `HwRenderContext::glInfo()` ("vendor / renderer / version", for logs). All of these are new measurements for 0.6. (0.6: decided by ADR 0014 D6: new measurements added.)
n. Remote viewer rows in 3g/3v/3x ("Decoder on her Player", bitrate, fps, loss for Lena/Jonas): each viewer already sends loss and received video kbit/s once per second over `fb-diag` (`SessionViewer::sendRxReport`, ADR 0012 D5); the host receives them in `SessionHost::onRxReport` and emits `rxReportReceived`, but `ViewerLinkStats` (state, RTT, connection type) does not keep them yet. 0.6 should retain and expose that report instead of adding new telemetry; only the viewer's decoded fps (and decoder name) needs an extra field in the report. Not decided. (0.6: decided by ADR 0014 D7: rx report gains optional `fps` and `dec`, kept on the host.)
o. Encoder/decoder names: `videoencoder.cpp` tries `h264_nvenc`, `h264_qsv`, `h264_amf`, `libopenh264`, `libx264`; the Windows vcpkg FFmpeg enables `nvcodec`, `qsv` and `amf`, Linux uses the distro FFmpeg (HW encoders depend on it); a HW encoder opening on a real GPU is verified only locally by Fabio. `videodecoder.cpp` uses `avcodec_find_decoder(H264)` (software); the drawn "Decoder D3D11VA" (3v/3x) does not exist today and `SessionStats` has no decoder name. (0.6: decided by ADR 0014 D8: real FFmpeg codec names.)
p. Connection pill: `connectionType` is "direct (host|srflx|prflx)", "relay (udp|tcp)" or empty. "Local" (own row) is not a value; "Direct" does not tell LAN from internet; the TCP relay variant and "unknown" (empty) have no drawn pill. (0.6: decided by ADR 0014 D9: Local / Direct / Relayed (TURN) / Relayed (TURN/TCP) / Connecting.)
q. Fallback hint "OpenGL requested · fell back to software (GL 4.3 not available)" (3t-3y): ADR 0013 D1/D4 make 3.3 the tested minimum and fall back on missing QGuiApplication, `FRAMEBEAM_DISABLE_HW_RENDER=1` or failed context/FBO creation, today only logged. Per ADR 0013 D5 only melonDS DS's "compute" renderer needs GL 4.3; the OpenGL renderer does not. Open: which version and reason text to show per cause, and the line "3× requested · needs OpenGL" (the requested value must be exposed from the core options to QML). (0.6: decided by ADR 0014 D10: fallback hint with reason per cause.)
r. F3 for diagnostics: no F3 (or F11) binding exists in `client/ui` today; F5 is drawn for "Save snapshot". F3 may collide with game hotkeys like Esc (point d); the Hotkeys tab (3f) is not drawn and which keys the keyboard mapping forwards to the core is not specified. (0.6: decided by ADR 0014 D4: F3, F5, F11 handled by the Player. 0.7.x: Space toggles speed-up (fast-forward) and is never forwarded to the core, ADR 0018.)
s. Persistence of "open/closed per section": today `SessionPanel.showDiagnostics` is a non-persisted QML property (default hidden, one panel). Player settings live in `settings/player.json` (`PlayerSettings`) and Session settings in `player-settings.json`; where the three states (overlay, Emulation, Streaming) are stored, and whether window, multiview and fullscreen share them, is open. The initial states in the mock (e.g. 3g: Emulation collapsed, Streaming open) are mock defaults. (0.6: decided by ADR 0014 D5: three booleans in `PlayerSettings`.)
t. Target bitrate: the Streaming rows show only the measured bitrate and "adaptive bitrate", not the current target, although the roadmap (0.6, "From 0.4") requires connection type and target bitrate to be shown clearly. `SessionStats` has `videoBitrateKbps` and `targetBitrateKbps` separately; the host line must show both (for example "6.0 / target 6.5 Mbit/s"); exact presentation open. (0.6: decided by ADR 0014 D11: host line shows measured and target bitrate.)

Open points (revision handoff 2026-10-08, after a friends playtest; source `source/framebeam-designs-v4.dc.html`, handoff sections at lines 290 and 1102). Each open question of the handoff with the default decided by the orchestrator on 2026-10-08; these are working defaults, Fabio can overrule them. The handoff's in-game points (3g-2 to 3c-5) first, then the Turn 4 points (3c-2 to 3s-3).

In-game (Turn 5):

u. Esc: pauses as today; no second Esc to the Library (default, 2026-10-08). Esc keeps its fixed role from ADR 0014 D4 (leave fullscreen, else pause).
v. The paused state is not shown to viewers specially; they see the frozen picture (default, 2026-10-08).
w. The Multiview layout switch applies to all tiles (as decided 2026-10-07, item a); in the Session view the layout switch applies to this game only. The drawn state is correct (default, 2026-10-08).
x. Selecting a remote tile in Multiview does not route controller input; input always goes to your game (default, 2026-10-08).
y. Diagnostics section open/collapsed state (Emulation, Streaming) stays global, not per view (ADR 0014 D5; default, 2026-10-08).
z. A paused background game does not auto-quit on idle; a hub disconnect quits and saves as today (default, 2026-10-08).

Library, saves, settings, controllers, Hub (Turn 4):

aa. "Save sync pending" does not count as ready for "Ready first" (default, 2026-10-08; as drawn).
ab. Size sort: both directions, largest first and smallest first, like the other sorts (default, 2026-10-08; the draw shows only "largest first").
ac. The toolbar count reflects the filter ("37 of 42", "0 of 37 ready games" in the empty result) (default, 2026-10-08).
ad. Slot tabs: more than 3 slots collapse into the switcher "Main · 8 ▾" (threshold 3, default, 2026-10-08).
ae. Snapshots can be created while the game runs, from the last written save, as F5 does today (default, 2026-10-08).
af. "Play" is disabled while a restore, delete or upload confirmation is open (default, 2026-10-08).
ag. At 1280 the category chips of Emulation collapse to "+N more ▾" (alternative horizontal scroll not taken; default, 2026-10-08).
ah. The read-only "Version" row gets no Copy button (default, 2026-10-08).
ai. The "Button labels" override and "Auto" are saved per physical device, not per profile (default, 2026-10-08; as drawn).
aj. The input test grid has no stick cells for now (default, 2026-10-08).
ak. Hub upload size validation: no per-system size ranges yet (the manifest has no save-size data). The Hub keeps its current limits; the error "This file can’t be a Nintendo DS save." and the 512 B to 8 MB range are not built until the system manifest carries save sizes (default, 2026-10-08).
al. Admins cannot upload into another user's slot (default, 2026-10-08).
am. 3s-2: the CURRENT bar and the upload result keep the checkpoint label "Rev {n}"; history versions stay "v{n}" (separate numbering per ADR 0005); a unified numbering is open (2026-10-08).
an. Blocked upload while a session of the game runs is not built; the Hub has no running-session check per game yet (open, 2026-10-08).

Notes on the revision against the built state (not open questions): the drawn speed menu ends with "Unlimited" while the built set is 1.5×, 2×, 3×, 4×, 6×, 8× (ADR 0018) and the panel's speed select is removed in the new design; "jitter" in the multiview streaming lines is not measured today; keys 1-4 select tiles in the new multiview (v3: they moved the audio focus).

Open points (plugin handoff 2026-10-11; source [plugins.md](plugins.md), review round 1 and check of round 2). Working defaults of the design (as drawn), not yet decided by Fabio; to be confirmed with plugin API v1 (ADR 0023 stays proposed). SoulLink-only questions (5a rules vote, 5b team row, 5e game without map, 5f approvals) are in the SoulLink repository.

Hotkeys and rules new in round 2:

ao. Reserved FrameBeam keys beyond F3, F4, F5, F11, Space, Esc: Alt+1 … Alt+9 (select panel tabs) and Ctrl+Shift+R (reload plugin, Developer mode) are drawn in 3f-4 and 4d-4-2. Default: reserved; ADR 0014 follow-up needed (keyboard forwarding to the core is still unspecified, points d and r).
ap. "FrameBeam keys always win · plugins only suggest keys": a conflicting plugin key stays unassigned until the user chooses another (3f-4). 3f-4 also adds a device row "Mouse · 3DS touch". Default: as drawn.
aq. Uninstalling a plugin pauses the user's own participation in an active run ("paused for you until you reinstall"); the run stays on the Hub (4b-2). Default: as drawn.
ar. A paused run records no new events (events stay visible) and any member can pause ("paused by Lena"); Resume is available to members (4jh-2). Default: as drawn.
as. The Hub run status "Starting" (warning tone) while players are still joining (4jh-2). Default: as drawn.
at. Toasts: max 3 visible, the rest as "+1 earlier notice" (4g-2). Default: as drawn.
au. 4d-3-2 "Which copy runs" selector: outside runs the user picks Local or Hub copy; a run always uses the Hub version, locked by the run (§16.10). Default: as drawn.
av. Cyan also marks the "✓ Official" badge, the NEW tag and the source of important toasts; this is wider than "open, selection, focus" (the sheet's info tone adds "progress"). Default: as drawn; the warning tone and the origin text keep unverified distinct.

Questions written into the designs (round 1), with the drawn default:

aw. 4a: Official plugin updates install automatically or ask? Default: ask (update chip, "Update to 0.10.0").
ax. 4b: hide "Only in runs" for plugins without runs? Default: yes, only run plugins offer it.
ay. 4d: file extension `.fbplugin`. Default: as drawn.
az. 4d: console as a detachable window? Default: no, side panel tab (last, Developer mode only).
ba. 4f/4g: max one sidebar entry per plugin; what if two plugins both want "Runs"? Default: one entry per plugin, no merge rule drawn (open).
bb. 4h: does "Play without run" switch the save slot? Default: run saves have their own slot "Run · <name>"; "Play without run" uses the normal slot (open: automatic switch).
bc. 4ih: can non-admins upload? Default: a Hub setting "Users can upload" (Lena uploaded); the admin confirms the permissions.
bd. 4j: who may delete a run? Default: creator and admin.
be. Hub run page as a browser source (SoulLink 5gs): revocable token link versus Hub login. Default: token link as drawn; needs a security decision before it is built.
bf. Toasts of a run event while in fullscreen (SoulLink 5h): shown by the plugin's Notices setting (all, important, deaths, off), not only as a HUD flash. Default: as drawn (4g-2 stack); open.
