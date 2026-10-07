# Design specification

Purpose: token-efficient preparation of the UI prototype, so that implementation agents read only the necessary part per screen. The prototype is a mock-up; where it contradicts, the architecture applies (`../architecture/09-ui-and-navigation.md`), deviations see below. Mock values (e.g. core version "1.2.0", user names, device names, versions, addresses) are illustrative; the seeded registry expects melonDS DS 1.4.0. Hosts in the mock are generic placeholders (`hub.example.com`, `hub.local`, `lena.example.net`).

Source: Claude Design handoff "FrameBeam Designs v3" (second round, accepted by Fabio on 2026-10-07); `source/framebeam-designs-v3.dc.html` is the current state, older versions are not carried over. Two further sources belong to it: `source/framebeam-logo.dc.html` (logo sheet) and `source/framebeam-mark.dc.html` (mark component), see [logo.md](logo.md). The files stay unchanged and do not render standalone (runtime `support.js` is missing from the repository). 19 screens (3a-3s) at 1440 x 900; mock data in the script from line 1227 (`renderVals` at line 1433).

Language note: the source texts are English throughout (the earlier German texts and labels are gone). Screen labels in the source are `data-screen-label` values; the heading names in the specification files may be shortened (3l appears in the source as "Hub Systems and Cores", 3q as "Hub Settings revised").

Reading note: read only the necessary file or section. Open the source HTML only for details in the given line range (lines = screen container from `data-screen-label` to the end; anchors `#3x` = block ID in the source). The screens appear in the source in this order: 3a-3f, 3p, 3g, 3h, 3r, 3i, 3j, 3k, 3s, 3l, 3m, 3n, 3o, 3q.

- `tokens.md`: colors, typography, spacing, radii, shadows, layout dimensions (Player dark, Hub light).
- `player.md`: screens 3a-3i, 3p, 3r (FrameBeam Player, Qt 6 QML).
- `hub.md`: screens 3j-3o, 3q, 3s (FrameBeam Hub web interface, `html/template` + htmx).
- `logo.md`: logo concept, tones, sizes, wordmark.

## Screens

| Screen | Specification | Source (anchor, lines) | Phase / version |
|---|---|---|---|
| 3a Player Connection | [player.md](player.md#3a-player-connection) | `#3a`, 30-52 | 2 |
| 3b Player Pairing | [player.md](player.md#3b-player-pairing) | `#3b`, 58-72 | 2 |
| 3c Player Library | [player.md](player.md#3c-player-library) | `#3c`, 78-177 | 2; filter chips and pills in the 0.5 Player UI pass |
| 3d Player Save Conflict | [player.md](player.md#3d-player-save-conflict) | `#3d`, 183-211 | 3 |
| 3e Player Emulation | [player.md](player.md#3e-player-emulation) | `#3e`, 217-293 | 5; rebuilt: 0.5 Player UI pass |
| 3f Player Controllers | [player.md](player.md#3f-player-controllers) | `#3f`, 299-373 | 5; rebuilt: 0.5 Player UI pass |
| 3p Player Settings (new) | [player.md](player.md#3p-player-settings) | `#3p`, 379-440 | 0.5 Player UI pass; hub edit (address, port): 0.4 feature, UI in 0.5 |
| 3g Player Session | [player.md](player.md#3g-player-session) | `#3g`, 446-512 | 4; layout switch, fullscreen: 0.5 Player UI pass; save slot and snapshot, Direct/Relayed: 0.4 features, UI in 0.5 |
| 3h Player Side-by-Side | [player.md](player.md#3h-player-side-by-side) | `#3h`, 518-571 | 4; per-tile connection type: 0.4 feature, UI in 0.5 |
| 3r Player Multiview Grid (new) | [player.md](player.md#3r-player-multiview-grid) | `#3r`, 577-615 | 0.4 feature (up to 4 tiles), UI in 0.5 |
| 3i Player PiP | [player.md](player.md#3i-player-pip) | `#3i`, 621-665 | 4 |
| 3j Hub Library | [hub.md](hub.md#3j-hub-library) | `#3j`, 681-729 | 1 |
| 3k Hub Saves | [hub.md](hub.md#3k-hub-saves) | `#3k`, 735-819 | 3; slots, snapshot markers: 0.4 features, UI in 0.6 |
| 3s Hub Saves Slots (new) | [hub.md](hub.md#3s-hub-saves-slots) | `#3s`, 825-883 | 0.4 feature (slots, snapshots, restore, retention), UI in 0.6 |
| 3l Hub Systems and Cores | [hub.md](hub.md#3l-hub-systems-and-cores) | `#3l`, 889-996 | 5; rebuilt: 0.6 Hub UI pass |
| 3m Hub Clients | [hub.md](hub.md#3m-hub-clients) | `#3m`, 1003-1039 | 1 |
| 3n Hub Users | [hub.md](hub.md#3n-hub-users) | `#3n`, 1045-1082 | 5 |
| 3o Hub Settings (superseded by 3q) | [hub.md](hub.md#3o-hub-settings-superseded-by-3q) | `#3o`, 1088-1116 | 1 (parts), 5; reference only |
| 3q Hub Settings revised (new) | [hub.md](hub.md#3q-hub-settings-revised) | `#3q`, 1122-1221 | 0.6 Hub UI pass; Network section (relay, public address): 0.4 feature, UI in 0.6 |

Phases per `../workflow.md` (phase plan); 0.4-0.7 are the versions of the roadmap (`../roadmap.md`). Borderline cases: 3o (reference only) phase 1 covered only Hub name, address, transport and certificate, admin account; appearance (dark/light) and "Allow users to upload games" phase 5 (requires users); these now live in 3q. 3m is phase 1 (Allow/Decline, Revoke); the assignment to an existing user (architecture 10) belongs to it as well, but requires created users (until phase 5 only admin). 3k and 3d both belong to phase 3, although 3k is a Hub page. 3l phase 5 (firmware path); nav badges of the Hub shell ("1 conflict", "2 issues", "1 request", Settings "1 update") follow the respective phases. The Hub shell (sidebar, page header, tables) is created with the first Hub page in phase 1. "UI in the 0.5/0.6 pass" means: the feature (protocol, storage, diagnostics) arrives in 0.4, the screen in the named UI pass.

## Decisions (Fabio, 2026-10-07)

1. Accent is Signal Cyan: `#3cbfd8` (Player dark) and `#1a7f96` (Hub light; hover `#0f5a6b`, selection `#eef8fa`, chip `#ddf1f6`, border `#a6d9e6`; Player chip surface `#15262b`). The old amber `#e9b44c` is gone ([tokens.md](tokens.md)).
2. Cyan is intentionally also the warning color; there is no separate warning color. Warnings (conflict, hub too old, awaiting approval, relayed) therefore read as "needs attention", not as danger; errors keep their red.
3. 3q "Hub Settings revised" supersedes 3o. 3o stays in the source only as reference and is marked superseded in the table.
4. Logo: concept 4a "Frame & Beam", palette "signal" ([logo.md](logo.md)).

## Deviations from the architecture

Noted against the architecture; the status after the PoC is given per item. Checked against `09-ui-and-navigation.md`, `03-saves.md` and `10-identity-pairing-tls.md`; other files not read (points there: unclear).

Resolved history (first handoff round):

- Fingerprint confirmation (3a) and "Certificate trusted" without confirmation (3b): the Player shows both fingerprints and requires confirmation (ADR 0003, ADR 0009). The architecture applies.
- Appearance "Light | Dark | System" (3o/3q): implemented in Hub and Player; the second palette per product is derived, not designed ([tokens.md](tokens.md), ADR 0007 D7).
- Revision numbering "Rev N" (checkpoint) vs "vN" (history) and "Restore" in the Hub: numbered separately by ADR 0005; "Restore" was not in phase 3 and is now drawn in 3k/3s (0.4 feature, confirmation drawn).
- Terms: UI texts use "Session"; "Stream" does not occur. Labels such as "Allow users to upload games", "Invite only", "Current Checkpoint" are taken from the source.
- "Needs attention" count in 3c: the chip counts now add up (All 42 = Ready 37 + Needs attention 2 + Not downloaded 3); the 8 sample tiles do not mirror them (three attention states, two downloads), which is mock data only. What counts as "needs attention" stays open.
- Diagnostics as an equal-ranking tab next to Session and Multiview (3g-3i): accepted as drawn (ADR 0006 D6); the architecture only requires it to be optional/collapsible, which the collapsible panel in 3g satisfies.
- Not drawn but named by the architecture: Hub switcher dialog, admin setup and login in the Hub, metadata actions in the library entry (later), firmware block states in the library detail (3c shows only the success case). Settings → Hubs is now drawn in 3p (previously open); the multiview picker "Add to multiview" is now drawn in 3r.

Open points (new handoff):

a. In-game layout switch vs Emulation default: 3g-3i have a layout switch (Stacked / Side by side / Top only) and 3e has the option "DS screen layout" as default. Open: does the in-game switch change the default permanently or only the running game (this Session)? In Multiview the layout applies to all tiles.
b. "Side by side" and "Top only" are DS-specific. For 3DS (0.7) the screens differ in size; the available layouts must come from the system manifest, not be hard-wired.
c. 3e options "Renderer" and "Internal resolution" need OpenGL hardware rendering, which the Player does not have until 0.7. Only options the core reports are shown (the screen says so); until 0.7 these two are not offered or shown as unavailable (open).
d. Esc exits fullscreen (toolbar "Exit fullscreen · F11 · Esc"). Esc may collide with game hotkeys (3f has a Hotkeys tab, not drawn); open. The code has only "Fullscreen on start" today (no button, no F11).
e. Relay port range: the design (3q) shows UDP 49160-49200; the implementation in the 0.4 work uses 49160-49199 (ADR 0012, proposed). Align the design text or the ADR; the port field is UDP 3478.
f. Mock values to ignore: melonDS DS "1.2.0" (the registry expects 1.4.0); "Bundled with Windows Player" in 3l (cores come from the Hub since 0.2, ADR 0010); GBA and SNES are example systems only (the second real system is 3DS with Azahar); the 3l toggle "Show example future systems" in the source script; "Last install failed ... HTTP 404" in 3q is mock text (a real update failure was observed separately, not part of the design).
g. Emulation (3e) no longer shows where an inherited value comes from (ADR 0007 inheritance chain Global → System/Core → Game). Only "changed vs default" (dot, "Reset", "Reset N changed") is shown. Decide whether that is enough; per-game overrides are "coming later".
h. 3p: the second column (Updates, Hubs, Appearance, Diagnostics) and the main area show all sections below each other. Open whether the column is only jump anchors or real sub-pages (in 3q they are real sub-pages); should be uniform.
i. Logo light accent is `#1a8aa3`, the Hub UI accent is `#1a7f96`: small inconsistency, one of them should win (open).
j. Descriptive texts in 3q and 3s state behavior that is not verified against ADRs or code: certificate "renews automatically 30 days before expiry", retention (all of the last 48 hours, then last per day for 30 days, then one per month; snapshots kept until deleted), "Players load the restored save on their next start", "not possible while a session of this game is running". Deleting a snapshot is mentioned but not drawn.
k. The logo sheet shows a favicon "FrameBeam Player · Web"; there is no web player. The Hotkeys tab (3f) and the "+" for a new save slot (3g, 3k, 3s) are drawn but have no screens.
l. "Default Multiview" in 3e offers only "Picture-in-Picture" and "Side-by-Side", while 3r adds the mode "Grid 2×2" (up to four tiles); whether the default can be the grid is open.
