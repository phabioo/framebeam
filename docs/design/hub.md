# Design: FrameBeam Hub web interface (Go html/template + htmx, light)

Source: `source/framebeam-designs-v3.dc.html`, line ranges per screen = screen container (without caption). Mock data in the script from line 1227 (`renderVals` at line 1433); all names, versions, addresses, counts and hashes are illustrative mock values (hosts are generic: `hub.example.com`, `hub.local`). Tokens: `tokens.md`; logo: `logo.md`. Dimensions in px at 1440 x 900. The Hub has no Sessions page (architecture 09). Screen order in the source: 3j, 3k, 3s, 3l, 3m, 3n, 3o, 3q.

## Common shell (3j-3q)

- Grid: sidebar 232 | content (3k, 3s: list 320 | detail; 3q: section column 280 | content; 3l: system list 300 inside the content area). Sidebar: surface `#efeee9`, right border `#e3e1dc`, padding 24 16, gap 28.
- Logo: Hub mark (tone `color-light`, 22px, see `logo.md`) + "FrameBeam Hub" 16/600.
- Navigation (order, item padding 9 10, 14px, label left, badge right 12/500, text only): Library; Saves (badge "1 conflict", warn cyan `#1a7f96`); Systems & Cores (badge "2 issues", error `#b5402f`); Clients (badge "1 request", warn); Users; Settings (badge "1 update", warn; drawn in 3q only). Active: surface `#fff`, ring 1px `#e3e1dc`, 500; inactive `#55544f`. Badges are counters of open, actionable states ("firmware" became "issues": firmware files and outdated clients). Phase: the badges follow the respective features; "1 update" comes with the update UI.
- Sidebar footer: "admin · Admin" (13/500), mono 12 "hub.local · v0.1.0", "Raspberry Pi 5 · arm64" (3q: "Home · v0.3.1-beta.215").
- Page header: title 26/600 + subtitle 14 `#6b6a66`. Tables: card `#fff`, radius 10, header row surface `#faf9f7` with mono-11 eyebrows, row dividers `#efeee9`.
- Status pills (radius 999, 12/600): ok green, neutral gray, warn cyan, error red (see `tokens.md`).
- Second-column pattern (3k, 3s list; 3q sections): surface `#faf9f7`, right border `#e3e1dc`, padding 28 20 (3q) or 32 20 (lists); selected item surface `#eef8fa` with a left bar `inset 3px 0 0 #3cbfd8` (3q: unselected items `#fff`).

## 3j Hub Library
Lines 816-864. Phase 1 (library, ROM upload/download).

- Layout: sidebar | main (padding 32 40, gap 24); Library active.
- Header: title "Library", subtitle "42 ROMs · 3.1 GB of 118 GB used"; on the right buttons "Rescan folder" (outline) and "Upload ROM" (primary dark), each 38 high.
- Filter row: pills "All systems · 42" (active, dark), "Nintendo DS · 42"; on the right search field 260 "Search title or hash…".
- Table, columns `2.2fr | 70 | 90 | 150 | 150 | 130`: TITLE (cover placeholder 34 + title 500), SYSTEM (mono "nds"), SIZE (mono), SHA-256 (mono, shortened "1b7e…04c9"), UPLOADED BY ("{user} · {DD.MM}"), SAVES (mono number + "▲ Conflict" in warn on conflict).
- Mock (9 rows, script from line 1433): among others "Clocktower Kids" 64.0 MB 3 saves; "Harbor Rally" 32.0 MB 6 saves with conflict marker; "Velvet Arcade" uploaded by "lena · 03.10", otherwise "admin".
- Open: row actions (delete, metadata; architecture 09 names metadata actions in the library entry, later), upload dialog, scan progress, error states (hash/duplicate), paging not drawn.

## 3k Hub Saves
Lines 870-954. Phase 3; slots and snapshot markers: 0.4 features, UI in the 0.7 Hub UI pass. Conflict next to the version history, per slot.

- Grid: sidebar | list 320 (surface `#faf9f7`, right border, padding 32 20) | detail (padding 32 40, gap 22).
- List: title "Saves" (22/600); filter "User: Max ▾"; games (cover 36 with initials, title 14/500, status line 12): selected surface `#eef8fa` + left bar; status "1 conflict" (warn) or "Synced · {time}" (ok green `#2a7a4c`; with slots: "Synced · today 18:42 · 2 slots"). Mock: Harbor Rally (conflict, selected), Lumen Drift (today 18:42 · 2 slots), Tide & Lantern (28.09.), Clocktower Kids (02.10.), Stylus Knights (21.09.).
- Detail header: game title "Harbor Rally" 26/600, "Max · Nintendo DS", on the right pill "▲ Conflict · unresolved" (warn, 13/600, padding 6 12).
- Slot tabs (under the header): "Main" with "1 conflict" (active, underline `#1b1b1d`), "Time trial" with "3 versions", on the right "+ New slot". The conflict belongs to the slot "Main". Slot names are mock.
- Bar "CURRENT CHECKPOINT": "Rev 41 · Laptop-Office · today 19:10 · Auto checkpoint", on the right "Download".
- Conflict card (border `#a6d9e6`, padding 22): title "▲ Conflict: upload is based on Rev 40, current is Rev 41" (16/600, `#0f5a6b`), text "Both contents are kept and backed up to the history before resolution."; two comparison cards (label column 110: device, time, base revision, content hash mono):
  - "Current Checkpoint on hub": "Rev 41", Laptop-Office, "today, 19:10", base "Rev 40", hash "a3f1…08cc".
  - "Backed-up upload · sync pending": "Local save", Desktop-LivingRoom, "today, 19:24", base "Rev 40", hash "6e02…d911".
  - Actions (38 high): "Use hub version", "Use local save as new current version" (outline), "Keep both, decide later" (primary dark).
- "HISTORY · PERMANENT VERSIONS": timeline (column 24 with vertical line and node | text | actions). Node current = filled `#3cbfd8` with ring, old = hollow circle. Row: mono "v{n}", device 500, badge "◆ Snapshot" (`#ddf1f6`/`#0f5a6b`, manual snapshots), badge "✓ Current" (ok, current only), meta "{time} · {reason}"; actions "Download" (always), "Restore" (old only). Mock (all old): v6 Desktop-LivingRoom yesterday 22:30 "Session end"; v5 Laptop-Office yesterday 18:05 "Device switch"; v4 Desktop-LivingRoom 03.10. 21:12 "Manual snapshot" (badge). The full slot history with diamond markers, filter, thinned-out marker and restore confirmation is 3s.
- Open: empty state without conflict (3s shows the synced state), revision numbering "Rev 41" (checkpoint) vs "v6" (history) is separate by ADR 0005.

## 3s Hub Saves Slots
Lines 960-1018. New. Slots per game, snapshots in the history, restore with confirmation, retention. Phase: slots, snapshots, restore, retention are 0.4 features, UI in the 0.7 Hub UI pass.

- Grid and list as 3k (Saves active); selected game Lumen Drift ("Synced · today 18:42 · 2 slots").
- Header: "Lumen Drift" 26/600, "Max · Nintendo DS", pill "✓ Synced" (ok).
- Slot tabs: one per slot with the version count ("Main · 5 versions", "100% run · 3 versions"; counts follow the mock), on the right "+ New slot". Selecting a slot switches the bar and the history.
- Bar "CURRENT": "v14 · Desktop-LivingRoom · today 18:42 · Auto checkpoint", right "Download". After a restore the new current version is e.g. "v15 · Hub web interface · just now · Restored from v13".
- "HISTORY" with filter segment "All" | "Snapshots" (track `#efeee9`, 12px; "Snapshots" shows manual snapshots and the current version, hides the thinned-out marker). Timeline as 3k, with node types: current (filled `#3cbfd8` with ring), snapshot (9px diamond `#1a7f96`), other (hollow circle `#b9b7b0`). Rows: version, device, badges "✓ Current" / "◆ Snapshot", meta "{time} · {reason}" (snapshots: "{time} · “{name}” · Manual snapshot"), actions "Download", "Restore" (old versions).
  - Mock "Main": v14 Desktop-LivingRoom today 18:42 "Auto checkpoint" (current); v13 Laptop-Office today 17:05 snapshot "Before the lighthouse"; v12 Desktop-LivingRoom yesterday 22:30 "Session end"; thinned-out marker; v6 Laptop-Office 05.10. 20:14 "Session end"; v5 Desktop-LivingRoom 03.10. 21:12 snapshot "100 % map". Mock "100% run": v3 Auto checkpoint, v2 snapshot "Clean start", v1 Session end.
  - Thinned-out marker (dashed vertical line, row text 12 `#6b6a66`): "⋯ 5 auto checkpoints from 03.–05.10. thinned out · last one per day kept".
  - Restore confirmation (inline card under the row, border `#a6d9e6`, radius 10, padding 14 16): title "Restore v13 as the current version of “Main”?" (14/600, `#0f5a6b`), text "v14 stays in the history. Players load the restored save on their next start. Not possible while a session of this game is running."; buttons "Cancel" (outline) and "Restore v13" (primary dark). The mock shows the confirmation open on v13. Restore creates a new version; nothing is overwritten.
- Retention box (bottom, surface `#efeee9`, radius 10): eyebrow "RETENTION" and text "Snapshots and the current version are kept until you delete them. Auto checkpoints, session ends and device switches: all from the last 48 hours, then the last one per day for 30 days, then one per month. Thinned-out versions are deleted for good." The policy text is design text; the actual retention rules are open (README).
- Open: deleting a snapshot, creating/renaming/deleting a slot, restore error states (session running), the Player-side counterpart (3g "Save snapshot", slot selector).

## 3l Hub Systems and Cores
Lines 1024-1132. Rebuilt: system list with detail and tabs. Phase 5; rebuilt in the 0.7 Hub UI pass. Registry, compatibility, firmware.

- Layout: sidebar | main (padding 28 40, gap 24). Title "Systems & Cores", subtitle "Which core each system uses and which files it needs. The hub does not run cores." Content grid `300 | 1fr`, gap 20.
- System list (left): search field 36 "Search systems…"; card list (surface `#fff`, radius 10), item 68 high: name 14 + id (mono, e.g. "nds"), line "{core} {version}" and a status chip on the right: "● Ready" (ok), "{n} firmware" (error), "{n} client" (warn). Selected: surface `#eef8fa` + left bar. Footer note: "New systems appear here when a core and manifest are added to the registry." Example systems "Game Boy Advance" and "Super Nintendo" appear in the mock only (README).
- Detail (right): header: name 20/600 + id mono; line "{core} {version} · {provisioning}" (mock "melonDS DS 1.2.0 · Bundled with Windows Player", outdated, see README); summary chip on the right: "✕ Not ready · 2 files missing or invalid" (error), "▲ 1 client outdated" (warn, launch still allowed per ADR 0007) or "● Ready" (ok). Tabs: "Firmware" (counter badge = invalid/missing files), "Clients" (counter = mismatched clients), "Core & manifest".
  - Firmware tab: text "Provided by the admin, not shipped with FrameBeam. Checked against the SHA-256 in the manifest." Table rows: display name 500, file name (mono), requirement ("required" / "optional"), optional note; status: "✓ Valid" (ok), "✕ Hash mismatch" (error), "○ Missing" (error); action: "Replace file" (mismatch), "Upload file" (missing), "Replace" (valid). Mock NDS: "ARM9 BIOS" mismatch (note "Uploaded file does not match the expected hash (2ab2…a2b2)."), "DS Firmware" missing (note "Games for this system cannot start until this file is provided."), "ARM7 BIOS" valid. Empty case: "This core needs no BIOS or firmware files." File names in the source are not carried over (rule: no real BIOS names).
  - Clients tab: text "Reported by Players when they connect." The mock sentence "Mismatched clients cannot start games for this system." contradicts ADR 0007 D3: a missing core or a core version mismatch is a warning and launching stays allowed; only an incompatible protocol (Player too old) blocks. The implementation shows core mismatches as warnings (warn colour) and only protocol mismatches as blocking (error colour). Rows: device, "Player {version} · {core} {version}" (mono), platform (mono), "● Compatible" (ok) or error text (mock: "Core version mismatch · 1.2.0 expected", "Player too old · protocol v1 required").
  - Core & manifest tab: two columns. CORE: "Preferred core" ("melonDS DS" + mono id "melonds_ds"), "Expected version", "Platforms" ("windows-x86_64"), "Provisioning". MANIFEST: "File extensions" (".nds"), "Input profile" ("nds"), "Display profile" ("dual_screen"), "BIOS / Firmware" ("3 files · see Firmware tab" or "none").
- The earlier "Core package cache" placeholder (badge "LATER") is no longer drawn.
- Open: file upload dialog, removing a firmware file, the nav badge count ("2 issues" in the mock equals the two firmware rows; clients are not included in the mock count), further systems, how the 0.7 pass shows core packages (ADR 0010).

## 3m Hub Clients
Lines 1138-1174. Phase 1 (pairing Allow/Decline, Revoke).

- Layout: sidebar | main (padding 32 40, gap 26). Title "Clients", subtitle "Devices allowed to access this hub".
- "PENDING REQUESTS · 1" (eyebrow): card (border `#a6d9e6`, padding 20 22; columns `1fr | 220 | auto`): "Device name" / "Lena's Gaming PC" (20/600) with pill "▲ Awaiting approval" (warn), mono "Windows x86-64 · Player 0.1.0 · Protocol v1", "requested 2 min ago"; select "Assign user" (value "Lena"); buttons "Decline" (outline) and "Allow" (primary dark).
- "DEVICES": table columns `1.3fr | 110 | 140 | 90 | 140 | 100 | 150`: DEVICE, USER, PLATFORM (mono), PLAYER (mono), LAST ACTIVE, STATUS (pill), action.
  - Trusted (green) with action "Revoke access" (red, text link): Desktop-LivingRoom (Max, 0.1.0, "just now"), Laptop-Office (Max, 0.1.0, "today 19:10"), Jonas's notebook (Jonas, 0.0.9 marked "▲", "yesterday"), Desktop admin (admin, 0.1.0, "02.10.").
  - Revoked (gray, row muted, action "—"): Old laptop (Max, 0.0.9, "12.09.").
- Footnote: "Revoking removes the device's access immediately, even with still-valid access tokens. ▲ marks a Player version the hub no longer accepts."
- Added in 0.7: a "Delete" text link (red) next to "Revoke access" on every row (also revoked ones). It opens a no-JS confirmation page (card, names what is removed, Cancel + red "Delete device"); the device disappears for good, saves stay and show "Deleted device" in the history.
- Open: confirmation dialog for revoking, denied state, renaming, reinstating a revoked device, certificate/compatibility errors per client (only in 3l) not drawn.

## 3n Hub Users
Lines 1180-1217. Phase 5. Hub-local accounts and onboarding invites.

- Grid: sidebar | main | side panel 420 (surface `#faf9f7`, left border, padding 32 28, gap 18).
- Main: title "Users", subtitle "Accounts apply only to this hub. Regular users have no password." Table columns `1.2fr | 80 | 70 | 110 | 110 | 110`: NAME (avatar initial 28 + name), ROLE, DEVICES (mono count), CREATED, STATUS (pill Active/Disabled), action.
  - Mock: admin (Admin, 1, 14.08.2026, Active, "—"); Max (User, 2, 14.08.2026, Active, "Disable" red); Lena (User, 1, 20.08.2026); Jonas (User, 1, 02.09.2026); Sam (User, 1, 11.09.2026, Disabled, row muted, action "Enable").
  - Footnote: "Disabled users cannot sign in on any device. Saves and uploads are preserved. Deleting a user removes their devices, invites and saves for good."
  - Added in 0.7: non-admin rows get a red "Delete" text link next to Disable/Enable. It opens a no-JS confirmation page (card listing what is removed: account, devices and tokens, saves and save history deleted for good, invites; uploaded games stay and move to the deleting admin) with Cancel and a red "Delete user" button. Admins cannot be deleted.
- Side panel "Onboarding invites" (18/600): text "The invited person redeems the code in the Player and picks a display name. This is not a session invite."; button "Create invite" (dark, 36).
  - Active invite (card): "Active", "expires in 42 min" (warn); code mono 24 "FB-7KQ2-M9XD"; toggle (on, black) "Approve first device directly"; "Single use"; buttons "Copy link" (outline), "Revoke" (red).
  - History (muted): "FB-2HC9-…" "redeemed by Jonas · 02.09."; "FB-Q81M-…" "expired · 30.09.".
- Open: "Create user" dialog (no button drawn), role change, deletion, selectable expiry duration (unclear); example codes are mock. Implemented without 'Copy link': the code is shown once at creation and only its hash is stored (ADR 0007).


## 3o Hub Settings (superseded by 3q)
Lines 1223-1251. Superseded by 3q (decision 2026-10-07); kept in the source as reference only, not to be implemented. Phase 1 (Hub name, address, certificate/TLS, admin account); phase 5 (appearance, upload option).

- Layout: sidebar | main (padding 32 40, gap 24). Title "Settings", subtitle "Admins only". Two columns (gap 20), cards (padding 18 22 6, radius 10, title 16/600); rows: label block left (14/500 + help text 12), control right (field 200 x 34 or segment/toggle), divider above.
- Left column: "General" (Hub name "Home", "Address for Players" "hub.local:8443"); "Appearance" (Mode: segment "Light" | "Dark" | "System"); "Library" ("Allow users to upload games", badge "Inactive", toggle off).
- Right column: "Transport & Certificate" (segment "Self-signed" | "Custom cert/key" | "Reverse Proxy"; "Fingerprint" SHA-256, mono); "Admin account" ("admin", button "Change password"); "Metadata" (dashed, badge "LATER").
- What 3q does differently: sub-pages instead of two columns, autosave, update section, network section. Not carried into 3q: the segment "Custom cert/key" / "Reverse Proxy" (open, README), the "Metadata" placeholder.

## 3q Hub Settings revised
Lines 1257-1356. New; supersedes 3o. Sections Updates, General, Network, Security as sub-pages, autosave. Phase: 0.7 Hub UI pass; the Network section (public address, built-in relay) is a 0.4 feature, UI in the 0.7 pass. Updates exist since 0.3.

- Grid: sidebar (Settings active, badge "1 update") 232 | section column 280 | content. Section column (surface `#faf9f7`, border right, padding 28 20): title "Settings" (13/500); items with name and a status line with a colored dot: "Updates" "Update available · 0.4.0" (warn), "General" "Name, address, theme, uploads" (muted), "Network" "Only in local network" (warn) or "Reachable from the internet" (ok), "Security" "HTTPS active · admin" (ok). Selected: surface `#eef8fa` + left bar `#3cbfd8`, others `#fff`. Footer text: "Changes save automatically. Admins only." In the mock, "Network" is selected by default.
- Content (padding 28 40, gap 24): header with section title (26/600) and subtitle (14), on the right the saved indicator "✓ All changes saved" (13, `#2a7a4c`). Autosave: every field saves on its own; no Save buttons (except "Save password" and "Save"-like actions drawn explicitly). Section cards: surface `#fff`, radius 10, border `#e3e1dc`, mono eyebrow title, rows with label block left (14/500 + 12 help) and control right.
- Updates (subtitle "Versions, channel and automatic installs for this hub"):
  - Update card: "▲ Update available" (warn), "0.4.0-beta.229" (mono), "beta"; text "Installed: 0.3.1-beta.215 · no session running · all recent Players compatible"; buttons "Release notes" (outline), "Install now" (primary dark).
  - Error card (surface `#fbf1ef`, border `#f0cfc8`): pill "✕ Last install failed", "today 16:33 · download returned HTTP 404" (mock), file name mono (mock "framebeam-hub_0.4.0-beta.229_arm64.deb"), button "Retry".
  - SETTINGS: "Update channel" (segment "Stable" | "Beta"; text "Released versions only. Recommended." / "Also gets pre-release builds from every change on main. Newest wins. May contain bugs."); "Install automatically" toggle (black) with "After each check. Only installs when all of these are true:" and three check lines "No session is running", "Players seen recently stay compatible", "Installed from the .deb package"; "Last checked" "today 16:33 · checks every 6 hours" with "Check now" (cadence illustrative: the Hub checks hourly on Beta and every 24 hours on Stable, `server/internal/hub/updates.go`; the text must follow the channel or omit the cadence) (shows "Checking…" with a spinner); "Installed version" "Last successful install: 07.10.2026 15:33", value "0.3.1-beta.215".
- General (subtitle "How the hub appears to Players and users"): IDENTITY: "Hub name" ("Shown to Players when they connect", value "Home"), "Address for Players" ("How Players reach this hub · set under Network", mono "hub.example.com:8443" + "Copy"). APPEARANCE: "Theme" ("Applies to this web interface on this browser", "Light" | "Dark" | "System"). LIBRARY: "Allow users to upload games" with chip "· Inactive" / "· Active", text "Currently only admins can upload ROMs." / "Users can upload ROMs. Uploads are tagged with uploaded_by; only admins can edit or delete other users’ entries.", toggle.
- Network (subtitle "Ports, public address and relay for Players outside your network"):
  - Reachability card: chip "▲ Only in local network" (warn: `#ddf1f6`, text `#1a7f96`, border `#a6d9e6`) or "✓ Reachable from the internet" (ok), "Reachability check · today 16:40", text "hub.example.com:8443 did not answer from outside. Players on your network connect normally; remote Players cannot reach the hub until TCP 8443 is forwarded to it." / "hub.example.com:8443 answered from outside, and the relay on UDP 3478 is reachable. Remote Players can connect."; button "Check again" ("Checking…" state).
  - LISTENING & PUBLIC ADDRESS: "Listen port" (help "TCP port for Players and this web interface · changing it restarts the hub", value 8443); "Public address for Players" (help "Handed to Players outside your network. Forward this TCP port on your router to the hub.", fields host "hub.example.com" ":" port "8443").
  - RELAY · TURN: "Built-in TURN relay" toggle (mock on): text on "Relays the stream for Players that cannot connect directly (strict NAT, mobile networks). Uses this hub’s upload." / off "Players that cannot connect directly will fail to join unless an external TURN server is set."; when on, row "Relay ports · UDP" (help "Forward the port and the whole range as UDP to the hub") with "port" 3478 and "range" 49160 – 49200 (decided by Fabio on 2026-10-07: the range follows the code, 49160–49199; the mock value is a deviation, see README).
  - EXTERNAL STUN / TURN SERVERS · OPTIONAL with "+ Add server": rows URL (mono) + type ("STUN", "TURN · with login") + "Remove" (mock "stun:stun.example.com:3478", "turn:turn.example.com:3478"); note "Offered to Players in addition to the built-in relay. Leave empty if the hub is reachable from the internet."
- Security (subtitle "Transport, certificate and the admin account"): TRANSPORT & CERTIFICATE: "Transport" ("HTTPS / WSS. Plain HTTP is only available in dev mode.", pill "✓ HTTPS"); "Certificate" ("Self-generated · valid until 07.10.2036 · renews automatically 30 days before expiry", button "Renew now"); "Fingerprint · SHA-256" (mono, two lines, placeholder value "AA:BB:CC:…"; "Players compare this on first connect. After a renewal they must confirm the new value.", button "Copy"). ADMIN ACCOUNT: "admin" ("Web login with username and password · signed in since today 14:02"), buttons "Change password" (opens an inline form) and "Sign out"; form fields "Current password", "New password · min. 8 characters", "Repeat new password", button "Save password".
- Open: "Custom cert/key" and "Reverse Proxy" transport options (in 3o, not in 3q); how autosave reports errors (e.g. invalid port, port in use); whether the relay port range is configurable; texts about certificate renewal are not checked against the implementation (README).
