# Design: FrameBeam Player (QML, dark)

Source: `source/framebeam-designs-v3.dc.html`, line ranges per screen = screen container (without caption). Mock data is in the script from line 1362 (`renderVals` at line 1626, diagnostics in `dgVals` at line 1573); all names, versions, addresses and counts in this file are illustrative mock values. Tokens: `tokens.md`; logo: `logo.md`. All dimensions in px at 1440 x 900. Screen order in the source: 3a-3f, 3p, 3g, 3h, 3r, 3i, 3t-3y.

## Common shell (3c, 3e, 3f, 3p)

- Grid: sidebar 232px | content (1-3 further columns per screen). Sidebar: background `#0e0f10`, border right, padding 24 16, gap 28.
- Sidebar top: Player mark (`color` tone, 22px, see `logo.md`) + "FrameBeam Player" 16/600.
- Navigation (order): Library, Emulation, Controllers, Settings. Active: surface `#1f2024`, radius 6, 500; inactive `#a3a3a8`. Item padding 9 10, 14px. Settings shows a small accent dot (7px, `#3cbfd8`) while a Player update is available (3p).
- Second column (pattern, 300px, surface `#16171a`, border right, padding 28 16): used by 3e (Defaults and systems), 3f (devices) and 3p (sections). Eyebrow title (mono 11) at the top; items radius 8, padding 11 12, selected surface `#1f2024` with ring `0 0 0 1px #2f3035`.
- Bottom: Hub switcher card (status dot green, Hub name, on the right "switch", below it mono 11 "hub.local · active hub"), then the user block (avatar 28 "M", "Max", device name "Desktop-LivingRoom"), shown in 3c, 3e, 3f and 3p.
- In game (3g, 3h, 3r, 3i, 3t-3y) the sidebar is dropped; instead a 56px header (see there). The status pill component (`● Ready`, `▲`, `✕`) is specified in `tokens.md`.

## 3a Player Connection
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
- "Renderer" and "Internal resolution" are available since 0.5 (OpenGL hardware rendering, ADR 0013 D5; they apply on next start); "Default Multiview" has no "Grid 2×2" option although 3r adds that mode; "DS screen layout" is the global default and the in-game layout switch (3g) applies only to the running game (decided by Fabio, 2026-10-07); where an inherited value comes from is no longer shown (README); views for "Firmware missing" (architecture: block launch) not drawn.

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

- Grid: rows 56 | 1fr; columns 1fr | 340; background `#0b0b0c`; no sidebar. Fullscreen: header and side panel are dropped, the play area fills the screen. The diagnostics overlay then follows 3y (Emulation only); the interactive 3g mock keeps Streaming visible in fullscreen, which is a mock gap, not the rule.
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
- Streaming per participant: line 1 name 13/500, role 11 (`#7d7d83`, "host" / "viewer"), connection pill right (small variant: Local neutral, Direct ok, Relayed (TURN) warn); line 2 mono 11 `#a3a3a8` encoder or decoder, codec, bitrate, fps (e.g. "Encoder NVENC · H.264 · 6.0 Mbit/s · 60.0 fps", "Decoder D3D11VA · H.264 · 5.8 Mbit/s · 59.9 fps"); line 3 mono 11 `#8e8e94` RTT, loss and, when relayed, "via hub.example.com:3478", or "Sending to 1 viewer · adaptive bitrate". Without a session: text 12 `#6f6f75` "No active session · appears when you share or watch one".
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
Lines 784-810. New. Fullscreen, Emulation diagnostics shown, F3 toggles.

- No header and no panels; the common overlay floats top left (350) over the game; Streaming is omitted. Hint at the bottom center: "Toolbar appears when you move the mouse to the top · F3 diagnostics".
- Closed: a small chip top left (surface `rgba(17,18,20,.8)`, radius 7, padding 6 10, 12 `#8e8e94`) with the badge "F3" and "Diagnostics" opens it.
- Phase of 3t-3y: 0.6 Player UI pass; the values need new measurements. Open: README m-t.
