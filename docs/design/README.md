# Design specification

Purpose: token-efficient preparation of the UI prototype, so that implementation agents read only the necessary part per screen. The prototype is a mock-up; where it contradicts, the architecture applies (`../architecture/09-ui-and-navigation.md`), deviations see below.

Source: Claude Design, export "FrameBeam Designs v2"; `source/framebeam-designs-v3.dc.html` (v3) is the current state, older versions are not carried over. The file stays unchanged and does not render standalone (runtime `support.js` is missing from the repository). 15 screens at 1440 x 900; mock data in the script from line 825.

Language note: the source file contains German UI texts and German `data-screen-label` values. The specification files use the English target texts of the Player and Hub UI instead; the screens 3d, 3l and 3n appear in the source as "Save-Konflikt", "Systeme und Cores" and "Benutzer".

Reading note: read only the necessary file or section. Open the source HTML only for details in the given line range (lines = screen container from `data-screen-label` to the end; anchors `#3x` = block ID in the source).

- `tokens.md`: colors, typography, spacing, radii, shadows, layout dimensions (Player dark, Hub light).
- `player.md`: screens 3a-3i (FrameBeam Player, Qt 6 QML).
- `hub.md`: screens 3j-3o (FrameBeam Hub web interface, `html/template` + htmx).

## Screens

| Screen | Specification | Source (anchor, lines) | Phase |
|---|---|---|---|
| 3a Player Connection | [player.md](player.md#3a-player-connection) | `#3a`, 30-52 | 2 |
| 3b Player Pairing | [player.md](player.md#3b-player-pairing) | `#3b`, 58-72 | 2 |
| 3c Player Library | [player.md](player.md#3c-player-library) | `#3c`, 78-175 | 2 |
| 3d Player Save Conflict | [player.md](player.md#3d-player-save-conflict) | `#3d`, 181-209 | 3 |
| 3e Player Emulation | [player.md](player.md#3e-player-emulation) | `#3e`, 215-276 | 5 |
| 3f Player Controllers | [player.md](player.md#3f-player-controllers) | `#3f`, 282-344 | 5 |
| 3g Player Session | [player.md](player.md#3g-player-session) | `#3g`, 350-391 | 4 |
| 3h Player Side-by-Side | [player.md](player.md#3h-player-side-by-side) | `#3h`, 397-438 | 4 |
| 3i Player PiP | [player.md](player.md#3i-player-pip) | `#3i`, 444-476 | 4 |
| 3j Hub Library | [hub.md](hub.md#3j-hub-library) | `#3j`, 492-540 | 1 |
| 3k Hub Saves | [hub.md](hub.md#3k-hub-saves) | `#3k`, 546-633 | 3 |
| 3l Hub Systems and Cores | [hub.md](hub.md#3l-hub-systems-and-cores) | `#3l`, 639-700 | 5 |
| 3m Hub Clients | [hub.md](hub.md#3m-hub-clients) | `#3m`, 706-742 | 1 |
| 3n Hub Users | [hub.md](hub.md#3n-hub-users) | `#3n`, 748-785 | 5 |
| 3o Hub Settings | [hub.md](hub.md#3o-hub-settings) | `#3o`, 791-819 | 1 (parts), 5 |

Phases per `../workflow.md` (phase plan). Borderline cases: 3o phase 1 only Hub name, address, transport and certificate, admin account; appearance (dark/light) and "Allow users to upload games" phase 5 (requires users). 3m is phase 1 (Allow/Deny, Revoke); the assignment to an existing user (architecture 10) belongs to it as well, but requires created users (until phase 5 only admin). 3k and 3d both belong to phase 3, although 3k is a Hub page. 3l phase 5 (firmware path); nav badges of the Hub shell (conflicts, firmware, requests) follow the respective phases. The Hub shell (sidebar, page header, tables) is created with the first Hub page in phase 1.

## Deviations from the architecture

Only noted, not resolved; the architecture applies. Checked against `09-ui-and-navigation.md`, `03-saves.md` and `10-identity-pairing-tls.md`; other files not read (points there: unclear).

1. Diagnostics appears in 3g-3i as an equal-ranking tab next to Session and Multiview. Architecture: only optionally shown/collapsible, not primary UX. In 3g additionally "▸ Show diagnostics".
2. Hub Settings (3o): appearance "Light | Dark | System"; architecture: "dark/light presentation selectable". The "System" option is additional. Only Player dark and Hub light are drawn; since phase 5 the other two palettes (Player light, Hub dark) exist in code but are derived from the drawn ones, not designed ([tokens.md](tokens.md), ADR 0007 D7).
3. Fingerprint confirmation "in the Hub web interface" on a changed certificate (3a): architecture 10 (line 40) leaves the confirmed pin change to be specified; open.
4. 3b shows "Certificate trusted" without confirmation. Architecture 10 (line 40) requires showing and confirming the fingerprint on first pairing. A real deviation, the architecture applies.
5. Not drawn, but named by the architecture: Player Settings and Settings → Hubs, Hub switcher dialog (only a "switch" card), admin setup and login in the Hub, metadata actions in the library entry (later), firmware block in the library detail (3c shows only the success case), multiview action "Add to multiview".
6. Terms: UI texts use "Session"; "Stream" does not occur. English labels ("Allow users to upload games", "Private", "Hub users", "Invite only", "Current Checkpoint", "Game Override", "Trusted/Revoked") are taken from the source; since the UI is now English throughout, the earlier question of German/English consistency no longer applies.
7. Inconsistent within the prototype: revisions "Rev 41" (conflict) vs. "v6" (history) in 3k; filter "Needs attention · 2" in 3c without an unambiguous count.
8. Hub Saves (3k) shows "Restore" in the history; whether this is intended in the Hub is open (`03-saves.md` names the conflict actions, not restoring). ADR 0005: no "Restore" in phase 3; "Rev N" = checkpoint and "vN" = history are numbered separately, which resolves the first point of deviation 7.
