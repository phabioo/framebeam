# Design specification

## Uzume design (valid from 0.18)

The **Uzume design** ([uzume/README.md](uzume/README.md)) is the valid design from milestone 0.18 on. It supersedes the FrameBeam design Rounds 1 to 4 below in look (tokens, type, components, themes Sumi and Kinari) and in every screen it draws: first start, join, accounts, the admin area inside the app, runs, the running game, Multiview, Settings, the plugin screens and the installers. Implementation agents read the design system once and then only the spec of the screen they build:

| File | Content |
|---|---|
| [uzume/README.md](uzume/README.md) | Overview: source, how to read the boards, sheet table, roadmap packages |
| [uzume/design-system.md](uzume/design-system.md) | Tokens for Sumi and Kinari, type, spacing, sizes, focus ring, markers, components, layout rules |
| [uzume/frame.md](uzume/frame.md) | Sheet 1: frame (0-4), first start (8a), join (8b), invite link (8c) |
| [uzume/accounts-and-server.md](uzume/accounts-and-server.md) | Sheet 2: accounts (8d-8g) and the admin screens in the app |
| [uzume/runs-and-server.md](uzume/runs-and-server.md) | Sheet 3: runs, what the server shows, sharing, On this PC, server problems, installers |
| [uzume/play-and-plugins.md](uzume/play-and-plugins.md) | Sheet 4: library and game page, running game, Multiview, Settings › This PC, developer console, SoulLink & Nuzlocke |
| [uzume/decisions.md](uzume/decisions.md) | Decisions, accepted review results and open points |

The handoff (boards and design system) is stored unchanged in `source/uzume/`; the boards need the Claude Design runtime `support.js`, which is not in the repository, so they do not render standalone.

## FrameBeam design (Rounds 1-4, legacy reference)

This part describes the FrameBeam design of Rounds 1 to 4 (Player dark, Hub light). It stays in place because code and ADRs link to it, and it stays valid **only for behaviour the Uzume boards do not cover**; wherever an Uzume board draws a screen, the Uzume spec wins. "Player" and "Hub" below are the app and the server of the Uzume design. Files: [tokens.md](tokens.md), [player.md](player.md), [hub.md](hub.md), [plugins.md](plugins.md), [logo.md](logo.md), [decisions.md](decisions.md).


Purpose: token-efficient preparation of the UI prototype, so that implementation agents read only the necessary part per screen. The prototype is a mock-up; where it contradicts, the architecture applies (`../architecture/09-ui-and-navigation.md`), deviations see below. Mock values (e.g. core version "1.2.0", user names, device names, versions, addresses) are illustrative; the seeded registry expects melonDS DS 1.4.0. Hosts in the mock are generic placeholders (`hub.example.com`, `hub.local`, `lena.example.net`).

Source: Claude Design handoff "FrameBeam Designs v3" (second round, accepted by Fabio on 2026-10-07; the diagnostics screens 3t-3y and the updated 3g/3h come from a later handoff, accepted by Fabio on 2026-10-08); `source/framebeam-designs-v3.dc.html` is the current state, older versions are not carried over. Two further sources belong to it: `source/framebeam-logo.dc.html` (logo sheet) and `source/framebeam-mark.dc.html` (mark component), see [logo.md](logo.md). The files stay unchanged and do not render standalone (runtime `support.js` is missing from the repository). 25 screens (3a-3y) at 1440 x 900 plus an intro block for 3t-3y (line 669); mock data in the script from line 1362 (`renderVals` at line 1626; the diagnostics mock data are in `dgVals` at line 1573).

Revision handoff (Claude Design, 08.10.2026, after a friends playtest): `source/framebeam-designs-v4.dc.html` (2959 lines). It contains the unchanged Turn 3 (all v3 screens, shifted down: 3a starts at line 1164) plus two new turns: Turn 4 (revised Player and Hub screens with suffix `-2`, `-3`, `-4`; component sheet at line 1001, handoff at line 1102) and Turn 5 (in-game screens, component sheet at line 229, handoff at line 290). Mock data of the new turns: `t4Vals` (line 2761), `t5Vals` (line 2856), diagnostics values in `dgVals` (line 2708). **v4 supersedes v3 for the revised screens only**; the v3 file and the v3 sections stay as reference for the originals (open them only to compare). Not revised screens (3a, 3b, 3d, 3j-3q) keep their v3 specification. The source has the same limits as v3: it does not render standalone. The component files it imports are listed under "Component sheets". Mock values (user names Max, Lena, Jonas, Sam, Sarah, game titles, `hub.example.com`) are placeholders as before; the fictional core "Lanterna 0.3.0" in the core-agnostic sheet is an example only. The playtest uploads of the handoff are not part of the repository.

Language note: the source texts are English throughout (the earlier German texts and labels are gone). Screen labels in the source are `data-screen-label` values; the heading names in the specification files may be shortened (3l appears in the source as "Hub Systems and Cores", 3q as "Hub Settings revised").

Reading note: read only the necessary file or section. Open the source HTML only for details in the given line range (lines = screen container from `data-screen-label` to the end; anchors `#3x` = block ID in the source). The screens appear in the source in this order: 3a-3f, 3p, 3g, 3h, 3r, 3i, 3t-3y, 3j, 3k, 3s, 3l, 3m, 3n, 3o, 3q.

- `tokens.md`: colors (plus the additions of Rounds 3 and 4), typography, spacing, radii, shadows, layout dimensions (Player dark, Hub light).
- `player.md`: screens 3a-3i, 3p, 3r, 3t-3y and the revisions 3c-2 to 3c-5, 3e-2, 3f-2/3, 3g-2/3, 3h-2, 3i-2, 3p-2, 3r-2, 3t-2, 3x-2, the Round 3 screens 3a-3, 3c-6, 3c-7, 3p-3, 3e-3, 3f-4, 3r-3, 3h-3, 3i-3, 3y-2 (2026-10-12), the Round 4 Player screens (2026-10-21: space plan 0-3, 6a-6x, 3e-4, 3c-8, 3g-4, 3r-4, 3y-3, 7c) plus the component specs (FrameBeam Player, Qt 6 QML).
- `hub.md`: screens 3j-3o, 3q, 3s, the revision 3s-2/3, the Round 3 screens 3l-2 to 3l-4, 3j-2, 3s-4, 3m-2, 3n-2, 3q-2 and the Round 4 Hub screens (6a-hub, 6b, 6c-hub, 6d-hub, 6f, 3j-3, 6j, 6k, 6l, 3s-5, 6q-hub, 6t-2, 7a, 7b; FrameBeam Hub web interface, `html/template` + htmx).
- `plugins.md`: plugin platform screens (4a-4j, 4ih, 4jh, 3f-4), building-block catalog, map view, HUD, tab and tile-strip specs (plugin handoff 2026-10-11; design only, plugin API v1 is not implemented), plus 4a-3, 4a-4, 4f-3, 4j-3, 4i-3 and 4jh-3 of Round 3 and 4d-6, 4jh-4/4j-4, the plugin parts of 6o and 6z of Round 4.
- `logo.md`: logo concept, tones, sizes, wordmark.
- `decisions.md`: decisions (incl. Round 3: decisions 20-23, open points bg-bk; Round 4: decisions 24-35, open points bl-bq), deviations from the architecture, open points per handoff round (incl. the defaults decided for the revision handoff of 2026-10-08).

### Screens

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

#### Revision screens (v4, 08.10.2026)

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

#### Round 3 screens (2026-10-12, 0.14 UI gaps and Multiview)

Claude Design handoff "Round 3", answering the 0.14 UI scan (finding IDs H = Hub, P = Player, D = design gap). Files in `source/`: `round-3-hub.dc.html` (479 lines), `round-3-player-a.dc.html` (289), `round-3-player-b.dc.html` (253), the sheet `round-3-sheet.dc.html` (cross-cutting rules 0-2, per-screen handoff table, new tokens, open questions in its script) and the nav components `hub-nav.dc.html` and `player-nav.dc.html`. Anchors `#id` are block IDs; the "Fixes" column repeats the sheet's handoff table. The older sections stay valid for everything Round 3 does not change and carry a "superseded" line. Decisions 20-23 and open points bg-bk: [decisions.md](decisions.md#decisions-fabio-2026-10-12-round-3-014-ui-gaps-and-multiview); new tokens: [tokens.md](tokens.md#additions-of-round-3-2026-10-12). The existing copies of v4, the plugin sheets and the components were re-imported from this handoff; after renaming the `dc-import` names they are identical to the earlier copies.

| Screen | Specification | Source (file, anchor, lines) | Fixes | Revises |
|---|---|---|---|---|
| 3l-2 Systems and Cores, Cores tab, auto-update | [hub.md](hub.md#3l-2-hub-systems-and-cores-round-3) | `round-3-hub`: `#3l-2` 21-68 | H1 H2 H5 H6 H8 | 3l |
| 3l-3 Firmware tab, no firmware needed | [hub.md](hub.md#3l-3-hub-systems-and-cores-no-firmware-round-3) | `round-3-hub`: `#3l-3` 69-91 | H3 H4 | 3l |
| 3l-4 Firmware files, drop zone, pin | [hub.md](hub.md#3l-4-hub-systems-and-cores-firmware-files-round-3) | `round-3-hub`: `#3l-4` 92-124 | H7 D4 | 3l |
| 3j-2 Library (+ 390, dialogs) | [hub.md](hub.md#3j-2-hub-library-round-3) | `round-3-hub`: `#3j-2` 125-156, `#3j-2-390` 157-171, `#hub-states` 419-433 | H10 H11 H12 H13 H14 D1 D2 | 3j |
| 3s-4 Saves | [hub.md](hub.md#3s-4-hub-saves-round-3) | `round-3-hub`: `#3s-4` 172-197 | H15 H16 H17 H18 D3 | 3s, 3k |
| 3m-2 Clients (+ 390) | [hub.md](hub.md#3m-2-hub-clients-round-3) | `round-3-hub`: `#3m-2` 198-221, `#3m-2-390` 222-235 | H19 H20 H22 D5 | 3m |
| 3n-2 Users | [hub.md](hub.md#3n-2-hub-users-round-3) | `round-3-hub`: `#3n-2` 236-265 | H21 D5 | 3n |
| 3q-2 Settings (Network, Security dark, pages) | [hub.md](hub.md#3q-2-hub-settings-round-3) | `round-3-hub`: `#3q-2` 266-307, `#3q-2-dark` 308-331, `#3q-2-pages` 332-358 | H9 H32-H38 D6 | 3q |
| 4i-3 Hub Plugins | [plugins.md](plugins.md#4i-3-hub-plugins-round-3) | `round-3-hub`: `#4i-3` 359-391 | H23-H26 | 4ih |
| 4jh-3 Hub Run detail | [plugins.md](plugins.md#4jh-3-hub-run-detail-round-3) | `round-3-hub`: `#4jh-3` 392-418 | H27-H31 | 4jh-2 |
| 3a-3 Connect, Pair again | [player.md](player.md#3a-3-player-connect-round-3) | `round-3-player-a`: `#3a-3` 21-40 | P2 P6 | 3a |
| 3c-6 Library (1280, 3840) | [player.md](player.md#3c-6-player-library-round-3) | `round-3-player-a`: `#3c-6` 41-67, `#3c-6-4k` 68-83 | P7 P8-P11 P13 | 3c-2, 3c-5 |
| 3c-7 Library detail column variants | [player.md](player.md#3c-7-player-library-detail-column-round-3) | `round-3-player-a`: `#3c-7` 84-104 | P12-P14 | 3c-2 |
| 3p-3 Settings (Updates, Hubs, folders) | [player.md](player.md#3p-3-player-settings-round-3) | `round-3-player-a`: `#3p-3` 105-134 | P1-P5 P16 P18 | 3p-2 |
| 4a-3 Settings Plugins (+ dialogs) | [plugins.md](plugins.md#4a-3-settings-plugins-round-3) | `round-3-player-a`: `#4a-3` 135-156, `#4a-3-dialogs` 179-190 | P19-P22 | 4a |
| 4a-4 Plugin detail sheet | [plugins.md](plugins.md#4a-4-plugin-detail-round-3) | `round-3-player-a`: `#4a-4` 157-178 | D16 P21 P22 | 4b |
| 4f-3 SoulLink screen under Runs | [plugins.md](plugins.md#4f-3-plugin-sidebar-screen-round-3) | `round-3-player-a`: `#4f-3` 191-203 | P23 P28 | 4f |
| 4j-3 Player Run detail (+ join dialog) | [plugins.md](plugins.md#4j-3-player-run-detail-round-3) | `round-3-player-a`: `#4j-3` 204-230, `#4j-3-join` 231-247 | P24-P27 | 4j-2 |
| 3e-3 Emulation | [player.md](player.md#3e-3-player-emulation-round-3) | `round-3-player-b`: `#3e-3` 21-42, `#3e-3-rows` 43-49 | P30-P32 D9 | 3e-2 |
| 3f-4 Controllers (Buttons, Hotkeys) | [player.md](player.md#3f-4-player-controllers-round-3) | `round-3-player-b`: `#3f-4` 50-83, `#3f-4-hotkeys` 84-99 | P33-P36 D11 | 3f-2, 3f-4 (plugins) |
| 3r-3 Multiview, 2/3/4 Sessions at 1440 and 1280 | [player.md](player.md#3r-3-player-multiview-round-3) | `round-3-player-b`: `#3r-3-2` `#3r-3-3` `#3r-3-4` `#3r-3-2s` `#3r-3-3s` `#3r-3-4s`, template 100-136, data 239-245 | P38 P41 P43 P44 D18 D20 (P37) | 3r-2, 3h-2 |
| 3h-3 Session, diagnostics docked | [player.md](player.md#3h-3-player-session-round-3) | `round-3-player-b`: `#3h-3` 137-157 | P40 P41 P45-P47 | 3g-2, 3t-2 |
| 3i-3 PiP | [player.md](player.md#3i-3-player-pip-round-3) | `round-3-player-b`: `#3i-3` 158-178 | P42 D20 | 3i-2 |
| 3y-2 Fullscreen, paused | [player.md](player.md#3y-2-player-fullscreen-paused-round-3) | `round-3-player-b`: `#3y-2` 179-190 | P34 P39 | 3y |

Not drawn separately: Player Settings at 3840 (follows 3p-3, decision bi). Reference only and not copied: the SoulLink artboards (5a-5i, 5e-5g, `SoulLinkPanel`, `RunScreen`, `HubRunWizard`) belong to the `framebeam-plugin-soullink` repository.

#### Round 4 screens (2026-10-21, features before 1.0)

Claude Design handoff "Round 4", answering the briefs for the feature waves 0-6 (milestones 0.16 to 0.22 in [../roadmap.md](../roadmap.md); wave n = 0.(16+n)). Files in `source/`: `round-4-a.dc.html` (571 lines, waves 0-1), `round-4-b.dc.html` (490, wave 2), `round-4-c.dc.html` (308, wave 3), `round-4-d.dc.html` (435, wave 4), `round-4-e.dc.html` (329, waves 5-6), the sheet `round-4-sheet.dc.html` (space plan 0-3 A-E, full handoff table in its script `handoff` at 411-459, tokens at 459, open questions `open` at 460), the new component `r4-header.dc.html` and the changed nav components `hub-nav.dc.html` and `player-nav.dc.html`. Anchors `#id` are block IDs; screens drawn from a data list (`sc-for`) show the template range and the data lines. Block IDs and labels differ in four places: `#6b-states` is labelled 6b-4, `#6d-states` 6d-2, `#3c-8-states` 3c-8-3, `#7b-states` 7b-2. v4, Round 3 and plugin tokens stay; the rules 0-2 of Round 3 apply. Decisions 24-35 and open points bl-bq: [decisions.md](decisions.md#decisions-fabio-2026-10-21-round-4-features-before-10); the only new color value: [tokens.md](tokens.md#additions-of-round-4-2026-10-21). Mock values (Lumen Drift, Harbor Rally, Coral Saga: Ember, people, devices, `hub.example.com`, versions 0.16.x) are placeholders. The sheet's `place` table, `strips` and `osd` data are the source for the space plan sections in [player.md](player.md#space-plan-0-3-round-4).

Fabio's answers of 2026-10-21 override the drawing where they differ: gamepad hold-Select rule, F5/F6/F8 (decision 24), SoulLink HUD toggle (25), backups never deleted on a full disk (26), three Auto states (27), who may ask "Ready?" (28), handover warning at 120 ms (29), voice goes directly between Players (30, the drawing's "through the Hub relay" in 6w is wrong), chat only in the browser (31), single plugin rollback (32), 24 h Session chat (33), read-only TURN variables in the setup assistant (34).

| Screen | Specification | Source (file, anchor, lines) | Wave / version |
|---|---|---|---|
| 0-3 A In-game header (zones, placement table) | [player.md](player.md#0-3-a-in-game-header-and-r4header) | `round-4-sheet`: `#0-3-header` 22-115, data `zones` 318, `overflow` 325, `place` 330 | all |
| 0-3 B Side panel tabs | [player.md](player.md#0-3-b-side-panel-tabs) | `round-4-sheet`: `#0-3-panel` 117-148, data `strips` 352 | all |
| 0-3 C OSD and zones over the game | [player.md](player.md#0-3-c-osd-and-the-zones-over-the-game) | `round-4-sheet`: `#0-3-osd` 150-191, data `osd` 366 | all |
| 0-3 D Player sidebar: Online block and bell | [player.md](player.md#0-3-d-player-sidebar-online-block-and-bell) | `round-4-sheet`: `#0-3-sidebar` 193-241, data `sides` 386 | 4 (0.20) |
| 0-3 E Hub navigation | [hub.md](hub.md#0-3-e-hub-navigation-round-4) | `round-4-sheet`: `#0-3-hubnav` 243-299, data `hubNav` 399 | 1 (0.17) |
| 6a What's new (Player), 6a-2 short | [player.md](player.md#6a-whats-new-player) | `round-4-a`: `#6a` `#6a-2` template 26-56, data 472-473 | 0 (0.16) |
| 6a-hub Banner after a Hub update, 6a-hub-2 notes page | [hub.md](hub.md#6a-hub-whats-new-hub) | `round-4-a`: `#6a-hub` 60-77, `#6a-hub-2` 79-97 | 0 (0.16) |
| 6b Backups, 6b-2 running, 6b-3 restored | [hub.md](hub.md#6b-backups) | `round-4-a`: `#6b` `#6b-2` `#6b-3` template 105-146, data 484-486 | 1 (0.17) |
| 6b-4 Backup card states | [hub.md](hub.md#6b-backups) | `round-4-a`: `#6b-states` 150-165 | 1 (0.17) |
| 6b-5 Restore confirmation | [hub.md](hub.md#6b-backups) | `round-4-a`: `#6b-5` 168-184 | 1 (0.17) |
| 6b-6 Restore in progress | [hub.md](hub.md#6b-backups) | `round-4-a`: `#6b-6` 186-195 | 1 (0.17) |
| 6c Player Updates, earlier versions; 6c-3 after going back | [player.md](player.md#6c-updates-roll-back-player) | `round-4-a`: `#6c` `#6c-3` template 202-224, data 506-508 | 1 (0.17) |
| 6c-2 Player Go back confirmation (+ disabled) | [player.md](player.md#6c-updates-roll-back-player) | `round-4-a`: `#6c-2` 228-243 | 1 (0.17) |
| 6c-hub Hub Updates, earlier versions + confirmation; 6c-hub-2 after going back | [hub.md](hub.md#6c-hub-updates-roll-back-hub) | `round-4-a`: `#6c-hub` `#6c-hub-2` template 247-282, data 519-520 | 1 (0.17) |
| 6d Report a problem (Player) | [player.md](player.md#6d-report-a-problem-player) | `round-4-a`: `#6d` 288-313 | 1 (0.17) |
| 6d-2 Report dialog footer states | [player.md](player.md#6d-report-a-problem-player) | `round-4-a`: `#6d-states` 314-327 | 1 (0.17) |
| 6d-hub Hub Reports | [hub.md](hub.md#6d-hub-reports) | `round-4-a`: `#6d-hub` 330-358 | 1 (0.17) |
| 6d-hub-2 Reports empty and 390 | [hub.md](hub.md#6d-hub-reports) | `round-4-a`: `#6d-hub-2` 359-371 | 1 (0.17) |
| 6e Connection check (4 outcomes) | [player.md](player.md#6e-connection-check) | `round-4-a`: `#6e` 376-397 | 1 (0.17) |
| 6f Hub Status | [hub.md](hub.md#6f-hub-status) | `round-4-a`: `#6f` 402-440 | 1 (0.17) |
| 6f-390 Hub Status phone | [hub.md](hub.md#6f-hub-status) | `round-4-a`: `#6f-390` 441-459 | 1 (0.17) |
| 6g Volume popover, OSD | [player.md](player.md#6g-volume) | `round-4-b`: `#6g` 24-56 | 2 (0.18) |
| 6g-2 Volume states, tile label, Settings rows | [player.md](player.md#6g-volume) | `round-4-b`: `#6g-2` 58-86 | 2 (0.18) |
| 3e-4 Emulation, Display tab | [player.md](player.md#3e-4-emulation-display-tab-round-4) | `round-4-b`: `#3e-4` 90-109 | 2 (0.18) |
| 3e-4-2 Rows that differ per system | [player.md](player.md#3e-4-emulation-display-tab-round-4) | `round-4-b`: `#3e-4-2` 110-118 | 2 (0.18) |
| 6h Settings for this game | [player.md](player.md#6h-settings-for-this-game) | `round-4-b`: `#6h` 122-137 | 2 (0.18) |
| 3c-8 Library: Continue, favourites, collections; 3c-8-2 at 1280 + popover; 3c-8-4 empty favourites | [player.md](player.md#3c-8-player-library-round-4) | `round-4-b`: `#3c-8` `#3c-8-2` `#3c-8-4` template 140-196, data 446-448 | 2 (0.18) |
| 3c-8-3 Library states | [player.md](player.md#3c-8-player-library-round-4) | `round-4-b`: `#3c-8-states` 199-213 | 2 (0.18) |
| 6i DS/3DS screen layouts | [player.md](player.md#6i-ds-and-3ds-screen-layouts) | `round-4-b`: `#6i` 218-248 | 2 (0.18) |
| 6i-2 Bottom screen in its own window | [player.md](player.md#6i-ds-and-3ds-screen-layouts) | `round-4-b`: `#6i-2` 249-272 | 2 (0.18) |
| 3j-3 Hub Library (favourites, collections, play time) | [hub.md](hub.md#3j-3-hub-library-round-4) | `round-4-b`: `#3j-3` 277-297 | 2 (0.18) |
| 6j Hub Screenshots gallery | [hub.md](hub.md#6j-hub-screenshots) | `round-4-b`: `#6j` 300-316 | 2 (0.18) |
| 6j-2 Screenshot open, empty gallery | [hub.md](hub.md#6j-hub-screenshots) | `round-4-b`: `#6j-2` 317-332 | 2 (0.18) |
| 6k Hub user page, Play time | [hub.md](hub.md#6k-hub-user-play-time) | `round-4-b`: `#6k` 336-354 | 2 (0.18) |
| 6k-2 Player game detail facts | [player.md](player.md#3c-8-player-library-round-4) | `round-4-b`: `#6k-2` 355-363 | 2 (0.18) |
| 6l Hub Storage | [hub.md](hub.md#6l-hub-storage) | `round-4-b`: `#6l` 369-405 | 2 (0.18) |
| 6m Save states, States tab | [player.md](player.md#6m-save-states) | `round-4-c`: `#6m` 23-54 | 3 (0.19) |
| 6m-2 Load, Undo | [player.md](player.md#6m-save-states) | `round-4-c`: `#6m-2` 56-75 | 3 (0.19) |
| 6m-3 States tab states | [player.md](player.md#6m-save-states) | `round-4-c`: `#6m-3` 77-90 | 3 (0.19) |
| 3c-3-4 Player Saves and states | [player.md](player.md#3c-3-4-player-saves-and-states) | `round-4-c`: `#3c-3-4` 93-123 | 3 (0.19) |
| 3s-5 Hub Saves, States tab | [hub.md](hub.md#3s-5-hub-saves-states-round-4) | `round-4-c`: `#3s-5` 126-153 | 3 (0.19) |
| 3s-5-2 Hub history with plugin snapshot | [hub.md](hub.md#3s-5-hub-saves-states-round-4) | `round-4-c`: `#3s-5-2` 154-161 | 3 (0.19) |
| 6n Rewind OSD | [player.md](player.md#6n-rewind) | `round-4-c`: `#6n` 165-178 | 3 (0.19) |
| 6o Memory write: OSD, chip, popover | [player.md](player.md#6o-6p-memory-writes-and-plugin-snapshots) | `round-4-c`: `#6o` 181-211 | 3 (0.19) |
| 6o-2 Run log entry, board marker, off by run rule; 6p plugin snapshot OSD | [plugins.md](plugins.md#6o-2-memory-edits-in-runs-round-4) and [player.md](player.md#6o-6p-memory-writes-and-plugin-snapshots) | `round-4-c`: `#6o-2` 213-249 (the handoff table points 6p here) | 3 (0.19) |
| 3g-4 In-game header and panel, 3g-4-2 at 1280 (People tab) | [player.md](player.md#3g-4-in-game-header-and-panel-round-4) | `round-4-d`: `#3g-4` `#3g-4-2` template 24-57, data 346-348 | 4 (0.20) |
| 6q Online popover | [player.md](player.md#6q-online-popover) | `round-4-d`: `#6q` 62-81 | 4 (0.20) |
| 6q-hub Hub Users with status | [hub.md](hub.md#6q-hub-hub-users-with-status) | `round-4-d`: `#6q-hub` 82-101 | 4 (0.20) |
| 6r Bell popover | [player.md](player.md#6r-notifications) | `round-4-d`: `#6r` 105-125 | 4 (0.20) |
| 6r-2 Settings, Notifications | [player.md](player.md#6r-notifications) | `round-4-d`: `#6r-2` 126-136 | 4 (0.20) |
| 6s Ready check | [player.md](player.md#6s-ready-check-and-pause-for-everyone) | `round-4-d`: `#6s` 140-157 | 4 (0.20) |
| 6s-2 Host view, pause for everyone, setting | [player.md](player.md#6s-ready-check-and-pause-for-everyone) | `round-4-d`: `#6s-2` 158-171 | 4 (0.20) |
| 6t Chat tab | [player.md](player.md#6t-chat) | `round-4-d`: `#6t` 174-195 | 4 (0.20) |
| 6t-2 (4jh-4 chat) Hub run Chat history | [hub.md](hub.md#6t-2-hub-run-chat-history) | `round-4-d`: `#6t-2` 198-209 | 4 (0.20) |
| 6u People tab (viewer), invite with role | [player.md](player.md#6u-people-quality-and-invite-role) | `round-4-d`: `#6u` 211-234 | 4 (0.20) |
| 6v Mixer | [player.md](player.md#6v-mixer) | `round-4-d`: `#6v` 239-259 | 4 (0.20) |
| 6w Settings, Voice | [player.md](player.md#6w-voice) | `round-4-d`: `#6w` 263-277 | 4 (0.20) |
| 6w-2 Voice states | [player.md](player.md#6w-voice) | `round-4-d`: `#6w-2` 278-287 | 4 (0.20) |
| 6x Controller handover | [player.md](player.md#6x-hand-over-the-controller) | `round-4-d`: `#6x` 291-303 | 4 (0.20) |
| 3r-4 Multiview Grid 2x2 with chat and voice | [player.md](player.md#3r-4-and-3y-3-multiview-and-fullscreen-round-4) | `round-4-d`: `#3r-4` 306-320 | 4 (0.20) |
| 3y-3 Fullscreen with chat and voice | [player.md](player.md#3r-4-and-3y-3-multiview-and-fullscreen-round-4) | `round-4-d`: `#3y-3` 322-337 | 4 (0.20) |
| 4d-6 Developer console (Memory) | [plugins.md](plugins.md#4d-6-developer-console-round-4) | `round-4-e`: `#4d-6` 23-49 | 5 (0.21) |
| 4d-6-2 Console Log, State, developer mode off | [plugins.md](plugins.md#4d-6-developer-console-round-4) | `round-4-e`: `#4d-6-2` 50-67 | 5 (0.21) |
| 4jh-4 Hub run Recap | [plugins.md](plugins.md#4jh-4-4j-4-run-recap-round-4) | `round-4-e`: `#4jh-4` template 71-102, data 290 | 5 (0.21) |
| 4j-4 Player run Recap | [plugins.md](plugins.md#4jh-4-4j-4-run-recap-round-4) | `round-4-e`: `#4j-4` template 71-102, data 291 | 5 (0.21) |
| 6z Privacy, Discord status; splits widget | [player.md](player.md#6z-privacy-and-discord-status) and [plugins.md](plugins.md#6z-splits-widget-round-4) | `round-4-e`: `#6z` 106-126 | 5 (0.21) |
| 7a Hub setup assistant (Cores, Internet access) | [hub.md](hub.md#7a-hub-setup-assistant) | `round-4-e`: `#7a` `#7a-2` template 131-157, data 301-302 | 6 (0.22) |
| 7a-3 Setup assistant, Done | [hub.md](hub.md#7a-hub-setup-assistant) | `round-4-e`: `#7a-3` 159-166 | 6 (0.22) |
| 7b Watch in the browser (1440) | [hub.md](hub.md#7b-watch-in-the-browser) | `round-4-e`: `#7b` 170-192 | 6 (0.22) |
| 7b-390 Watch on a phone | [hub.md](hub.md#7b-watch-in-the-browser) | `round-4-e`: `#7b-390` 194-204 | 6 (0.22) |
| 7b-2 Watch states | [hub.md](hub.md#7b-watch-in-the-browser) | `round-4-e`: `#7b-states` 205-213 | 6 (0.22) |
| 7c Handheld Library (1280 x 800) | [player.md](player.md#7c-handheld-1280-x-800-gamepad-only) | `round-4-e`: `#7c` 217-239 | 6 (0.22) |
| 7c-2 Handheld in game, hotkey map | [player.md](player.md#7c-handheld-1280-x-800-gamepad-only) | `round-4-e`: `#7c-2` 240-257 | 6 (0.22) |

Component notes (Round 4):

- `source/r4-header.dc.html` (R4Header, 77 lines) is the single in-game header of Round 4 and replaces `game-header.dc.html` for all new screens (the older components stay for the v4 and Round 3 screens). Props: `compact` (1280, icons only), `mode` (`session` | `multiview` | `watch`), `share` (`not` | `shared` | `invite`), `watchers`, `conn` / `relayed` (watch mode), `title`, `people` (comma list, max 3 avatars shown), `speaking` (name that gets the speaking ring), `voice` (`off` | `ptt` | `live` | `muted` | `host` | `open`), `muted`, `vol`, `unread`, `open` (`vol` | `mic` | `panel` | `more`), `mvMode`, `chip` / `chipBg` / `chipFg` (e.g. "✎ Memory edited", "Developer mode"), `layout`. Specification: [player.md](player.md#0-3-a-in-game-header-and-r4header).
- `source/hub-nav.dc.html` and `source/player-nav.dc.html` gained the opt-in prop `r4` (default off: the Round 3 screens render unchanged). HubNav with `r4`: groups Play / SETUP / ADMIN, new props `member`, `savesBadge`, `statusBadge`, `reportsBadge`, `ver` (default "0.16.4"), footer link "What's new". PlayerNav with `r4`: Online block (`online`: `full` | `compact`, `offline`), bell in the user row (`bell`, `bellOpen`). Specification: [hub.md](hub.md#0-3-e-hub-navigation-round-4), [player.md](player.md#0-3-d-player-sidebar-online-block-and-bell).
- The imported Round 4 files `dc-import` HubNav, PlayerNav, R4Header, SettingsRow and SaveRow under their repository names (`hub-nav`, `player-nav`, `r4-header`, `settings-row`, `save-row`); the links inside them point to the repository file names.

#### Component sheets

Shared parts of the revision are real components (imported by the v4 file as `dc-import`, names renamed to the file names): `source/settings-row.dc.html` (SettingsRow), `source/save-row.dc.html` (SaveRow, dark and light), `source/pad-glyph.dc.html` (PadGlyph), `source/input-test.dc.html` (InputTest), `source/hub-nav.dc.html` and `source/player-nav.dc.html` (sidebars; with the `r4` option since Round 4), `source/r4-header.dc.html` (in-game header of Round 4), `source/game-header.dc.html` (GameHeader), `source/game-panel.dc.html` (GamePanel), `source/diag-overlay.dc.html` (DiagOverlay), `source/support.js` (design runtime shared by all `.dc.html` files, not product code) and the existing `source/framebeam-mark.dc.html`. Sheets in v4: in-game components (GameHeader variants, anchored popovers, GamePanel, tiles and Now running) at lines 229-289; SettingsRow, core-agnostic options, SaveRow, sort and Ready first, D-pad and glyphs at lines 1001-1101. Specs per component: [player.md](player.md#components-revision-v4); new tokens: [tokens.md](tokens.md).

#### Plugin screens (2026-10-11)

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
