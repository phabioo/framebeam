# Design: FrameBeam Player (QML, dark)

Source: `source/framebeam-designs-v3.dc.html`, line ranges per screen = screen container (without caption). Mock data is in the script from line 825 (`renderVals`). Tokens: `tokens.md`. All dimensions in px at 1440 x 900.

## Common shell (3c, 3e, 3f)

- Grid: sidebar 232px | content (1-3 further columns per screen). Sidebar: background `#0e0f10`, border right, padding 24 16, gap 28.
- Sidebar top: logo (22px square, radius 5, accent) + "FrameBeam Player" 16/600.
- Navigation (order): Library, Emulation, Controllers, Settings. Active: surface `#1f2024`, radius 6, 500; inactive `#a3a3a8`. Item padding 9 10, 14px.
- Bottom: Hub switcher card (status dot green, Hub name, on the right "switch", below it mono 11 "hub.local · active Hub"). Only 3c additionally shows the user block (avatar 28 "M", "Max", device name "Desktop Living Room").
- In game (3g-3i) the sidebar is dropped; instead a 56px header (see there).

## 3a Player Connection
Lines 30-52. Start/connection screen before the main navigation.

- Purpose: stored Hubs, connection status, add Hub, connect.
- Layout: screen centered (`place-items:center`), column 620px, gap 28; background `#121315`, no sidebar.
- Header: logo + "FrameBeam Player", title "Connect to a Hub" (30/600).
- Hub cards (column, gap 10, padding 16 18, radius 10, surface `#1a1b1e`); per card name 16/600 + status mark, mono 12 line with address and details:
  - State reachable (selected, accent ring 1.5): "Home", "● reachable" (green), "hub.local:8443 · as Max · last today 18:40", button "Connect" (40 high, accent).
  - State certificate changed: "Lena's Studio", "✕ Certificate changed" (error), "lena-hub.fritz.box:8443 · as Max", link "Check fingerprint"; error box (surface `#2a1a17`): "The fingerprint no longer matches the stored one. The connection is blocked until you have checked and confirmed the new fingerprint in the Hub web interface." No Connect button.
  - State Hub too old: "Office", "▲ Hub too old" (warn), "hub.office.lan · Hub speaks protocol v0, Player requires v1", action "Remove".
- Add Hub: input field (44 high, placeholder "Hub address, e.g. hub.local:8443", mono) + button "Add Hub" (outline).
- Footer (divider): toggle (on) "Connect automatically to the last used Hub on startup"; mono 12 "This device: Desktop Living Room · Player 0.1.0 · Windows x86-64".
- States: reachable, certificate changed (blocked), Hub incompatible (protocol). Offline/unreachable: not shown (unclear).
- Remove is shown only for the incompatible Hub; whether it is available on all cards is unclear.

## 3b Player Pairing
Lines 58-72. Add Hub: certificate (TOFU) and approval by admin.

- Layout: centered, column 640px, gap 14. Title "Add Hub" (30/600) + mono 13 address "hub.local:8443".
- Three step cards (grid 18px | text, gap 4 12; status dot 10px; radius 10, padding 16-18):
  1. "Hub detected": "Home · FrameBeam Hub 0.1.0 · protocol v1 · compatible" (dot filled, accent).
  2. "Certificate trusted": SHA-256 fingerprint in mono 12 (two lines, hex pairs), note "Stored. If the certificate changes later, the connection is blocked." (dot filled).
  3. "Authorize device" (active, accent ring; dot as ring):
     - Segment switch: "Request approval" (active) | "Redeem invite".
     - Status: spinner + "Waiting for approval by the admin" / "The request appears in the Hub under Clients."
     - Data block (label column 130px): device name "Desktop Living Room", platform "Windows x86-64", Player version "0.1.0".
     - Button "Cancel request" (38 high, outline).
- Not shown: "Redeem invite" view (code entry, display name), states denied/revoked/error (unclear).

## 3c Player Library
Lines 78-175. Main screen; readiness states, save conflict, discovering Sessions.

- Grid: sidebar 232 | main (flexible) | detail column 392 (surface `#16171a`, border left, padding 28).
- Main (padding 28 32, gap 28):
  - Header: title "Library" (26/600), meta "42 games · Nintendo DS", on the right search field 220 x 34 ("Search…").
  - Filter chips (pills): "All" (active), "Ready", "Needs attention · 2".
  - Section "SESSIONS ON THIS HUB" (eyebrow green with dot); 2-column grid, gap 12; card: avatar 30, "{user} · {game}", meta 12. Examples: "Lena · Harbor Rally", meta "Hub users · for 24 min", action "Watch Session"; "Jonas · Clocktower Kids", meta "inviting you", actions "Decline" / "Join" (accent).
  - Game grid: 4 columns, gap 20 18. Tile: square, mono 11 "NDS" at the top, monogram (initials, 36/600) at the bottom; selection = accent border 2px (offset -3, radius 8). Below it title 14/500 (ellipsis) and status 12.
  - Status variants per game: "● Ready"; "↓ Download required · {size}"; "▲ Save conflict" (warn, 500); "✕ Hash mismatch · reload" (error, 500); "⟳ Save sync pending".
  - Mock: Lumen Drift (ready, selected), Harbor Rally (conflict), Paper Wizards (download 128 MB), Clocktower Kids (ready), Orbit Gardens (download 16 MB), Tide & Lantern (sync pending), Copper Courier (hash mismatch), Stylus Knights (ready).
- Detail column (selection):
  - Header: cover 112 + title "Lumen Drift" (22/600) + "Nintendo DS".
  - Table (label left muted, value right, rows padding 11): ROM "Cached locally · verified"; Core "melonDS DS · ready"; Firmware "from Hub · verified"; Save "Checkpoint current · 18:42"; "Last saved by" "Laptop Office".
  - Link "Show details (hashes, size, paths)" (12, muted).
  - Section "START" (eyebrow): checklist, row grid 18 | label | meta; dot filled = done, ring = active: "Game data from Hub" (current), "ROM verified from cache" (no download), "Firmware present" (verified), "Current checkpoint loaded" (Rev 88), "melonDS DS starting" (active).
  - Bottom: primary button "Play" (48, accent), secondary "Play and share Session" (44, outline).
- Open/unclear: states of the detail column for download, hash mismatch and conflict (only the success case is drawn); search/filter behavior; "Needs attention · 2" does not count unambiguously according to the mock data (conflict, hash mismatch, sync pending are three tiles).

## 3d Player Save Conflict
Lines 181-209. Modal dialog on start reconciliation.

- Layout: screen `#08090a` with dimmed library (stripe pattern, caption "Library (dimmed)" top left, mono 12); dialog centered, 760 wide, surface `#16171a`, border `#2c2d32`, radius 12, padding 32, gap 24.
- Header: eyebrow "▲ SAVE CONFLICT · HARBOR RALLY" (warn), title "The Hub and this device have different saves" (24/600), text "This device kept playing offline while another device secured a new checkpoint. Nothing is overwritten until you decide. Both saves are secured in the history beforehand."
- Two comparison cards (2 columns, surface `#1d1e22`, radius 9):
  - "On the Hub": "Current checkpoint · Laptop Office"; "today, 19:10 · Rev 41"; "Base: Rev 40".
  - "On this device": "Local · Desktop Living Room"; "today, 19:24 · sync pending"; "Base: Rev 40".
- Actions (column, 46 high, gap 8; on the right a note 12 each):
  - "Use Hub version" (outline; "local save stays secured").
  - "Adopt local save as new current version" (outline; "becomes the new checkpoint").
  - "Keep both, decide later" (primary, accent; highlighted as default).
- Footnote: "Decide later: the conflict stays visible on the game and on the Saves page in the Hub."
- Open: confirmation step before an overwriting action, error case while securing, keyboard operation not drawn.

## 3e Player Emulation
Lines 215-276. Cores, readiness, settings hierarchy.

- Grid: sidebar (Emulation active, without user block) 232 | system list 300 (surface `#16171a`) | main.
- System list: eyebrow "EMULATION"; card (selected, surface `#1f2024`): "Nintendo DS", "melonDS DS · 1.2.0", "● Ready · included in the Player" (green), "Firmware from Hub · verified"; dashed placeholder card "More systems" / "Will be added later via core, manifest and profiles."
- Main (padding 28 36, gap 22): title "Nintendo DS · melonDS DS", subtitle "Settings apply locally to this device".
- Level switch (segment): "Global" | "System / Core" (active) | "Game Override · later" (disabled); note next to it "Global → System/Core → Game Override · only explicitly set values override".
- Option groups (title 15/600 + subtitle 12, divider); row = grid `1fr | 220 | 200`, gap 20:
  - Column 1: label 14 (+ badge "Restart required", warn) and description 12.
  - Column 2: select field 34 high (value + "▾").
  - Column 3: origin: "inherited · {source}" (muted) or "● set here" (warn color) + link "reset".
- Group "FrameBeam" (subtitle "Presentation in the Player"): "Fullscreen on start" (Off, inherited Global), "DS screen layout" (Stacked, set here), "Default multiview" (Picture-in-picture, inherited Global).
- Group "melonDS DS" (subtitle "from Libretro core options · only options reported by the core"): "Renderer" (OpenGL, set here, restart required), "Internal resolution" (3× (768×576), set here), "Console type" (DS, inherited core default, restart required), "Audio interpolation" (None, core default), "Touch mode" (Mouse, core default).
- Open: views "Global" and "Game Override", state "Firmware missing" (architecture: block launch) not drawn.

## 3f Player Controllers
Lines 282-344. Profiles, remapping, input test.

- Grid: sidebar (Controllers active) 232 | device list 300 | main | input test 340 (surface `#16171a`).
- Device list: eyebrow "DEVICES"; entries (radius 8, padding 12; selected surface `#1f2024`): name 14/500, on the right slot (mono 11), below it profile 12. Mock: "Xbox Wireless Controller" P1 "Standard Gamepad" (selected); "Keyboard" slot "—" "Keyboard · Standard"; "Mouse" slot "Touch" "DS touch". Footer: "Profiles stay local on this device and are not synchronized."
- Main: title "Xbox Wireless Controller", on the right profile select "Profile: Standard Gamepad ▾" (34 high).
- Table grid `1fr | 180 | 120`: header (eyebrow mono 11) "FRAMEBEAM INPUT" | "MAPPING" | "NDS". Rows (padding 8): input | mapping field (30 high) | NDS target (mono). Mock: A→"Ⓑ  B"→A; B→"Ⓐ  A"→B; X→"Ⓨ  Y"→X; Y→"Ⓧ  X"→Y; L→"LB"→L (listening state: border accent, text "Press a button…"); R→"RB"→R; Start→"Menu"→START; Select→"View"→SELECT; D-pad→"D-Pad / left stick"→D-PAD; "Close lid"→"not mapped"→LID.
- Actions at the bottom: "Reset to default", "Duplicate profile" (outline, 38 high).
- Input test: eyebrow "INPUT TEST", text "Press buttons on the controller — active inputs light up."; grid 4 columns, tiles 44 high: A, B, X, Y, L, R, ▲, ▼, ◀, ▶, ST, SE; active tile = accent surface (mock: A and ▶). Section "DS TOUCH": surface 150 high with text "Mouse on lower screen", cursor ring; note "Left mouse button = stylus".
- Open: analog stick/trigger display, slot assignment and profile management (delete/rename) not drawn; SDL3 mapping details are not part of the design.

## 3g Player Session
Lines 350-391. In game, Session shared, visibility "Invite only" with user selection.

- Grid: rows 56 | 1fr; columns 1fr | 340; background `#0b0b0c`; no sidebar.
- Header (across both columns, surface `#111214`, padding 0 20): "← Library", divider, game title "Lumen Drift" (15/600), green pill "Session shared · 1 watching"; on the right tab segment "Session" (active) | "Multiview" | "Diagnostics".
- Play area: two DS screens stacked (each 480 x 360, placeholders "upper DS screen" / "lower screen · touch via mouse", lower one with dashed outline).
- Side panel 340 (surface `#111214`, padding 24, gap 20):
  - "VISIBILITY": segment "Private" | "Hub users" | "Invite only" (active).
  - "INVITED · 2" with note on the right "only you can change"; list: "Lena" (green dot, "watching", action "Remove"), "Jonas" (gray dot, "invited · offline", action "Withdraw").
  - Invite field (accent border, input "Sa", note on the right "users of this Hub") with result list: "Sam" ("online"), "Sarah" ("offline · receives the invite as long as the Session is running"), each with button "Invite" (accent).
  - Note: "Invitees only watch and listen. They send no input and cannot invite others."
  - Bottom: "Checkpoint saved 40 s ago · final sync on pause or exit"; buttons "Stop sharing" (outline), "End game and save" (surface `#1f2024`); collapsible "▸ Show diagnostics".
- Open: views for visibility "Private" and "Hub users", behavior when the Hub or connection fails, header pill without a Session unclear.

## 3h Player Side-by-Side
Lines 397-438. Multiview, side-by-side, diagnostics expanded.

- Grid: rows 56 | 1fr | auto; background `#0b0b0c`.
- Header: "← Library", divider, title "Multiview", mode segment "PiP" | "Side-by-Side" (active); on the right tab segment "Session" | "Multiview" (active) | "Diagnostics" (accent underline, since expanded).
- Middle: two equal-width columns (gap 2, background `#1e1f22` as divider); per column: header with avatar 28, "{who} · {game}", meta 12 and audio button ("Audio on" accent or "Audio here" outline); below it two screens (each 360 x 270, placeholders "{label} · top/bottom").
- Mock (script, lines 871-878): surface 1 "You · Lumen Drift", meta "local", audio on; surface 2 "Lena · Harbor Rally", meta "Session from Lena", button "Audio here".
- Diagnostics mock: "local" 60.0 fps · encoder NVENC · H.264 · 6.0 Mbit/s · Opus 128 kbit/s; "Lena" 59.9 fps · WebRTC direct · RTT 14 ms · 5.8 Mbit/s · loss 0.1 %.
- Diagnostics panel at the bottom (surface `#111214`, border top): title "▾ Diagnostics" + "technical details · optional"; two columns, per participant a mono-12 row (name + four values `a` to `d`).
- Exactly one surface has audio; switching via "Audio here".

## 3i Player PiP
Lines 444-476. Multiview, picture-in-picture.

- Layout like the 3h header, mode segment with "PiP" active; tab "Multiview" active, "Diagnostics" not highlighted.
- Main picture: local Session, two screens (480 x 360, placeholders "local · top/bottom") centered.
- PiP window bottom right (offset 28, width 248, surface `#16171a`, radius 10, padding 8): header row green dot, "Lena · Harbor Rally", on the right "muted"; two remote screens (each 174 high, "remote · top/bottom"); buttons "Swap" and "Remove" (50 % each).
- Open: moving/resizing the PiP window, multiple PiPs, audio control in the PiP (only status "muted") not drawn.
