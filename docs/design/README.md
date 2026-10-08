# Design specification

Purpose: token-efficient preparation of the UI prototype, so that implementation agents read only the necessary part per screen. The prototype is a mock-up; where it contradicts, the architecture applies (`../architecture/09-ui-and-navigation.md`), deviations see below. Mock values (e.g. core version "1.2.0", user names, device names, versions, addresses) are illustrative; the seeded registry expects melonDS DS 1.4.0. Hosts in the mock are generic placeholders (`hub.example.com`, `hub.local`, `lena.example.net`).

Source: Claude Design handoff "FrameBeam Designs v3" (second round, accepted by Fabio on 2026-10-07; the diagnostics screens 3t-3y and the updated 3g/3h come from a later handoff that Fabio has not accepted yet); `source/framebeam-designs-v3.dc.html` is the current state, older versions are not carried over. Two further sources belong to it: `source/framebeam-logo.dc.html` (logo sheet) and `source/framebeam-mark.dc.html` (mark component), see [logo.md](logo.md). The files stay unchanged and do not render standalone (runtime `support.js` is missing from the repository). 25 screens (3a-3y) at 1440 x 900 plus an intro block for 3t-3y (line 669); mock data in the script from line 1362 (`renderVals` at line 1626; the diagnostics mock data are in `dgVals` at line 1573).

Language note: the source texts are English throughout (the earlier German texts and labels are gone). Screen labels in the source are `data-screen-label` values; the heading names in the specification files may be shortened (3l appears in the source as "Hub Systems and Cores", 3q as "Hub Settings revised").

Reading note: read only the necessary file or section. Open the source HTML only for details in the given line range (lines = screen container from `data-screen-label` to the end; anchors `#3x` = block ID in the source). The screens appear in the source in this order: 3a-3f, 3p, 3g, 3h, 3r, 3i, 3t-3y, 3j, 3k, 3s, 3l, 3m, 3n, 3o, 3q.

- `tokens.md`: colors, typography, spacing, radii, shadows, layout dimensions (Player dark, Hub light).
- `player.md`: screens 3a-3i, 3p, 3r, 3t-3y (FrameBeam Player, Qt 6 QML).
- `hub.md`: screens 3j-3o, 3q, 3s (FrameBeam Hub web interface, `html/template` + htmx).
- `logo.md`: logo concept, tones, sizes, wordmark.
- `decisions.md`: decisions, deviations from the architecture, open points per handoff round.

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

Phases per `../workflow.md` (phase plan); 0.4 and later are the versions of the roadmap (`../roadmap.md`). Borderline cases: 3o (reference only) phase 1 covered only Hub name, address, transport and certificate, admin account; appearance (dark/light) and "Allow users to upload games" phase 5 (requires users); these now live in 3q. 3m is phase 1 (Allow/Decline, Revoke); the assignment to an existing user (architecture 10) belongs to it as well, but requires created users (until phase 5 only admin). 3k and 3d both belong to phase 3, although 3k is a Hub page. 3l phase 5 (firmware path); nav badges of the Hub shell ("1 conflict", "2 issues", "1 request", Settings "1 update") follow the respective phases. The Hub shell (sidebar, page header, tables) is created with the first Hub page in phase 1. "UI in the 0.6/0.7 pass" means: the feature (protocol, storage, diagnostics) arrives in 0.4, the screen in the named UI pass.

Decisions made on the handoff and the deviations from the architecture, with open points: [decisions.md](decisions.md).
