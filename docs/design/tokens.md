# Design tokens

Extracted from `source/framebeam-designs-v3.dc.html` (only values that actually occur; roles are derived from usage, the source does not name tokens). Shared: fonts, accent, layout grid. The canvas chrome of the prototype page (background `#d9d8d4`, headings `#1b1b1d`/`#3a3936`/`#5b5a56`, links `#1a7f96`, hover `#0f5a6b`) is not UI and not listed. The old amber accent no longer occurs in the UI (the logo sheet canvas still carries the old amber link color; canvas only). Decisions on accent and warning color: `README.md`.

## Shared

| Token | Value | Note |
|---|---|---|
| Font Sans | IBM Plex Sans, fallback `system-ui, sans-serif` | Weights 400, 500, 600; antialiasing `-webkit-font-smoothing: antialiased` |
| Font Mono | IBM Plex Mono, fallback `monospace` | Weights 400, 500; hashes, addresses, versions, eyebrow labels |
| Accent (Player dark) | `#3cbfd8` (Signal Cyan) | Primary button, focus, selection ring, active input, toggle on, links; also the warning color (see Status) |
| Accent (Hub light) | `#1a7f96` | Text, links, chips and warning marks in the Hub; hover `#0f5a6b`, selection surface `#eef8fa`, chip surface `#ddf1f6`, border `#a6d9e6`. Primary buttons in the Hub stay dark (`#1b1b1d`) |
| Accent bright (Hub light) | `#3cbfd8` | Only for marks in the Hub: current history marker, selection bar on list/section items |
| Text on accent | `#161512` | Player primary button |
| Screen size | 1440 x 900 | Prototype canvas, no responsive specification (unclear) |
| Sidebar width | 232px | Player main navigation and Hub navigation |

Note: In the source, the fonts are loaded via Google Fonts. For the Hub (no Node build, self-hosted) and the Player, embedding the fonts is open (licence/offline operation not decided).

## Typography (both themes)

| Role | Value |
|---|---|
| Page title | Sans 26px/600, letter-spacing -.015em (Hub 3k column title 22px) |
| Start/pairing title (Player) | Sans 30px/600, letter-spacing -.02em |
| Dialog title (3d) | Sans 24px/600, -.01em |
| Card title / section | 15-17px/600 (Hub cards 16px/600, system card heading 20px/600) |
| Body text, rows | 13-14px/400-500 |
| Meta, help text | 12px/400, line height 1.5-1.55 |
| Eyebrow / column header | Mono 11px/500, UPPERCASE, letter-spacing .08em (table headers .06em) |
| Number/hash/address values | Mono 12-13px/400 |
| Invite code (Hub 3n) | Mono 24px/500, letter-spacing .04em |
| Monogram on cover placeholder | Sans 34-36px/600, -.02em |

## Player (dark)

Colors

| Role | Value |
|---|---|
| Background app | `#121315` |
| Background sidebar | `#0e0f10` |
| Background side panel (middle/detail column, dialog) | `#16171a` |
| Surface card / input | `#1a1b1e` |
| Surface raised / selected / Hub card | `#1f2024` / Hub card in sidebar `#17181b` |
| Surface menu/dialog tile | `#1d1e22` |
| Background in game (3g-3i) | `#0b0b0c`, header and side panel `#111214` |
| Backdrop dialog (3d) | `#08090a`, pattern `#0d0e0f`/`#101113` |
| Video placeholder | Stripes `#17181a`/`#1b1c1f`; PiP `#1c1d20`/`#222327` |
| Border sidebar/divider | `#232428` (in game `#1e1f22`, table row `#1f2024`) |
| Border card | `#26272b` |
| Border input/chip, active segment, avatar | `#2c2d32` |
| Border selection ring, popup | `#2f3035` |
| Border button (outline) | `#3a3b40` |
| Text primary | `#ecebe7` |
| Text secondary | `#c9c8c4` |
| Text muted | `#a3a3a8`, `#8e8e94` (meta) |
| Text placeholder/eyebrow | `#7d7d83`; on tiles `#6f6f75` |
| Text disabled / offline dot | `#5e5e63`; monogram `#45464c` |
| Accent chip surface | `#15262b` (text `#3cbfd8`); update card border `#1a3238` |
| Info/relay hint | Surface `#14222a`, border `#1a3238`, text `#a9cdd6` (icon `#3cbfd8`) |
| Input error line | `#c4544a` |
| Status ok | `#6fd39a` (surface `#17291f`) |
| Status warn | `#3cbfd8` (surface `#15262b`); same value as the accent, intentionally |
| Status error | `#ef8a78` (surface `#2a1a17`, text on it `#e6d6d2`) |
| Status neutral (e.g. "Local") | Text `#a3a3a8` on `#232428` |
| Diagnostics overlay (3t-3y) | Surface `rgba(17,18,20,.94)`, border `#26272b`, section divider `#1e1f22`, section title mono 11/500 `#7d7d83` letter-spacing .08em, summary mono 11 `#8e8e94`; sparkline total `#3cbfd8` 1.25, emu `#6f6f75` 1, area `rgba(60,191,216,.14)`, 16.7 ms line dashed `#3a3b40`; "Fallback" pill and info box as above |

Radii: 4, 5 (segment), 6, 7 (input, button), 8 (card, primary button), 9, 10 (Hub card), 11 (toggle), 12 (dialog), 16 (filter chip, 32 high), 999 (pill), 50% (avatar, status dot).

Shadows: screen `0 30px 60px rgba(0,0,0,.25)` (prototype frame only); selection ring `0 0 0 1px #2f3035`; focus/active card `0 0 0 1.5px #3cbfd8`; tab underline `inset 0 -2px 0 #3cbfd8`; audio-focus tile ring in Multiview `inset 0 0 0 2px #3cbfd8`; fullscreen toolbar `0 12px 30px rgba(0,0,0,.5)` (surface `rgba(22,23,26,.92)`, border `#2c2d32`); multiview picker `0 16px 40px rgba(0,0,0,.55)` (surface `#1d1e22`, border `#2f3035`); dialog `0 40px 80px rgba(0,0,0,.6)`; PiP `0 16px 40px rgba(0,0,0,.5)`; popup `0 12px 28px rgba(0,0,0,.45)`.

Dimensions (recurring)

| Dimension | Value |
|---|---|
| Sidebar padding / gap | 24px 16px / 28px; nav item 9px 10px, radius 6 |
| Main padding | 28px 32px (3e: 28px 36px) |
| Columns | Library detail 392px; middle column 300px (3e/3f/3p); input test 340px; Session side panel 340px |
| Header in game | 56px high, padding 0 20px |
| Button heights | 48 (primary), 46 (dialog option), 44, 42, 40, 38; input/select 34 |
| Spacing (gap) | 4, 6, 8, 10, 12, 14, 16, 20, 28 (most frequent: 10, 4, 8) |
| Library grid | 4 columns, gap 20px 18px; cover square |

## Hub (light)

Colors

| Role | Value |
|---|---|
| Background app | `#f6f5f2` |
| Background sidebar | `#efeee9` |
| Surface card, table, active nav item | `#fff` |
| Surface light (table header, list column, inputs) | `#faf9f7` |
| Cover/avatar placeholder | `#ebeae5`; stripes `#ebeae5`/`#e2e0da` |
| Border card/sidebar | `#e3e1dc` |
| Border input/button | `#d9d7d1` |
| Row divider | `#efeee9` (also `#e9e7e2`) |
| Border dashed (placeholder "LATER") | `#cfccc5` |
| Accent selection (list/section item) | Surface `#eef8fa`, left bar `inset 3px 0 0 #3cbfd8` |
| Neutral marker (history old) | `#b9b7b0` |
| Text primary | `#1b1b1d` |
| Text secondary | `#55544f` |
| Text muted | `#6b6a66` |
| Text placeholder / disabled | `#8a8984` |
| Primary button | Surface `#1b1b1d`, text `#f6f5f2` |
| Accent (UI) | `#1a7f96`, hover `#0f5a6b`, chip surface `#ddf1f6`, border `#a6d9e6` (see Shared) |
| Status ok | Text `#1f6a3f` on `#e2f1e7`; text "compatible" `#2a7a4c` |
| Status warn | Text `#1a7f96` on `#ddf1f6`, border `#a6d9e6`, dark text `#0f5a6b`; same family as the accent, intentionally |
| Status error | Text `#b5402f` on `#fbe7e3` (pill); strong `#9b2f20` on `#f7dcd6` (counter, "Last install failed"); card `#fbf1ef`, border `#f0cfc8` |
| Neutral badge (Revoked/Disabled/Inactive) | Surface `#ebeae5`, text `#55544f` |

Radii: 2, 4 (cover), 5, 6 (nav item), 7 (input, button), 8, 10 (card, table), 11 (toggle), 999 (pill), 50% (avatar). The logo tile has its own radius (see `logo.md`).

Shadows: screen `0 30px 60px rgba(0,0,0,.15)` (prototype frame only); active nav item/segment `0 0 0 1px #e3e1dc`; current history marker `0 0 0 4px #f6f5f2, 0 0 0 5px #3cbfd8` (dot `#3cbfd8`, snapshot marker: 9px diamond `#1a7f96`, rotated square).

Dimensions (recurring)

| Dimension | Value |
|---|---|
| Sidebar | 232px, padding 24px 16px, gap 28px; footer mono 12px (`#6b6a66`) |
| Main padding | 32px 40px (3l: 28px 40px) |
| Columns | Saves list 320px; Settings section column 280px (3q); Systems list 300px (inside content, 3l); Users side panel 420px |
| Table row | Padding 12-13px 20px, column gap 16px, header padding 10-12px 20px |
| Button heights | 38 (standard), 36 (card/form), 34 (input), 32 (small) |
| Spacing (gap) | 2, 3, 4, 8, 10, 12, 16, 20, 24, 28 (most frequent: 10, 16, 4) |
| Cards | Radius 10, border `#e3e1dc`, padding 18-24px |

## Status pills, chips, toggles (both themes)

Status is shown as a pill (surface + text), no longer as colored text only (exception: game status lines in the Player library grid and nav badges are text only).

| Component | Spec |
|---|---|
| Status pill | inline-flex, gap 5, padding 3px 9px, radius 999, Sans 12px/600, leading symbol `●` ok, `▲` warn, `✕` error, `✓`, `○`; small variant (diagnostics connection, snapshot/current marker): padding 1-2px 7-8px, 11px/600 |
| Pill Player | ok `#6fd39a` on `#17291f`; warn `#3cbfd8` on `#15262b`; error `#ef8a78` on `#2a1a17`; neutral `#a3a3a8` on `#232428` |
| Pill Hub | ok `#1f6a3f` on `#e2f1e7`; warn `#1a7f96` on `#ddf1f6` (border `#a6d9e6` on large chips); error `#b5402f` on `#fbe7e3` or `#9b2f20` on `#f7dcd6`; neutral `#55544f` on `#ebeae5` |
| Filter chip (Player) | 32 high, padding 0 14, radius 16, 13px; active surface `#ecebe7`, text `#121315`; inactive text `#c9c8c4`, border `#2c2d32`; counter badge inside: min 18 x 18, padding 0 5, radius 9, 11/600, surface `#15262b`, text `#3cbfd8` |
| Segment (Player) | padding 3, radius 7, surface `#1a1b1e`, gap 2-4; active option surface `#2c2d32`, text `#ecebe7`, radius 5; inactive text `#a3a3a8` |
| Segment (Hub) | active option surface `#fff` with ring `0 0 0 1px #e3e1dc`, text `#1b1b1d`; inactive `#55544f` (filter variant in 3s: track `#efeee9`) |
| Nav badge (Hub) | text only, 12/500: warn `#1a7f96` ("1 conflict", "1 request", "1 update"), error `#b5402f` ("2 issues") |
| Toggle (both) | 40 x 22, radius 11, knob 16 at top 3, left 3 (off) / 21 (on) |
| Toggle Player | on `#3cbfd8`, off `#2c2d32`, knob `#ecebe7` |
| Toggle Hub | on `#1b1b1d`, off `#d9d7d1`, knob `#fff` with `0 1px 2px rgba(0,0,0,.2)` (not accent) |
| Changed marker | 7px dot `#3cbfd8` before the label of an option or binding changed from its default (3e, 3f); "Reset" link (`#a3a3a8`, underlined) or "Default" (`#5e5e63`) on the right |
| Tabs (Hub) | Sans 14, active 600 with 2px underline `#1b1b1d`, inactive `#6b6a66`; count either as counter badge (3l: min 18 x 18, radius 9, 11/600, `#9b2f20` on `#f7dcd6`, padding 10 14 per tab) or as 12px text (3k/3s slot tabs: `#1a7f96` for "1 conflict", `#8a8984` for "N versions") |

## Mapping (note, not code)

- Player (QML): one theme singleton with the roles above as properties; dark/light switching (architecture 09) via two palettes with the same role names. A light Player palette is not designed in the source; `Theme.qml` carries one derived from the Hub tokens (ADR 0007 D7).
- Hub (CSS): roles as CSS variables on `:root`; dark variant via `[data-theme]`/`prefers-color-scheme` (settings offer Light/Dark/System). A dark Hub palette is not designed in the source; phase 5 derives it from the Player tokens as CSS variables under `[data-theme="dark"]` (and `prefers-color-scheme` for System) in `server/internal/web/static/app.css`. It is a derivation, not a designed palette; the Player set is not adopted 1:1 (ADR 0007 D7).
- Role names proposed to be the same in both themes (bg, bg-sidebar, surface, border, text, text-muted, accent, ok, warn, error), so that the design language stays common.
