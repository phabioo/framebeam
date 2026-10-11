# Design specification

Purpose: token-efficient preparation of the UI prototype, so that implementation agents read only the necessary part per screen. The prototype is a mock-up; where it contradicts, the architecture applies (`../architecture/09-ui-and-navigation.md`), deviations see below. Mock values (e.g. core version "1.2.0", user names, device names, versions, addresses) are illustrative; the seeded registry expects melonDS DS 1.4.0. Hosts in the mock are generic placeholders (`hub.example.com`, `hub.local`, `lena.example.net`).

Source: Claude Design handoff "FrameBeam Designs v3" (second round, accepted by Fabio on 2026-10-07; the diagnostics screens 3t-3y and the updated 3g/3h come from a later handoff, accepted by Fabio on 2026-10-08); `source/framebeam-designs-v3.dc.html` is the current state, older versions are not carried over. Two further sources belong to it: `source/framebeam-logo.dc.html` (logo sheet) and `source/framebeam-mark.dc.html` (mark component), see [logo.md](logo.md). The files stay unchanged and do not render standalone (runtime `support.js` is missing from the repository). 25 screens (3a-3y) at 1440 x 900 plus an intro block for 3t-3y (line 669); mock data in the script from line 1362 (`renderVals` at line 1626; the diagnostics mock data are in `dgVals` at line 1573).

Revision handoff (Claude Design, 08.10.2026, after a friends playtest): `source/framebeam-designs-v4.dc.html` (2959 lines). It contains the unchanged Turn 3 (all v3 screens, shifted down: 3a starts at line 1164) plus two new turns: Turn 4 (revised Player and Hub screens with suffix `-2`, `-3`, `-4`; component sheet at line 1001, handoff at line 1102) and Turn 5 (in-game screens, component sheet at line 229, handoff at line 290). Mock data of the new turns: `t4Vals` (line 2761), `t5Vals` (line 2856), diagnostics values in `dgVals` (line 2708). **v4 supersedes v3 for the revised screens only**; the v3 file and the v3 sections stay as reference for the originals (open them only to compare). Not revised screens (3a, 3b, 3d, 3j-3q) keep their v3 specification. The source has the same limits as v3: it does not render standalone. The component files it imports are listed under "Component sheets". Mock values (user names Max, Lena, Jonas, Sam, Sarah, game titles, `hub.example.com`) are placeholders as before; the fictional core "Lanterna 0.3.0" in the core-agnostic sheet is an example only. The playtest uploads of the handoff are not part of the repository.

Language note: the source texts are English throughout (the earlier German texts and labels are gone). Screen labels in the source are `data-screen-label` values; the heading names in the specification files may be shortened (3l appears in the source as "Hub Systems and Cores", 3q as "Hub Settings revised").

Reading note: read only the necessary file or section. Open the source HTML only for details in the given line range (lines = screen container from `data-screen-label` to the end; anchors `#3x` = block ID in the source). The screens appear in the source in this order: 3a-3f, 3p, 3g, 3h, 3r, 3i, 3t-3y, 3j, 3k, 3s, 3l, 3m, 3n, 3o, 3q.

- `tokens.md`: colors, typography, spacing, radii, shadows, layout dimensions (Player dark, Hub light).
- `player.md`: screens 3a-3i, 3p, 3r, 3t-3y and the revisions 3c-2 to 3c-5, 3e-2, 3f-2/3, 3g-2/3, 3h-2, 3i-2, 3p-2, 3r-2, 3t-2, 3x-2 plus the component specs (FrameBeam Player, Qt 6 QML).
- `hub.md`: screens 3j-3o, 3q, 3s and the revision 3s-2/3 (FrameBeam Hub web interface, `html/template` + htmx).
- `plugins.md`: plugin platform screens (4a-4j, 4ih, 4jh, 3f-4), building-block catalog, map view, HUD, tab and tile-strip specs (plugin handoff 2026-10-11; design only, plugin API v1 is not implemented).
- `logo.md`: logo concept, tones, sizes, wordmark.
- `decisions.md`: decisions, deviations from the architecture, open points per handoff round (incl. the defaults decided for the revision handoff of 2026-10-08).

## Screens

| Screen | Specification | Source (anchor, lines) | Phase / version |
|---|---|---|---|
| 3a Player Connection | [player.md](player.md#3a-player-connection) | `#3a`, 31-53 | 2 |
| 3b Player Pairing | [player.md](player.md#3b-player-pairing) | `#3b`, 59-73 | 2 |
| 3c Player Library | [player.md](player.md#3c-player-library) | `#3c`, 79-178 | 2; filter chips and pills built in 0.6 |
| 3d Player Save Conflict | [player.md](player.md#3d-player-save-conflict) | `#3d`, 184-212 | 3 |
| 3e Player Emulation | [player.md](player.md#3e-player-emulation) | `#3e`, 218-294 | 5; rebuilt in 0.6 (built, [ADR 0014](../adr/0014-player-ui-pass.md)) |
| 3f Player Controllers | [player.md](player.md#3f-player-controllers) | `#3f`, 300-374 | 5; rebuilt in 0.6 (built, [ADR 0014](../adr/0014-player-ui-pass.md)) |
| 3p Player Settings (new) | [player.md](player.md#3p-player-settings) | `#3p`, 380-441 | 0.6 Player UI pass; hub edit (address, port): 0.4 feature, UI in 0.6 |
| 3g Player Session | [player.md](player.md#3g-player-session) | `#3g`, 447-518 | 4; layout switch, fullscreen: 0.6 Player UI pass; save slot and snapshot, Direct/Relayed: 0.4 features, UI in 0.6; diagnostics overlay drawn in 3t-3y |
| 3h Player Side-by-Side | [player.md](player.md#3h-player-side-by-side) | `#3h`, 524-572 | 4; per-tile connection type: 0.4 feature, UI in 0.6; diagnostics panel drawn in 3x |
| 3r Player Multiview Grid (new) | [player.md](player.md#3r-player-multiview-grid) | `#3r`, 578-616 | 0.4 feature (up to 4 tiles), UI in 0.6 |
| 3i Player PiP | [player.md](player.md#3i-player-pip) | `#3i`, 622-666 | 4 |
| 3t Player Diagnostics Software (new) | [player.md](player.md#3t-player-diagnostics-software) | `#3t`, 680-699 | 0.6 Player UI pass (built) |
| 3u Player Diagnostics OpenGL (new) | [player.md](player.md#3u-player-diagnostics-opengl) | `#3u`, 703-722 | 0.6 Player UI pass (built) |
| 3v Player Diagnostics Session (new) | [player.md](player.md#3v-player-diagnostics-session) | `#3v`, 726-745 | 0.6 Player UI pass (built) |
| 3w Player Diagnostics Fallback (new) | [player.md](player.md#3w-player-diagnostics-fallback) | `#3w`, 749-768 | 0.6 Player UI pass (built) |
| 3x Player Multiview Diagnostics (new) | [player.md](player.md#3x-player-multiview-diagnostics) | `#3x`, 772-780 | 0.6 Player UI pass (built) |
| 3y Player Fullscreen Diagnostics (new) | [player.md](player.md#3y-player-fullscreen-diagnostics) | `#3y`, 784-810 | 0.6 Player UI pass (built) |
| 3j Hub Library | [hub.md](hub.md#3j-hub-library) | `#3j`, 816-864 | 1 |
| 3k Hub Saves | [hub.md](hub.md#3k-hub-saves) | `#3k`, 870-954 | 3; slots, snapshot markers: 0.4 features, UI implemented in 0.7 |
| 3s Hub Saves Slots (new) | [hub.md](hub.md#3s-hub-saves-slots) | `#3s`, 960-1018 | 0.4 feature (slots, snapshots, restore, retention), UI implemented in 0.7 |
| 3l Hub Systems and Cores | [hub.md](hub.md#3l-hub-systems-and-cores) | `#3l`, 1024-1132 | 5; rebuilt: 0.7 Hub UI pass (implemented) |
| 3m Hub Clients | [hub.md](hub.md#3m-hub-clients) | `#3m`, 1138-1174 | 1 |
| 3n Hub Users | [hub.md](hub.md#3n-hub-users) | `#3n`, 1180-1217 | 5 |
| 3o Hub Settings (superseded by 3q) | [hub.md](hub.md#3o-hub-settings-superseded-by-3q) | `#3o`, 1223-1251 | 1 (parts), 5; reference only |
| 3q Hub Settings revised (new) | [hub.md](hub.md#3q-hub-settings-revised) | `#3q`, 1257-1356 | 0.7 Hub UI pass (implemented); Network section (relay, public address): 0.4 feature, UI implemented in 0.7 |

### Revision screens (v4, 08.10.2026)

Lines refer to `source/framebeam-designs-v4.dc.html`; anchors `#id` are block IDs. Each revised screen supersedes the v3 original named in the last column; the original rows above stay valid for everything the revision does not change.

| Screen | Specification | Source (anchor, lines) | Version | Revises |
|---|---|---|---|---|
| 3c-2 Player Library revised (also 1280 and empty-result variants) | [player.md](player.md#3c-2-player-library-revised) | `#3c-2`, 330-407; `#3c-2-1280`, 408-492; `#3c-2-empty`, 493-534 | 0.7.x revision | 3c |
| 3c-3 Player Saves view | [player.md](player.md#3c-3-player-saves-view) | `#3c-3`, 535-572; states `#3c-states`, 612-715 | 0.7.x revision | 3c |
| 3c-4 Player Upload confirmation | [player.md](player.md#3c-4-player-upload-confirmation) | `#3c-4`, 573-611 | 0.7.x revision | 3c |
| 3c-5 Player Library with a running game (+ quit and start dialog) | [player.md](player.md#3c-5-player-library-with-a-running-game) | `#3c-5`, 164-208; `#3c-5-dialog`, 209-228 | 0.7.x revision | 3c-2 |
| 3e-2 Player Emulation revised (also 1280) | [player.md](player.md#3e-2-player-emulation-revised) | `#3e-2`, 720-748; `#3e-2-1280`, 749-776 | 0.7.x revision | 3e |
| 3p-2 Player Settings revised | [player.md](player.md#3p-2-player-settings-revised) | `#3p-2`, 777-814 | 0.7.x revision | 3p |
| 3f-2 Player Controllers Xbox | [player.md](player.md#3f-2-3f-3-player-controllers-revised) | `#3f-2`, 818-864 | 0.7.x revision | 3f |
| 3f-3 Player Controllers DualSense | [player.md](player.md#3f-2-3f-3-player-controllers-revised) | `#3f-3`, 865-919 | 0.7.x revision | 3f |
| 3g-2 / 3g-3 Player Session (+ 1280 variant) | [player.md](player.md#3g-2-3g-3-player-session-revised) | `#3g-2`, 37-45; `#3g-3`, 46-54; `#3g-2-1280`, 67-79 | 0.7.x revision | 3g |
| 3r-2 Player Multiview Grid (+ 1280 variant) | [player.md](player.md#3r-2-3h-2-3i-2-player-multiview-revised) | `#3r-2`, 80-106; `#3r-2-1280`, 148-163 | 0.7.x revision | 3r |
| 3h-2 Player Multiview Side by side | [player.md](player.md#3r-2-3h-2-3i-2-player-multiview-revised) | `#3h-2`, 122-133 | 0.7.x revision | 3h |
| 3i-2 Player Multiview PiP | [player.md](player.md#3r-2-3h-2-3i-2-player-multiview-revised) | `#3i-2`, 134-147 | 0.7.x revision | 3i |
| 3t-2 Player Diagnostics (single game) | [player.md](player.md#3t-2-3x-2-player-diagnostics-revised) | `#3t-2`, 55-66 | 0.7.x revision | 3t-3w, 3y |
| 3x-2 Player Multiview Diagnostics | [player.md](player.md#3t-2-3x-2-player-diagnostics-revised) | `#3x-2`, 107-121 | 0.7.x revision | 3x |
| 3s-2 Hub Saves upload | [hub.md](hub.md#3s-2-3s-3-hub-saves-upload) | `#3s-2`, 924-963 | 0.7.x revision | 3s, 3k |
| 3s-3 Hub Saves upload result (+ states) | [hub.md](hub.md#3s-2-3s-3-hub-saves-upload) | `#3s-3`, 964-990; `#3s-3-states`, 991-1000 | 0.7.x revision | 3s |

Turn 3 inside v4 (the unchanged v3 screens) starts at line 1164 (3a); the v3 line ranges in the table above refer to `framebeam-designs-v3.dc.html` and stay correct for that file.

### Component sheets

Shared parts of the revision are real components (imported by the v4 file as `dc-import`, names renamed to the file names): `source/settings-row.dc.html` (SettingsRow), `source/save-row.dc.html` (SaveRow, dark and light), `source/pad-glyph.dc.html` (PadGlyph), `source/input-test.dc.html` (InputTest), `source/game-header.dc.html` (GameHeader), `source/game-panel.dc.html` (GamePanel), `source/diag-overlay.dc.html` (DiagOverlay), `source/support.js` (design runtime shared by all `.dc.html` files, not product code) and the existing `source/framebeam-mark.dc.html`. Sheets in v4: in-game components (GameHeader variants, anchored popovers, GamePanel, tiles and Now running) at lines 229-289; SettingsRow, core-agnostic options, SaveRow, sort and Ready first, D-pad and glyphs at lines 1001-1101. Specs per component: [player.md](player.md#components-revision-v4); new tokens: [tokens.md](tokens.md).

### Plugin screens (2026-10-11)

Claude Design handoff "Plugins + SoulLink" (round 1, then correction round 2; accepted by Fabio on 2026-10-11). Only the platform part is imported, the SoulLink screens (5a-5i) belong to the `framebeam-plugin-soullink` repository. Files in `source/`: `plugins-4a-4d.dc.html`, `plugins-4e-4h.dc.html`, `plugins-4i-4j-sheet.dc.html` (round 1), `plugins-round-2.dc.html` (round 2) and the components `plugin-settings-screen`, `block-sheet`, `region-map`, `creature-icon` (plus the existing `game-header`, `game-panel`, `diag-overlay`, `framebeam-mark`). **Ids with `-2` supersede the round-1 id**; round-1 artboards marked "reference only" still show DS screens or old wording. Specification: [plugins.md](plugins.md); decisions (items 6-19) and open points (ao-bf): [decisions.md](decisions.md); new tokens: [tokens.md](tokens.md). Mock values (3DS game "Coral Saga: Ember", creatures, players, plugins, `hub.example.com`, versions 0.11.0) are placeholders. Version: 0.11 (plugins), after plugin API v1.

Lines refer to the file in the "File" column; the 3f-4 screen is in the Player controllers family (extends 3f-2).

| Screen | Specification | Source (file, anchor, lines) | Version |
|---|---|---|---|
| 4a Settings Plugins (dark, light, empty) | [plugins.md](plugins.md#4a-settings-plugins) | `plugins-4a-4d`: `#4a` 30-33, `#4a-light` 34-37, `#4a-empty` 38-43 | 0.11 package 2 |
| 4b Plugin detail (dark, light) | [plugins.md](plugins.md#4b-plugin-detail-4b-2-uninstall) | `plugins-4a-4d`: `#4b` 46-49, `#4b-light` 50-53 | 0.11 package 2 |
| 4b-2 Uninstall confirmation | [plugins.md](plugins.md#4b-plugin-detail-4b-2-uninstall) | `plugins-round-2`: `#4b-2` 30-35 | 0.11 package 2 |
| 4c-1, 4c-2 Permission dialog (first turn-on, unverified) | [plugins.md](plugins.md#4c-permission-dialog) | `plugins-4a-4d`: `#4c-1`..`#4c-4` template 58-79, data 191-194 | 0.11 package 2 |
| 4c-3-2, 4c-4-2 Permission dialog (new permission, change game memory) | [plugins.md](plugins.md#4c-permission-dialog) | `plugins-round-2`: template 36-52, data 414-420; replaces 4c-3, 4c-4 | 0.11 package 2 |
| 4d-1, 4d-2 Install from file | [plugins.md](plugins.md#4d-install-from-file-console) | `plugins-4a-4d`: `#4d-1` 82-92, `#4d-2` 93-111 | 0.11 package 2 |
| 4d-3-2 Installed from file (Local) | [plugins.md](plugins.md#4d-install-from-file-console) | `plugins-round-2`: `#4d-3-2` 54-68; replaces 4d-3 | 0.11 package 2 |
| 4d-4-2 Plugin console | [plugins.md](plugins.md#4d-install-from-file-console) | `plugins-round-2`: `#4d-4-2` 69-88; replaces 4d-console | 0.11 package 2 (developer mode) |
| 4d-5 Console error | [plugins.md](plugins.md#4d-install-from-file-console) | `plugins-4a-4d`: `#4d-console-error` 154-173 | 0.11 package 2 |
| 4e-1-2 to 4e-4-2 Side panel tabs (1 tab, HUD + diagnostics, 4 tabs, 1280 overflow) | [plugins.md](plugins.md#4e-in-game-tabs-hud-tile-strip-fullscreen) | `plugins-round-2`: `#4e-1-2` 91-102, `#4e-2-2` 103-121, `#4e-3-2` 122-135, `#4e-4-2` 136-148 | 0.11 package 2 |
| 4e-5 HUD corners, menu, hidden | [plugins.md](plugins.md#4e-in-game-tabs-hud-tile-strip-fullscreen) | `plugins-4e-4h`: `#4e-hud` 109-136 | 0.11 package 2 |
| 4e-6-2 Multiview tile strip, 4e-7 rings | [plugins.md](plugins.md#4e-in-game-tabs-hud-tile-strip-fullscreen) | `plugins-round-2`: `#4e-6-2` 149-168; `plugins-4e-4h`: `#4e-7` 168-183 | 0.11 package 2 |
| 4e-8-2 Fullscreen HUD | [plugins.md](plugins.md#4e-in-game-tabs-hud-tile-strip-fullscreen) | `plugins-round-2`: `#4e-8-2` 169-180 | 0.11 package 2 |
| 4f Plugin sidebar screen | [plugins.md](plugins.md#4f-plugin-sidebar-screen) | `plugins-4e-4h`: `#4f` 202-215 | 0.11 package 2 |
| 4g-2 Notices (toast stack) | [plugins.md](plugins.md#4g-2-notices) | `plugins-round-2`: `#4g-2` 181-205 | 0.11 package 2 |
| 4h-2 Game detail plugin section | [plugins.md](plugins.md#4h-2-game-detail-section) | `plugins-round-2`: `#4h-2` 206-231 | 0.11 package 2 |
| 3f-4 Controllers Hotkeys (plugin groups) | [plugins.md](plugins.md#3f-4-controllers-hotkeys) | `plugins-round-2`: `#3f-4` 234-259 | 0.11 package 2 |
| 4ih Hub Plugins, upload of an unverified plugin | [plugins.md](plugins.md#4ih-hub-plugins) | `plugins-4i-4j-sheet`: `#4ih` 30-56, `#4ih-upload` 58-70 | 0.11 package 3 |
| 4j-2 Player Runs, 4jh-2 Hub Runs (paused) | [plugins.md](plugins.md#4j-2-player-runs-4jh-2-hub-runs) | `plugins-round-2`: `#4j-2` 262-277, `#4jh-2` 278-291; replaces 4j, 4jh | 0.11 package 3 |
| r2-sheet Building blocks (dark, light, focus) | [plugins.md](plugins.md#building-blocks-component-sheet-r2-sheet) | `plugins-round-2`: `#r2-sheet` 389-395; `block-sheet.dc.html` | 0.11 package 2 |
| Map view states | [plugins.md](plugins.md#map-view) | `plugins-4i-4j-sheet`: `#4-sheet` 122-145; `region-map.dc.html` | 0.11 package 2 |

Reference only (superseded by `-2`): 4c-3, 4c-4, 4d-3, 4d-console, 4e-1, 4e-2, 4e-3, 4e-4, 4e-6, 4e-8, 4g, 4h, 4j, 4jh, the BlockSheet part of 4-sheet. The imported files still `dc-import` `SoulLinkPanel`, `RunScreen` and `HubRunWizard`, which are not copied (SoulLink repository). Plugin tokens (warning tone, player colours, HP bar, stream size set): [tokens.md](tokens.md#additions-of-the-plugin-handoff-2026-10-11).

Phases per `../workflow.md` (phase plan); 0.4 and later are the versions of the roadmap (`../roadmap.md`). Borderline cases: 3o (reference only) phase 1 covered only Hub name, address, transport and certificate, admin account; appearance (dark/light) and "Allow users to upload games" phase 5 (requires users); these now live in 3q. 3m is phase 1 (Allow/Decline, Revoke); the assignment to an existing user (architecture 10) belongs to it as well, but requires created users (until phase 5 only admin). 3k and 3d both belong to phase 3, although 3k is a Hub page. 3l phase 5 (firmware path); nav badges of the Hub shell ("1 conflict", "2 issues", "1 request", Settings "1 update") follow the respective phases. The Hub shell (sidebar, page header, tables) is created with the first Hub page in phase 1. "UI in the 0.6/0.7 pass" means: the feature (protocol, storage, diagnostics) arrives in 0.4, the screen in the named UI pass.

Decisions made on the handoff and the deviations from the architecture, with open points: [decisions.md](decisions.md).
