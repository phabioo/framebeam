# Design tokens

Extracted from `source/framebeam-designs-v3.dc.html` (only values that actually occur; roles are derived from usage, the source does not name tokens). Shared: fonts, accent, layout grid. The canvas chrome of the prototype page (background `#d9d8d4`, headings `#3a3936`/`#5b5a56`, links `#9a6a12`) is not UI and not listed.

## Shared

| Token | Value | Note |
|---|---|---|
| Font Sans | IBM Plex Sans, fallback `system-ui, sans-serif` | Weights 400, 500, 600; antialiasing `-webkit-font-smoothing: antialiased` |
| Font Mono | IBM Plex Mono, fallback `monospace` | Weights 400, 500; hashes, addresses, versions, eyebrow labels |
| Accent | `#e9b44c` | Primary button and focus in the Player; in the Hub only logo mark and history marker |
| Text on accent | `#161512` | |
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
| Status ok | `#6fd39a` (surface `#17291f`) |
| Status warn | `#e9b44c` (surface `#2a2418`) |
| Status error | `#ef8a78` (surface `#2a1a17`, text on it `#e6d6d2`) |

Radii: 4, 5 (segment, logo), 6, 7 (input, button), 8 (card, primary button), 9, 10 (Hub card), 12 (dialog), 999 (chip, pill, toggle), 50% (avatar, status dot).

Shadows: screen `0 30px 60px rgba(0,0,0,.25)` (prototype frame only); selection ring `0 0 0 1px #2f3035`; focus/active card `0 0 0 1.5px #e9b44c`; tab underline `inset 0 -2px 0 #e9b44c`; dialog `0 40px 80px rgba(0,0,0,.6)`; PiP `0 16px 40px rgba(0,0,0,.5)`; popup `0 12px 28px rgba(0,0,0,.45)`.

Dimensions (recurring)

| Dimension | Value |
|---|---|
| Sidebar padding / gap | 24px 16px / 28px; nav item 9px 10px, radius 6 |
| Main padding | 28px 32px (3e: 28px 36px) |
| Columns | Library detail 392px; middle column 300px (3e/3f); input test 340px; Session side panel 340px |
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
| Neutral marker (history old) | `#b9b7b0` |
| Text primary | `#1b1b1d` |
| Text secondary | `#55544f` |
| Text muted | `#6b6a66` |
| Text placeholder / disabled | `#8a8984` |
| Primary button | Surface `#1b1b1d`, text `#f6f5f2` |
| Status ok | Text `#1f6a3f` on `#e2f1e7`; text "compatible" `#2a7a4c` |
| Status warn | Text `#9a6a12`, dark `#6e4a08`, border `#e8d3a6`, badge surface `#f6e6c4` |
| Status error | Text/surface `#b5402f`; row background `#fbf1ef`, lighter `#fdf8f6` |
| Neutral badge (Revoked/Disabled/Inactive) | Surface `#ebeae5`, text `#55544f` |

Radii: 2 (logo mark), 4 (cover), 5, 6 (nav item), 7 (input, button), 8, 10 (card, table), 999 (pill, toggle), 50% (avatar).

Shadows: screen `0 30px 60px rgba(0,0,0,.15)` (prototype frame only); active nav item/segment `0 0 0 1px #e3e1dc`; current history marker `0 0 0 4px #f6f5f2, 0 0 0 5px #e9b44c`.

Dimensions (recurring)

| Dimension | Value |
|---|---|
| Sidebar | 232px, padding 24px 16px, gap 28px; footer mono 12px (`#6b6a66`) |
| Main padding | 32px 40px (3l: 28px 40px) |
| Columns | Saves list 320px; Users side panel 420px |
| Table row | Padding 12-13px 20px, column gap 16px, header padding 10-12px 20px |
| Button heights | 38 (standard), 36 (card/form), 34 (input), 32 (small) |
| Spacing (gap) | 2, 3, 4, 8, 10, 12, 16, 20, 24, 28 (most frequent: 10, 16, 4) |
| Cards | Radius 10, border `#e3e1dc`, padding 18-24px |

## Mapping (note, not code)

- Player (QML): one theme singleton with the roles above as properties; dark/light switching (architecture 09) via two palettes with the same role names. A light Player palette is not designed in the source (open).
- Hub (CSS): roles as CSS variables on `:root`; dark variant via `[data-theme]`/`prefers-color-scheme` (settings offer Light/Dark/System). A dark Hub palette is not designed in the source (open); the Player set cannot simply be adopted.
- Role names proposed to be the same in both themes (bg, bg-sidebar, surface, border, text, text-muted, accent, ok, warn, error), so that the design language stays common.
