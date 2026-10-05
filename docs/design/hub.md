# Design: FrameBeam Hub web interface (Go html/template + htmx, light)

Source: `source/framebeam-designs-v3.dc.html`, line ranges per screen = screen container (without caption). Mock data in the script from line 825. Tokens: `tokens.md`. Dimensions in px at 1440 x 900. The Hub has no Sessions page (architecture 09).

## Common shell (3j-3o)

- Grid: sidebar 232 | content. Sidebar: surface `#efeee9`, right border `#e3e1dc`, padding 24 16, gap 28.
- Logo: 22px square (radius 5, `#1b1b1d`) with an accent square inside + "FrameBeam Hub" 16/600.
- Navigation (order, item padding 9 10, 14px, label left, badge right 12/500): Library; Saves (badge "1 conflict", warn); Systems & Cores (badge "2 firmware", error); Clients (badge "1 request", warn); Users; Settings. Active: surface `#fff`, ring 1px `#e3e1dc`, 500; inactive `#55544f`. Badges are counters of open, actionable states.
- Sidebar footer: "admin · Admin" (13/500), mono 12 "hub.local · v0.1.0", "Raspberry Pi 5 · arm64".
- Page header: title 26/600 + subtitle 14 `#6b6a66`. Tables: card `#fff`, radius 10, header row surface `#faf9f7` with mono-11 eyebrows, row dividers `#efeee9`.
- Status pills (radius 999, 12/600): ok green, neutral gray, warn/error see tokens.

## 3j Hub Library
Lines 492-540. Phase 1 (library, ROM upload/download).

- Layout: sidebar | main (padding 32 40, gap 24); Library active.
- Header: title "Library", subtitle "42 ROMs · 3.1 GB of 118 GB used"; on the right buttons "Rescan folder" (outline) and "Upload ROM" (primary dark), each 38 high.
- Filter row: pills "All systems" (active, dark), "Nintendo DS · 42"; on the right search field 260 "Search title or hash…".
- Table, columns `2.2fr | 70 | 90 | 150 | 160 | 80`: TITLE (cover placeholder 34 + title 500), SYSTEM (mono "nds"), SIZE (mono), SHA-256 (mono, shortened "1b7e…04c9"), UPLOADED BY ("{user} · {DD.MM}"), SAVES (mono number + "▲" warn on conflict).
- Mock (9 rows, script from line 878): among others "Clocktower Kids" 64.0 MB 3 saves; "Harbor Rally" 32.0 MB 6 saves with conflict marker; "Velvet Arcade" uploaded by "lena · 03.10", otherwise "admin".
- Open: row actions (delete, metadata; architecture 09 names metadata actions in the library entry, later), upload dialog, scan progress, error states (hash/duplicate), paging not drawn.

## 3k Hub Saves
Lines 546-633. Phase 3. Conflict next to the version history.

- Grid: sidebar | list 320 (surface `#faf9f7`, right border, padding 32 20) | detail (padding 32 40, gap 22).
- List: title "Saves" (22/600); filter "User: Max ▾"; games (cover 36, title 14/500, line 12): selected surface `#fff` + ring; conflict "▲ Conflict · unresolved" (warn) or "Checkpoint · {time}". Mock: Harbor Rally (conflict, selected), Lumen Drift (today 18:42), Tide & Lantern (28.09.), Clocktower Kids (02.10.), Stylus Knights (21.09.).
- Detail header: game title "Harbor Rally" 26/600, user "Max".
- Bar "CURRENT CHECKPOINT": "Rev 41 · Laptop Office · today 19:10 · Auto checkpoint", on the right "Download".
- Conflict card (border `#e8d3a6`, padding 22): title "▲ Conflict: upload is based on Rev 40, current is Rev 41" (16/600, `#6e4a08`), text "Both contents are preserved and secured in the history before resolution."; two comparison cards (label column 110: device, time, base revision, content hash mono):
  - "Current checkpoint on the Hub": "Rev 41", Laptop Office, "today, 19:10", base "Rev 40", hash "a3f1…08cc".
  - "Secured upload · sync pending": "Local save", Desktop Living Room, "today, 19:24", base "Rev 40", hash "6e02…d911".
  - Actions (38 high): "Use Hub version", "Adopt local save as new current version" (outline), "Keep both, decide later" (primary dark).
- "HISTORY · PERMANENT VERSIONS": timeline (column 24 with vertical line and node | text | actions). Node current = filled accent with ring, old = hollow circle. Row: mono "v{n}", device 500, badge "CURRENT" (current only), meta "{time} · {reason}"; actions "Download" (always), "Restore" (old only). Mock (all old): v6 Desktop Living Room yesterday 22:30 "Session end"; v5 Laptop Office yesterday 18:05 "Device change"; v4 Desktop Living Room 03.10. 21:12 "Manual snapshot".
- Open: "Restore" in the Hub is not backed by the architecture (see README deviations); empty state without conflict, confirmation of "Restore", revision numbering (Rev 41 vs. v6) inconsistent.

## 3l Hub Systems and Cores
Lines 639-700. Phase 5. Registry, compatibility, firmware.

- Layout: sidebar | main (padding 28 40, gap 14). Title "Systems & Cores", subtitle "Registry: which system expects which core in which version. The Hub does not run cores."
- System card (2 columns, padding 24, divider): left "Nintendo DS" (20/600) + mono "nds"; fields (label 150): Preferred core "melonDS DS" (mono "melonds_ds"), Expected version "1.2.0", Platforms "windows-x86_64", Provisioning "Included in the Windows Player". Right "MANIFEST": file extension ".nds", input profile "nds", display profile "dual_screen", "BIOS / Firmware" "per manifest · see below".
- "REPORTED BY CLIENTS": table columns `1.2fr | 150 | 260 | 1.4fr`: device, platform (mono), "Player {version} · melonDS DS {version}" (mono), status "● compatible" (green) or error text (error/500). Mock: Desktop Living Room (Player 0.1.0, core 1.2.0, compatible); Lena's gaming PC (core 1.1.3, "Core version mismatch · 1.2.0 expected"); Jonas's notebook (Player 0.0.9, "Player too old · protocol v1 required").
- "BIOS / FIRMWARE · NDS" + text "provided by the admin, not shipped with FrameBeam", on the right button "Provide file" (dark, 32 high). Table columns `1fr | 120 | 1.5fr | 150 | 150`: FILE (display name + file name mono), REQUIREMENT ("required"), SHA-256 EXPECTED / PRESENT (shortened, mono 12, two lines), STATUS (pill), action.
  - Row 1 (ARM7 BIOS): "✓ Valid" (green), no action ("—").
  - Row 2 (ARM9 BIOS): "✕ Hash mismatch" (solid red, row `#fbf1ef`, differing hash red), action "Replace".
  - Row 3 (DS firmware): "○ Missing" (dashed red, row `#fdf8f6`, present "—"), action "Provide".
  - File names in the source are not carried over (rule: no real BIOS names); display names "ARM7 BIOS", "ARM9 BIOS", "DS Firmware" appear as such in the source.
- "Core package cache" (dashed, badge "LATER"): "Core packages with version, platform, SHA-256, origin and licence will be stored here later and distributed to Players. Not active in 0.1."
- Open: file upload dialog, removing a firmware file, further systems; the nav badge "2 firmware" corresponds to rows 2 and 3.

## 3m Hub Clients
Lines 706-742. Phase 1 (pairing Allow/Deny, Revoke).

- Layout: sidebar | main (padding 32 40, gap 26). Title "Clients", subtitle "Devices allowed to access this Hub".
- "PENDING REQUESTS · 1" (eyebrow): card (border `#e8d3a6`, padding 20 22; columns `1fr | 220 | auto`): "Device name" / "Lena's gaming PC" (20/600), mono "Windows x86-64 · Player 0.1.0 · protocol v1", "requested 2 min ago"; select "Assign user" (value "Lena"); buttons "Deny" (outline) and "Allow" (primary dark).
- "DEVICES": table columns `1.3fr | 110 | 140 | 90 | 140 | 100 | 150`: DEVICE, USER, PLATFORM (mono), PLAYER (mono), LAST ACTIVE, STATUS (pill), action.
  - Trusted (green) with action "Revoke access" (red, text link): Desktop Living Room (Max, 0.1.0, "just now"), Laptop Office (Max, 0.1.0, "today 19:10"), Jonas's notebook (Jonas, 0.0.9, "yesterday"), Desktop admin (admin, 0.1.0, "02.10.").
  - Revoked (gray, row muted, action "—"): Old laptop (Max, 0.0.9, "12.09.").
- Footnote: "Revoking immediately removes the device's access, even with still-valid access tokens."
- Open: confirmation dialog for revoking, denied state, renaming, reinstating a revoked device, certificate/compatibility errors per client (only in 3l) not drawn.

## 3n Hub Users
Lines 748-785. Phase 5. Hub-local accounts and onboarding invites.

- Grid: sidebar | main | side panel 420 (surface `#faf9f7`, left border, padding 32 28, gap 18).
- Main: title "Users", subtitle "Accounts apply only on this Hub. Normal users have no password." Table columns `1.2fr | 80 | 70 | 110 | 110 | 110`: NAME (avatar initial 28 + name), ROLE, DEVICES (mono count), CREATED, STATUS (pill Active/Disabled), action.
  - Mock: admin (Admin, 1, 14.08.2026, Active, "—"); Max (User, 2, 14.08.2026, Active, "Disable" red); Lena (User, 1, 20.08.2026); Jonas (User, 1, 02.09.2026); Sam (User, 1, 11.09.2026, Disabled, row muted, action "Enable").
  - Footnote: "Disabled users cannot sign in on any device. Saves and uploads are preserved."
- Side panel "Onboarding invites" (18/600): text "The invited person redeems the code in the Player and chooses a display name. This is not a Session invite."; button "Create invite" (dark, 36).
  - Active invite (card): "Active", "expires in 42 min" (warn); code mono 24 "FB-7KQ2-M9XD"; toggle (on) "Authorize first device directly"; "Single use"; buttons "Copy link" (outline), "Revoke" (red).
  - History (muted): "FB-2HC9-…" "redeemed by Jonas · 02.09."; "FB-Q81M-…" "expired · 30.09.".
- Open: "Create user" dialog (no button drawn), role change, deletion, selectable expiry duration (unclear); example codes are mock.

## 3o Hub Settings
Lines 791-819. Phase 1 (Hub name, address, certificate/TLS, admin account); phase 5 (appearance, upload option; "Allow users to upload games" requires users).

- Layout: sidebar | main (padding 32 40, gap 24). Title "Settings", subtitle "Admins only". Two columns (gap 20), cards (padding 18 22 6, radius 10, title 16/600); rows: label block left (14/500 + help text 12), control right (field 200 x 34 or segment/toggle), divider above.
- Left column:
  - "General": "Hub name" (help "Shown to Players during identification", value "Home"); "Address for Players" (value "hub.local:8443").
  - "Appearance": "Mode" (help "Applies to this web interface"), segment "Light" (active) | "Dark" | "System".
  - "Library": "Allow users to upload games" + badge "Inactive", text "Currently only admins can upload ROMs.", help "Own uploads land in the same library and are marked with uploaded_by. Managing or deleting others' entries remains reserved for admins."; toggle off.
- Right column:
  - "Transport & certificate": "Certificate" (help "HTTPS/WSS active. HTTP only in dev mode or on localhost."), segment "Self-generated" (active) | "Own cert/key" | "Reverse proxy"; "Fingerprint": SHA-256 (mono 12, two lines) + "Players compare this value on first connect · valid until 05.10.2027".
  - "Admin account": "admin" (help "Web login with username and password"), button "Change password".
  - "Metadata" (dashed, badge "LATER"): "Provider, language and region".
- Open: configuration for "Own cert/key" and "Reverse proxy", save mechanics (no save button; with htmx possibly immediate, unclear), dark variant of the Hub design (only light drawn).
