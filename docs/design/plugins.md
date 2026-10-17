# Design: plugin platform (Player and Hub)

Plugin API v1 is planned for 0.11 and not implemented; this file specifies drawn screens only. Interface: [../architecture/12-plugins.md](../architecture/12-plugins.md) §16, [ADR 0023](../adr/0023-plugins.md) (proposed). Where a drawing and §16 differ, §16 was aligned in the same PR (decisions.md, items 6-19). Index and screen table: [README.md](README.md). Tokens: [tokens.md](tokens.md) (section "Additions of the plugin handoff"). Decisions and open points: [decisions.md](decisions.md) (items 6-19, open points ao-bf).

## Source and supersession

Claude Design handoff "Plugins + SoulLink" (round 1 and correction round 2, accepted by Fabio on 2026-10-11). Only the platform part is imported; SoulLink artboards (5a-5i, 5e-5g), `SoulLinkPanel`, `RunScreen` and `HubRunWizard` belong to the `framebeam-plugin-soullink` repository and are not copied (the imported files still `dc-import` `SoulLinkPanel`, `RunScreen` and `HubRunWizard` by their original names; those panes stay empty here). Files in `source/` (they do not render standalone; `support.js` is not product code):

| File | Content |
|---|---|
| `plugins-4a-4d.dc.html` (round 1) | 4a Settings → Plugins, 4b detail, 4c permission dialogs, 4d install and console |
| `plugins-4e-4h.dc.html` (round 1) | 4e in-game tabs, HUD, tile strip, fullscreen; 4f sidebar screen; 4g notices; 4h game detail |
| `plugins-4i-4j-sheet.dc.html` (round 1) | 4ih Hub plugins, 4j / 4jh runs, `4-sheet` (blocks and map states), `4-handoff` (10 entries) |
| `plugins-round-2.dc.html` (round 2) | the `-2` artboards, 3f-4, `r2-sheet`, `r2-changes`; also SoulLink artboards 5ah-2, 5b-2, 5c-2, 5d-2, 5e-*-2, 5f-*-2 (spec in the SoulLink repo) |
| `plugin-settings-screen.dc.html` | 4a, 4b and 4b-2 as component (`view`: list, empty, detail, uninstall; `theme`) |
| `block-sheet.dc.html` | building-block sheet, dark and light (`theme`) |
| `region-map.dc.html` | map view in six states (`state` default, cluster, focus, empty; `filter`; `theme`) |
| `creature-icon.dc.html` | placeholder icon (sizes, states), needed by the map and the sheet |

Reused existing components: `game-header`, `game-panel`, `diag-overlay`, `framebeam-mark`. Mock data is fictional: game "Coral Saga: Ember / Tide" (Nintendo 3DS, core "Azahar"), creatures from a made-up set, players Max, Lena, Jonas, Sam, plugins "Creature Core", "SoulLink & Nuzlocke", "Item Helper", "Party Editor", "Shiny Counter", host `hub.example.com`, Player and Hub 0.11.0, Item Helper 0.3.2. Addresses in the console are fake (`0x0800_1A10`). Stale content in round-1 artboards (DS wording, DS addresses, equal 480 x 360 DS screens) must not be ported; see the table below.

**Round 2 supersedes round 1.** Artboard ids with `-2` replace the round-1 id; round-1 artboards listed as reference only still show DS screens (480 x 360 top and bottom), DS addresses or old wording. Take the `-2` ids.

| Round 2 id (file `plugins-round-2`) | Replaces (round 1) | Round-1 status |
|---|---|---|
| 4b-2 | new state of 4b (uninstall) | 4b stays valid |
| 4c-3-2, 4c-4-2 | 4c-3, 4c-4 | reference only (4c-3 has the own "Show notices" permission line) |
| 4d-3-2, 4d-4-2 | 4d-3, 4d-console | reference only (4d-3 signed local file replaces the Hub copy; console with DS addresses) |
| 4e-1-2, 4e-2-2, 4e-3-2, 4e-4-2, 4e-6-2, 4e-8-2 | 4e-1, 4e-2, 4e-3, 4e-4, 4e-6, 4e-8 | reference only (equal 480 x 360 DS screens) |
| 4g-2, 4h-2 | 4g, 4h | reference only (4h shows "Nintendo DS · melonDS DS 1.4.0") |
| 3f-4 | new, extends v4 3f-2 | - |
| 4j-2, 4jh-2 | 4j, 4jh | reference only (no paused status) |
| r2-sheet | BlockSheet part of 4-sheet | the six map states in `4-sheet` stay valid |

Still valid and not redrawn: 4a, 4a-light, 4a-empty, 4b, 4b-light, 4c-1, 4c-2, 4d-1, 4d-2, 4d-console-error, 4e-hud (4e-5), 4e-7, 4f, 4ih, 4ih-upload. (SoulLink: 5a-1 to 5a-5, 5b-states, 5h, 5i, 5e-team, 5e-grave, 5gh, 5gs, 5gs-narrow.) The round-1 files carry a banner and the colour, version and name fixes in place; their layout is superseded as listed.

**Round 3 (2026-10-12) supersedes parts of rounds 1 and 2** for 4a, 4b (detail as sheet), 4f, 4h-2 (placement), 3f-4 (layout), 4ih, 4j-2 and 4jh-2; see [Round 3](#round-3-2026-10-12) below and the one-line notes in each older section. Files: `round-3-player-a.dc.html`, `round-3-hub.dc.html` (with `player-nav.dc.html`, `hub-nav.dc.html`) and the sheet `round-3-sheet.dc.html`. Older sections stay valid for everything Round 3 does not redraw.

Reading note: line ranges are artboard containers without the caption; anchors `#id` are block IDs. Dialog artboards 4c-x come from `sc-for` templates; the range is the template, the data (`dlg(...)` / `dialogs`) is at the lines given.

## Common rules

- Plugins render with FrameBeam blocks only (see Building blocks); plugins pick a tone and a size set, never a hex or px value.
- **Origin** is the signature, not the install route: "✓ Official" = signed by FrameBeam wherever installed from (plugin index, Hub upload or local file); "! Hub upload · unverified" = unsigned, offered by the Hub; "Local file" = unsigned file on this device (needs Developer mode). Management screens show the full badge; in-game surfaces (tab tooltip, overflow row, HUD widget, toast, tile strip) show a small "Unverified" or "Local" marker on hover or focus; Official shows none.
- Player colours identify people (avatars, owner dots, strips); they never carry status and are neither cyan nor near the danger red. Cyan is for open, selection, focus, progress and the Official badge. Amber is the warning tone. A death is grey + ✕, never red.
- In-game views (header, side panel, HUD, tiles, fullscreen) stay dark in both themes. Light applies to Settings, Library, Runs, the run screen and the Hub.
- 3DS play areas (stacked, top 5:3, bottom 4:3, whole pixel steps): 1440 x 900: 680 x 408 + 544 x 408, gap 6; 1280 x 800: 600 x 360 + 480 x 360; tile 320 x 192 + 256 x 192; fullscreen 720 x 576. Bottom screen has a dashed outline (touch). Original sizes 400 x 240 and 320 x 240 are labelled on the placeholders.
- Hotkeys: F3 diagnostics, F4 hide all HUD widgets, F5 snapshot, F11 fullscreen, Space speed-up, Esc pause or leave fullscreen are FrameBeam's (ADR 0014 D4, ADR 0018). Drawn and not yet specified in the code: Alt+1 … Alt+9 panel tabs, Ctrl+Shift+R reload plugin (developer mode) (open point ao). Plugin keys are suggestions; FrameBeam keys win (open point ap).

## 4a Settings Plugins
Superseded in part by [4a-3](#4a-3-settings-plugins-round-3) (Round 3: installed first, Available on Home Hub below, update button, dialogs).
Source `plugins-4a-4d.dc.html`: `#4a` 30-33, `#4a-light` 34-37, `#4a-empty` 38-43; component `plugin-settings-screen.dc.html` (`view="list"`, `"empty"`). 1440 x 900, dark and light. Still valid (round 2 changed only data: Player 0.11.0, 3DS, colours).

- Purpose: see, switch on/off and update installed plugins, install from file, Developer mode.
- Shell as 3p-2: sidebar with plugin entries under Library (4f); settings column 300 gets "Plugins" between Appearance and Diagnostics (meta "3 on · 1 update", empty "None installed"; bottom of the sidebar "Player 0.11.0 · Plugin API v1").
- Main (padding 28 36, gap 20): title 26/600, help text 13 ("If one fails it is paused; your game keeps running."), button "Install from file…" (36 high, outline). Eyebrow "INSTALLED · 4" + "3 on · 1 update".
- Row (padding 16 0, divider): icon chip 40 x 40 radius 9 (plugin icon, mono monogram fallback), name 15/600, version mono 12, origin pill (11/600, padding 2 8: "✓ Official" info tone, "! Hub upload · unverified" warning tone, "Local file" neutral), chip "Update available · 0.10.0", description 13, dependency line 12 ("✓ Needs Creature Core 1.x" ok, "⊘ Needs Creature Core 2.x · you have 1.2.0" blocked), "Adds: panel tab · HUD · tile strip · Runs screen"; toggle 40 x 22 (cyan when on) and "Details ›". Blocked by an unmet dependency: toggle at 40 % opacity, not switchable, reason in the dependency line.
- Developer mode row: toggle, text "Allows unsigned local plugins, adds the Plugin console tab in game and reloads plugins when their files change." Footer link "hub.example.com → Plugins · your Hub admin chooses what is offered."
- Empty (4a-empty): 2 x 2 dashed squares (24, one filled accent) as illustration, "No plugins yet", help text, "Browse plugins on your Hub" + "Install from file…" (centred, 440 wide).
- Open: automatic updates of Official plugins (open point aw).

## 4b Plugin detail, 4b-2 Uninstall
Superseded in part by [4a-4](#4a-4-plugin-detail-round-3) (Round 3: detail as a sheet, one primary).
Source `plugins-4a-4d.dc.html` `#4b` 46-49, `#4b-light` 50-53; `plugins-round-2.dc.html` `#4b-2` 30-35; component `plugin-settings-screen.dc.html` (`view="detail"`, `"uninstall"`).

- Header: "← Plugins", icon chip, name, version, origin pill, author line ("by FrameBeam Community · github.com/example/framebeam-plugin-soullink"), buttons "Update to 0.10.0" (when available) and the switch "Turn on". Scope select next to it: "For all matching games" | "Only in runs" | "Off" (the "Only in runs" option appears only for plugins that provide runs; Item Helper offers "For all matching games", open point ax). Subline "Active while you play a game that belongs to a run · 1 active run".
- Two columns (gap 32): left flexible, right 340. Left: WHAT IT DOES (13, `text-wrap: pretty`), PERMISSIONS · 4 with "Granted 02.10.2026" (rows: letter chip 28 x 28 radius 7, label 13/500, plain-language sub text), SETTINGS "from the plugin's settings schema" (SettingsRow controls, "All 14 settings →"). Right: WORKS WITH (games from the Library, "Nintendo 3DS · Azahar · matched by game ID · 2 in your Library"), DEPENDS ON · USED BY ("✓ 1.2.0 on"), HOTKEYS ("Controllers → Hotkeys", list), DATA (cached icons and map 12.4 MB from the user's own game file, run data on the Hub, "Clear cached icons"), "Uninstall plugin".
- Permission letters and wording (plain language for the `12-plugins.md` §16.5 permissions): R "Read game memory" (`memory.read`), F "Read your game file to show creature icons and the map" (`rom.read`), P "Show panels in game" (`ui`, includes notices), H "Share run progress with your Hub" (`run.state`), W "Change game memory" (`memory.write`), V "Capture the game picture" (`video.capture`).
- 4b-2: confirmation dialog 500 wide, padding 26 28, radius 12, border danger line, backdrop `rgba(5,5,6,.7)` (light `rgba(27,27,29,.45)`). Eyebrow "UNINSTALL PLUGIN" (danger, mono 11), title 20/600 "Uninstall SoulLink & Nuzlocke?", text 14, list in a card: the active run stays on the Hub and is paused for you until you reinstall; cached icons and map are deleted from this computer; save slots are not touched. Buttons Cancel (outline) and Uninstall (danger fill, 40 high, radius 8). Initial focus on Cancel. Open point aq (uninstall pauses own participation).

## 4c Permission dialog
Source `plugins-4a-4d.dc.html` `#4c-1` to `#4c-4` template 58-79 (data 191-194); round 2 `#4c-3-2`, `#4c-4-2` template 36-52 (data 414-420). Canvas 640 x 760 (round 2: 800), dialog 540 wide, padding 26 28, radius 12, surface `#16171a`, border `#2c2d32`, shadow `0 40px 80px rgba(0,0,0,.6)`.

- Anatomy: eyebrow (mono 11, e.g. "TURN ON PLUGIN"), title 20/600, plugin header (icon chip, name, version, origin pill, "by …" line), optional note row, lead ("It will be able to:"), permission rows (letter chip 28, label 13/500, sub 12), scope select, footer text, Cancel + primary.
- 4c-1 (valid): first turn-on, Official; scope "Only in runs".
- 4c-2 (valid): unverified Hub upload; warning note in the warning tone "FrameBeam has not checked this plugin. Turn it on only if you trust Lena and where the file came from."; scope "For all matching games".
- 4c-3-2: update asks for one new permission (Item Helper 0.3.2 → 0.4.0, "Share run progress with your Hub"). The new row is highlighted (info surface `#15262b`, border `#1f4650`, tag NEW cyan fill with `#161512` text), existing rows are dimmed with the tag ALLOWED. Buttons "Keep 0.3.2" and "Update and allow"; footer "Until you allow, 0.3.2 keeps running." Notices are part of "Show panels in game"; no own line.
- 4c-4-2: "Let Party Editor change Coral Saga: Ember?". High-risk row W (danger surface `#2a1a17`, border `#4a2a24`, tag HIGH RISK); notes: "A save snapshot is made before the first change." and "Every change is written to the run log that all players see." (info tone), "Runs can forbid this. In "Ember & Tide" it is forbidden, so Party Editor stays off there." (warning tone). Scope "Asked once for Coral Saga: Ember" (select), footer "Other games ask again." Primary "Allow changes" in danger fill. Initial focus on Cancel.

## 4d Install from file, console
Source `plugins-4a-4d.dc.html` `#4d-1` 82-92, `#4d-2` 93-111, `#4d-console-error` 154-173; `plugins-round-2.dc.html` `#4d-3-2` 54-68, `#4d-4-2` 69-88. Dialog canvases 640 x 620 (4d-1, 4d-2) and 640 x 800 (4d-3-2), console 1440 x 900.

- 4d-1 (valid): drop zone "Drop a .fbplugin file here" / "Choose file…" (system picker). Note: "Installed plugins count as Local file. Signed files are checked against FrameBeam's key; unsigned files need Developer mode." (the file extension `.fbplugin`; open point ay).
- 4d-2 (valid): summary read from the manifest before anything runs: File, Version, Origin, Signature ("None · allowed because Developer mode is on"), Needs ("Creature Core 2.x · you have 1.2.0"), Adds, permission list. "Installs turned off. It can't be turned on until Creature Core 2.x is installed."
- 4d-3-2: "Item Helper 0.3.2 is installed from a file", origin Local file, "not signed", checklist: "No signature · allowed because Developer mode is on" (warning), "Creature Core 1.x found" (ok), "The Hub version (0.3.2, unverified) stays installed. The run "Ember & Tide" keeps using it." Selector "WHICH COPY RUNS": Outside runs = "Local file · 0.3.2" (select), Run "Ember & Tide" = "Hub version · locked by run" (open point au). The local copy never replaces a Hub version used by a run (§16.10). Buttons Done, "Turn on…" (leads to 4c).
- 4d-4-2 console (Developer mode only, last tab): side panel tab ">_" "Plugin console"; plugin select with status dot and "↻ Reload" (tooltip "Reload plugin · Ctrl+Shift+R"); WATCHED · 4 ("live · 60 Hz", rows address 92 | value 58 | label, mono 11); EVENTS (time, kind chip, text); LOG (levels info, debug, warn in the warning tone, error) with "Copy · Clear". Fake addresses `0x0800_1A10` … `0x0800_3E00`.
- 4d-console-error (valid): "Paused after an error · your game keeps running", Lua error line and traceback, "Panels from this plugin show "Paused". Fix the file and Reload, or reload as is to try again. Saved with the log in Settings → Diagnostics." (the plugin pauses, the game keeps running).
- Open: detachable console window (open point az).

## 4e In-game tabs, HUD, tile strip, fullscreen
Source `plugins-round-2.dc.html`: `#4e-1-2` 91-102, `#4e-2-2` 103-121, `#4e-3-2` 122-135, `#4e-4-2` 136-148, `#4e-6-2` 149-168, `#4e-8-2` 169-180. Round 1 (valid): `plugins-4e-4h.dc.html` `#4e-hud` (4e-5) 109-136, `#4e-7` 168-183. Revises 3g-2 (panel), 3r-2 (tiles), 3i-2 (rail), 3y (fullscreen). Header and Game tab are unchanged. Panel 340 (1280: 320).

**Side panel tabs** (4e-1-2 one tab with the encounter hint, 4e-2-2 two tabs, 4e-3-2 four tabs with tooltip, 4e-4-2 at 1280 with overflow):
- Tab bar 44 high, padding 0 12, gap 2, tab padding 0 9, 13/400 `#a3a3a8`; open tab `#ecebe7` 500 with a 2px `#3cbfd8` bottom bar; controller focus ring `0 0 0 2px #111214, 0 0 0 4px #3cbfd8` with radius 6. Icon 18 x 18 radius 5 (plugin icon, mono monogram fallback, surface `#232428`).
- Order: Game, plugins in install order (user can reorder), Plugin console last (Developer mode). Keys Alt+1 … Alt+9 in that order (open point ao).
- Fitting: 1 tab, labels for all; 2 tabs, labels while they fit; 3 or more, Game and the open tab keep their label, the rest are icon-only with a tooltip (name, key, origin marker "Unverified" for non-official); still too wide (1280): "+n ▾" anchored popover listing the rest (name, "Local" marker, key). A tab with news gets a 6px cyan dot; a paused plugin's tab an amber dot. The collapsed 48px rail (3i-2) shows one icon per tab under the tile badge; click opens the panel on that tab.

**HUD widgets** (4e-5; 4e-2-2 and 4e-8-2 in context):
- Corners: bottom left, bottom right, top left; top right belongs to diagnostics (DiagOverlay stays there, the corner is not offered while diagnostics exist in the view). A stack per corner grows away from the corner; toasts sit above the bottom-right stack.
- Widget: surface `rgba(17,18,20,.82)`, border `1px rgba(255,255,255,.08)`, radius 8, padding 6 10, text 12, plugin icon 16, max 240 wide, max 4 per corner, gap 6, 16 from the play-area edge. Always dark. It takes no input; on hover or controller focus it gets brighter, a "⋯" menu and, for non-official plugins, the marker "Unverified" (or "Local").
- Menu: "Hide this widget · Ctrl+H", "Move to corner ↙ ↘ ↖", "Hide all HUD widgets · F4". Ctrl+H means one thing: show or hide this plugin's HUD widget; expanding a widget is a click or controller A (decisions item 12). F4 hides all widgets: a 2 s hint "HUD hidden · F4 to show", then nothing.
- Fullscreen (4e-8-2): widgets stay in their corner, side panel and tabs are hidden; hint bar "F3 Diagnostics · F4 HUD · F11 Exit".

**Tile strip** (4e-6-2 Grid 2 x 2: Lena's strip collapsed, Jonas's expanded; 4e-7 rings):
- Remote tiles only, never on your own tile (your HUD covers that). Strip 20 high (key-badge height), 8px right of the key badge, inside the 2px inset audio ring, so neither ring is covered; the white selection ring stays outside the tile. Content: plugin icon 14, player-colour dot 7, location, "· status", "▾".
- Click or A expands it (max 300 x 120, over the top screen, header strip with "▴"); the tile stays selectable around it. One strip per tile: when several plugins offer one, the user picks it in the tile's "⋯" menu (decisions item 11).

**In-game screen sheet** (opened by `fb.ui.open("screen")`, since the sidebar screen does not exist in a game): covers the body below the GameHeader (play area and panel; in fullscreen the play area), always dark. Top bar 52: "← Back to game" (quiet), plugin icon, screen title · plugin name, key hint "Esc"; the paused banner as in the panel. Content is the `screen` tree, max 1100 wide, centered, scrolling vertically. The game keeps running underneath; Esc and Back return to the game (Esc does not pause while the sheet is open).

## 4f Plugin sidebar screen
Superseded in part by [4f-3](#4f-3-plugin-sidebar-screen-round-3) (nested under Runs, offers the active run).
Source `plugins-4e-4h.dc.html` `#4f` 202-215 (valid). A plugin entry sits indented under Library with its icon (16, radius 4) and a count badge (min 18 x 18, radius 9, 11/600, `#15262b`/`#3cbfd8`) the plugin reports (e.g. run invites). One entry per plugin (open point ba). The screen frame: title row ("Runs", icon, plugin name with ✓ Official, "Plugin settings"), content area full width, scrolls vertically, built only from FrameBeam blocks.

## 4g-2 Notices
Source `plugins-round-2.dc.html` `#4g-2` 181-205 (supersedes 4g). Notices belong to the permission `ui`.

- Stack at the bottom right of the play area (16 from the edge, 390 wide, gap 8), newest at the bottom, max 3 visible; overflow shown as the pill "+1 earlier notice" (12, `rgba(17,18,20,.85)`). Toast: grid `28 | flex | auto`, padding 12, radius 10, shadow `0 18px 40px rgba(0,0,0,.5)`; icon 28 x 28 radius 7; source line 11/500 ("Plugin · warning", "FrameBeam · plugin paused"), message 13/1.45, actions 28 high (12/600), "⋯" and "×" 22 x 22.
- Tones: info and important (surface `#15262b`, border `#1f4650`, source line in cyan), warning (surface `#2a2414`, border `#4a3d1c`, source line `#e5b45a`). Examples: important "Lena's Tidepup fainted…" with "Open run" and "Undo death…"; warning Item Helper "Bag data not found…" with the "Unverified" marker (non-official) and the mute menu; "FrameBeam · plugin paused" "SoulLink & Nuzlocke was paused after an error. Your game keeps running." with "Restart plugin" and "Details".
- Mute menu (250 wide, surface `#1d1e22`, border `#34353a`, items 30 high): "Mute Item Helper's notices", "Only important ones", "Notice settings…".

## 4h-2 Game detail section
Placement superseded in part by [player.md 3c-7](player.md#3c-7-player-library-detail-column-round-3) (collapsible plugin section above START, no clipping).
Source `plugins-round-2.dc.html` `#4h-2` 206-231 (supersedes 4h). Revises 3c-2 detail column. 3DS chips on the library tiles; detail "Nintendo 3DS · Azahar", status table with ROM, Core ("✓ Azahar · ready") and a new row "Plugins · 3 on · Creature Core, SoulLink, Item Helper". Section under SAVE: save row "Run · Ember & Tide · v31 · yesterday 23:10 · this device · ✓ Synced" (run saves have their own slot), then the plugin section "RUNS WITH THIS GAME · 1 ACTIVE" (run card: name, status pill, plugin and players, "Open →", "Start a run…"; several runs as a list, max 3). Play becomes "Play run · Ember & Tide" with "Play without run" next to it. Open: whether "Play without run" switches the save slot (open point bb).

## 3f-4 Controllers Hotkeys
Layout superseded by [player.md 3f-4 (Round 3)](player.md#3f-4-player-controllers-round-3) (full values, aligned conflicts, gamepad column, Esc row, system selector); the plugin groups and conflict rules below stay.
Source `plugins-round-2.dc.html` `#3f-4` 234-259. Extends v4 3f-2 (columns 232 | 300 | flex | 340): device list adds "Mouse · Connected · 3DS touch" (open point ap).

- Banner line "FrameBeam keys always win · plugins only suggest keys". Tabs "Buttons" | "Hotkeys" and the search "Search hotkeys…".
- Groups: FRAMEBEAM first (meta "always win": Pause / Resume Esc, Speed-up Space, Diagnostics F3, Hide all HUD widgets F4, Save snapshot F5, Fullscreen F11, Panel tabs Alt+1 … Alt+9), then one group per plugin in install order with icon, name, origin badge and key count: SOULLINK & NUZLOCKE ✓ Official (Ctrl+H show or hide this plugin's HUD widget, Ctrl+D, Ctrl+R), ITEM HELPER Unverified (meta "1 conflict").
- Row: label, key (mono), "Default" or action ("Choose a key", underlined). Conflict row: surface `#1a1710`, border `#4a3d1c` (warning), key shown "Ctrl+H · not active" in amber, note "Already used by SoulLink & Nuzlocke. Unassigned until you choose." The key stays unassigned until the user picks another (open point ap).

## 4ih Hub plugins
Superseded in part by [4i-3](#4i-3-hub-plugins-round-3) (Round 3: monograms, amber origin, Needs / Used by, catalog status).
Source `plugins-4i-4j-sheet.dc.html` `#4ih` 30-56, `#4ih-upload` 58-70 (valid). Hub shell as 3j with nav Library, Saves, Systems & Cores, Clients, Users, Plugins, Runs, Settings (Plugins meta "1 update", Runs "1 active"); footer "Home · v0.11.0 · hub.example.com".

- Title "Plugins", help "Players on this Hub can install what is listed here.", button "Upload plugin". Table PLUGIN | ORIGIN | PERMISSIONS | PLAYERS with row action Details or Update; selected row `#f6fbfc`.
- OFFICIAL CATALOG: "signed index · checked today 21:04", search, rows with icon, name, version, description, "✓ Official" and Install / Update (primary cyan) / "✓ Installed".
- Detail of an unverified plugin: "Item Helper 0.3.2 · uploaded by Lena · 02.10.2026", note "Not signed by FrameBeam. Players see "Hub upload · unverified" and a warning before turning it on.", PERMISSIONS · CONFIRMED BY ADMIN, USED ON · 2 PLAYERS (user, device, on), "Download file", "Remove from Hub" ("Players keep their copy until they uninstall it.").
- Upload dialog (4ih-upload, canvas 640 x 640, backdrop `rgba(27,27,29,.5)`): "Offer Run Notes 0.2.0 to Players?", "FrameBeam has not checked this file.", every requested permission has a tick the admin must set ("Tick every permission to confirm you reviewed it."), buttons Cancel and "Offer to Players". Open: whether non-admins may upload (open point bc).

## 4j-2 Player Runs, 4jh-2 Hub Runs
Superseded in part by [4j-3](#4j-3-player-run-detail-round-3) and [4jh-3](#4jh-3-hub-run-detail-round-3) (Round 3: one primary action, state actions in the menu, "Starting" in cyan).
Source `plugins-round-2.dc.html` `#4j-2` 262-277, `#4jh-2` 278-291 (supersede 4j, 4jh; round 1 `plugins-4i-4j-sheet.dc.html` `#4j` 73-95, `#4jh` 96-121). A run is a FrameBeam object plugins build on: participants, plugin, a game each, status, event log. The Player sidebar entry is provided by the plugin ("Runs", with the count of invites), list, invite and log belong to FrameBeam. The Hub run detail has a plugin content slot (run board, 5gh).

- Player (4j-2): title "Runs" + "Start a run"; invite card "RUN INVITE" ("Lena invites you to "Tide Hardcore" · Nuzlocke", plugin and your game, "2 of 3 joined") with Decline and Join; groups "ACTIVE · 1 · PAUSED · 1" and "FINISHED · 2". Run row: name, status pill, sub line, participant avatars (player colours), time, "Open →" or "View".
- Status pills: Active (info), Paused (warning), Starting (warning, Hub), Finished (ok), Abandoned (neutral).
- Hub (4jh-2): runs list column 320 ("RUNS · 4", filter "All ▾"), detail with header "Summer Duo · Paused", "Paused by Lena · 09.10.2026 · SoulLink & Nuzlocke 0.9.2 · events still visible, no new ones recorded", buttons "Resume run", Archive, "Delete…"; participants table (avatar, name, game, status); plugin content slot with a "Paused" banner; EVENT LOG · n (time, author, text, source "automatic · Lena's Player" / "manual · Sam" / "Hub", "Export"). Active run: "Pause run" instead of "Resume run".
- Rules drawn (open points ar, as): any member can pause ("paused by Lena"), a paused run records no new events, status "Starting" while players are still joining. Who may delete a run: creator and admin (open point bd).

## Round 3 (2026-10-12)

Source `source/round-3-player-a.dc.html` (4a-3, 4a-4, 4f-3, 4j-3) and `source/round-3-hub.dc.html` (4i-3, 4jh-3); rules in `source/round-3-sheet.dc.html` (0-2, handoff table) and decisions 20-23 in [decisions.md](decisions.md). Finding IDs (H, P, D) are those of the 0.14 UI scan. Design only; plugin API v1 is not implemented. The Player screens use the page rules of [player.md](player.md#round-3-2026-10-12) (880 px content, 280 px control column), the Hub screens those of [hub.md](hub.md#round-3-2026-10-12). Pill colors per decision 23: "Hub upload · unverified" is amber everywhere (Hub list and detail, Player Settings, SoulLink screen; finding H23), "Local file" grey, Official has no pill, "Starting" and "update available" are cyan (information, no ▲). Mock plugins are "Pokémon Core 0.3.1", "SoulLink & Nuzlocke 0.3.2", "Run Notes 1.0.0".

### 4a-3 Settings Plugins (Round 3)
Source: `source/round-3-player-a.dc.html#4a-3` (135-156), `#4a-3-dialogs` (179-190). Fixes P19, P20, P21, P22.

- Settings nav as in 3p-3 with the entry "Plugins" ("2 installed · 1 update"). Content max 880; header "Plugins" with "If a plugin fails it is paused; your game keeps running."
- INSTALLED · 2 with the header action "Install from file…". Rows (58 high, toggle in the 280 control column): monogram, name 14/500 + version, amber pill "Hub upload · unverified" (none for Official), meta line ("Used by SoulLink & Nuzlocke · Read game memory, Read the game file"; "Needs Pokémon Core · Show panels in game, Share run progress"), a visible "Update to 0.3.2" button where an update exists, toggle, chevron "›" opening 4a-4. Every row has a meta line, so heights are equal (P21; Pokémon Core needs a short description in its manifest, finding PL2).
- AVAILABLE ON HOME HUB · 1 below, header right "Checked 2 min ago · Refresh": row "RN · Run Notes · 1.0.0 · Hub upload · unverified · Can: Share run progress with your Hub · 6 KB" with one button "Install". Installed plugins are not listed twice (P21). Footer "Developer mode and local folders: Developer settings".
- Dialogs (4 cards): "Install SoulLink & Nuzlocke?" — "Hub upload · unverified. It can show panels in game and share run progress with your Hub." with a checked box "Also install Pokémon Core 0.3.1 · needed by SoulLink & Nuzlocke", buttons "Cancel", "Install both" (P20); "Turn on SoulLink & Nuzlocke?" — "It needs Pokémon Core, which is off. Both turn on; Pokémon Core reads game memory." buttons "Cancel", "Turn on both"; empty "No plugins installed" — "Home Hub offers 2 plugins: Pokémon Core and SoulLink & Nuzlocke. They are listed below under Available on Home Hub." button "Show available" (P19); error "Could not reach Home Hub" — "Available plugins could not be loaded. Installed plugins keep working." button "Retry". Raw plugin ids never appear in user text.

### 4a-4 Plugin detail (Round 3)
Source: `#4a-4` (157-178). Fixes P21, P22, D16.

- A second-column sheet (460 wide) over the plugin list; header "‹ Plugins" and "✕". Title block: monogram, "SoulLink & Nuzlocke", "0.3.2 · up to date · from Home Hub".
- One primary control: the on/off toggle with the line "Pokémon Core is on too (needed)". Origin: amber pill "Hub upload · unverified" + "Not checked by FrameBeam. Uploaded by Max."
- PERMISSIONS: "Show panels in game", "Share run progress with your Hub". SETTINGS (plugin schema, see SoulLink 5i): rows "Show HUD widget — On", "Encounter rule — First per route ▾", "Duplicate clause — On". RELATIONS: "Needs Pokémon Core · 0.3 or newer · installed 0.3.1"; "Adds: panel tab · HUD widget · tile strip · SoulLink screen · 3 hotkeys".
- Footer: "Installed 14 Oct · 33 KB", button "Hotkeys…", "⋯" holding "Uninstall…" (red, with confirmation, 4b-2); note "⋯ holds “Uninstall…” (red, with confirmation). Run data stays on the Hub."

### 4f-3 Plugin sidebar screen (Round 3)
Source: `#4f-3` (191-203). Fixes P23, P28; open point ba.

- The plugin entry "SoulLink" is nested under Runs in the nav (indent 30), so it no longer collides with the built-in Runs entry. Header: monogram, "SoulLink", "SoulLink & Nuzlocke 0.3.2", amber pill, link "Plugin settings". Max 880.
- "YOUR RUNS · 1": row "Harbor Nuzlocke" + cyan pill "Starting", "Harbor Rally · waiting for Sam · created today 07:12", button "Open run"; note "The run board, map and team open in game when you play Harbor Rally from this run." Empty state: "No SoulLink runs yet. Start a run with SoulLink & Nuzlocke in Runs." with "Go to Runs".

### 4j-3 Player Run detail (Round 3)
Source: `#4j-3` (204-230), join dialog `#4j-3-join` (231-247). Fixes P24, P25, P26, P27.

- Content max 880. "‹ Runs"; title "Harbor Nuzlocke" + cyan pill "Starting" (no ▲); "Created by Max · today 07:12 · SoulLink & Nuzlocke 0.3.2". Actions: primary "Play Harbor Rally", secondary "Invite players", "⋯" with state actions "Start run now", "Finish run…", "Abandon run…" (red). No five-button row.
- PLAYERS · 2: avatar in the player color, name ("Max (you)", "Sam"), game ("Harbor Rally", "—"), status ("Joined" green, "Invited" grey).
- BOARD · "from SoulLink & Nuzlocke": the plugin's `ui.run` board: stats "Deaths 0 · Routes 1 / 35 · Badges 2", table ROUTE | MAX | SAM | LINK (e.g. "Route 101 · Spark · not joined · Open"). Falls back to the plugin name when the board has no title (H31).
- EVENT LOG · 3: time per the date rule ("today 09:02", full value in the tooltip, no seconds, P25), text "Max resent the invite to Sam", source/actor ("Hub", "Desktop-LivingRoom").
- Join dialog (460): title "Join “Tide Hardcore”?" (no nested quotes, P26), "Lena invited you · today 10:00 · 1 of 2 joined", "Run Notes 1.0.0 · Share run progress with your Hub" + amber pill, YOUR GAME card: with the game "● Paper Wizards · Nintendo DS · in your library" and "Join" enabled; without it "Tide & Lantern is not in your library · This run needs Tide & Lantern (Game Boy)." in amber, "Join" disabled and the reason "Join is off until you have the game. Ask a Hub admin to upload it, or add a library folder that contains it." (P27); buttons "Cancel", "Join".

### 4i-3 Hub Plugins (Round 3)
Source: `source/round-3-hub.dc.html#4i-3` (359-391). Fixes H23, H24, H25, H26.

- Header "Plugins" 28/600, "Players on this Hub can install what is listed here.", button "Upload plugin". Rows: monogram built from letters ("PC", "SN"; non-letter words such as "&" are skipped, H24), name + version, amber pill "Hub upload · unverified", meta "Used by SoulLink & Nuzlocke · Read game memory, Read the game file" / "Needs Pokémon Core · Show panels in game, Share run progress with your Hub" (H25), button "Details". Selected row: surface `#f2f8f9` + left bar.
- Catalog status line below the list: "Official catalog · signed · checked today 17:52 · 0 plugins" + "Check now" (one statement, not "checked" and "empty or not loaded" at once, H26); error variant in place of it (amber banner): "▲ Could not load the catalog · <catalog host> did not answer · today 17:52" + "Retry".
- Detail panel (400): "SN · SoulLink & Nuzlocke · 0.3.2 · uploaded by Max · 16 Oct", amber pill, "Not signed by FrameBeam. Players see “Hub upload · unverified” and a warning before they turn it on."; NEEDS "PC · Pokémon Core 0.3 or newer · ✓ on this Hub"; PERMISSIONS · CONFIRMED BY ADMIN ("Show panels in game", "Share run progress with your Hub"); FILE "Size 33.9 KB · SHA-256 fdbb86…5f73 · Copy"; actions "Download file" and "⋯" holding "Remove from Hub…" (red, confirmation); note "Players keep their copy until they uninstall it."

### 4jh-3 Hub Run detail (Round 3)
Source: `#4jh-3` (392-418). Fixes H27, H28, H29, H30, H31.

- List column 300: header "RUNS · 2", filter chips on one line "All | Active | Paused | Finished" plus a separate toggle "Show archived · 1" (H30); items: run name, cyan pill "Starting", meta ("1 of 2 joined · today 07:12").
- Detail head: "Harbor Nuzlocke" + cyan pill "Starting", "Created by Max · today 07:12 · SoulLink & Nuzlocke 0.3.2"; actions "Export JSON" (secondary), "Start now" (primary), "⋯" with "Pause · after start", "Finish run…", "Archive", divider, "Delete run…" (red) (H28).
- Waiting banner: "Waiting for Sam to join · 1 of 2 joined · invited 2 h ago" + "Resend invite" and "Start without Sam" (run creator and admins, decision bh) (H27).
- PARTICIPANTS · 2: avatar, name, game ("Harbor Rally", "—"), status pill ("Joined" green, "Invited" grey), for an invited member "Resend" and "⋯" (holds "Remove invite").
- BOARD · "SoulLink & Nuzlocke · no board title set, plugin name used" (H31); empty "No board yet. It appears after a participant’s Player opens the run."
- EVENT LOG · 3, columns `120 minmax(0,1fr) 180` (time, event, source/device); no "Author" and "Source" columns that repeat "Hub"; time per the date rule, tooltip with the full value, no seconds (H29).

## Building blocks (component sheet `r2-sheet`)
Source `plugins-round-2.dc.html` `#r2-sheet` 389-395, component `block-sheet.dc.html` (dark and light), supersedes the BlockSheet part of `4-sheet`. Normal state on the left, controller focus on the right. Reference only for the catalog: §16.8 lists the building blocks as open; the sheet is the draft the official plugins were drawn with. The prefix `fb.ui.*` in the sheet is drawing text, not API.

Rules for all blocks: plugins choose tone and size set, not colours or pixel values; focus ring `0 0 0 2px <block surface>, 0 0 0 4px #3cbfd8`; static blocks say "Not focusable" and focus moves through their children in reading order; focus lands on the one action in empty and error states; initial focus in dialogs is on Cancel for danger, otherwise the primary.

| Block | Props / states | Rules |
|---|---|---|
| column, row, grid | gap 4, 8, 12, 16, 20 | not focusable |
| tabs | tab labels, selected | LB / RB switch tabs |
| segmented control | 2-3 short options | more options: select |
| radio list | options with sub text, selected | (5f link group) |
| scroll area | rows | has focus when it has no focusable children; right stick scrolls |
| card, section header | static / clickable (focus) | eyebrow + link |
| text, label | size set "ui": title 17/600, heading 15/600, body 13/400, meta 12, eyebrow mono 11 uppercase, value mono | not focusable |
| chip, badge | tones; static badge, focusable filter chip ("Only me", "Open routes") | |
| avatar | player colour, initial | identity only; never cyan, never near danger red |
| icon, image / sprite | 68 x 56, 40 x 30, 24 x 18; alive, dead (grey + ✕, 55 % opacity), missed (dashed), empty; owner dot | nearest-neighbour at whole steps; focus sits on the row or chip around it |
| HP / progress bar | tone by value: > 50 % ok, 20-50 % warning, < 20 % danger; 3-4 high, radius 2 | not focusable |
| stat row | label, value, optional "›" | focus only when it links |
| table, list | sortable header, selected, focused rows | |
| button | primary, secondary, quiet | |
| toggle, select | | schema mapping below |
| text field | empty, filled, focus, error | |
| wizard stepper | done (✓, clickable), current, next | vertical in dialogs, horizontal on wide pages |
| anchored popover | anchor stays pressed | opens on click only; B / Esc returns focus to the anchor |
| form from a schema | boolean → toggle; enum ≤ 3 → segmented control; enum > 3 → select | generated from the manifest schema |
| confirmation dialog | title, text, Cancel, confirm | danger variant |
| toast | tones info, warning, important; origin marker on hover or focus for non-official plugins | max 3 visible |
| empty state, error state | one action | |
| location status card | open (info), encounter now (info), caught (ok), missed / not a route (neutral) | |
| HUD widget container | always dark | see 4e |
| tile strip container | always dark, 20 high, inside the audio ring | see 4e |
| map view | see below | |

Tones (dark / light), the only colour choice for plugins (values also in tokens.md):

| Tone | Dark | Light | Use |
|---|---|---|---|
| info | `#3cbfd8` on `#15262b` | `#1a7f96` on `#ddf1f6` | open, selection, focus, progress |
| ok | `#6fd39a` on `#17291f` | `#2f7d4f` on `#e2f3e8` | caught, synced, HP > 50 |
| warning | `#e5b45a` on `#2a2414`, line `#4a3d1c` | `#8a5a00` on `#f7ecd6`, line `#ead6a8` | unverified, paused, pending, manual, HP mid |
| danger | `#ef8a78` on `#2a1a17` | `#b5402f` on `#f8e4e0` | destructive actions, HP low, field errors; never deaths |

Size set "stream" (OBS and browser-source layouts only, no focus, no interaction): counter 44 mono, display 32, title 20, body 18, meta 16, eyebrow 13 mono; panels `rgba(12,13,14,.86)` on transparent. (The prompt asked for 16-44; the sheet draws 13-44 with the 13 mono eyebrow.)

Light shell of the sheet: bg `#f6f5f2`, cells `#fff`, lines `#e3e1dc`. Dark: bg `#0b0b0c`, cells `#121315`, cards `#16171a`, lines `#26272b`.

## Map view
Source `plugins-4i-4j-sheet.dc.html` `#4-sheet` 122-145 (map states 127-135, notes 138-141), component `region-map.dc.html` (props `w`, `h`, `state`, `filter`, `selected`, `theme`, `radius`). Still valid after round 2 (SoulLink's `5e-map-2` shows it in context). Positions are at location or route level (map ID), not exact overworld position (§16.8). Background image of any aspect (fit to width, supplied by the plugin from the user's own game file), markers in image coordinates.

States (720 x 440 boards): default with all players and the popover above the selected marker; zoomed out with clusters; controller focus on a marker with the hint bar; filter "Open routes" (done routes fade to 35 %); light with filter "Only me"; loading ("map not read yet").

- Markers: avatar 26 circle in the player colour, 2px ring in the map background, pointer down; two players on one spot fan out 14px. Route marker: chip with one 24 x 18 icon per player (owner dot), group number, "open" text when empty.
- Clusters: below 60 % zoom, routes merge per plugin-defined area; chip (30 high, pill) shows count and the icon of the newest catch, label 10/600; a click zooms into the cluster.
- Controls: filter segment top left (All, Only me, Open routes), zoom + / − / fit top right, legend bottom left (collapsible: Caught · dot = owner, Dead, Missed). Drag or right stick pans; wheel and LB / RB zoom.
- Popover: 300 wide, anchored 40px above the marker, flips below near the top edge; rows: owner dot 8, name 46, icon 24, status (ellipsis).
- Keyboard / controller: Tab or D-pad moves to the nearest marker in that direction, A / Enter opens the popover, B closes it, Y opens filters; focus ring = the standard ring around the chip.

## SoulLink artboards (not specified here)
5a-5i, 5e-5g and the Round 2 ids 5ah-2, 5b-2, 5b-1280-2, 5c-2, 5d-2, 5e-map-2, 5e-map-light-2, 5e-routes-2, 5e-log-2, 5f-2-2, 5f-3-2 are specified in the SoulLink repository. They use the platform blocks above; the in-game ones stay dark (the 5b light variant was removed), the run screen and the Hub wizard are light.
