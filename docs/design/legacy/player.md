# Design: FrameBeam Player (QML, dark)

Source: `source/framebeam-designs-v3.dc.html`, line ranges per screen = screen container (without caption). Mock data is in the script from line 1362 (`renderVals` at line 1626, diagnostics in `dgVals` at line 1573); all names, versions, addresses and counts in this file are illustrative mock values. Tokens: `tokens.md`; logo: `logo.md`. All dimensions in px at 1440 x 900. Screen order in the source: 3a-3f, 3p, 3g, 3h, 3r, 3i, 3t-3y.

Revision handoff of 2026-10-08 (after a friends playtest): the sections 3c-2 to 3c-5, 3e-2, 3p-2, 3f-2/3f-3, 3g-2/3g-3, 3r-2/3h-2/3i-2 and 3t-2/3x-2 at the end of this file and the component specs under "Components (revision v4)" come from `source/framebeam-designs-v4.dc.html` (line ranges there refer to that file) and supersede the v3 sections of the same screens where they differ; unchanged parts of the v3 sections stay valid. Open questions of that handoff and the defaults decided: `decisions.md`.

## Common shell (3c, 3e, 3f, 3p)

- Grid: sidebar 232px | content (1-3 further columns per screen). Sidebar: background `#0e0f10`, border right, padding 24 16, gap 28.
- Sidebar top: Player mark (`color` tone, 22px, see `logo.md`) + "FrameBeam Player" 16/600.
- Navigation (order): Library, Emulation, Controllers, Settings. Active: surface `#1f2024`, radius 6, 500; inactive `#a3a3a8`. Item padding 9 10, 14px. Settings shows a small accent dot (7px, `#3cbfd8`) while a Player update is available (3p).
- Second column (pattern, 300px, surface `#16171a`, border right, padding 28 16): used by 3e (Defaults and systems), 3f (devices) and 3p (sections). Eyebrow title (mono 11) at the top; items radius 8, padding 11 12, selected surface `#1f2024` with ring `0 0 0 1px #2f3035`.
- Bottom: Hub switcher card (status dot green, Hub name, on the right "switch", below it mono 11 "hub.local · active hub"), then the user block (avatar 28 "M", "Max", device name "Desktop-LivingRoom"), shown in 3c, 3e, 3f and 3p.
- In game (3g, 3h, 3r, 3i, 3t-3y) the sidebar is dropped; instead a 56px header (see there). The status pill component (`● Ready`, `▲`, `✕`) is specified in `tokens.md`.

## 3a Player Connection
Superseded in part by [3a-3](#3a-3-player-connect-round-3) (Round 3: one host:port field, Pair again).
Lines 31-53. Start/connection screen before the main navigation.

- Purpose: stored Hubs, connection status, add Hub, connect.
- Layout: screen centered (`place-items:center`), column 620px, gap 28; background `#121315`, no sidebar.
- Header: Player mark + "FrameBeam Player", title "Connect to a hub" (30/600).
- Hub cards (column, gap 10, padding 16 18, radius 10, surface `#1a1b1e`); per card name 16/600 + status mark, mono 12 line with address and details:
  - State reachable (selected, accent ring 1.5): "Home", pill "● Reachable" (ok), "hub.local:8443 · as Max · last today 18:40", button "Connect" (40 high, accent).
  - State certificate changed: "Studio Lena", pill "✕ Certificate changed" (error), "lena.example.net:8443 · as Max", link "Verify fingerprint"; error box (surface `#2a1a17`): "The fingerprint no longer matches the saved one. The connection is blocked until you have verified and confirmed the new fingerprint in the hub web interface." No Connect button. (Where the confirmation happens: see README, resolved by ADR 0009.)
  - State Hub too old: "Office", pill "▲ Hub too old" (warn, cyan), "hub.office.lan · Hub speaks protocol v0, Player requires v1", action "Remove".
- Add Hub: input field (44 high, placeholder "Hub address, e.g. hub.local:8443", mono) + button "Add hub" (outline).
- Footer (divider): toggle (on, accent) "Automatically connect to the last used hub on startup"; mono 12 "This device: Desktop-LivingRoom · Player 0.1.0 · Windows x86-64".
- States: reachable, certificate changed (blocked), Hub incompatible (protocol). Offline/unreachable: not shown (unclear).
- Remove is shown only for the incompatible Hub; whether it is available on all cards is unclear. Hubs are also managed in 3p (Hubs section, with Edit).

## 3b Player Pairing
Lines 59-73. Add Hub: certificate (TOFU) and approval by admin.

- Layout: centered, column 640px, gap 14. Title "Add hub" (30/600) + mono 13 address "hub.local:8443".
- Three step cards (grid 18px | text, gap 4 12; status dot 10px; radius 10, padding 16-18):
  1. "Hub detected": "Home · FrameBeam Hub 0.1.0 · Protocol v1 · compatible" (dot filled, accent).
  2. "Certificate trusted": SHA-256 fingerprint in mono 12 (two lines, hex pairs), note "Saved. If the certificate changes later, the connection will be blocked." (dot filled). The drawn "Certificate trusted" without a confirmation is a deviation, see README.
  3. "Approve device" (active, accent ring; dot as ring):
     - Segment switch: "Request approval" (active) | "Redeem invite".
     - Status: spinner + "Waiting for admin approval" / "The request appears in the hub under Clients."
     - Data block (label column 130px): device name "Desktop-LivingRoom", platform "Windows x86-64", Player version "0.1.0".
     - Button "Cancel request" (38 high, outline).
- Not shown: "Redeem invite" view (code entry, display name), states denied/revoked/error (unclear).

## 3c Player Library
Lines 79-178. Main screen; readiness states, save conflict, discovering Sessions.

- Grid: sidebar 232 | main (flexible) | detail column 392 (surface `#16171a`, border left, padding 28).
- Main (padding 28 32, gap 28):
  - Header: title "Library" (26/600), meta "42 games · Nintendo DS", on the right search field 220 x 34 ("Search…").
  - Filter chips with counts (32 high, radius 16, spec in `tokens.md`): "All · 42" (active), "Ready · 37", "Needs attention" + counter badge "2" (cyan on `#15262b`), "Not downloaded · 3". The counts add up to 42; what exactly counts as "needs attention" is open.
  - Section "SESSIONS ON THIS HUB" (eyebrow green with dot); 2-column grid, gap 12; card: avatar 30, "{user} · {game}", meta 12. Examples: "Lena · Harbor Rally", meta "Hub users · for 24 min", action "Watch session"; "Jonas · Clocktower Kids", meta "invites you", actions "Decline" / "Join" (accent).
  - Game grid: 4 columns, gap 20 18. Tile: square, mono 11 "NDS" at the top, monogram (initials, 36/600) at the bottom; selection = accent border 2px (`#3cbfd8`, offset -3, radius 8). Below it title 14/500 (ellipsis) and status 12/500.
  - Status variants per game (text only in the grid): "● Ready" (ok); "↓ Download required · {size}" (muted `#a3a3a8`); "▲ Save conflict" (warn, cyan); "✕ Hash mismatch · reload" (error); "⟳ Save sync pending" (muted).
  - Mock: Lumen Drift (ready, selected), Harbor Rally (conflict), Paper Wizards (download 128 MB), Clocktower Kids (ready), Orbit Gardens (download 16 MB), Tide & Lantern (sync pending), Copper Courier (hash mismatch), Stylus Knights (ready).
- Detail column (selection):
  - Header: cover 112 + status pill "● Ready to play" (ok pill, see `tokens.md`) + title "Lumen Drift" (22/600) + "Nintendo DS · melonDS DS 1.2.0" (13, muted; core version is a mock value).
  - Table (label left muted, value right, rows padding 11), values with a leading check: ROM "✓ Cached locally"; Core "✓ Ready"; Firmware "✓ Valid"; Save "✓ Synced · 18:42"; "Last saved by" "Laptop-Office".
  - Link "Show details (hashes, size, paths)" (12, muted).
  - Section "START" (eyebrow): checklist, row grid 18 | label | meta; dot filled = done, ring = active: "Game data from hub" (current), "ROM verified from cache" (no download), "Firmware present" (verified), "Current Checkpoint loaded" (Rev 88), "melonDS DS starting" (active).
  - Bottom: primary button "Play" (48, accent), secondary "Play and share session" (44, outline).
- Open/unclear: states of the detail column for download, hash mismatch and conflict (only the success case is drawn); search behavior; whether the filter chips combine with the search.

## 3d Player Save Conflict
Lines 184-212. Modal dialog on start reconciliation.

- Layout: screen `#08090a` with dimmed library (stripe pattern, caption "Library (dimmed)" top left, mono 12); dialog centered, 760 wide, surface `#16171a`, border `#2c2d32`, radius 12, padding 32, gap 24.
- Header: eyebrow "▲ SAVE CONFLICT · HARBOR RALLY" (warn, cyan), title "The hub and this device have different saves" (24/600), text "This device kept playing offline while another device saved a new checkpoint. Nothing is overwritten until you decide. Both saves are backed up to the history first."
- Two comparison cards (2 columns, surface `#1d1e22`, radius 9):
  - "On the hub": "Current Checkpoint · Laptop-Office"; "today, 19:10 · Rev 41"; "Base: Rev 40".
  - "On this device": "Local · Desktop-LivingRoom"; "today, 19:24 · Sync pending"; "Base: Rev 40".
- Actions (column, 46 high, gap 8; on the right a note 12 each):
  - "Use hub version" (outline; "local save stays backed up").
  - "Use local save as new current version" (outline; "becomes new checkpoint").
  - "Keep both, decide later" (primary, accent; highlighted as default).
- Footnote: "Decide later: the conflict stays visible on the game and on the Saves page in the hub."
- Open: confirmation step before an overwriting action, error case while securing, keyboard operation not drawn.


## 3e Player Emulation
Lines 218-294. Rebuilt: defaults and per-system settings. Replaces the earlier level switch (Global → System/Core → Game Override) and the per-row origin text.

- Grid: sidebar (Emulation active) 232 | second column 300 (surface `#16171a`) | main.
- Second column: eyebrow "EMULATION"; item "Defaults" (sub "Apply to every system"); eyebrow "SYSTEMS"; one item per system: name 14/600, on the right "{n} changed" (11, `#3cbfd8`, only if changed), sub 12 in ok green: "melonDS DS 1.2.0 · ● Ready". Selected: surface `#1f2024`, ring `0 0 0 1px #2f3035`. Example systems "Game Boy Advance" and "Super Nintendo" appear in the mock only (the second real system is 3DS, see README). Bottom: dashed card (border `#2c2d32`) "Per-game settings" / "Coming later. Set from a game's page in the Library."
- Main (padding 28 36, gap 20): header with title (26/600) and search field (260 x 36, "Search settings…"):
  - Defaults selected: title "Defaults for all systems", sub "Systems use these unless you change them there. Saved on this device only."
  - System selected: title "{system} · {core} {version}" (mock "Nintendo DS · melonDS DS 1.2.0"), sub "Only options this core reports. Saved on this device only."
- Category chips (32 high, radius 16): "All" + the categories that have options in the current scope, from Display, Video, Audio, Input, System. Right: link "Reset {N} changed" (13, underlined) when any option differs from its default (mock: "Reset 2 changed" for Nintendo DS).
- Restart hint (surface `#15262b`, text `#3cbfd8`, 13, radius 8, padding 10 14): "Some changes apply after the next game start." Shown once an option marked "applies on next start" has been changed.
- Option groups per category (eyebrow title uppercase, e.g. "DISPLAY"); row = grid `1fr | auto | 90px`, gap 20, padding 13, divider `#1f2024`:
  - Column 1: label 14 with a changed dot (7px, `#3cbfd8`) when changed and a badge "applies on next start" (11, surface `#232428`, radius 4) for restart options; description 12 below.
  - Column 2: control: segment, toggle (cyan) or select (200 x 34, value + "▾").
  - Column 3: "Reset" (underlined, resets to the default) when changed, otherwise "Default" (`#5e5e63`).
- Options (mock catalog; the real list comes from the core, see open points):
  - Defaults (system independent): Display: "Fullscreen on start" (toggle, default Off), "Default Multiview" (Picture-in-Picture | Side-by-Side; default Picture-in-Picture).
  - Nintendo DS: Display: "DS screen layout" (Stacked | Side by side | Top only; default Stacked). Video: "Renderer" (Software | OpenGL; default Software; applies on next start), "Internal resolution" (1× (256×192) | 2× (512×384) | 3× (768×576) | 4× (1024×768); default 1×; "OpenGL renderer only"). Audio: "Audio interpolation" (None | Linear | Cosine | Cubic; default None). Input: "Touch mode" (Mouse | Touch | Joystick; default Mouse). System: "Console type" (DS | DSi; default DS; applies on next start).
  - Values of "Renderer" and "Internal resolution" come from the core and are not filtered by the Player (Fabio, 2026-10-07): melonDS DS v1.4.0 offers 1x to 8x via `melonds_opengl_resolution` and a "compute" renderer (needs GL 4.3); the 1× to 4× list above is illustrative.
  - Mock state: Renderer "OpenGL" and Internal resolution "3× (768×576)" changed, the rest default.
  - Game Boy Advance (example): "Color correction" (toggle), "Solar sensor level" (0 | 5 | 10). Super Nintendo (example): "Region" (Auto | NTSC | PAL), "Hi-res blending" (toggle).
- "Renderer" and "Internal resolution" are available since 0.5 (OpenGL hardware rendering, ADR 0013 D5; they apply on next start); "Default Multiview" offers "Grid 2×2" (ADR 0014 D13); "DS screen layout" is the global default and the in-game layout switch (3g) applies only to the running game (decided by Fabio, 2026-10-07); where an inherited value comes from is no longer shown (README); views for "Firmware missing" (architecture: block launch) not drawn.

## 3f Player Controllers
Lines 300-374. Profiles, remapping, input test.

- Grid: sidebar (Controllers active) 232 | device list 300 | main | input test 340 (surface `#16171a`).
- Device list: eyebrow "DEVICES"; entries (radius 8, padding 12; selected surface `#1f2024`): name 14/500, on the right slot (mono 11), below it status and profile 12. Mock: "Xbox Wireless Controller" P1 "● Connected · Standard Gamepad" (selected); "Keyboard" "—" "● Connected · Keyboard Standard"; "Mouse" "Touch" "● Connected · DS-Touch"; "DualSense" "—" "Not connected · last used 02.10." (muted).
- Main: header: title "Xbox Wireless" (26/600) over the pill "● Connected · P1" (ok), sub "Saved on this device only"; on the right profile dropdown "Standard Gamepad ▾" (210 x 36).
- Tab chips (32 high, radius 16): "Buttons" (active), "Hotkeys" (not drawn); on the right link "Reset 2 changed" (13, underlined).
- Table grid `1fr | 170 | 80 | 60`, gap 16: header (eyebrow mono 11) "INPUT" | "MAPPING" | "NDS". Rows (padding 8, divider `#1f2024`): input name with a changed dot (7px cyan) when changed | mapping field (30 high, surface `#1a1b1e`, border `#2c2d32`) | NDS target (mono 13, `#8e8e94`) | "Reset" (changed) or "Default". Mock: A→"Ⓑ  B"→A (changed); B→"Ⓐ  A"→B (changed); X→"Ⓨ  Y"→X; Y→"Ⓧ  X"→Y; L→"LB"→L (listening state: border and text `#3cbfd8`, text "Press a button…"); R→"RB"→R; Start→"Menu"→START; Select→"View"→SELECT; D-pad→"D-pad / left stick"→D-PAD; "Close lid"→"unassigned"→LID.
- Actions at the bottom: "Reset profile to default", "Duplicate profile" (outline, 38 high).
- Input test: eyebrow "INPUT TEST", text "Press buttons on the controller — active inputs light up."; grid 4 columns, tiles 44 high: A, B, X, Y, L, R, ▲, ▼, ◀, ▶, ST, SE; active tile = accent surface (mock: A and ▶). Section "DS-TOUCH": surface 150 high with text "Mouse on bottom screen", cursor ring; note "Left mouse button = stylus".
- Hotkeys tab: now built; its content is not drawn and follows the Buttons table (ACTION | KEY | Reset/Default, changed dot, "Press a key…"). Esc stays fixed (read-only row). Open: analog stick/trigger display, slot assignment and profile management (delete/rename) not drawn; SDL3 mapping details are not part of the design. The earlier note "Profiles stay local" is replaced by "Saved on this device only".

## 3p Player Settings
Lines 380-441. New. Updates, Hubs (with edit), Appearance, Diagnostics.

- Grid: sidebar (Settings active, accent dot while an update is available) 232 | section column 300 | main (padding as 3e).
- Section column: eyebrow "SETTINGS"; items (name + status line): "Updates" "▲ Update available · 0.3.2"; "Hubs" "2 saved · Home active"; "Appearance" "Dark"; "Diagnostics" "Logs and support". Bottom mono: "Player 0.3.1-beta.215" / "Windows x86-64 · Protocol v1" (mock versions). Open: anchors or sub-pages (README).
- Main: title "Settings", sub "Saved on this device only"; sections below each other:
  - UPDATES: update card (surface `#1a1b1e`, border `#1a3238`): "▲ Update available" (warn), "0.3.2-beta.219" (mono), "24 MB · released today"; text "You have 0.3.1-beta.215. Installs on the next start, never during a game."; buttons "What's new" (outline) and "Restart and update" (accent). Rows: "Update channel" with segment "Stable" | "Beta" (tag "this build" on Beta) and text "Tested releases. Recommended for everyday use." / "Pre-release builds from every change on main. May contain bugs."; "Install updates automatically" toggle (on), "Downloaded in the background, applied on the next start"; "Last checked" "today, 18:41 · checks every 6 hours while the Player runs" with button "Check now". The cadence in the mock is illustrative: the Player checks hourly on Beta and every 24 hours on Stable (`client/core/updater.cpp`), so the text must follow the selected channel or leave the cadence out.
  - HUBS: header "HUBS" with action "+ Add hub". Hub row: name 15/600 + pill (mock "Home" "● Connected"; "Studio Lena" "Not connected" neutral), mono line "{address} · as Max · last today 18:40" (mock "hub.example.com:8443"; "lena.example.net:8443 · as Max · last 02.10."), right: "Active hub" (active row) or "Switch", then "Edit", "Remove".
    - Inline edit (opens under the row; mock shows it open on "Home"): grid `auto | 1fr | auto | 96 | auto`: label "Address" + input (host name only), label "Port" + input, buttons "Cancel" (outline) and "Save" (accent `#3cbfd8`/`#161512` when valid; disabled `#2c2d32`/`#6f6f75` otherwise). Invalid field: border `#c4544a`; messages (joined with " · ", error text under the row): empty "Enter an address"; contains `://` "Leave out https:// — host name only"; contains spaces "No spaces in the address"; contains a colon (outside `[...]`) "Put the port in the port field"; other characters "Only letters, digits, dots and hyphens"; port not a number 1 to 65535 "Port must be a number from 1 to 65535".
    - Hint under the edit row: "Certificate fingerprint stays the same. If the hub at the new address shows a different certificate, the Player stops and asks you to compare it."
    - Toggle "Connect automatically on startup" (on), "Uses the last active hub".
  - APPEARANCE: "Theme" with segment "Dark" | "Light" | "System", text "The game view always stays dark".
  - DIAGNOSTICS: "Log file" with mono path (mock "…\FrameBeam Player\data\logs\player.log") and buttons "Copy path", "Open folder".
- Phase note: editing address and port is a 0.4 feature (see README); the screen is built in the 0.6 UI pass.
- Open: add-hub flow from here (reuses 3a/3b), confirmation on Remove, what "Switch" shows while connecting, error state of "Check now".

## 3g Player Session
Lines 447-518. In game, Session shared, visibility "Invite only" with user selection; layout switch, fullscreen, save slot, diagnostics overlay (Emulation and Streaming, see 3t-3y).

- Grid: rows 56 | 1fr; columns 1fr | 340; background `#0b0b0c`; no sidebar. Fullscreen: header and side panel are dropped, the play area fills the screen. The diagnostics overlay then shows the same sections as windowed (Emulation and Streaming; changed from 3y "Emulation only" after a user report).
- Header (across both columns, surface `#111214`, padding 0 20): "← Library", divider, game title "Lumen Drift" (15/600), pill "● Session shared · 1 watching" (ok); tab segment "Session" (active) | "Multiview" | "Diagnostics"; on the right (after a spacer):
  - Layout switch (segment, 13): "Stacked" | "Side by side" | "Top only", each with a mini icon (two 9 x 7 boxes with 1.5 border, laid out in a column or row; "Top only": second box at 25 % opacity). Default "Stacked". Open: does it change the default (README).
  - Fullscreen button (34 high, padding 0 12, radius 7, border `#3a3b40`, 13/500): square icon (12, dashed border) + "Fullscreen" + mono 11 "F11".
- Play area: two DS screens (each 480 x 360, placeholders "top DS screen" / "bottom screen · touch via mouse", lower one with dashed outline) in a column (Stacked), a row (Side by side) or only the top screen (Top only).
- Diagnostics overlay (top left of the play area, offset 16, 350 wide): the common overlay with two sections as specified under 3t-3y. Mock state: Emulation collapsed to its summary line ("59.83 fps · 9.4 ms"), Streaming open with "You" (host, Local, "Encoder NVENC · H.264 · 6.0 Mbit/s · 60.0 fps", "Sending to 1 viewer · adaptive bitrate") and "Lena" (viewer, Relayed (TURN), "Decoder on her Player · 3.9 Mbit/s · 59.7 fps", "RTT 48 ms · Loss 0.4 % · via hub.example.com:3478"). Opened by the Diagnostics tab, the toggle at the bottom of the side panel ("▾ Hide diagnostics" / "▸ Show diagnostics") or F3.
- Fullscreen toolbar (see below).
- Side panel 340 (surface `#111214`, padding 20 24, gap 14):
  - "VISIBILITY": segment "Private" | "Hub users" | "Invite only" (active).
  - "INVITED · 2" with note on the right "only you can change this"; list: "Lena" (green dot, "watching", pill "Relayed (TURN)", action "Remove"), "Jonas" (gray dot, "invited · offline", action "Withdraw"). Relay hint (surface `#14222a`, border `#1a3238`, text `#a9cdd6`, 12): "ⓘ Lena is relayed via the hub (TURN) · may lag slightly."
  - Invite field (accent border, input "Sa", note on the right "Users on this hub") with result list: "Sam" ("online"), "Sarah" ("offline · receives the invite while the session is running"), each with button "Invite" (accent).
  - Note: "Invitees can only watch and listen. They send no input and cannot invite others."
  - Bottom: block "SAVE SLOT" with note "synced to hub": slot selector (segment, mock "Main" | "100% run", plus a "+" button for a new slot), button "Save snapshot" with mono "F5", status line: "Checkpoint 40 s ago to “Main” · final sync on pause or exit"; after a snapshot "Snapshot v15 saved to “Main” · kept until you delete it". Then buttons "Stop sharing" (outline, 42), "Quit game and save" (surface `#1f2024`, 42) and the diagnostics toggle (with the F3 hint in the diagnostics overlay).
- Fullscreen toolbar (3g, 3h, 3i): floating bar at the top center (top 18, padding 6 6 6 14, radius 10, surface `rgba(22,23,26,.92)`, border `#2c2d32`, shadow `0 12px 30px rgba(0,0,0,.5)`): title (13/600; 3h/3i "Multiview · Side-by-Side" / "Multiview · Picture-in-Picture"), divider, layout switch, button "Exit fullscreen" with mono "F11 · Esc". Hint at the bottom center (12, `#5e5e63`): "Toolbar appears when you move the mouse to the top". The mock draws the bar permanently; the intended behavior is the hint text (appears on mouse at the top).
- "← Library" pauses the game and keeps it loaded in the background; the Library shows it as running with Resume and Quit game (interim UI; 0.7.x).
- Phase note: layout switch and fullscreen: 0.6 UI pass; save slot, snapshot and Direct/Relayed: 0.4 features (README).
- Open: views for visibility "Private" and "Hub users", behavior when the Hub or connection fails, header pill without a Session; Esc vs game hotkeys, layout vs Emulation default, 3DS layouts (README); slot creation and deletion not drawn; fullscreen diagnostics see 3y.

## 3h Player Side-by-Side
Lines 524-572. Multiview, side-by-side, diagnostics expanded (two sections).

- Grid: rows 56 | 1fr | auto; background `#0b0b0c`.
- Header: "← Library", divider, title "Multiview", mode segment "PiP" | "Side-by-Side" (active) | "Grid 2×2"; on the right tab segment "Session" | "Multiview" (active) | "Diagnostics", then layout switch and "Fullscreen" button as in 3g. The layout applies to all tiles at once (local and remote).
- Middle: two equal-width columns (gap 2, background `#1e1f22` as divider); per column: header with avatar 28, "{who} · {game}", meta 12 and audio button ("Audio active" accent or "Audio here" outline); below it two screens (placeholders "{label} · top/bottom"; sizes per layout: Stacked 360 x 270, Side by side 300 x 225, Top only 480 x 360 with the bottom screen hidden).
- Mock (script): surface 1 "You · Lumen Drift", meta "local", audio active; surface 2 "Lena · Harbor Rally", meta "Lena's session", button "Audio here".
- Diagnostics panel at the bottom (surface `#111214`, border top `#26272b`, padding 12 20 14, windowed mode only): the multiview variant of the common overlay, two sections Emulation and Streaming with the F3 hint ("F3 show / hide", "Hide"); per-tile content see 3x. Mock: Emulation tile 1 local "melonDS DS 1.4.0", tile 2 remote note; Streaming grouped per tile session ("You" Local, "Jonas" Relayed (TURN) under tile 1; "Lena" Direct under tile 2).
- Fullscreen: header and diagnostics panel disappear (diagnostics in fullscreen: 3y); toolbar see 3g.
- Exactly one surface has audio; switching via "Audio here". Phase: per-tile connection type 0.4 feature, UI in 0.6.

## 3r Player Multiview Grid
Lines 578-616. New. Up to four tiles, picker "Add to multiview", audio focus on one tile.

- Grid: rows 56 | 1fr; background `#0b0b0c`.
- Header: "← Library", divider, "Multiview", mode segment "PiP" | "Side-by-Side" | "Grid 2×2" (active), mono 12 "{n} of 4 tiles · keys 1–4 move audio", tab segment "Session" | "Multiview" (active) | "Diagnostics", button "+ Add to multiview" (34 high, border `#3a3b40`; surface `#2c2d32` while the picker is open). No layout switch and no fullscreen button are drawn in this screen (open).
- Picker (popup under the header, top 50, right 20, width 400, surface `#1d1e22`, border `#2f3035`, radius 10, shadow `0 16px 40px rgba(0,0,0,.55)`): title row "Running sessions", count "{n} of 4 tiles", "Close". Row per watchable session: avatar 28, "{who} · {game}", meta 12 (relayed ones end with " · relayed"), right: "Add" (accent, 12/600) / "✓ Added · Remove" (added) / disabled "Add" (surface `#26272b`, text `#6f6f75`, when full). Note: "Sessions you may watch. Private sessions are not listed. Sound plays from one tile only." When full: "Multiview is full. Remove a tile to add another session."
- Tiles: 2 x 2 grid, gap 2, background `#1e1f22`; tile padding 14 20 20, gap 12:
  - Header row: key badge 20 x 20 (radius 4, border `#3a3b40`, mono 11, "1" to "4"), avatar 28, "{who} · {game}" 14/500, meta 12 with the connection pill (Direct ok / Relayed (TURN) cyan; none on the local tile), audio button ("Audio active" accent / "Audio here" outline) and remove "×" (28 x 28, tooltip "Remove from multiview"; not on the local tile).
  - Two screens (300 x 225 placeholders "{local|remote} · top/bottom") side by side.
  - Audio focus: the tile with sound gets ring `inset 0 0 0 2px #3cbfd8`; keys 1–4 or "Audio here" move it.
  - Empty tile: "+" / "Add a session" / "Tile {n} is free" (click opens the picker).
- Mock states: 3 of 4 (You · Lumen Drift local; Lena · Harbor Rally, Hub users · 24 min, Direct; Jonas · Clocktower Kids, Invite only · 8 min, Relayed), tile 4 empty, picker open. Further picker sessions: Sam · Orbit Gardens (Hub users · 3 min, Direct), Sarah · Paper Wizards (Invite only · you are invited · 41 min, Relayed). Full state: 4 of 4, all picker "Add" buttons disabled.
- Phase: 0.4 feature (up to 4 tiles, audio focus), UI in 0.6. Open: layout switch/fullscreen in the grid, behavior when a Session ends (tile becomes empty?), diagnostics per tile.

## 3i Player PiP
Lines 622-666. Multiview, picture-in-picture.

- Layout like the 3h header (mode segment "PiP" active, "Grid 2×2" added, layout switch and Fullscreen button); tab "Multiview" active. No diagnostics bar.
- Main picture: local Session, two screens (480 x 360, placeholders "local · top/bottom") centered.
- PiP window bottom right (offset 28, width 248, surface `#16171a`, border `#2c2d32`, radius 10, padding 8): header row green dot, "Lena · Harbor Rally", on the right "muted"; two remote screens (each 174 high, "remote · top/bottom"); buttons "Swap" and "Remove" (50 % each).
- Fullscreen: toolbar title "Multiview · Picture-in-Picture"; see 3g.
- Open: moving/resizing the PiP window, multiple PiPs, audio control in the PiP (only status "muted"), connection type of the remote not shown not drawn.

## Diagnostics overlay (3t-3y, common)
Lines 669-677 (intro). New; the older single "DIAGNOSTICS · CONNECTIONS" panel is gone. From the handoff after Fabio's request of 2026-10-07 to split emulator and streaming diagnostics; accepted by Fabio on 2026-10-08 and built in 0.6 per ADR 0014 (decisions.md, item 5). All values are illustrative; open points README m-t.

Three rules of the intro block:
- One overlay with two sections, each collapsing to one summary line; same look in the window, multiview and fullscreen.
- One way to open it: Diagnostics tab, the toggle at the bottom of the side panel and F3 do the same; open/closed is remembered per section.
- Streaming only when relevant: "No active session" in the window, left out in fullscreen.

Overlay (windowed single game; position top left of the play area, offset 16, width 350, padding 14, radius 10, surface `rgba(17,18,20,.94)`, border `#26272b`, gap 12, above the game):
- Section header (click toggles): caret (`▾` open, `▸` collapsed), title mono 11/500 `#7d7d83` letter-spacing .08em ("DIAGNOSTICS · EMULATION", "DIAGNOSTICS · STREAMING"), right-aligned summary mono 11 `#8e8e94` shown only while collapsed (Emulation: "59.83 fps · 9.4 ms"; Streaming: "2 · 1 relayed" or "No session"). Sections are separated by a 1px line `#1e1f22` (padding-top 12).
- Emulation rows (label column 76, 11/`#7d7d83`; value mono 12 `#ecebe7`; optional subline mono 11 `#8e8e94`): Core ("melonDS DS 1.4.0"); Renderer ("OpenGL 4.6 Core", subline "Example GPU · Driver 1.2.3"; Software: "Software", subline "CPU · 4 threads"); Resolution (scale and pixels, subline per-screen size, e.g. "3× · 768×1152", "2 screens of 768×576"); FPS ("59.8 / 59.83 fps" = actual / core target); Frame ("9.4 ms · emu 7.1 · readback 2.3"; Software "6.2 ms · emu 6.2 · no readback"); then the frame-time sparkline (236 x 34, indent 86; last 5 s; total line accent 1.25, emu line `#6f6f75` 1, area under total `rgba(60,191,216,.14)`, dashed 16.7 ms line `#3a3b40`; legend mono 10 `#6f6f75`); then Audio ("Buffer 42 ms · 0 underruns", with underruns a subline such as "raise the buffer in Emulation settings").
- Fallback hint (only when OpenGL was requested but software runs): info box (surface `#14222a`, border `#1a3238`, text `#a9cdd6` 12, radius 8, padding 9 10, icon `ⓘ` accent) "OpenGL requested · fell back to software (GL 4.3 not available)" above the rows; the Renderer row gets the pill "Fallback" (warn, cyan) and subline "requested: OpenGL"; the Resolution subline says "3× requested · needs OpenGL".
- Streaming per participant: line 1 name 13/500, role 11 (`#7d7d83`, "host" / "viewer"), connection pill right (small variant: Local neutral, Direct ok, Relayed (TURN) warn); line 2 mono 11 `#a3a3a8` encoder or decoder, codec, bitrate, fps (e.g. "Encoder NVENC · H.264 · 6.0 Mbit/s · 60.0 fps", "Decoder D3D11VA · H.264 · 5.8 Mbit/s · 59.9 fps"); the host line gets the suffix " · GPU-direct" while the encoder takes GPU frames without readback (ADR 0019) and " · readback (GPU-direct off)" after GPU-direct failed during the Session, otherwise it is unchanged; line 3 mono 11 `#8e8e94` RTT, loss and, when relayed, "via hub.example.com:3478", or "Sending to 1 viewer · adaptive bitrate". Without a session: text 12 `#6f6f75` "No active session · appears when you share or watch one".
- Target bitrate (not drawn, required by the roadmap): the host line also shows the current target bitrate next to the measured one while adaptive bitrate is active (README open point t).
- Footer (padding-top 10, top line `#1e1f22`): key badge "F3" (mono 10, border `#3a3b40`, radius 3) + "show / hide", right "Hide" (12 `#a3a3a8`).
- Closed: overlay gone; Diagnostics tab and side-panel toggle show the state (tab underline accent while open).

Per-screen differences follow. All 1440 x 900, background `#0b0b0c`; the windowed ones (3t-3w) have the game header (as 3g: "← Library", title "Lumen Drift", Session pill, tabs, layout switch, Fullscreen) and the side panel 340 with visibility, save slot and "Quit game and save".

## 3t Player Diagnostics Software
Lines 680-699. New. Single game, no session, Renderer Software.

- Pill "Not shared"; side panel visibility "Private" ("Only you can see this game. Choose Hub users or Invite only to share it.").
- Overlay: Emulation open (Software values, "no readback", Audio "Buffer 42 ms · 0 underruns"), Streaming open showing "No active session · appears when you share or watch one".

## 3u Player Diagnostics OpenGL
Lines 703-722. New. Single game, no session, Renderer OpenGL.

- Overlay: Emulation open with Renderer "OpenGL 4.6 Core" + GPU/driver subline, Resolution "3× · 768×1152", Frame with readback; Streaming collapsed to the summary "No session".

## 3v Player Diagnostics Session
Lines 726-745. New. Session shared.

- Pill "Session shared · 1 watching"; side panel "INVITED · 1" with Lena (watching, Relayed (TURN), "Remove"), relay hint, "+ Invite someone", "Stop sharing".
- Overlay: both sections open; Streaming with "You" (host, Local) and "Lena" (viewer, Relayed (TURN)).

## 3w Player Diagnostics Fallback
Lines 749-768. New. OpenGL requested, software in use.

- Pill "Not shared". Overlay: Emulation open with the fallback hint, Renderer "Software" + pill "Fallback" (subline "requested: OpenGL"), Resolution "1× · 256×384" (subline "3× requested · needs OpenGL"), Frame "11.6 ms · emu 11.6 · no readback" with a spiky sparkline, Audio "Buffer 18 ms · 3 underruns" (subline "raise the buffer in Emulation settings"); Streaming collapsed ("No session").
- Open: README q (text and reasons of the fallback).

## 3x Player Multiview Diagnostics
Lines 772-780. New. Multiview side by side (tiles with 320 x 240 screens), header as 3h with tab "Diagnostics".

- Panel at the bottom (surface `#111214`, border top `#26272b`, padding 12 20 14); the sections span the width in two columns (gap 24, indent 18), F3 footer. Same panel as in 3h.
- Emulation per tile: title ("You · Lumen Drift", "tile 1 · local"), values in one wrapped mono 12 line ("melonDS DS 1.4.0", "OpenGL 4.6 Core · 3× · 768×1152", "59.8 / 59.83 fps", "frame 9.4 ms · emu 7.1 · readback 2.3", "audio 42 ms · 0 underruns") and a sparkline (200 x 22). Remote tile ("Lena · Harbor Rally", "tile 2 · remote"): "Emulation runs on Lena's Player · not measured here".
- Streaming grouped per tile session: "Tile 1 · your session" (You Local "Sending to 1 viewer", Jonas Relayed (TURN)); "Tile 2 · Lena's session" (Lena host, Direct, "Decoder D3D11VA · H.264 · 5.8 Mbit/s · 59.9 fps", "RTT 14 ms · Loss 0.1 %").
- Collapsed summaries: "Tile 1 · 59.83 fps · 9.4 ms", "3 participants · 1 relayed".

## 3y Player Fullscreen Diagnostics
Esc behavior superseded by [3y-2](#3y-2-player-fullscreen-paused-round-3) (Esc pauses, also in fullscreen).
Lines 784-810. New. Fullscreen, Emulation diagnostics shown, F3 toggles.

- No header and no panels; the common overlay floats top left (350) over the game; Streaming is omitted. Hint at the bottom center: "Toolbar appears when you move the mouse to the top · F3 diagnostics".
- Closed: a small chip top left (surface `rgba(17,18,20,.8)`, radius 7, padding 6 10, 12 `#8e8e94`) with the badge "F3" and "Diagnostics" opens it.
- Phase of 3t-3y: 0.6 Player UI pass; the values need new measurements. Open: README m-t.

## 3c-2 Player Library revised
Superseded in part by [3c-6](#3c-6-player-library-round-3) and [3c-7](#3c-7-player-library-detail-column-round-3) (Round 3: header row, core notice, Local badges, detail variants, 3840 width).
Lines 330-407 in `framebeam-designs-v4.dc.html` (`#3c-2`); 1280 variant 408-492 (`#3c-2-1280`); empty result 493-534 (`#3c-2-empty`). Supersedes 3c. Sort, Ready first and the compact SAVE summary; mock core version 1.4.0.

- Grid: sidebar 232 | main flexible | detail column 392. Main padding 28 32 (1280: 24 28); rows header, chips, toolbar and tiles with gap 16.
- Header (36 high, gap 12): title "Library" 26/600; on the right button "Upload ROM" (36 high, padding 0 14, radius 7, border `#3a3b40`, 13/500; new here) and search field 240 x 36 ("Search…"). The game count moved out of the header.
- Chips as in 3c (32 high, counts "All · 42", "Ready · 37", "Needs attention" badge "2", "Not downloaded · 3"); the sessions cards of 3c stay below them ("Lena · Harbor Rally", "Jonas · Clocktower Kids" with Decline / Join).
- Toolbar row under the chips (32 high, bottom padding 12, bottom border 1px `#232428`): left count "42 games · Nintendo DS" (13, `#8e8e94`); right the sort select and the toggle "Ready first".
  - Sort select: 32 high, padding 0 10 0 12, radius 7, surface `#1a1b1e`, border `#2c2d32`, label "Sort:" (`#8e8e94`) + value (default "Name A–Z") + "▾" (11, `#7d7d83`); open: "▴". States drawn: default, hover, focus, open.
  - Sort menu (252 wide, 30 high items, eyebrow headers, selected item with "✓" in a 16px slot, footer note): NAME: "A–Z", "Z–A"; DATE ADDED TO HUB: "Newest first", "Oldest first"; MORE: "Size largest first", "System then name", "Last played this device". Footer: "Never played games come last. Sort and Ready first are saved on this device." Size sorts in both directions (default, decisions.md).
  - "Ready first": toggle 32 x 18 (knob 14) + label 13. On: ready games first, then a full-width group divider "NOT READY · {n} · same sort" (mono 11 eyebrow `#7d7d83` + 1px line `#26272b`, margin 2 inside the grid gap) which appears only when both groups have games; the order inside each group follows the sort. Disabled while the "Ready" chip is active (every result is ready). Sort and Ready first are saved per device. "Save sync pending" does not count as ready (decisions.md).
- Tile grid: 4 columns when the main area is at least 720px wide, otherwise 3 (the 1280 variant); gap 18. Tile as in 3c (cover radius 6, surface `#1a1b1e`, border `#26272b`, padding 12; mono 11 "NDS", monogram 32/600 `#45464c`; selection = border 2px `#3cbfd8`, inset -3, radius 8; title 14/500; status 12/500). With a date sort the 1280 mock adds a line "Added {DD.MM.YYYY}" to the tile.
- Detail column: padding 24, gap 20 (1280: 18); thumbnail 96 (1280: 88). Pill "● Ready to play", title 22/600, "Nintendo DS · melonDS DS 1.4.0" (13, `#a3a3a8`). Status table: ROM "✓ Cached locally · verified", Core "✓ melonDS DS 1.4.0 · ready", Firmware "Built-in BIOS · not required". The "Save" row and the "Show details (hashes, size, paths)" link of 3c are gone (details live in the saves view, 3c-3).
- SAVE summary block (replaces the save row): surface `#1a1b1e`, border 1px `#26272b`, radius 9, padding 12 14, gap 10. Header: eyebrow mono 11 "SAVE", on the right link "Manage saves →" (13/500; offline: "View saves →"); it opens the saves view (3c-3). Row grid 16 | flex | auto: current-version dot (10px, `#3cbfd8` with ring), slot name 14/600 + version mono 13 `#a3a3a8` ("Main v14"), meta 12 `#8e8e94` ("today 18:42 · Laptop-Office"), pill "✓ Synced". Footer 12 `#7d7d83`, indent 26: "{n} slots · {n} versions in {slot}" (mock "2 slots · 8 versions in Main"; one slot: "1 slot · 3 versions").
- START checklist: "Game data from hub", "ROM verified from cache", "Save checked with hub", "Core ready", "Emulator starts on Play". Bottom: "Play" (48, accent), "Play and share session" (44, outline), hint (12) "Visibility: Hub users · change it in the Session panel". The 1280 variant keeps Play above the fold.
- Summary states (mock `#3c-states`, 612-715, 392px column): Sync pending (pill "⟳ Sync pending", meta "today 19:24 · this device"); Hub offline (link "View saves →", meta "last synced today 18:42", pill "Offline", note "You can play. The save syncs when the hub is back."); Save conflict (eyebrow "▲ SAVE CONFLICT", text "The hub and this device have different saves for “Main”. Nothing is overwritten until you decide.", button "Resolve conflict" opens 3d); No saves yet (link "Upload save file…", "No saves yet", "The first save appears in slot “Main” after you play.").
- Empty result (1280 x 800): search "harbour" with "✕" to clear; chips recount to the result ("All · 1", "Ready · 0", "Needs attention 1", "Not downloaded · 0"); toolbar count reflects the filter ("0 of 37 ready games"; default "37 of 42" style, decisions.md); message "No ready games match “harbour”", hint "1 match outside this filter: Harbor Rally (needs attention · save conflict).", actions "Clear search" and "Show in All"; detail column "No game selected" / "Select a game to see its status and saves."
- Open: see decisions.md (revision handoff).

## 3c-3 Player Saves view
Lines 535-572 in `framebeam-designs-v4.dc.html` (`#3c-3`); states 612-715 (`#3c-states`). Supersedes the slot dropdown, history card, label field, "Refresh" text and "Show details" of 3c.

- Decision of the handoff: the detail column switches to a saves mode with a back arrow, not a drawer (no new surface type over the library; Play and Play and share session stay pinned, so "pick a version, then play" is one place; keyboard focus stays in the column; same at 1280 and 1440). Esc or "←" returns to the game detail; the column stays 392 wide. Entered by "Manage saves →" / "View saves →" of 3c-2.
- Layout (padding 24, gap 14), top to bottom:
  - Back row (28 high): "← Lumen Drift" (13, `#a3a3a8`), on the right "updated 22:06" (mono 11 `#7d7d83`) and the refresh icon button "↻" (28 x 28, radius 6, border `#2c2d32`, tooltip "Refresh from hub").
  - Title "Saves" 20/600.
  - Slot tabs (13; active 600 with 2px `#3cbfd8` underline; version count after the name: "Main 8", "Speedrun 3"; on the right "+ New slot" 13/500 `#c9c8c4`). More than 3 slots: tabs collapse into a switcher "Main · 8 ▾" with "+ New slot" as its last item (threshold 3, decisions.md).
  - CURRENT bar (padding 10 12, radius 8, surface `#1a1b1e`, border `#26272b`, gap 6): eyebrow "CURRENT", mono 13/500 "v14", pill "✓ Synced"; line "today 18:42 · Laptop-Office · Session end" (12 `#8e8e94`); toggle "File details ▸" (12 `#a3a3a8`). Expanded: SHA-256 (mono, "9f3c41e0…7b2da21e"), Size "512 KB", Local path (mock "…\saves\melonDS DS\Lumen Drift.sav"), buttons "Copy hash", "Open folder".
  - Actions (32 high, gap 8): "◆ Create snapshot" (outline, 13/500) and "Upload save file…" (text button `#c9c8c4`).
  - HISTORY: eyebrow + 2-option segment "All" | "Snapshots" (12, track `#1a1b1e`, padding 2, radius 6); below it the timeline (SaveRow dark, see Components) fills the remaining height and scrolls.
  - Pinned bottom: "Play" (48) and "Play and share session" (44) as in 3c.
- Saves view in the game (implemented, not drawn in the mock): "Manage saves →" in the in-game side panel (3g) opens this view in the game. Snapshot and delete work live; restore and upload upload the current local save to the Hub, make a local backup, write the save atomically and reload the game with it ("The game restarts from this save."); they are refused when the core's SAVE_RAM size does not match. Slots cannot change while the game runs. The conflict state shows "Resolve conflict" with a device/Hub comparison and the actions "Keep this device's save" and "Use the Hub save" (backup of the device copy first); the game does not start.
- Mock timeline "Main": v14 current (today 18:42, Laptop-Office, Session end); v13 (today 17:58, Laptop-Office, Auto checkpoint) with the restore confirmation open; v12 snapshot "Before the lighthouse" (yesterday 22:30, Desktop-LivingRoom, Manual snapshot); v11 (yesterday 21:47, Device switch); thinned marker "6 older versions thinned out"; v4 snapshot "Chapter 2" (03.10. 21:12).
- Confirmations (inline under the row, see SaveRow): restore (accent box) "Restore v13 as the current version of “Main”?" / "v14 stays in the history."; delete (danger box, manual snapshots only) "Delete snapshot v12 “…”?" / "Only this snapshot is removed. The current version and all other versions stay." Play is disabled while a restore, delete or upload confirmation is open (decisions.md).
- States (all in `#3c-states`):
  - Create snapshot: inline label field under the actions (title "◆ Create snapshot of v14", input e.g. "Before the clock tower", note "Label is optional. Snapshots are kept until you delete them.", buttons "Cancel" / "Create snapshot"); after: "✓ Snapshot “Before the clock tower” created from v14." Snapshots can be created while the game runs, from the last written save (decisions.md).
  - New slot: inline name field (input e.g. "Second run", note "Starts from v14 of “Main”. “Main” is not changed.", buttons "Cancel" / "Create slot").
  - Snapshots filter: shows manual snapshots (with Delete) and the current version.
  - Hub offline: "Hub offline · read-only. Showing the history from the last sync, today 18:42. Restore, snapshots and uploads need the hub." with "Try again"; "◆ Create snapshot" and "Upload save file…" shown disabled-looking.
  - Game running (saves view opened from the Library): "Lumen Drift is running on this device. Restore and upload are possible after the game is closed. Snapshots still work." Same for another device with the same slot: "Running on Laptop-Office".
  - Upload in progress: "Uploading to “Main”" with "62 %" and steps "✓ Local backup made", "✓ File checked · 512 KB", "○ Sending lumen-drift-backup.sav"; "Play is disabled until the upload finishes."
  - Upload failed: "✕ Upload failed", "The hub did not answer within 30 seconds. Nothing changed: v14 is still the current version, and the local backup is kept." Buttons "Cancel" / "Try again". Wrong file (drawn for a DS save): "Wrong file. lumen-drift.png is 1.2 MB. Nintendo DS saves are 512 B to 8 MB raw save files (.sav). Choose another file." The size-range check is not built until the system manifest carries save sizes (decisions.md).
  - After upload: "✓ Uploaded as v15. v14 is in the history as “Before upload”."
  - Save conflict: "▲ SAVE CONFLICT · MAIN", "Hub: v14 · Laptop-Office · today 19:10", "This device: local · today 19:24 · sync pending", "Resolve conflict" opens the inline resolution in the saves view (see "Saves view in the game" below; 3d stays the dialog design); "Restore, upload and new slot are paused for “Main” until the conflict is resolved. Other slots work normally."
  - Loading: "Loading history from hub.example.com:8443…"

## 3c-4 Player Upload confirmation
Lines 573-611 in `framebeam-designs-v4.dc.html` (`#3c-4`), 1280 x 800. Shown in the saves view after "Upload save file…" and the file choice.

- Confirmation box in place of the actions (padding 14, radius 9, surface `#15262b`, border 1px `#1f4650`, gap 12): title "Upload as the new current version?" (14/600, `#3cbfd8`); data block (surface `#121315`, radius 7, padding 10 12, label column auto, gap 6 14, 13px): "File" (mono, ellipsis) "lumen-drift-backup.sav"; "Size" (mono) "512 KB · valid for Nintendo DS" (the suffix in ok green); "Into slot" "Main · becomes v15"; text 12/1.55 `#c9c8c4` "The current version is kept in the history as “Before upload”. A local backup is made first."; buttons right-aligned (32 high): "Cancel" (outline), "Upload as new current version" (accent, 600).
- After confirming: steps as under 3c-3 (local backup, file check, sending); the timeline then shows a new current version with reason "Uploaded" and the replaced one with reason "Before upload" (SaveRow). Uploads join retention like session ends.

## 3c-5 Player Library with a running game
Header row and core notice follow [3c-6](#3c-6-player-library-round-3).
Lines 164-208 in `framebeam-designs-v4.dc.html` (`#3c-5`); dialog 209-228 (`#3c-5-dialog`). Changes vs 3c-2: "← Library" in game pauses and keeps the game loaded; the Library shows the running game.

- "Now running" strip in the sidebar above the Hub switcher card (spec under Components), on every page (Library, Emulation, Controllers, Settings).
- Running tile: pill "❚❚ Paused" top right of the cover (top 10, right 10, 11/600, padding 3 8, `#15262b`/`#3cbfd8`, pause icon of two 2 x 8 bars); status line "❚❚ Running · paused" in cyan; the selection ring stays around the cover. Not-ready tiles follow the divider as in 3c-2.
- Detail column of the running game: thumbnail 96; pill "❚❚ Running · paused"; title and "Nintendo DS · melonDS DS 1.4.0"; status table (padding 10 0, divider `#26272b`): "Running since" "22:19 · paused 22:31", "Sharing" "Not shared", "Core" "✓ melonDS DS 1.4.0"; SAVE summary with meta "checkpoint 40 s ago · this device" and neutral pill "Syncs on quit" (`#232428`/`#a3a3a8`); text (12/1.55 `#8e8e94`) "The game stays loaded and paused while you browse, change settings or watch someone else’s session."; buttons "Resume" (48, accent, play icon, 15/600) and "Quit game" (44, outline), note (12 `#7d7d83`, centered) "Quit saves and syncs to the hub first". The START checklist is not shown.
- Dialog "Play on another game while one is running": overlay `rgba(5,5,6,.7)`; dialog 500 wide, padding 28, radius 12, surface `#16171a`, border `#2c2d32`, shadow `0 40px 80px rgba(0,0,0,.6)`, gap 18. Eyebrow mono 11 `#3cbfd8` "LUMEN DRIFT IS RUNNING · PAUSED"; title 20/600 "Quit Lumen Drift and start Harbor Rally?"; text 14 `#a3a3a8` "Lumen Drift is saved and synced first."; buttons 40 high, right-aligned: "Cancel" (outline), "Quit and start Harbor Rally" (accent). Footer (top border `#232428`, padding-top 14): "After confirming" (12 `#8e8e94`) with progress "✓ Lumen Drift saved to “Main” as v15", spinner "Syncing to hub.example.com…", "○ Starting Harbor Rally"; the start happens in place.
- Copy of the strip: "Paused while you watch Lena · Harbor Rally" (while watching another session), "Saving and syncing…" (quitting).
- Open: decisions.md (idle auto-quit and hub disconnect: default no auto-quit).

## 3e-2 Player Emulation revised
Superseded in part by [3e-3](#3e-3-player-emulation-round-3) (Round 3: no preselected core, firmware only where needed, one Reset style).
Lines 720-748 in `framebeam-designs-v4.dc.html` (`#3e-2`); 1280 variant 749-776. Supersedes the option rows of 3e (one row component); shell, second column and chips as in 3e.

- Second column (300): "EMULATION", item "Defaults" ("Apply to every system"), "SYSTEMS", item "Nintendo DS" with "2 changed" (11, `#3cbfd8`), "melonDS DS · 1.4.0" and "● Ready · core from the hub" (12, ok green); selected as in 3e; bottom dashed card "Per-game settings" / "Coming later. Set from a game’s page in the Library."
- Main (padding 28 36, gap 16): title "Nintendo DS · melonDS DS 1.4.0" (26/600), sub "Only options this core reports. Saved on this device only.", search 240 x 36 "Search settings…".
- Category chips (32 high, radius 16, 13): "All" plus the categories the core reports, in core order, e.g. "System", "Date/Time", "Video", "Audio", "Screen", "Firmware", "Network"; right link "Reset 2 changed" (13, underlined). At 1280 the chips collapse to "+4 more ▾" (default, decisions.md; alternative was horizontal scroll).
- Groups: eyebrow (mono 11, `#7d7d83`, .08em, bottom padding 8, rule 1px `#26272b`), 24 between groups, rows are SettingsRow (see Components). Mock rows (labels and values come from the core, melonDS DS 1.4.0 names): VIDEO: "Render Mode" (select "OpenGL (Compute)", changed, badge "applies on next start"), "Internal Resolution" (select "3x native (768 x 576)", changed; 12 values, menu shows 8 and scrolls), "Threaded Software Renderer" (toggle on, disabled: "Software renderer only. Render Mode is OpenGL (Compute)."); AUDIO: "Microphone Input Mode" (4 options, falls back to select when it would truncate; description with an option list behind "More · 4 options"), "Bit Depth" (segment Automatic | 10-bit | 16-bit), "Interpolation" (segment None | Linear | Cosine | Cubic).
- Mock core-agnostic sheet (`Sheet core-agnostic`, line 1027): the Player renders only what the core reports (key, label, description, category, values, default, restart flag); categories become eyebrows and chips in core order, options without a category go under "OTHER"; a boolean value list (enabled/disabled, on/off) becomes a toggle; value labels are shown as the core sends them with only the first letter capitalised; restart option -> badge. The example core "Lanterna 0.3.0" is fictional.
- 1280: the label column shrinks, the control column stays 280.

## 3p-2 Player Settings revised
Superseded in part by [3p-3](#3p-3-player-settings-round-3) (Round 3: Updates segment, Hub rows, library folders, 880 px width).
Lines 777-814 in `framebeam-designs-v4.dc.html` (`#3p-2`). Supersedes the rows of 3p; all four sections use SettingsRow, controls end on the same x as in 3e-2.

- Shell as 3p: sidebar | section column 300 ("Updates" "Up to date", "Hubs" "1 saved · Home active", "Appearance" "Dark", "Diagnostics" "Logs and support"; bottom mono "Player 0.7.12" / "Windows x86-64 · Protocol v1") | main (padding 28 36, gap 20); title "Settings", sub "Saved on this device only". Reset column stays empty (72) so the controls line up with Emulation.
- UPDATES: section header with pill "✓ Up to date · 0.7.12" on the right (12/600, ok). Rows: "Update channel" (inline meta "Channel: beta", segment Stable | Beta, "Pre-release builds from every change on main. May contain bugs."), "Install updates automatically" (toggle on, "Downloaded in the background, applied on the next start."), "Last checked" (button "Check now", "today 22:06 · checks every hour while the Player runs"; cadence per channel, see 3p). The update card of 3p (update available) is indented 14 to the label edge.
- HUBS: header with "+ Add hub" (13/500 `#c9c8c4`). Hub card indented 14 to the label edge and 72 from the right (margin 12 72 2 14, padding 12 14, radius 9, surface `#1d1e21`, border `#26272b`, active hub with a left bar `inset 3px 0 0 #3cbfd8`; grid name | actions): name 14/600 + pill "● Connected", mono 12 `#8e8e94` "hub.example.com:8443 · as Max · last today 22:05", right "Active hub" (13 `#7d7d83`), "Edit" (13/500), "Remove" (13 `#a3a3a8`). Inline edit and validation as in 3p. Row "Connect automatically on startup" (toggle now in the control column; "Uses the last active hub.").
- APPEARANCE: "Theme" segment Dark | Light | System, "The game view always stays dark." Appearance and Diagnostics no longer sit side by side.
- DIAGNOSTICS: "Log file" with buttons "Copy path", "Open folder"; the description holds the path (mock value).
- Read-only value row (sheet): "Version" as mono 13 `#c9c8c4`, description "Windows x86-64 · Protocol v1"; no Copy button (default, decisions.md).

## 3f-2, 3f-3 Player Controllers revised
Superseded in part by [3f-4 (Round 3)](#3f-4-player-controllers-round-3) (system selector, 3DS test, hotkeys table).
Lines 818-864 (`#3f-2`, Xbox) and 865-919 (`#3f-3`, DualSense) in `framebeam-designs-v4.dc.html`. Supersedes 3f; shell and device list as in 3f (columns 232 | 300 | flex | 340).

- Main (padding 28 32, gap 18) header: device name 26/600 over the pill "● Connected · P1"; on the right two labelled selects (label 11 `#7d7d83` above, 34 high, radius 7, surface `#1a1b1e`, border `#2c2d32`): new "Button labels" (170, "Auto (Xbox)" / "Auto (PlayStation)") and "Profile" (180, "Standard Gamepad").
- "Button labels" menu (3f-3 open; 220 wide, padding 4, radius 8, surface `#1d1e22`, border `#34353a`, 32 high items): "Auto" (with the detected type on the right, "PlayStation"; selected "✓"), "Xbox", "PlayStation", "Generic" ("DS names"); note "Auto uses the detected controller type. Saved per device." Generic shows the emulated system’s names (DS: A, B, X, Y, L, R, Start, Select). The override and Auto are saved per physical device (decisions.md).
- Tab chips "Buttons" (active) / "Hotkeys" and link "Reset 2 changed" as in 3f.
- Mapping table, grid `14 | flex | 240 | 72` (same dot / Reset / "Default" pattern as SettingsRow): header (mono 11, .06em) "DS INPUT" | "MAPPING"; the redundant "NDS" column is gone, the DS input is the first column. Row: padding 7 0, divider `#1f2024`, 14px; mapping field 32 high, radius 6, surface `#1a1b1e`, border `#2c2d32`, padding 0 6 0 5, containing PadGlyph chips (size 22) + name (13, `#c9c8c4`). Mock Xbox: A -> glyph B "B" (changed), B -> A (changed), X -> Y, Y -> X, L -> LB "Left bumper", R -> RB "Right bumper", Start -> Menu, Select -> View, D-pad -> four arrow chips "D-pad · or left stick", Close lid -> "Unassigned" (no glyph). Mock DualSense: A -> circle "Circle", B -> cross "Cross", X -> triangle "Triangle", Y -> square "Square", L -> L1, R -> R1, Start -> Options, Select -> Create.
- Actions at the bottom: "Reset profile to default", "Duplicate profile" (outline, 36 high, padding 0 14, 13).
- Input test column (340, padding 28 24, gap 16): eyebrow "INPUT TEST" with "Labels: Xbox" (12 `#8e8e94`) on the right; text "Press buttons on the controller. Active inputs light up."; InputTest grid (see Components) instead of the 4 x 3 key row; then "DS-TOUCH" panel as in 3f (150 high, "Mouse on bottom screen", "Left mouse button = stylus"). Mock pressed: Xbox A, D-pad right, RB; DualSense Cross, L1. Sticks and stick clicks have no cells (default, decisions.md).
- Keyboard device: glyph chips show key names (X Z S A Q W 1 3 Shift Enter for the DS layout). Monochrome only; no PlayStation shape colours, no Xbox letter colours.

## 3g-2, 3g-3 Player Session revised
Esc and fullscreen behavior superseded by decision 21 ([3y-2](#3y-2-player-fullscreen-paused-round-3)); docked diagnostics by [3h-3](#3h-3-player-session-round-3).
Lines 37-45 (`#3g-2`, not shared, Private), 46-54 (`#3g-3`, shared, Invite only, "2 watching", Reset popover open, speed-up on) and 67-79 (`#3g-2-1280`, 1280 x 800, compact header, "⋯" menu open) in `framebeam-designs-v4.dc.html`. Supersedes 3g.

- Grid: rows 56 | 1fr, columns flexible | 340 (1280: 320); background `#0b0b0c`. Header = GameHeader (mode session), side panel = GamePanel (kind own); both specified under Components. Play area: two DS screens 480 x 360 (stacked) centered, as in 3g; the diagnostics overlay (3t-2) floats top right of the play area.
- Changed vs 3g:
  - Header in five fixed zones (Back, Context, Game controls, View, Display). Pause, Speed-up (with speed dropdown) and Reset share one button style in one group. "Diagnostics" left the view segment and is an on/off button (with F3) in the Display group. "Quit" is removed from the header. "← Library" pauses the game and keeps it loaded (tooltip "Game keeps running, paused"). Reset asks first (anchored popover "Reset Lumen Drift?").
  - Side panel in a fixed order: SHARING (visibility, hint, Share Session / Stop sharing, invites), SAVE (slot as text, Save snapshot F5, last checkpoint, "Manage saves →" opens the saves view of the Library), Quit game pinned at the bottom. Removed: the speed select in the panel, the slot "+" and slot segment, the diagnostics toggle at the bottom, the empty gap.
  - The layout button changes the layout of this game only ("Session = this game").
- Copy: "Reset Lumen Drift?" / "Unsaved progress since the last checkpoint is lost."; "Quit game" / "Saves and syncs first"; "Saving and syncing…"; tooltip on "← Library": "Game keeps running, paused".
- 1280: see GameHeader (compact); panel 320 wide.
- Open: decisions.md (Esc, paused state for viewers). Phase: UI of the 0.7.x revision; the functions (speed-up, hotkeys, saves) exist, see roadmap.

## 3r-2, 3h-2, 3i-2 Player Multiview revised
Superseded in part by [3r-3](#3r-3-player-multiview-round-3), [3h-3](#3h-3-player-session-round-3) and [3i-3](#3i-3-player-pip-round-3) (Round 3: layout "Row" instead of "Side by side", Grid 2x2 only from 4 Sessions, paused remote tile, single selection style, Add popover). Wherever this section says "Side by side" for the Multiview layout, read "Row" (decision 22).
Lines 80-106 (`#3r-2`, Grid 2x2, own tile selected, picker open), 122-133 (`#3h-2`, Side by side, Lena's tile selected, own game paused), 134-147 (`#3i-2`, PiP, panel collapsed) and 148-163 (`#3r-2-1280`, 1280 x 800) in `framebeam-designs-v4.dc.html`. Supersede 3r, 3h and 3i.

- Same GameHeader as the Session view (mode multiview): Context "Multiview" + mode segment "PiP" | "Side by side" | "Grid 2×2"; your game's controls stay, labelled "YOUR GAME" with its share state; "+ Add · n/4" owns the tile count and anchors the Running sessions popover (it never opens on its own; empty tiles open it too); the layout button says "All tiles" and applies to all tiles. The v3 mode segment/bottom diagnostics bar and the "Add to multiview" header button are gone.
- Panel follows the selected tile (GamePanel): own tile -> full panel with the label "TILE n · YOUR GAME"; remote tile -> who, connection, "Audio here", "Remove from multiview", "Your game runs in tile 1 [Select]". "⇥" collapses the panel to a 48px rail (3i-2, 1280).
- Tiles: area padding 16, gap 16, tile radius 8, surface `#0f1011`; screens per layout: Grid 248 x 186 (two per tile, stacked), Side by side 440 x 330, PiP main 520 x 390 with the PiP window (288 x 432, bottom right offset 24, radius 8, border `#2c2d32`, inset ring 2px `#3cbfd8`, shadow `0 18px 40px rgba(0,0,0,.6)`, screens 256 x 192, header dot + "Lena · Harbor Rally" + "♪"). Key badge top left (offset 10, 20 x 20, radius 4, mono 11; idle `#26272b`/`#c9c8c4`, selected white `#ecebe7`/`#121315`), name chip bottom left (padding 4 8, radius 6, `rgba(17,18,20,.88)`, 12). Empty tile: dashed 1.5px `#2c2d32`, "+ Add session" (14/500) / "Opens Running sessions" (12 `#7d7d83`); at 1280 "2 of 4 tiles used".
- Selection vs audio (sheet, lines 272-289): Idle; Hover (cursor pointer, surface `#141517`); Selected = outline 2px `#ecebe7`, offset 3, white key badge; Audio focus = inset ring 2px `#3cbfd8` + chip "♪ Audio" (bottom right, 11/600, padding 3 8, `#15262b`/`#3cbfd8`); both can show at once. Keys 1-4 select tiles (v3: keys moved the audio); audio moves with "Audio here" in the panel. Selecting a remote tile does not route controller input; input always goes to your game (decisions.md).
- 3h-2: own tile label "You · Lumen Drift · paused" with chip "Paused · Resume in the header or Esc"; the header shows "Resume" (your game stays controllable from the header while a remote tile is selected).
- 1280 (3r-2): game controls icon-only, "YOUR GAME" label stays, panel collapsed (see GameHeader compact).
- Open: decisions.md (layout scope, input routing). Not drawn: behavior when a remote session ends (tile becomes empty?).

## 3t-2, 3x-2 Player Diagnostics revised
Placement superseded in part by [3h-3](#3h-3-player-session-round-3): diagnostics are docked as a panel tab and no longer cover the game or remote tiles.
Lines 55-66 (`#3t-2`, single game, overlay open) and 107-121 (`#3x-2`, multiview) in `framebeam-designs-v4.dc.html`. Supersede the placement and toggles of 3t-3y; the row content of 3t-3w (Software, OpenGL, Session, Fallback hint) is unchanged and not redrawn; 3y keeps the chip "F3 Diagnostics" and the top-left position, but the fullscreen overlay shows Emulation and Streaming like windowed.

- One toggle: the header button "Diagnostics F3" and F3 (the side-panel toggle and the "show / hide" footer are gone; the overlay footer reads "Hide diagnostics" + key badge "F3", the same label and key as the header). Open state shown on the header button (accent).
- Position everywhere: top right of the play area, 16 under the header and 16 from the side panel, directly below its button; same in single game and multiview; no bottom bar in multiview.
- DiagOverlay (spec under Components) in two sections, Emulation and Streaming, each collapsing to a summary. 3t-2 mock: Emulation open, Streaming collapsed ("not shared").
- 3x-2: the same 340 overlay with a block per tile; Emulation shows your tile only ("Only your tile is emulated on this device."), Streaming one block per remote tile, badges 1-4 match the tile keys; summaries "tile 1 · 59.8 fps", "2 tiles · 1 relayed".
- New in the mock vs the measured values: "jitter" in the Streaming lines is not measured today (open); the GPU line uses the placeholder "Example GPU · driver 1.2.3".
- Open: decisions.md (state per view or global: global, ADR 0014 D5).

## Round 3 (2026-10-12)

Source `source/round-3-player-a.dc.html` (289 lines: 3a-3, 3c-6, 3c-7, 3p-3, 4a-3, 4a-4, 4f-3, 4j-3) and `source/round-3-player-b.dc.html` (253 lines: 3e-3, 3f-4, 3r-3, 3h-3, 3i-3, 3y-2), dark theme; rules in `source/round-3-sheet.dc.html` (0-2, handoff table) and decisions 20-23 in [decisions.md](decisions.md); new tokens: [tokens.md](tokens.md#additions-of-round-3-2026-10-12). Finding IDs (P = Player, D = design gap) are those of the 0.14 UI scan. The plugin screens 4a-3, 4a-4, 4f-3 and 4j-3 are specified in [plugins.md](plugins.md). Unchanged parts stay as in the older section of the same screen. Player nav (`source/player-nav.dc.html`, props `active`, `plugin`, `offline`): sidebar 232, surface `#16171a`, order Library, Runs (accent mono badge, mock "1"), plugin screen entry (only while a plugin offers one, indented 30, directly under Runs, see 4f-3), Emulation, Controllers, Settings; Hub card (surface `#1b1c1f`, radius 10, padding 12: status dot, name, "Switch", mono 11 address with ellipsis and the full address as tooltip); user block below it on two lines (name 13; device name 12 `#9a9a9f`, ellipsis) with the avatar 28 (P3, P9). Content widths: Settings and Runs max 880 px, Library grid max 1680 px, both left-aligned (decision 23, P7); detail column 340; SettingsRow control column 280. Mock names (Max, Lena, Home Hub, hosts under `example.com`, folders under `D:\Games`) are illustrative.

### 3a-3 Player Connect (Round 3)
Source: `source/round-3-player-a.dc.html#3a-3` (21-40), two states side by side. Fixes P2, P6.

- Form centred, 520 wide, title 32/600, body 14/20, input 44 high (radius 8, mono 14); one field "Hub address" with the value as `host:port` (hint "host:port, without https://", 12 `#9a9a9f`); Hub edit in Settings uses the same single field (3p-3). Below a divider the toggle (36 x 20) with the label "Connect automatically on startup"; the drawing shows the toggle left of the label; the rule is toggle right in the control column like Settings (finding P4 fixed also here, ADR 0026 D5). Footer line (mono 12): "This device: Desktop-LivingRoom · Player 0.14.0 · Windows x86-64".
- First start: eyebrow "FIRST START", title "Connect to a Hub", body "Enter the address of your FrameBeam Hub. You find it in the Hub under Settings → General.", field with accent border, button "Connect".
- Pairing lost (known Hub): eyebrow "HOME HUB · PAIRING LOST", title "Pair again", body "Home Hub no longer knows this device. Its admin may have revoked it. Pair again to get a new approval; your local saves are kept.", the address is shown read-only (grey), button "Pair again", hint (amber) "The Hub admin approves the request under Clients." (replaces the wrong heading "Add hub").

### 3c-6 Player Library (Round 3)
Source: `#3c-6` (41-67, 1280 x 800), `#3c-6-4k` (68-83, 3840 x 2160 shown at 50 %). Fixes P7, P8, P9, P10, P11, P13 (with 3c-7).

- Header is one row (title, search, "Upload ROM") so that "Upload ROM" can no longer overlap the title on first load (P8). Title "Library" 26/600; search field and the button as in 3c-2.
- Chips carry the only count: "All · 10", "Ready · 3", "Needs attention · 1", "Not downloaded · 6"; no extra "10 games" (P11). Toolbar: "Sort: Name ▾", toggle "Ready first". "Needs attention" follows decision bg (missing required firmware or no core; an available update is not attention).
- Core notice: one dismissible line, grouped by core, instead of one banner per system (P10): "2 cores download from Home Hub on first start: mGBA (GB, GBC, GBA) · Azahar (3DS)" + "Dismiss". Drawn as an inline strip above the grid (surface `#16242a`, border `#23414a`, radius 8, padding 10 14, 13px, "Dismiss" grey); the core names are 500.
- Tiles: mono system label ("NDS", "GBC", "GBA", "GB", "3DS"), monogram, title, status line: "↓ Download · 12 KB" (grey), "Core downloads at start" (grey), "● Ready" (green `#5fd28a`), "▲ Save conflict" (amber `#e5b45a`). Badges for local files: "Local" (grey pill `#26272b`) for a file only on this device, "Local · on Hub" (accent pill `#16242a` / `#3cbfd8`) for a local copy whose hash matches a Hub game. Selection: 2px `#3cbfd8`.
- Grid 3 columns at 1280 (detail column 340), 7 columns at 3840; the grid stops at 1680 px and stays left-aligned, the detail column keeps 340 px, the empty space to the right is intentional (P7). Detail column: see 3c-7.

### 3c-7 Player Library detail column (Round 3)
Source: `#3c-7` (84-104), four variants, each 340 x 800 with the buttons pinned to the bottom. Fixes P12, P13, P14.

- Common: caption (mono 11), status pill, title 22/600, "Nintendo DS · melonDS DS" 13, fact table (ROM, Core, Firmware, File, Matches, On the Hub), START checklist (dot ok green `#5fd28a`, error amber `#e5b45a`, pending hollow), primary button (48, accent), secondary row (outline buttons).
- Local file, not on the Hub (pill "Local"): facts File (path), Core "mGBA · ready", On the Hub "Not uploaded"; checklist "Local file found · 12 KB · hash checked", "Core ready · mGBA <version>", "Save checked with Hub · No save yet", "Emulator starting"; "Play"; secondary "Upload to Hub", "Remove from library" (an outline button like the others, not plain text; P12).
- Local copy of a Hub game (pill "Local · on Hub"): facts File, Matches "Hub ROM 8e33…a8c8", Core; checklist "Local file found · same hash as the Hub", "Core ready", "Save checked with Hub · Main · synced today 17:40", "Emulator starting"; "Play"; secondary "Play and share", "Remove from library".
- Error, Hub offline (pill "Needs attention", amber): facts ROM "Local file · 1.0 GB", Core "Azahar · not on this device" (amber), Home Hub "Offline since 18:02" (amber); checklist "Core ready · Azahar must download from Home Hub" with an amber dot; the primary "Download and play" is disabled (`#26272b`, text `#6a6a70`) with the reason directly below: "Home Hub is offline, so Azahar cannot download. Try again when the Hub is back."; secondary "Retry connection", "Hub settings" (P13).
- With plugin section (pill "Ready to play", green): a collapsible section above START ("SOULLINK CORE · Collapse ▴ · Party 4 · Box 12 · Badges 2"); the checklist and the buttons stay inside 340 x 800, no clipped "Emulator starting", no empty badge above the title (P14); secondary "Play and share", "Manage saves".

### 3p-3 Player Settings (Round 3)
Source: `#3p-3` (105-134), 1440 x 1280, nav column as in 3p-2 (subtitles "0.14.0 · Stable", "2 saved · Home Hub active", "Dark", "2 folders · 15 games", "1 KB of 20 GB", "2 installed · 2 on", "Logs and support"). Fixes P1-P5, P16, P18 (as drawn also P6 and P17); at 3840 the content follows the 880 px rule (decision bi).

- Content max 880, left-aligned; header "Settings" with "Saved on this device only" once (no duplicate, P17).
- UPDATES: status card in row width ("Player 0.14.0 is up to date" / "Stable channel · checked today 17:52"); "Update channel" with the segment "Off | Stable | Beta" and help "Stable: tested releases. Beta: earlier, less tested." (P5); "Install updates automatically" "Never while a game is running."
- HUBS · 2: section header with "+ Add hub" right-aligned with the 280 control column (P1). Active card "Home Hub" with "● Connected · active", "hub.example.com:8443 · since today 17:40", buttons "Edit", "⋯"; "Connect automatically on startup" ("Uses this Hub when the Player starts.") sits inside the active Hub card. Second card "Lena’s Hub": pill "Pairing lost" (amber), "lena.example.net:8443 · last 3 Oct", button "Pair again". Edit panel "EDIT · HOME HUB": one field "Address" (`host:port`, no separate port field, P6), "Cancel", "Save", help "host:port, without https://. If the Hub shows a different certificate, the Player asks you to compare it." The long address is no longer cut in the sidebar Hub card (P3).
- LIBRARY FOLDERS · 2: header action "+ Add folder…"; description "The Player finds games in these folders and starts them from there. It tells Home Hub which games you have; the files stay on this device." (full text, no "More" link on short texts, P18). Folder rows (card rows): path with middle ellipsis (full path as tooltip), meta line, row action, "Remove": "D:\Games\GB" "12 games · scanned 5 min ago" "Rescan"; a long path "D:\Games\Archive\Nintendo…\DS-and-3DS" "Scanning · 41 of 120 files" (cyan) "Stop scan"; "E:\Retro" "Drive not found · last scanned 3 Oct" (amber) "Rescan". Footer "Remove only takes the folder off the list. Your files are not deleted." Plurals per decision 23 (P16).

### 3e-3 Player Emulation (Round 3)
Source: `source/round-3-player-b.dc.html#3e-3` (21-42), `#3e-3-rows` (43-49). Fixes P30, P31, P32, D9.

- Left list: Defaults ("Apply to every system · 1 changed") and SYSTEMS with core line and a firmware line only where it applies: "Game Boy · mGBA · from Hub", "Game Boy Color · Gambatte · from Hub", "Game Boy Advance · mGBA · from Hub", "Nintendo DS · No core chosen yet" + "Firmware optional", "Nintendo 3DS · Azahar · from Hub" (no preselected core since 0.13, P31).
- Detail "Nintendo DS" (title 28/600, "Saved on this device only" 13, text link "Reset all to default" 13 `#9a9a9f` on the right; one Reset style, a plain text link, P32). Rows (SettingsRow: padding 12 0, gap 24, control 280 with padding 7 12 and radius 8, a 60 px right column holds "Reset" in accent `#3cbfd8` for a changed value or "Default" in grey `#7a7a80`; content max 880, padding 28 36):
  - "Core": "No core chosen yet · picked on first start. Home Hub offers melonDS DS and DeSmuME." control "Choose a core ▾" (accent border).
  - "Firmware" (only for cores that can use files): "Only shown because melonDS DS can use files. Built-in works for most games." "Built-in ▾".
  - "Firmware files": "Native mode needs <ARM7 BIOS>, <ARM9 BIOS> and firmware from the Hub." control amber "▲ <file> missing on Hub".
  - "Screen layout": "Overrides Defaults for DS games." "Use default (Stacked) ▾".
  - "Speed-up speed": "How fast the game runs while speed-up is on." "2× ▾"; "Audio during speed-up": "Plays sound at a higher pitch while sped up." toggle "On" with "Reset".
  - Hint "Core options appear after the core loaded once. Per-game settings are under Library → game → ⋯." (accent link style, P32). The per-game card is removed.
- `#3e-3-rows`: rows that differ per system: 3DS · Azahar "System language" ("Language the 3DS reports to games.", "English ▾"; Azahar core option "3DS System Language"); 3DS · Azahar and GB · mGBA have no Firmware row ("No firmware needed for this core. Row not shown."; P30); Defaults has "DS and 3DS screen layout" ("Stacked, Side by side or Top only. Systems can override.", "Stacked ▾").

### 3f-4 Player Controllers (Round 3)
Source: `#3f-4` (50-83) Buttons, `#3f-4-hotkeys` (84-99) Hotkeys. Fixes P33, P34, P35, P36, D11. Supersedes the layout of the plugin round's 3f-4 ([plugins.md](plugins.md#3f-4-controllers-hotkeys)); its plugin groups stay.

- Left column DEVICES: "Gamepad · Xbox layout" "● Connected · P1"; "Keyboard" "● Connected"; "Mouse" "Touch screen for DS and 3DS". Tabs "Buttons | Hotkeys"; "Profile" select ("My 3DS layout ▾") + "Rename" + "⋯" (profile actions).
- Buttons: segment "System" GB | GBC | GBA | DS | 3DS shows only that system's buttons (P33): 3DS list A, B, X, Y, L / R, ZL / ZR, Start / Select, D-pad, Circle pad ("Left stick · dead zone 12 %"), C-stick ("Right stick"), Home ("not set"), each with its key and "Default". Note: "DS shows no ZL, ZR or sticks. GB and GBC show A, B, Start, Select and D-pad only. GBA adds L and R."
- INPUT TEST · 3DS: shoulder row ZL, L, R, ZR (active `#16242a`); "Circle pad · x 0.42 y −0.61" with a live position; X Y A B cluster; "C-stick · centered"; "Bottom screen · mouse = stylus".
- Hotkeys table (`#3f-4-hotkeys`), columns `1fr 150 170 70` (ACTION | KEY | GAMEPAD | state), all values shown in full without ellipsis (P35). Groups: FRAMEBEAM ("always wins"): Pause / pause overlay Esc · Select + Start · Fixed; Toggle fullscreen F11 · Select + Y; Diagnostics F3 · Select + R3; Save snapshot (editing: "Press a key…", accent ring) · Select + LB; Speed-up Space · Select + RB; Show or hide all HUD widgets F4 · Select + L3; Panel tabs Alt+1 – Alt+9 · not set · Fixed. Plugin groups: "SOULLINK & NUZLOCKE · 3 keys" (Show or hide HUD widget Ctrl+H, Open team Ctrl+D; gamepad "not set"), "ITEM HELPER · LOCAL · 2 conflicts" (amber). Conflict rows (surface `#1d1a12`, left bar and key border amber): "Peek bag Ctrl+H → Unassigned · Already used by SoulLink & Nuzlocke → Show or hide HUD widget. Pick another key."; "Quick save F11 → Unassigned · Already used by Toggle fullscreen. FrameBeam keys always win." The note sits aligned under the key column (P35).
- Footer: "Gamepad hotkeys: hold Select, then press the second button. They are never sent to the game." Plugin hotkeys have no gamepad default (decision bk); every FrameBeam row has a gamepad column and an Esc row (P34, P36).

### 3r-3 Player Multiview (Round 3)
Source: `#3r-3-2`, `#3r-3-3`, `#3r-3-4` (1440 x 900), `#3r-3-2s`, `#3r-3-3s`, `#3r-3-4s` (1280 x 800), template and data at lines 100-136 and 239-245. Fixes P38, P41, P43, P44, D18, D20 (and P37, decision 20).

- Header (56, game header): "← Library", "Multiview", layout segment "PiP | Row | Grid 2×2" (for fewer than 4 Sessions "Grid 2×2 · from 4" is shown disabled, `#6a6a70`), "❚❚ Pause", "↺ Reset" (stays visible at 1280, P44), "+ Add · n/4", "F3", "F11".
- Layouts: 2 Sessions Row (columns `1fr 1fr`); 3 Sessions Row (`1fr 1fr 1.25fr`: two DS tiles and one GBA tile, the wider cell for the landscape tile); 4 Sessions Grid 2×2 (`1fr 1fr`). Never a Grid with an empty fourth slot (P38, roadmap 0.14). Area padding 16, tile gap 12; right panel 300 (1280: 260).
- Tile: radius 10, surface `#121315`, padding 10, gap 8, border 2px; selected tile border 2px `#3cbfd8`, others `#24252a`: the one selection style (P43). Info row: index chip (mono 11, `#26272b`), name ("You · Lumen Drift"), pill (11, radius 10: "♪ Audio" accent `#16242a` / `#3cbfd8`, "Paused" neutral `#26272b` / `#d0d0d4`).
- Remote tile states (picture at opacity .45, centred card rgba(18,19,21,.88), border `#34353a`, radius 10, padding 10 16, title 14/600 + sub 12 `#9a9a9f`): paused "❚❚ Paused by Lena" / "Frozen picture · resumes here" (decision 20); loading "Jonas’s game is loading" / "Connecting · 3 s"; ended "Session ended" / "Sam quit at 18:58 · Remove tile".
- Right panel for a selected remote tile: eyebrow "TILE 2 · WATCHING", "Lena · Harbor Rally", status "❚❚ Paused by Lena · 1 min", button "♪ Audio here", "Remove from Multiview", note "You only watch and listen. Lena’s picture stays frozen until she resumes." (decision x: input never goes to a remote tile).
- Add popover (`#3r-3-3`): opens below the toolbar, never over the panel header (P41): "Running sessions · n of 4 tiles", row "Sam · Meadow Courier · GB · for 4 min" + "Add", help "Opens below the toolbar; never covers the panel header." The "+4" tab overflow popover follows the same rule (plugins.md 4e).

### 3h-3 Player Session (Round 3)
Source: `#3h-3` (137-157), 1440 x 900, panel 360. Fixes P40, P41, P45 (artboard caption) and P46, P47 (sheet range P45-P47; the tile strip and map legend are specified in [plugins.md](plugins.md)).

- Header: "← Library", "Lumen Drift", "● Shared · 2 watching", "❚❚ Pause", "↺ Reset", segment "Session | Multiview", "Diagnostics F3", "Fullscreen F11".
- Diagnostics docked as a tab of the side panel (tabs: Game, plugin icons "⛓" and "◉" with the plugin name on hover or focus, Diagnostics); never an overlay over the game frame or the remote tiles. Rows (mono key/value): Core "melonDS DS 2026.10.09", Renderer "OpenGL 4.6 · 3×", FPS "59.8 / 59.83", Frame "5.1 ms · emu 3.7", Audio "84 ms · 0 underruns", Encoder "<encoder> · 6.0 Mbit/s", viewer lines "Lena · Direct · RTT 18 ms · 0.4 %", "Jonas · Relayed (TURN) · RTT 74 ms". Footer "Docked as a panel tab so it never covers the game or remote tiles. F3 opens and closes it."
- HUD: "1 widget hidden · Show" is a pill (12, `#1f2024`, border `#34353a`) above the game's bottom edge (bottom offset 62), not on it (P45); HUD widget top left "Route 3 · 2 alive" (16, 16).

### 3i-3 Player PiP (Round 3)
Source: `#3i-3` (158-178), 1440 x 900. Fixes P42, D20.

- Header as 3r-3, layout "PiP" active. The main tile is inset by 16 inside the frame (border 2px `#3cbfd8`, caption "You · Lumen Drift" bottom left); the PiP tile (240 wide at M, surface `#17181b`, border `#34353a`, radius 10, padding 8, shadow `0 8px 24px rgba(0,0,0,.5)`) sits inside the main tile and no longer overlaps its border.
- PiP controls row: index chip, size steps "S | M | L" (active M, accent), "♪" audio, "⇄" swap, "✕" remove. Corner snap targets: dashed 60 x 40 areas (`#3a3b40`) in three corners while dragging, hint "Drag to snap to a corner · S / M / L size". Paused remote: picture dimmed and "❚❚ Paused by Lena" centred on the PiP picture; caption "Lena · Orbit Gardens".

### 3y-2 Player Fullscreen, paused (Round 3)
Source: `#3y-2` (179-190), 1440 x 900. Fixes P34, P39; decisions 20, 21.

- Black frame, game dimmed to .45. Toolbar centred at top 20 (surface `#17181b`, border `#2a2b30`, radius 10, padding 8 10, 13px, nowrap): game name (600), primary "▶ Resume Esc" (accent, mono 11 key), layout segment "Stacked | Side by side | Top only", button "Leave fullscreen F11".
- Pause card centred (380 wide, padding 24 32, radius 14, surface rgba(18,19,21,.92), border `#34353a`): "Paused" 22/600; "Viewers see “Paused by Max”. Saved to “Main” · today 18:52" (13, `#9a9a9f`; the first sentence only while the game is shared); buttons "Resume" (accent) and "Leave fullscreen" (outline); hint "Esc or Resume continues · F11 leaves fullscreen" (12, `#7a7a80`).
- Behavior (decision 21): Esc pauses in a window and in fullscreen and opens this overlay; a second Esc or "Resume" continues; F11 toggles fullscreen. The diagnostics section of 3y stays for the unpaused fullscreen.

## Round 4 (2026-10-21)

Source `source/round-4-a.dc.html` (6a, 6c, 6d, 6e), `round-4-b.dc.html` (6g, 3e-4, 6h, 3c-8, 6i, 6k-2), `round-4-c.dc.html` (6m, 3c-3-4, 6n, 6o, 6p), `round-4-d.dc.html` (3g-4, 6q-6x, 3r-4, 3y-3), `round-4-e.dc.html` (6z, 7c) and the rules in `round-4-sheet.dc.html` (space plan 0-3, handoff table in the script `handoff`); dark theme. Hub and plugin screens of the same handoff are in [hub.md](hub.md#round-4-2026-10-21) and [plugins.md](plugins.md#round-4-2026-10-21). Table with all artboards and line ranges: [README.md](../README.md#round-4-screens-2026-10-21-features-before-10); decisions 24-35 and open points bl-bq: [decisions.md](decisions.md); the only new color: [tokens.md](tokens.md#additions-of-round-4-2026-10-21). Mock names (Max, Lena, Jonas, Sam, Home Hub, Lumen Drift, Harbor Rally, Coral Saga: Ember, `hub.example.com`, versions 0.16.x) are illustrative. Round 4 builds on the Round 3 page rules (880 px content, 280 px control column, SettingsRow, date rule). The Settings navigation grows by "Voice" and "Notifications" (order: General, Hubs & library, Updates, Network, Voice, Notifications, Diagnostics, Plugins).

### Space plan 0-3 (Round 4)
Decided before any screen: every new control lands in one of five places, the in-game header, a panel tab, the OSD, the Player sidebar or the Hub sidebar (decision 35).

#### 0-3 A In-game header and R4Header
Source: `round-4-sheet#0-3-header` (22-115), component `source/r4-header.dc.html`. Replaces GameHeader for every Round 4 screen. 1440: 56 high, padding 0 16, surface `#111214`, bottom border 1px `#1e1f22`; groups padding 2, buttons 30 high, side padding 9-10; 1280: icons only, Reset moves into "⋯".

- Zone 1 title: "← Library" (compact "←"), game title 15/600, share pill ("Not shared" / "Shared · 2 watching" / "Invite only · 2"; compact "Shared · 2"), then up to 3 avatars (22, 20 in the sheet, speaking ring 2px `#6fd39a` outside a 2px gap); more than 3 shows "+2"; click opens the People tab. Chat unread is not shown here. Optional chip after the pill (Round 4 uses "✎ Memory edited" amber `#2a2414` / `#e5b45a`, "Developer mode" grey `#26272b` / `#c9c8c4`).
- Zone 2 your game: Pause, Speed-up with speed, Reset. Save states are no header button: hotkeys with OSD, the "⋯" menu and the States tab.
- Zone 3 audio and view: Screen layout "▾" (DS/3DS only, hidden for one-screen systems; menu 6i), Volume with its value (click opens slider and mixer popover, 6g/6v), Microphone (only while voice is on in this Session: "Hold V" / "Live" / "Mic off" / "Muted by host", 6w).
- Zone 4 window: Fullscreen (F11), Panel toggle carrying the unread count of Chat and People requests (cyan pill 16 high, `#3cbfd8` on `#0d1214`; grey `#2c2d32` for counts only; max "9+"), "⋯" for everything else. Diagnostics left the header: it is a panel tab (F3 still opens it).
- "⋯" menu (230 wide): THIS GAME: Save state F6, Load state F8, Take screenshot F12, Reset Lumen Drift…, Settings for this game…; TOGETHER: Ready check…, Pause for everyone Shift+Esc; HELP: Hotkeys…, Report a problem…
- Multiview mode: mode segment (PiP | Row | Grid 2×2 per decision 22) replaces the layout button; watch mode (watching only): "Watching {who} · {game}", connection pill "Direct · 18 ms" (green) or "Relayed · 48 ms" (cyan), "Leave".
- Placement of every action (Button = always in the header, ⋯ = overflow, Key = hotkey with OSD; gamepad column = hold Select + the button, decision 24; the keyboard column acts only while the key is not bound to the game):

| Action | 1440 / 1280 | Keyboard | Gamepad (hold Select +) | Feedback |
|---|---|---|---|---|
| Pause / resume | Button / icon | Esc | Start | Pause card (3y-2) |
| Speed-up | Button / icon | Space (hold or toggle) | R2 | "▶▶ 4×" while on |
| Reset | Button / ⋯ | none on purpose | none | confirm popover |
| Volume | Button + value / icon | + / − , M mute | D-pad ↑ / ↓, X mute | "♪ Volume 72 %", "Muted" |
| Mixer | in the volume popover | none | none | 6v |
| Mic mute | Button (voice on) | Shift+M | R3 click | "Mic off" / "Mic on" |
| Push-to-talk | Button shows state | hold V | hold R3 (if PTT; read as Select + R3, open point bm) | button "Live" |
| Chat | Panel tab | Enter opens input | A on the input opens the on-screen keyboard | unread on panel toggle |
| People | Panel tab + avatars | none | L1 / R1 in the panel | request count on panel toggle |
| Save state | ⋯ + States tab | F6 (slot), Shift+F6 next slot | R1 | "▣ State 2 saved" |
| Load state | ⋯ + States tab | F8 | L1 | "State 2 loaded · Undo" 6 s |
| Undo load | OSD action | Shift+F8 | L1 again within 6 s | restores the Auto state |
| Rewind | key only | hold Backspace | hold L2 | "◀◀ Rewinding" + bar |
| Screenshot | ⋯ | F12 | Y | "Screenshot saved" 1.5 s |
| Ready check | ⋯ + People tab | none | none | overlay 6s |
| Pause for everyone | ⋯ | Shift+Esc | Start (hold 1 s) | "Lena paused for everyone" |
| Screen layout | Button ▾ / icon | F7 cycles (Shift+F7 swap) | → (6i: Select + →) | "Layout: Side by side" |
| Panel | Button + badge | F4 | Select double-tap (the one exception) | |
| Fullscreen | Button / icon | F11 | B | |
| Diagnostics | Panel tab / panel ⋯ | F3 | none | |
| Save snapshot (file to Hub) | Session tab button | F5 | none | unchanged, decision 24 |

#### 0-3 B Side panel tabs
Source: `#0-3-panel` (117-148). Panel 340 (1280: 300); tab row 40, side padding 8, tabs 13px, padding 8 (300: 6), active 600 with a 2px `#3cbfd8` underline, icon tabs 32, "⋯" without badge, menu 230.

- Order: Session · (Multiview) · People · Chat · States | Diagnostics · plugin tabs. Never more than 4 text tabs. Multiview sits after Session only while Multiview is on; States hides on a remote tile (states belong to your game).
- Default tab Session; opening from the unread badge jumps to Chat, from avatars to People. Session tab keeps Sharing, Save and Quit; participants moved to People ("Invite a hub user…" stays in Sharing).
- Overflow: at 340 two icon tabs (Diagnostics, one plugin), the rest in "⋯"; at 300 all icon tabs fold into "⋯" and a selected overflow tab shows as the fifth tab until you leave it.
- Badges: cyan = unread chat or a request waiting for you; grey = count only (People). States dimmed (label `#5e5e63`) when off by run rule; inside: "Save states are off in this run · rule set by Max".
- Gamepad: Select double-tap opens the panel and focuses the active tab; L1 / R1 switch tabs; B returns focus to the game; focus ring 2px cyan, inset 4.

#### 0-3 C OSD and the zones over the game
Source: `#0-3-osd` (150-191). One OSD style for everything.

- Pill 36 high, radius 18, surface `#111214` at 88 %, line `#34353a`, text 13/500, optional icon (`#a3a3a8`), key badge, bar 96 × 4 (`#3cbfd8`). Amber variant only for "off by run rule" and "may not load": text `#e5b45a`, line `#4a3d1c`. No red.
- Position: top centre of the game area (not the window), 16 px below its top edge; the same in fullscreen and on a Multiview tile (tile-scoped events only, 13 → 12 px). Stack of max 2, newest on top, gap 8; the same kind replaces in place (volume, state slot); persistent ones (paused, rewinding) stay first.
- Duration: 1.5 s plain, 3 s with names, 6 s with an action (Undo), persistent while held or paused; fade 120 ms in / 200 ms out, Reduce motion = no fade.
- Zones: OSD top centre; plugin toasts bottom right (4g-2, max 3); chat bubbles bottom left (max 3); HUD corners (4e-5). Toasts and bubbles stack upwards from 102 px above the edge when a HUD corner widget is shown, from 16 px otherwise. Bottom centre stays free for the fullscreen toolbar (3y-2) and gamepad hints (7c). Notifications (6r) never use the OSD; in fullscreen only mentions show, as a chat bubble.
- Specimens: "♪ Volume 72 %" with bar, "♪ Muted" (M), "◰ Screenshot saved", "▣ State 2 saved" (F6), "▣ State 2 loaded · Undo" (Shift+F8, 6 s), "◀◀ Rewinding · 18 s left" with bar, "▶▶ Speed-up 4×" (icon cyan), "● Lena paused for everyone" (dot in player colour), "◆ SoulLink saved a snapshot · before Gym 3" (3 s), "⊘ Save states are off in this run · rule set by Max" (amber, 3 s).

#### 0-3 D Player sidebar: Online block and bell
Source: `#0-3-sidebar` (193-241); component `source/player-nav.dc.html` with `r4`. Order: nav · flexible space · Online block · Hub card · user row; nothing new above the nav.

- Online block above the Hub card: eyebrow mono "ONLINE · 3" with link "All" (opens 6q). Max 3 rows ordered playing first, then in Multiview, then idle; offline people only in "All". Row 36: avatar 24 in the player colour with status dot 8 (green `#6fd39a`/`#5fd28a` active, amber `#e5b45a` idle, grey offline), name 13, status 11 ("Playing Harbor Rally", "In Multiview", "Idle · 12 min"). Hover or focus shows one action: Watch if shared, otherwise Invite; Message is in the row "⋯" (0-2 rule).
- Below 860 px window height (1280 × 800) the block folds to one row of avatars with the count; a click opens the Online popover.
- Bell: in the user row at the right edge, 32 × 32, cyan count badge 16; opens the notification list (6r) as a popover to the right; no bell in the header. Tab order: nav → Online → Hub card → bell. Focus ring 2px cyan.
- Hub offline: Online shows one grey line "Not available while Home Hub is offline."; the bell keeps local notifications (update, backup failed) but no Hub count; Hub card shows "Retry" and "offline · 4 min".

### 6a What's new (Player)
Source: `round-4-a#6a` / `#6a-2` (template 26-56, data 472-473). Wave 0. Shown once after an update, over the dimmed Library (1440 × 900; 6a-2 at 1280 × 800).

- Sheet 600 wide, max 720 high, centred. Eyebrow "WHAT’S NEW" mono 11 cyan, title "FrameBeam 0.16.4" 24/600, meta "Stable · today · changes since 0.15.3, including the 0.16.0 – 0.16.3 betas" (a stable release sums up the betas since the previous stable).
- Groups ADDED · n, CHANGED · n, FIXED · n; item 14/21 with a tag column 56 (Player, Hub, Both). Long notes scroll with a fade at the bottom edge. Links "Full release notes ↗" and "Also in Settings → Updates"; primary "Got it" (footer padding 16 × 28).
- 6a-2: very short notes ("Stable · 28 Oct · one fix", one FIXED item) still look intentional, same sheet.
- Keys: Enter or A = Got it, Esc closes, arrows scroll. Reopen from Settings → Updates ("What’s new in 0.16.4") and from the bell.

### 6c Updates, roll back (Player)
Source: `round-4-a#6c` / `#6c-3` (template 202-224, data 506-508), `#6c-2` (228-243). Wave 1. Settings → Updates, content 880, control column 280.

- Status row "Player 0.16.4 is up to date · Stable channel · checked today 17:52" with link "What’s new in 0.16.4"; Update channel segment Off | Stable | Beta; "Install updates automatically" ("Never while a game is running.").
- EARLIER VERSIONS · 3: "Go back if something stopped working after an update. Settings stay; saves and states are on Home Hub and are not touched. The Player keeps the last 3 versions (412 MB)." Rows: version mono, state ("installed today 03:20", "used 2 Oct – today"), outline button "Go back…" (focus ring 2px cyan with 2px gap); the running version shows "Running".
- 6c-2 dialog 560: "Go back to 0.16.3?" / "The Player closes, installs 0.16.3 and starts again. About 30 s." with rows Settings (kept; newer settings are ignored and come back after the update), Saves and states (not touched), Plugins (amber line when one needs the newer Player: "SoulLink & Nuzlocke needs 0.16.4 and is turned off until you update."), Updates (paused). Buttons "Cancel", "Go back to 0.16.3". While a game runs the primary is muted and the dialog says "⊘ Lumen Drift is running. Quit the game first; it saves and syncs."
- 6c-3 after going back: amber card (`#1d1a12` / `#4a3d1c`) "Running 0.16.3 · automatic updates paused until you resume them" / "You went back from 0.16.4 today 18:52. 0.16.4 stays available." with "Update to 0.16.4" and "Resume updates"; the automatic-updates toggle is off ("Paused after going back. “Resume updates” turns it on again."); list shows 0.16.4 as "skipped · available"; the affected plugin row reads "SoulLink & Nuzlocke is off · Needs Player 0.16.4. Turns on again after you update." with link "Settings → Plugins".
- Not drawn (decision 32): rolling back one plugin on its own, "Use previous version" on the plugin row and in the plugin detail (4a-3, 4a-4); runs keep their pinned plugin version.

### 6d Report a problem (Player)
Source: `round-4-a#6d` (288-313), `#6d-states` (314-327, label 6d-2). Wave 1. Dialog 580 over the Library, opened from an error banner ("Session with Lena ended unexpectedly · today 18:41 · the game was saved" with "Connection check" and "Report a problem"), from Settings → Diagnostics and from the in-game "⋯".

- Title "Report a problem", sub "Goes to the admins of Home Hub. They see it under Reports." Field "What happened?" (textarea 96 high), chip "Linked: “Session ended unexpectedly” · today 18:41". ATTACHED · 2.4 MB with link "See what is included"; rows 34 high, 13px, checkbox + name + size: Logs of the last 3 days (2.1 MB), Settings (no passwords, no Hub token; 18 KB), Connection check from today 18:43 (4 KB), System info: Windows version, GPU, driver (2 KB, unchecked by default). Line "Never included: passwords, the Hub pairing token, ROMs, saves."
- Footer: "Save as file…" (left), "Cancel", primary "Send to Home Hub". Keys: Ctrl+Enter sends, Esc cancels.
- Footer states (6d-2): sending ("Sending to Home Hub…", "1.0 of 2.4 MB. You can keep using the Player.", progress bar, "Cancel"); sent ("✓ Report sent · #12", "The admins of Home Hub see it under Reports. Nothing else to do.", "Done"); Hub offline ("⊘ Home Hub is offline", "Save the report as a file and send it later, or keep the dialog open; it sends when the Hub is back.", "Save as file…", disabled "Send to Home Hub"); failed ("Could not send", "Upload stopped at 40 % (connection lost). Your text is kept.", "Save as file…", primary "Try again").

### 6e Connection check
Source: `round-4-a#6e` (376-397). Wave 1. Dialog 600 "Connection check · This device · Desktop-LivingRoom · to Home Hub", opened from Settings → Network, Settings → Diagnostics, a failed Session and the "Relayed" pill.

- Five steps in a list (columns 22 | 150 | 1fr): Hub reachable → STUN → TURN relay → NAT type → Test connection. Each shows an icon (✓ green `#6fd39a` on `#17291f`, "!" amber, ✕ grey `#26272b`, running = 2px cyan ring, waiting and skipped = 1.5px grey ring), the result (13) and a plain explanation (12).
- Outcomes (summary card, 12 × 14, below the steps): running ("step 3 of 5", button "Cancel"); direct possible (green card "Your network allows direct connections" / "Sessions go straight between Players. The relay is ready as a fallback."); only relayed (amber `#1d1a12` / `#4a3d1c`: "Only relayed connections are possible here" / "This works but adds delay (about 30 ms more). Common on office and mobile networks. Turning on UPnP on your router, or using another network, usually allows direct connections."; NAT row "Symmetric NAT" amber); Hub not reachable (grey card "Home Hub cannot be reached", the other steps "Skipped", extra button "Edit Hub address…").
- Actions "Copy result", "Add to problem report", "Run again" (right, primary when the Hub was unreachable). Keys: R = Run again, Ctrl+C copies the result.

### 6g Volume
Source: `round-4-b#6g` (24-56), `#6g-2` (58-86). Wave 2. Header Volume button, popover and OSD (1440 × 900).

- Header button: icon + value ("72") at 1440, icon only at 1280 (value in the tooltip). States: normal, muted (grey `#8e8e94`, slash on the icon, label "Muted"), focus (ring `0 0 0 2px #111214, 0 0 0 4px #3cbfd8`), open (`#15262b`, text `#3cbfd8`).
- Popover 300: "Volume 72 %" with slider (4px track, 16px knob with focus ring) and mute button (♪), hints "M mute", "+ / − 5 %", "Select + ↑ ↓", entry "Mixer: My game · Voice · Plugin sounds ›" (opens 6v), status "Muted in the background: on · Sound settings". A or Enter on the focused button opens it.
- OSD while changing: "♪ Volume 72 %" with bar (updates in place), "♪ Muted M", "♪ Volume 100 % max".
- Multiview tile label (12 on `#111214` at 82 %, top right): "Muted" only when muted on purpose (mixer, M on that tile); tiles that are silent only because audio plays elsewhere show nothing; the tile with the audio shows "♪ Audio here" (3r-3); master mute shows "Muted" on every tile and in the header.
- Settings rows (SettingsRow): "Mute when the window is in the background" (toggle, default on; "Sessions you watch stay audible unless you mute them."), "Volume steps" (segment 2 % | 5 % | 10 %, default 5 %), "Show volume on screen" (toggle).

### 3e-4 Emulation, Display tab (Round 4)
Source: `round-4-b#3e-4` (90-109), `#3e-4-2` (110-118). Wave 2. Emulation page of 3e-3: system list, header "Nintendo DS · melonDS DS 1.4 · saved on this device only" with "Reset all to default"; tabs Display | Core options | Firmware | Audio.

- Display tab sections (eyebrow mono 11, 20 above): PICTURE (Integer scaling toggle; Aspect ratio segment Core | 4:3 | Stretch; Screen layout select, "Default for DS games. Change in game with F7; a game can keep its own (6h)."), RUN-AHEAD (segment Off | 1 frame | 2 frames with meta "CPU +35 %"; "Hides input delay by running a few frames ahead. Uses more CPU. Allowed in runs."), REWIND (toggle; "Hold Backspace (gamepad: hold Select + L2) … Can be turned off by a run rule."; Rewind length segment 10 s | 30 s | 60 s with meta "about 180 MB memory"), RENDERER (segment OpenGL | Vulkan; Vulkan disabled with the reason "Vulkan: melonDS DS offers OpenGL only", reset column empty).
- 3e-4-2 rows that differ per system (same SettingsRow, only rows the core uses): Nintendo 3DS · Azahar: Run-ahead disabled ("Azahar cannot run ahead: 3DS games are too heavy to run twice"), Rewind length 10 s with meta "about 1.1 GB memory" and badge "high memory", Renderer Vulkan offered ("applies on next start"); Game Boy / Color / Advance · mGBA: no Screen layout row, Renderer row hidden (software), Aspect "Core: 10:9 for Game Boy, 3:2 for Game Boy Advance".

### 6h Settings for this game
Source: `round-4-b#6h` (122-137). Wave 2. Sheet 560 from the right over a 60 % scrim, opened from the detail badge "3 changes" (3c-8) or from the in-game "⋯" → Settings for this game….

- Header "Settings for Lumen Drift" with "×", sub "Game Boy Advance · mGBA · 3 changes from your defaults" and "Reset all 3". Tabs Display (2) | Controls (1) | Screen layout with change counts (active underline cyan).
- SettingsRow list: unchanged rows read "Uses default (On).", changed rows "Changed for this game · default Core." with the cyan dot and "Reset". Controller profile row "Uses default (Xbox pad · Standard). Set on the Controls tab." Footer "Changed for this game · Reset goes back to your default for Game Boy Advance". Keys: Esc closes, L1 / R1 switch tabs.

### 3c-8 Player Library (Round 4)
Source: `round-4-b#3c-8` / `#3c-8-2` / `#3c-8-4` (template 140-196, data 446-448), `#3c-8-states` (199-213, label 3c-8-3), `#6k-2` (355-363). Wave 2. Builds on 3c-6 (header row, chips, grid max 1680, detail 340).

- Continue row above the grid: card 1.6fr "CONTINUE · Lumen Drift · Main · v14 · 2 h ago" with button "Continue" (plus 1-2 recent tiles 1fr, "yesterday", "16 Oct"). Gamepad: Continue gets first focus (ring 2px `#3cbfd8`, gap 2); Enter on Continue starts.
- Chips: "All · 10", "★ Favourites · 3", collections ("Couch co-op · 4", "Nuzlocke · 2"), "+ Collection" (13px, active = filled `#ececea`). Sort "Recently played ▾" (falls back to Name when nothing was played). Tile: star 26 top right (☆ grey `#7d7d83` / ★ amber `#e5b45a`; tooltip "Add to favourites · F"), system badge, play time line ("12 h 40 min · 2 h ago", "Never played"). Selected tile border 2px cyan.
- Detail 340: star, title, "Game Boy Advance · mGBA", collection chips with "+ Collection" opening the popover ADD TO COLLECTION (checkbox rows, name + count, "Create"), badge "3 changes" (opens 6h), facts Play time / Sessions / Save ("Main · v14 · synced") / States ("3 of 4 slots"), SCREENSHOTS · 8 strip with "All on Home Hub ↗", buttons "Play · Main v14" (primary) and "Play and share Session".
- 3c-8-2 (1280 × 800): chips shorten ("★ 3", "+"), 2 columns, popover open. 3c-8-4: Favourites chip with none ("☆ No favourites yet" / "Star a game on its tile or in the detail. Favourites are yours only and follow you to every Player on Home Hub." / "Show all games").
- 3c-8-3 states: empty collection ("Short sessions is empty", Rename / Delete collection…); first start (no Continue strip until you have played once); Continue when the save is not on this device ("Downloads the save from Home Hub first (“Getting save…”), then starts. Never starts an older local save without asking."); collection "⋯" (Rename, Delete collection…; deleting never deletes games; collections are per user and sync through Home Hub).
- 6k-2 game detail facts: played (Play time, Last played, Sessions · since 3 Sep), never played (Play time —, Last played never, Added 14 Oct), in a run (In this run 9 h 15 min, Total 31 h, Run started 2 Oct). Key F = favourite.

### 6i DS and 3DS screen layouts
Source: `round-4-b#6i` (218-248), `#6i-2` (249-272). Wave 2 (1280 × 800 and 1440).

- Layout menu from the header button "▾" (300 wide, rows 36, layout glyph 22): Stacked, Side by side (✓), Large top small bottom, Top screen only, Bottom screen only, divider, "Swap screens Shift+F7", "Bottom screen in its own window", footer "Saved for Clocktower Kids. Default for all DS games: Emulation → Nintendo DS." OSD "Layout: Side by side F7". F7 cycles, Select + → cycles.
- 6i-2: the bottom screen in a second window on monitor 2 (4:3, resizable, touch works with mouse or touch screen); the header shows the pill "Bring back"; the main window's top screen fills it. "Bring back", closing the second window, F7 or any layout puts the screen back in the main window; an unplugged monitor does the same with the OSD "Bottom screen is back in the main window"; position and size are remembered per game.

### 6m Save states
Source: `round-4-c#6m` (23-54), `#6m-2` (56-75), `#6m-3` (77-90). Wave 3. A save is what the game writes; a state is a picture of the whole emulator and lives on Home Hub; loading a state never changes a save until the game saves again; every load makes an Auto state first so it can be undone.

- States tab (panel 340): header "STATES · 3 OF 4 ✓ on Home Hub". Rows: thumbnail 72 × 48, number, name ("Slot 2 · “Lighthouse”"), meta "just now · game time 12 h 40 min", tags "Auto" (grey) / "New" (cyan `#15262b` / `#3cbfd8`); padding 8, radius 9; selected `#15262b` with a `#3cbfd8` border; empty slot "Empty · select and press F6". Auto first (decision 27: the last 3 Auto states, newest first; the drawing shows one row), then slots 1-4. Buttons 36: "Save to 2 F6", "Load 2 F8", "⋯" (Rename, Download, Delete state… red). Hint "Shift+F6 next slot · Shift+F8 undo load · gamepad Select + R1 / L1"; note "States are not your save. Main v14 changes only when the game saves."
- 6m-2: loading from the panel asks inline (card: "Load state 1 from today 17:02?" / "Replaces the running game (1 h 29 min of play). Your progress is kept as Auto first; Undo for 6 s or load Auto any time." / "Cancel", "Load state 1"; checkbox "Don’t ask again (F8 never asks)"). F8 loads at once. Both make an Auto state ("before loading state 1") and show the OSD "▣ State 1 loaded · Undo Shift+F8" with a ring counting the 6 s. After Undo: "↶ Back to where you were"; the Auto state stays until the next load replaces it (oldest of the three).
- 6m-3 states: no states ("Empty" rows, "Save to 1 · F6"); off by run rule (amber banner "Save states are off in this run · rule set by Max" / "Harbor Nuzlocke. Your states stay; you can use them again after the run.", rows at 45 %, buttons disabled, hint "F6 and F8 show the same text as amber OSD"); state from an older core (row text amber "May not load, made with melonDS DS 1.3. If it fails, nothing changes.", button "Try to load 2"); Home Hub offline (banner "Saved on this device only" / "Uploads to Home Hub when it is back. States from other devices are not available now.", row "just now · waiting to upload", gamepad focus ring).

### 3c-3-4 Player Saves and states
Source: `round-4-c#3c-3-4` (93-123). Wave 3. Library → game → "Saves and states" (replaces the Saves view title of 3c-3); content grid 1.15fr | 1fr.

- Left SAVE · WHAT THE GAME WRITES with slot select "Main ▾": SaveRow history (v15 current, v14 diamond "◆ Snapshot" with label "by SoulLink · before Gym 3" and reason "Plugin snapshot", plain versions, thinned row "10 older versions"); text "Versions arrive each time the game saves. Restore makes an older version current; the newer ones stay."
- Right STATES · WHOLE EMULATOR AT ONE MOMENT: rows (thumbnail 64 × 42, Auto/1-4, name, meta "today 18:40 · 12 h 40 min", action "Play from state" or "Try to load" with "may not load" amber for an older core, "⋯"); text "Made with F6 or from the States tab. Loading one never changes the save above until the game saves again." and "“Play from state” starts the game and loads it; the save in Main is used as well."

### 6n Rewind
Source: `round-4-c#6n` (165-178). Wave 3. Hold Backspace or Select + L2.

- OSD "◀◀ Rewinding · 18 s left" with a bar (120 × 4) that empties from the right; the game runs backwards at 2× while held, audio muted; release resumes from here and nothing is saved until the game saves. Start of the buffer: "Start of rewind · 30 s", bar empty, stays on the first frame (length: Emulation → Display → Rewind length). Off by run rule: amber "Rewind is off in this run · rule set by Max" for 3 s on each key press, the game keeps running.
- Viewer side: the stream goes back and the tile shows "◀◀ Max is rewinding" (12px, picture saturate .4). In a run with rewind off the host cannot rewind, so viewers never see it.

### 6o, 6p Memory writes and plugin snapshots
Source: `round-4-c#6o` (181-211), `#6o-2` (213-249). Wave 3. Plugin part (run log, board, permission): [plugins.md](plugins.md#6o-2-memory-edits-in-runs-round-4).

- 6o: after a plugin writes game memory, the amber OSD "✎ Creature Editor changed game memory · Undo to snapshot" for 6 s, then the header chip "✎ Memory edited" (12/500, `#2a2414` / `#e5b45a`) until the next save; chip focusable, Enter opens the popover (340, amber line `#4a3d1c`): "Game memory was changed", lines "✎ 18:31 · Creature Editor set Mossling level 14 → 20", "A snapshot “before Creature Editor · 18:31” was made first. Undo loads it and replaces 1 min of play.", "Logged in Harbor Nuzlocke · run board shows “Editor used”.", buttons "Keep changes" and "Undo to snapshot".
- 6p: OSD "◆ SoulLink saved a snapshot · before Gym 3" (3 s, never stacked with a toast for the same event). The snapshot label "by SoulLink · before Gym 3" shows in the Player saves view and in the Hub history; plugin snapshots are exempt from retention and clean-up.

### 3g-4 In-game header and panel (Round 4)
Source: `round-4-d#3g-4` / `#3g-4-2` (template 24-57, data 346-348). Wave 4. Header = R4Header (0-3 A), panel tabs per 0-3 B.

- 3g-4 (1440 × 900): Session sharing own game, voice push-to-talk, 2 unread; panel 340, Session tab: SHARING ("Shared · 2 watching", segment Private | Hub users | Invite only, "+ Invite a hub user…", "Stop sharing"), SAVE ("Saves and states →", "Main v14 ✓ synced", "◆ Save snapshot F5"), "Quit game" with "Saves and syncs first". Tabs: Session, People 3 (grey), Chat 2 (cyan), States, then icons (diagnostics, plugin).
- 3g-4-2 (1280 × 800): icons only, panel 300 with "⋯", OSD "♪ Volume 72 %", People tab: "IN THIS SESSION · 3" with "Ready check"; rows (padding 8, avatar 28 in the player colour, name, role "Host" / "Viewer", line: host "Encoding 6.0 Mbit/s · 60 fps", viewers "Relayed · 48 ms · gets High 1280 × 960" / "Direct · 18 ms · picked Medium", speaking ring, "⋯"). Note "Quality = what the viewer receives. You send one stream; each viewer gets the best their connection allows unless they pick lower."

### 6q Online popover
Source: `round-4-d#6q` (62-81). Wave 4. From "All" in the Online block, over the Library. Popover 340, rows 44, avatar 28, status dot 9; header "On Home Hub · 3 online · 1 offline".

- Row: avatar, name, status ("Playing Harbor Rally · shared", "In Multiview · watching Lena", "Idle · 12 min", "Offline · 2 h ago" at 60 %), one action (outline button: Watch if shared, otherwise Invite; none for you and offline people), "⋯" (Message). Footer "Show what I play: On · Settings → General" (privacy line). Keys: arrows move, Enter = the row action, menu key = "⋯". Users with "Show what I play" off show only Online / Idle / Offline. Hub counterpart: [hub.md](hub.md#6q-hub-hub-users-with-status).

### 6r Notifications
Source: `round-4-d#6r` (105-125), `#6r-2` (126-136). Wave 4.

- Bell popover 400 anchored to the bell (1280 × 800, Online folded): header "Notifications", "Mark all read", "⚙" (opens 6r-2); sections NEW and EARLIER; row grid `8 | 28 | 1fr | auto`: unread dot cyan, icon (avatar 28 round or tile 28 radius 6: "SN", "↑", "!"), text 13 + sub 12 ("just now · Invite"), one action button (Watch, Open run, What’s new, Open Status; cyan outline when unread); unread row surface `#212226`. Examples: "Lena invited you to watch Harbor Rally", "Lena’s Tidepup fainted · your Mossling is linked", "Player 0.16.5 is available", "Backup failed on Home Hub · not enough space" (admins only, amber icon). Earlier notifications are kept 14 days.
- Empty "Nothing new"; Hub offline: only notifications from this Player (update, local errors); Hub notifications arrive when it is back. Bell focusable in the sidebar tab order.
- 6r-2 Settings → Notifications (SettingsRow, segment Toast | List only | Off): Invites (Toast), Sessions started (List only), Run events (Toast), Updates (List only), Hub problems (Toast; admins only); toggle "Quiet in fullscreen" (on: only mentions (@Max) show, as a chat bubble; everything else waits in the bell); toggle "Sound for toasts" (off).

### 6s Ready check and pause for everyone
Source: `round-4-d#6s` (140-157), `#6s-2` (158-171). Wave 4. Who may ask: decision 28 (anyone in runs, only the host in Sessions).

- Participant view (card 460 over the dimmed game): "Ready?" / "Lena asked · Harbor Nuzlocke · before Gym 3"; countdown ring 52 with the seconds left (30 s); rows 42 per person with status pill: Ready (`#17291f` / `#6fd39a`), Not ready (amber `#2a2414` / `#e5b45a`), No answer yet (grey `#26272b` / `#a3a3a8`); buttons "Not yet" (B) and "I’m ready" (A, primary). Text "Your game is paused while the check runs. When everyone is ready, Lena presses Start and all games resume together after 3 · 2 · 1. No answer after 30 s counts as not ready." Keys A / B.
- 6s-2 host view: "3 of 4 ready" → card "Sam is not ready" / "Lena, Jonas and you are ready. Start anyway or wait; the check closes after 30 s." with "Cancel check", "Start without Sam", "Wait"; all ready: OSD "Starting in 3" (cyan dot) counting 3 · 2 · 1.
- Pause for everyone (Shift+Esc): OSD "● Lena paused for everyone" (dot in the player colour) on your game and, tile-scoped, on that Multiview tile; card "Paused by Lena" / "All games in Harbor Nuzlocke are paused. Lena or the run creator (Max) can resume." with "Resume for everyone"; someone who may not resume sees "⊘ Only Lena or Max can resume. Ask in Chat." with "Open chat". Setting card: "Who can ask “Ready?” and pause for everyone" segment Host only | Everyone; "Resume: the person who paused or the host."

### 6t Chat
Source: `round-4-d#6t` (174-195). Wave 4. Chat tab of the panel (1440, input focused). Hub history: [hub.md](hub.md#6t-2-hub-run-chat-history).

- Header "Run chat · Harbor Nuzlocke · 4 people" and "Bubbles: on". Messages: avatar 20 in the player colour, "name · time" 11, text 13 in a bubble (radius 8, padding 6 10, surface `#1a1b1e`, border `#26272b`; own messages `#232428`); mention (@Max) border `#3cbfd8`; pings are emphasised bubbles (☠ "Down!" amber `#2a2414` / `#4a3d1c` with the speaker ring when the person is talking, "Gym next" cyan `#15262b` / `#1f4650`). System lines from plugins are centred grey text ("SoulLink · Lena’s Tidepup fainted · Mossling linked", "Today 18:02 · Jonas joined").
- Ping chips (28 high, number keys while the input is empty): ☠ Down! (1), Gym next (2), Wait for me (3), GG (4), "+ Custom". Input 38 high ("Enter sends · Esc back to game"). Keys go to the chat while it has focus; the game keeps running. Gamepad: A on the field opens the on-screen keyboard (7c).
- Bubbles appear on tiles (3r-4) and in fullscreen (3y-3), fade after 6 s, switchable ("Bubbles"). Session chat outside runs: decision 33.

### 6u People, quality and invite role
Source: `round-4-d#6u` (211-234). Wave 4.

- Viewer side of the People tab: host card "L Lena · host · Harbor Rally · speaking"; YOUR STREAM segment (4 cells) Auto | High | Medium | Low; "Now: 1280 × 960 · 60 fps · 4.1 Mbit/s · Relayed · 48 ms. Low saves data on a phone hotspot." and "Same control on the tile: hover → ⋯ → Quality."; button "Ask to play…" (6x).
- Invite dialog 412: "Invite to Lumen Drift", person row "Sam · online · idle", "Sam can" with two radio cards: "Can watch" (Sees and hears the game, can chat and talk. Sends no input.) and "Can ask to play" (Like watch, plus “Ask to play”. You still decide each time (6x).); "Cancel", "Send invite".

### 6v Mixer
Source: `round-4-d#6v` (239-259). Wave 4. Panel inside the volume popover (6g), opened by "Mixer ›"; 360 wide, row columns `24 | 1fr | 110 | 28` (icon, label + sub, slider, mute). Multiview with 3 tiles.

- "‹ Mixer · Master 72 %". Channels: My game (80 %), each tile with the player avatar ("Lena · tile 2" · "100 % · remembered for Lena"; muted tile at 60 % opacity, cyan mute), Voice ("70 % · lowers game while someone speaks"), Plugin sounds ("SoulLink & Nuzlocke", 40 %). Toggle "Mix all tiles" ("Off: only the tile with “♪ Audio here” plays"). Tile labels show the per-tile volume ("♪ 80 %") or "Muted". Values are remembered per person. Keys: arrows adjust the focused slider, M mutes the focused channel.

### 6w Voice
Source: `round-4-d#6w` (263-277), `#6w-2` (278-287). Wave 4. Decision 30 corrects the drawing: voice flows directly between Players over the same peer connection as the video (TURN relay only as the fallback); it does not go through the Hub.

- Settings → Voice (SettingsRow): intro "Talk with people in your Session or run. Voice goes directly between Players like the video." (drawing: “… goes through Home Hub like the video”, wrong); Microphone (select "Headset Microphone (USB)", level meter 8 high with the voice-activation threshold line 2 × 14); Talk (segment Push-to-talk | Voice activation); Push-to-talk key (buttons "V", "R3 (hold)": click, then press the key or button); Noise suppression (on); "Lower game volume while someone speaks" (on; game and tiles go down by half); "Join voice automatically" (off; "In runs and invite-only Sessions. Off: press the mic button to join.").
- 6w-2 header mic states: push-to-talk idle "Hold V" (Shift+M turns the mic off; the speaker's avatar gets the 2px `#6fd39a` ring in header, People tab, tiles and fullscreen); talking "Live" (`#15262b` / `#3cbfd8`, cyan fill; own ring is green for others; game volume drops if "Lower game volume" is on); "Mic off" (slashed; Shift+M / Select + R3; others see a slashed mic next to your name; you still hear everyone); "Muted by host" (amber; only the host can undo it, ask in Chat); "No mic" (grey; you can listen, connecting a microphone makes the button appear without a restart); "Blocked" (permission denied: “Windows blocks the microphone for FrameBeam. Open Settings → Privacy → Microphone.”; Linux: the PipeWire portal prompt again).

### 6x Hand over the controller
Source: `round-4-d#6x` (291-303). Wave 4. Request → host decides → indicator on both sides → take back. Off in runs unless the rule allows it. Latency rule: decision 29.

- Cards 330 (border `#1f4650`; amber `#4a3d1c` for rule off). Host, request arrives: "Lena wants to play as Player 2" / "Harbor Rally supports 2 players. Lena is relayed (48 ms): her inputs arrive about 3 frames late." with "Decline", "Hand over Player 1", primary "Allow Player 2". Above 120 ms RTT the card shows a clear amber warning; the host still decides.
- Indicator chip (cyan `#15262b` with a player-colour dot): host "Lena controls Player 2" (card: "Your controller stays Player 1. Take back any time; Lena gets a notice.", "Take back"); handed over Player 1: chip "Lena controls Player 1 · you watch", card "Your controller is paused" / "Press Start on your controller or “Take back” to play again. The save stays yours."; viewer: chip "You control Player 2 · 48 ms", card "Your controller sends to Max’s game" / "Latency 48 ms. Esc menu → Give back control. Max can take back at any time." with "Give back". Start on the host pad takes back.
- Viewer ask dialog "Ask Max to play?" / "Pick what to ask for. Max decides." with "Cancel", "As Player 2". In a run with the rule off: "Handing over is off in this run · rule set by Max" / "Harbor Nuzlocke: every player plays their own game. “Ask to play” is hidden for viewers."

### 3r-4 and 3y-3 Multiview and fullscreen (Round 4)
Source: `round-4-d#3r-4` (306-320), `#3y-3` (322-337). Wave 4. Extend 3r-3 and 3y-2.

- 3r-4 Grid 2×2 (1440): gap 12; tile label with avatar 20 in the player colour; right label ("♪ Audio here" cyan, "Muted", "Low · relayed" grey); speaking ring 2px `#6fd39a` (gap 1) on the speaker's avatar; per-tile chat bubbles (12px, newest at full opacity, older .55, fade); a paused tile (Lena) shows the tile-scoped OSD "Lena paused for everyone" 44 px from the tile top (14 otherwise) and the frozen picture. Selected tile border 2px cyan.
- 3y-3 fullscreen: toolbar at the top (top 20; Pause, "♪ 72", "● Live", "Chat 2", "Leave fullscreen F11"; shown on mouse move); OSD below the toolbar (84 from the top while the toolbar shows, 16 otherwise); chat bubbles bottom left with a cyan outline for mentions ("Lena @Max heal first, then gym"); voice avatars bottom right; "Enter replies · bubbles fade after 6 s · Settings: Chat bubbles in fullscreen". Only mentions break the fullscreen quiet (6r).

### 6z Privacy and Discord status
Source: `round-4-e#6z` (106-126). Wave 5. Plugin part (splits widget): [plugins.md](plugins.md#6z-splits-widget-round-4).

- Settings → General, PRIVACY (SettingsRow): "Show what I play" (toggle; “Others on Home Hub see the game you play in Online and Users. Off: only Online / Idle / Offline.”), "Discord status" (segment Show game | Show run | Off, default Show run), "Show play time to other members" (toggle; admins always see it, 6k).
- Previews (cards): SHOW GAME "Playing Coral Saga: Ember · via FrameBeam · 1 h 12 min"; SHOW RUN "Harbor Nuzlocke · day 19 · 3 badges · 6 alive"; OFF "Nothing is sent · Also when Discord is closed". Note: Discord must be running on this device; otherwise the row shows "Discord not found" and nothing is sent; no account linking, no logo.

### 7c Handheld, 1280 x 800, gamepad only
Source: `round-4-e#7c` (217-239), `#7c-2` (240-257). Wave 6.

- Library: focus ring 3px `#3cbfd8` with a 3px gap and the focused tile scaled 1.03; chips filtered with L1 / R1 (chips show "L1" and "R1" glyphs at their ends); search ("Y") opens the on-screen keyboard (keys 44 × 38, rows qwertyuiop / asdfghjkl / zxcvbnm, Space 260, Done 120; hint "Search · B close · X backspace · Y space · Start done"); the detail column becomes a sheet (A). Hint bar 56 at the bottom: A Play, X Favourite, Y Search, B Back, ☰ Details, L1 R1 Chips; labels follow the connected pad (Xbox / PlayStation / Steam Deck).
- 7c-2 in game: holding Select shows the full hotkey map as an overlay (release to close): Start Pause, R2 Speed-up, ↑ ↓ Volume, X Mute, R1 Save state, L1 Load state, hold L2 Rewind, Y Screenshot, → Screen layout, B Fullscreen, R3 Mic on/off, ↓ Splits plugin: split (grey, plugin-defined). Select double-tap puts focus on the header; the hint bar (A Open, B Back to game, ← → Move, L1 R1 Panel tabs) shows only while the header or panel has focus and is hidden during play.

## Components (revision v4)
Component sources are the `.dc.html` files in `source/` (README, "Component sheets"). Values below are taken from them and from the sheets.

### SettingsRow
`source/settings-row.dc.html`; sheet at line 1004 (`Sheet SettingsRow`), used by 3e-2 and 3p-2. Props: label, desc, ctrl (`toggle` | `seg` | `select` | `buttons` | `value`), value, on, opts, menu, buttons, changed, badge, meta, disabled (reason text), expanded, state (`hover` | `focus`), reset (`none` = empty reset column).

- Grid `14 | flex | 280 | 72`, align start, padding 14 0, divider 1px `#1f2024`. The control column's right edge is the content edge, so toggles, segments, selects and buttons end on the same x on every page.
- Dot slot (14): changed dot 6px `#3cbfd8`, vertically centered on the first line; the label never shifts. Label line min-height 32 (= control height) so dot, label, control and reset share one center line.
- Text column (padding-right 32): label 14/20 `#ecebe7` (disabled `#8e8e94`); badge 11/16 on `#232428` (text `#a3a3a8`, padding 1 6, radius 4, e.g. "applies on next start"); inline meta mono 11 `#7d7d83` (e.g. "Channel: beta"); description 12/18 `#8e8e94`, max 560, clamped to 2 lines with an underlined "More" (12 `#a3a3a8`; "More · n options" when a list exists). Expanded: full intro, option list (grid `auto | 1fr`, gap 4 12, padding 8 12, radius 6, surface `#16171a`, key 12/500 `#c9c8c4`, text `#8e8e94`), "Less". Disabled rows show the reason under the description (12 `#c9c8c4`, "⊘ {reason}") and the control at 40 % opacity.
- Description parsing: core text is split on line breaks; lines shaped "- Option: explanation" (or "Option: explanation" after the first line) become the two-column list, the rest is the intro; "More" shows when a list exists or the intro exceeds 2 lines.
- Controls: toggle 40 x 22 (on `#3cbfd8`, off `#2c2d32`, knob 16 `#ecebe7`); segment 280 x 32 (track `#1a1b1e`, border `#2c2d32`, padding 3, gap 2, radius 7, equal cells, active `#2c2d32` 13/500, inactive `#a3a3a8`); select 280 x 32 (surface `#1a1b1e`, border `#2c2d32`, radius 6, 13px, "▾" 11 `#7d7d83`; open: accent border); select menu 280 wide, padding 4, radius 8, surface `#1d1e22`, border `#34353a`, shadow `0 18px 40px rgba(0,0,0,.5)`, 30 high items (selected `#2c2d32` with accent "✓" in a 16px slot), max 8 visible then scroll, footer "Scroll for more · type to jump" (11 `#7d7d83`); buttons 32 high, padding 0 14, radius 7, border `#3a3b40`, 13/500, gap 8, natural width, right-aligned; read-only value mono 13 `#c9c8c4`.
- Segment vs select: segment when there are at most 4 options and every label fits its cell (280 / n - 12px); otherwise select (e.g. Time Mode, Cursor Mode). Decided per option at render time, not per core.
- Reset column (72): "Reset" 12 underlined `#a3a3a8` when changed, "Default" 12 `#5e5e63` otherwise, empty with `reset: none`.
- States: hover row `#17181b`; focus ring on the control `0 0 0 2px #121315, 0 0 0 4px #3cbfd8`.
- Keyboard: Tab moves row to row (focus lands on the control); Space toggles; left/right moves segments; Enter opens a select; "More" is a separate tab stop; Reset is reachable with Tab after the control.

### SaveRow (dark; light in the Hub)
`source/save-row.dc.html`; sheet at line 1045. Props: row `{n, kind: current | snap | plain | thin, label?, time, device, reason, actions?, confirm?: restore | delete, disabled?, text?}`, theme (`dark` | `light`), slot, currentN, bg. Light values are used by the Hub (hub.md, 3s-2).

- Layout: grid `20 | flex | auto`, gap 12. Rail column 20 with a 1px line (`#34353a` dark / `#d9d7d1` light). Nodes: current = 12px filled `#3cbfd8` with a 3px gap ring in the row background and a 1px `#3cbfd8` ring; snapshot = 9px diamond (`#3cbfd8` dark / `#1a7f96` light); other = 10px hollow, 2px border `#6f6f75` / `#b9b7b0`; thinned = dashed rail and a text row "⋯ {text}" (12).
- Text: version mono 13/500 "v{n}"; badges 11/600 pills padding 2 8 ("✓ Current": dark `#17291f`/`#6fd39a`, light `#e2f1e7`/`#1f6a3f`; "◆ Snapshot": dark `#15262b`/`#3cbfd8`, light `#ddf1f6`/`#0f5a6b`); snapshot label 13/500 in quotes; meta 12/17 "{time} · {device} · {reason}" (muted `#8e8e94` / `#6b6a66`), wraps in the 392 column; row padding 7 0 9, divider `#232428` / `#e9e7e2`.
- Actions: "Restore" 28 high outlined button (padding 0 10, radius 6, border `#3a3b40`); "Delete" text link, manual snapshots only (`#ef8a78` / `#b5402f`); "Download" text link, Hub only. With `disabled` every action except Download is muted.
- Confirmation: opens under the row, indented 32 (margin 8 0 10 32), padding 12 14, radius 9, gap 10; the active action's border turns accent (restore) or danger (delete). Buttons 30 high, right-aligned: "Cancel" (outline) and the confirm button. Restore: dark box `#15262b` / 1px `#1f4650`, title `#3cbfd8`, text `#c9c8c4`, confirm accent `#3cbfd8`/`#161512`; light box `#fff` / `#a6d9e6`, title `#0f5a6b`, confirm `#1b1b1d`/`#f6f5f2`. Delete: dark box `#2a1a17` / 1px `#4a2a24`, title and confirm `#ef8a78` (text `#161512`); light box `#fff` / `#efc9c2`, title and confirm `#b5402f`. Copy: "Restore v{n} as the current version of “{slot}”?" / "v{current} stays in the history." / "Restore v{n}"; "Delete snapshot v{n} “{label}”?" / "Only this snapshot is removed. The current version and all other versions stay." / "Delete snapshot".

### PadGlyph and the D-pad icons
`source/pad-glyph.dc.html`, `source/input-test.dc.html`; sheet at line 1076 (`Sheet D-pad and glyphs`). PadGlyph props: `g`, size (16-48, default 24), active, flat (no chip, icon only), fill, square.

- Chip: surface `#26272b` with inset ring 1px `#34353a`, glyph `#c9c8c4`; active = surface `#3cbfd8`, glyph `#161512` (flat active: glyph `#3cbfd8`). Shape: round for single letters and the four shapes, radius 25 % for words (padding 0 30 % of the size), radius 20 % for D-pad (`square`). Text: mono 500 at 50 % of the chip height; icon 62 % of the chip. Monochrome only.
- Glyph names: A B X Y LB RB LT RT View Menu; cross circle square triangle L1 R1 L2 R2 Create Options; up down left right; any other text is drawn as text (Select, Start, Shift, Enter, key names).
- D-pad icons: one SVG path in a 16 x 16 box (`M8 3.75 L12.5 10.25 H3.5 Z`, fill plus 1.6 stroke, round join) rotated 0/90/180/270 degrees about the center, so all four have the same size, weight and optical center; drawn icons, not font glyphs (identical on Windows and Linux). Shape icons (cross, circle, square, triangle) are 1.7 stroke outlines.
- Glyph sets (Button labels): Xbox (XInput / Xbox vendor IDs: A B X Y LB RB LT RT View Menu), PlayStation (DualShock 4 / DualSense: cross circle square triangle L1 R1 L2 R2 Create Options), Generic (unknown pads, DS names: B A Y X L R L2 R2 Select Start), Keyboard (key names).
- InputTest: grid 7 x 5 cells of 36px, gap 6, centered in the 292px panel; cell = PadGlyph (fill). Layout: triggers on top (2-cell pills, columns 1-2 and 6-7), shoulders below them, back and start in the middle (columns 3 and 5 of row 2), D-pad cross on the left (square chips radius 7), face diamond on the right (round). Props: `set` (xbox | ps | generic), `pressed` (glyph names, e.g. `["A", "right"]`).

### GameHeader
`source/game-header.dc.html`; sheet at line 232 (`Sheet GameHeader variants`). Props: mode (`session` | `multiview`), own (own game running; false = watching only), compact (1280), title, share (`not` | `shared` | `invite`), watchers, mvMode, tiles (e.g. `3/4`), paused, speedOn, speed, resetOpen, diagOn, pickerOpen, pickerState (`list` | `empty` | `full`), overflowOpen.

- Frame: 56 high, padding 0 16, zone gap 12, bottom border 1px `#1e1f22`, surface `#111214`; dividers between zones 1 x 20 `#2c2d32`.
- Five zones, left to right:
  1. Back: "← Library" (32 high, padding 0 8, radius 7, `#a3a3a8`); pauses the game and keeps it loaded; tooltip "Game keeps running, paused". Compact: "←" (32 x 32), tooltip "Library · game keeps running, paused".
  2. Context: Session: game title 15/600 (max 240; compact 150, ellipsis) + share pill (padding 4 10, radius 999, 12/500, 6px dot): "Not shared" (`#1a1b1e`/`#8e8e94`), "Shared · {n} watching" and "Invite only · {n} watching" (`#17291f`/`#6fd39a`). Watching only: "Watching" (13 `#8e8e94`), "{who} · {game}" 15/600, pill "Direct · 18 ms". Multiview: "Multiview" 15/600 + mode segment PiP | Side by side | Grid 2×2 (track `#1a1b1e`, padding 3, items padding 5 11, active `#2c2d32`).
  3. Game controls (your game only; group box surface `#16171a`, border 1px `#26272b`, radius 9, padding 2, gap 2): "Pause" / "Resume" (icon + label; tooltip "Pause · Esc", in multiview "Pause your game · Esc"), Speed-up split button, "↺ Reset" (tooltip "Reset game"). In multiview the group is preceded by the label "YOUR GAME" (mono 10/500, .08em, `#7d7d83`) over the share state (dot + text, 11). Watching only: instead one "Leave" button (32 high, padding 0 12, radius 7, border `#3a3b40`, 13/500).
  4. View: segment Session | Multiview (padding 5 12); in multiview additionally "+ Add" with the count "· 3/4" (mono 11 `#7d7d83`) as a 32 high button (padding 0 11, radius 7, surface `#1a1b1e`, border `#2c2d32`); popover open: surface `#15262b`, border and text `#3cbfd8`.
  5. Display (group box as above): screen layout button (icon of two stacked 10 x 6 boxes + "▾"; label "All tiles" in multiview at 1440; tooltip "Screen layout · this game only" or "Screen layout · all tiles"), "Diagnostics" (bars icon + label + key badge "F3"; tooltip "Diagnostics · F3"), "Fullscreen" (dashed square icon + label + key badge "F11"; tooltip "Fullscreen · F11").
- Button style (one for all actions): 30 high, radius 7, padding 0 10, 13/500, transparent inside a group box; hover `#1f2024`; on state (Speed-up, Diagnostics, open popover) `#15262b` with text `#3cbfd8` (key badge border `#1f4650`); danger-open (Reset) `#2a1a17` with text `#ef8a78`. Key badge: mono 10/500, padding 1 4, radius 3, border `#3a3b40`, text `#a3a3a8`.
- Segments are only for choosing a mode (Session | Multiview, PiP | Side by side | Grid 2×2). Diagnostics is an on/off button.
- Speed-up split button: left half toggles speed-up (Space; icon two triangles + label "Speed-up"), right half shows the speed ("4×", mono 12, "▾") and opens the speed menu 1.5× · 2× · 3× · 4× · 8× · Unlimited; divider 1 x 16 `#34353a`; on state as above. (The built speed set is 1.5×, 2×, 3×, 4×, 6×, 8×, ADR 0018; the drawn menu replaces 6× by Unlimited: open, reconcile when building.)
- 1280 collapse (compact): "← Library" becomes "←"; Pause, Speed-up, Diagnostics and Fullscreen become icon + key badge; the speed value stays visible; Reset and Hotkeys move into a "⋯" button (32 x 32, radius 7) with a popover (250 wide, 32 high items): "Reset {title}…", "Pause" / "Resume" with "Esc", divider, "Hotkeys…" with "Controllers"; "All tiles" moves into the layout tooltip; title truncates at 150. Multiview at 1440 already shows Diagnostics and Fullscreen as icon + key, so the Context zone keeps its natural width.
- Anchored popovers opened from this header: Reset, Running sessions, speed, screen layout, "⋯" (pattern below).

### Anchored popover
Sheet at line 252 (`Sheet anchored popovers`).

- Pattern: 12px rotated-square pointer centered on the anchor, 6px above the panel; panel 10px below the button; surface `#1d1e22`, border 1px `#34353a` (danger: `#4a2a24`), radius 10, shadow `0 18px 40px rgba(0,0,0,.55)`. Click outside or Esc closes; focus returns to the anchor. Used for Reset, Running sessions, speed, screen layout and "⋯". The anchor stays pressed while the popover is open.
- Reset popover (danger): 290 wide, padding 14, gap 10; title "Reset {game}?" (14/600), text "Unsaved progress since the last checkpoint is lost." (12/1.5 `#c9c8c4`), "Last checkpoint 40 s ago" (12 `#8e8e94`); buttons 30 high, right-aligned: "Cancel" (outline), "Reset" (`#ef8a78`, text `#161512`, 600).
- Running sessions picker: 340 wide (right edge 60 beyond the anchor), padding 12, gap 8; header "Running sessions" (14/600) and count "{n} of 4 tiles" (mono 11 `#8e8e94`). List (surface `#16171a`, border `#26272b`, radius 8): row padding 8 10, avatar 26, "{who} · {game}" 13, meta 11 `#8e8e94` ("Hub users · in tile 2", "Invited you · in tile 3", "Hub users · for 8 min"); right "✓ Added" (12, ok green) or "Add" (accent, 12/600, padding 5 10, radius 5). Empty: dashed box "No other sessions right now" / "Sessions appear here when someone on hub.example.com shares a game with Hub users or invites you." Full (4 of 4): box `#232428` "Multiview is full. Remove a tile to add Sam · Orbit Gardens." Footer (11 `#7d7d83`): "Private sessions are not listed. Sound plays from one tile only." Opens only on click of "+ Add" or an empty tile.

### GamePanel
`source/game-panel.dc.html`; sheet at line 261 (`Sheet GamePanel`). Props: kind (`own` | `remote`), multi, share, vis, watchers, collapsed, tileN, remoteWho, remoteGame, audioHere, quitting.

- Frame: 340 wide (1280: 320), surface `#111214`, left border 1px `#1e1f22`, padding 20 24, section gap 20 with 1px `#1e1f22` between sections; the Game section is pinned to the bottom.
- Own game, sections in this order:
  - SHARING (eyebrow mono 11 `#7d7d83` .08em; right: share state with 6px dot, 12, "Not shared" `#8e8e94` / "Shared · {n} watching" `#6fd39a`): visibility segment "Private" | "Hub users" | "Invite only" (3 equal cells, padding 3, gap 2, track `#1a1b1e`, border `#2c2d32`, cells padding 6 0); hint 12/1.5 `#8e8e94` — not shared: "Only you can see this game. Choose Hub users or Invite only, then share." (Private) or "Not shared yet. Share to let hub users watch." / "… invited users watch."; shared: "Everyone on hub.example.com can watch. Viewers send no input." (Hub users) or "Only invited users can watch. They send no input and cannot invite others." (Invite only); then "Share Session" (40 high, accent, 14/600) when not shared, or, when shared, the invite list (Invite only: rows padding 8 10 with avatar 26, name, status line 11 "watching · relayed" / "watching · direct" / "invited · offline" and an underlined action "Remove" / "Withdraw"; field "+ Invite a hub user…" 34 high) and "Stop sharing" (36 high, outline, 500).
  - SAVE (eyebrow; right link "Manage saves →" 12 `#c9c8c4`): slot as text "Main" 14/600 + version mono 12 `#a3a3a8` "v14" + right "✓ synced to hub" (12, ok); button "Save snapshot" with diamond icon and key badge "F5" (36 high, outline); line "Last checkpoint 40 s ago · final sync on pause or quit" (12/1.5 `#8e8e94`).
  - Game (pinned bottom, top border 1px `#1e1f22`, padding-top 16, gap 8): "Quit game" (40 high, surface `#1f2024`, 14/500) + note "Saves and syncs first" (12 `#7d7d83`, centered). Quitting state: box (surface `#1a1b1e`, border `#26272b`, radius 8, padding 12): spinner + "Saving and syncing…" (600), "✓ Saved to “Main” as v15", "○ Uploading to hub.example.com", "Returns to the Library when done."
- Multiview adds a tile header (padding 12 16 12 24, bottom border 1px `#1e1f22`): key badge 20 x 20 (`#ecebe7` / `#121315`), label mono 11 .08em `#c9c8c4` "TILE 1 · YOUR GAME" or "TILE 2 · WATCHING", and the hide button "⇥" (28 x 28, radius 6, border `#2c2d32`, tooltip "Hide panel").
- Remote tile selected: avatar 36 + who 15/600 + game 13 `#a3a3a8`; pill "● Direct · 18 ms" + "Hub users · for 24 min" (12 `#8e8e94`); audio button 36 high: "♪ Audio here" (outline) or, when this tile has the sound, "♪ Audio plays from this tile" (`#15262b`, border `#1f4650`, text `#3cbfd8`); "Remove from multiview" (36, outline); text "You only watch and listen. {who} sees you as a viewer."; bottom row (top border): badge "1", "Your game runs in tile 1", underlined "Select".
- Collapsed rail: 48 wide; "⇤" button (32 x 32, tooltip "Show panel"), tile badge, share dot (8px, tooltip with the share text), diamond (9px, tooltip "Save snapshot · F5").

### DiagOverlay
`source/diag-overlay.dc.html`; props: multi (one block per tile). Used by 3t-2 and 3x-2; fullscreen (3y) unchanged.

- Frame: 340 wide, padding 14, radius 10, surface `rgba(17,18,20,.95)`, border 1px `#2c2d32`, shadow `0 18px 40px rgba(0,0,0,.45)`, gap 12; absolute, top 16, right 16 of the play area. Sections separated by a 1px line `#1e1f22` (padding-top 12).
- Section header (click toggles): caret (10, `▾` open / `▸` collapsed, `#8e8e94`), title mono 11/500 `#7d7d83` .08em "EMULATION" / "STREAMING", right-aligned summary mono 11 `#8e8e94`.
- Emulation (single): grid `76 | 1fr`, gap 7 10, value mono 12, label Sans 11 `#7d7d83`: Core "melonDS DS 1.4.0"; Renderer "OpenGL 4.6 Core" + subline (11 `#8e8e94`) "Example GPU · driver 1.2.3"; Resolution "3× · 768×1152" + "2 screens of 768×576"; FPS "59.8 / 59.83 fps"; Frame "9.4 ms · emu 6.1 · readback 2.8"; frame-time sparkline 236 x 34 (total line `#3cbfd8` 1.25, emu line `#6f6f75` 1, dashed 16.7 ms line `#3a3b40`; legend 10 `#6f6f75`: "last 5 s", "— total", "— emu", "┄ 16.7 ms"); Audio "buffer 64 ms · 0 underruns".
- Emulation (multi): one block for your tile only (badge, "You · Lumen Drift" 13/500, "59.8 fps · frame 9.4 ms · emu 6.1" mono 11 `#c9c8c4`, "OpenGL 4.6 · 3× · audio 64 ms · 0 underruns" mono 11 `#8e8e94`) and the note "Only your tile is emulated on this device." (11 `#6f6f75`).
- Streaming: summary collapsed "not shared" (single) / "2 tiles · 1 relayed" (multi). Single without a session: "No active session · appears when you share or watch one" (12 `#6f6f75`). Multi: one block per remote tile (grid `20 | 1fr`: key badge `#26272b`; name 13/500 + connection pill small (Direct `#17291f`/`#6fd39a`, Relayed `#15262b`/`#3cbfd8`; 11/600, padding 1 8); mono 11 `#c9c8c4` "59.9 fps · 5.8 Mbit/s · RTT 14 ms"; mono 11 `#8e8e94` "loss 0.1 % · jitter 3 ms · H.264", relayed "… · via TURN").
- Footer (top border `#1e1f22`, padding-top 10, right-aligned): "Hide diagnostics" (12 `#a3a3a8`) + key badge "F3".

### Now running strip
Sheet at line 272 (`Sheet tiles and Now running`); in 3c-5. In the Player sidebar above the Hub switcher card, on every page; disappears when the game quits.

- Card: 200 wide (sidebar padding 0 8), padding 12, radius 9, surface `#15262b`, border 1px `#1f4650`, gap 6. Header: mono 10 "NOW RUNNING" `#3cbfd8` .08em, right the running time (11 `#8e8e94`, "12 min"); name 14/600; status 12 `#a9cdd6`; buttons (margin-top 4, gap 6, 30 high, radius 6): "Resume" (flex 1, accent, `#161512` 13/600, play icon) and "Quit" (padding 0 10, border 1px `#2b5560`, 13).
- States: Paused ("Paused · saved 40 s ago"); Paused while watching ("Paused while you watch Lena · Harbor Rally"); Quitting ("Saving and syncing…", no buttons).
