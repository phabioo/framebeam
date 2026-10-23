# Play and plugins (sheet 4)

Canvas page "4 Play and plugins": the screens Round 5 did not replace, redrawn in the Uzume design from the Round 4 and SoulLink content: library and game page (3c), the running game (3g), Multiview (3r), Emulation and Controllers (3e, 9a), Settings › This PC (9b, 6g, 9c, 6l, 6w, 6r), the plugin developer console (4d) and SoulLink & Nuzlocke (5a to 5i). Where Round 5 changed a flow (Runs inside Play, no plugin rows in the sidebar, Settings with This PC / Server) Round 5 wins. Tokens and components: [design-system.md](design-system.md).

Rules that hold on every screen of this sheet:

- **Roadmap mapping.** These boards mostly redraw existing player screens. The restyle of an existing screen falls to 0.18 package 3 (design system in the app); the "Built in" line names the milestone that adds what the board shows beyond that (rows, tabs, states). Waves: 0.19 player comfort and library, 0.20 emulation, 0.21 together, 0.22 randomizer, 0.23 plugins, 0.24 platforms. Plugin screens are built in the plugin repositories; the app side is the run-tab slot of 0.18 package 9.
- **Running game.** `RunHeader` (56 px) + `GameStage` (empty stage: only Toasts, the F3 diagnostics block and the hold-Select hotkey sheet may draw on it) + 320 px side panel with `RunTabs`: Session · People · Chat · Saves (states live in Saves), in a run Run · Chat · Saves · plugin tab. Diagnostics left the tabs for **F3**. Notices are Toasts at the **bottom left of the stage**; persistent ones (Paused, Rewinding) drop the close mark.
- **Overlays.** Pause, load-state and hold-Select use `Dialog` over a scrim on the stage only; the run header stays visible.
- **Gamepad hotkeys** use one modifier, **Select** (tapped alone it goes to the game; held it opens the hotkey sheet; no Guide button). Keyboard and gamepad rows are always shown together.
- **Plugin content** is marked with a Mono "DRAWN BY SOULLINK" line over a rule. Creature sprites are placeholder boxes with two letters in the boards; the plugin draws real sprites from the player's own game file at runtime. HP is a thin `Progress`.
- Boards are 1280 x 800, Sumi with a Kinari twin.

## Index

### 4 Play and plugins · Index

Boards: [`S4Index`](../source/uzume/S4Index.dc.html) (1280 x 1660). Not built.

Index of sheet 4 (95 boards) with the rules above and 18 "did not fit" items; see [decisions.md](decisions.md).

## 3c Library and game page

### 3c-0 Library · continue and collections

Boards: [`S3c0`](../source/uzume/S3c0.dc.html) · [`S3c0Kinari`](../source/uzume/S3c0Kinari.dc.html). Built in: 0.19 (player comfort and library).

Library `10 GAMES · HOME SERVER`, `SearchField`, primary Add games.... Above the table: **Continue** (last game with save, e.g. "GBA · Main v14 · Desktop-LivingRoom" and a Continue button) and two RECENT entries. Collections as `FilterChip`s (All · 10, Favourites · 3, Couch co-op · 4, Nuzlocke · 2, + Collection) and a sort `Select` (Recently played). The Round 4 tile grid became the table (no star on `TableRow`, so favourites are a chip plus a Favourite button on the game page).

### 3c-1 Game page · overview

Boards: [`S3c1`](../source/uzume/S3c1.dc.html) · [`S3c1Kinari`](../source/uzume/S3c1Kinari.dc.html). Built in: 0.19 (player comfort and library).

Header: back to Library, title, `Button` Favourite (toggle), primary **Play · Main v14**; meta `GBA · mGBA 0.10.3 · ON HOME SERVER`. Tabs Overview / Saves and states. `GameCover` plus facts (play time and last played, sessions, save "Main v14 · synced", states "3 of 4 slots", collections with + Collection). "How to start": Play and share..., Play from a state..., Settings for this game... (ghost, with a "3 changes" tag). Screenshots (8) as a strip with dates.

### 3c-2 Game page · saves and states

Boards: [`S3c2`](../source/uzume/S3c2.dc.html) · [`S3c2Kinari`](../source/uzume/S3c2Kinari.dc.html). Built in: 0.19 (player comfort and library), 0.20 (emulation).

Two side-by-side sections. **Save** ("WHAT THE GAME WRITES"): slot `Select`, version list (v15 Current ok tag, v14 Snapshot "by SoulLink · before Gym 3", older versions with Restore; "A new version arrives each time the game saves. Restore makes an older one current; newer ones stay."). **States** ("WHOLE EMULATOR AT ONE MOMENT"): slots A (auto), 1 to 4 with name, time and game time, Play from state; a state from an older core carries a danger tag "Older core" and the action "Try to load". States are not the save: "Loading one never changes the save until the game saves again."

### 3c-3 Settings for this game

Boards: [`S3c3`](../source/uzume/S3c3.dc.html) · [`S3c3Kinari`](../source/uzume/S3c3Kinari.dc.html). Built in: 0.19 (player comfort and library).

`Dialog` lg (it was a sheet from the right in Round 4), label "GBA · mGBA · 3 changes from your defaults". Tabs (Display · 2 changed). `SettingsRow`s with per-row state: "Uses your default (On)" or "Changed for this game" with the default shown and a reset; Integer scaling, Aspect ratio, Run-ahead, Speed-up speed. "Reset goes back to your default for GBA in Emulation."

### 3c-4 Library · states

Boards: [`S3c4`](../source/uzume/S3c4.dc.html) · [`S3c4Kinari`](../source/uzume/S3c4Kinari.dc.html). Built in: 0.19 (player comfort and library).

Six states: no favourites (empty state; favourites are per person and follow you to every PC), empty collection (Rename, Delete collection...; deleting a collection never deletes games), first start (no Continue until played once; sort falls back to Name), save from another PC (Continue gets the newest save first with a progress strip; never starts an older local save without asking), add to collection (checkboxes + New collection), collection menu.

## 3g Running game

All 3g boards share the frame: `RunHeader` with back to Library, title, system, sub ("Shared · 2 watching" or "Not shared"), watcher avatars, header buttons Pause, Speed-up, Volume, Multiview, Panel (toggled), `⋯` menu.

### 3g-1 In game · Session tab

Boards: [`S3g1`](../source/uzume/S3g1.dc.html) · [`S3g1Kinari`](../source/uzume/S3g1Kinari.dc.html). Built in: 0.19 (player comfort and library), 0.21 (together).

`RunTabs` Session · People · Chat · Saves. Session: **Sharing** (`Segmented` "Who can watch": Home server / Invite only / Private; who is watching; Invite someone..., Stop sharing), **Save** (Synced tag, "Saved by the game 4 min ago · on Home server", Save snapshot F5, Saves and states), **Quit** (Quit game, "Saves and syncs first."). Hints F4 Panel, F3 Diagnostics.

### 3g-2 In game · Saves tab

Boards: [`S3g2`](../source/uzume/S3g2.dc.html) · [`S3g2Kinari`](../source/uzume/S3g2Kinari.dc.html). Built in: 0.20 (emulation).

States list "3 OF 4 · ON HOME SERVER" (A auto, 1 to 3 with names, New tag, empty slot 4), actions Save to 2 (primary), Load 2, `⋯` menu per state. "States are not your save. Main v14 changes only when the game saves." Hints F6 Save, F8 Load, Shift+F6 Next slot.

### 3g-3 In game · paused

Boards: [`S3g3`](../source/uzume/S3g3.dc.html) · [`S3g3Kinari`](../source/uzume/S3g3Kinari.dc.html). Built in: 0.19 (player comfort and library).

`Dialog` over a scrim on the stage only: "Paused" with the game and play time; "Your game is paused. Lena and Jonas see 'Paused' on your tile." Shows SAVE (Main v14 · synced 4 min ago) and STATE (Slot 2 · just now).

### 3g-4 In game · quick menu

Boards: [`S3g4`](../source/uzume/S3g4.dc.html) · [`S3g4Kinari`](../source/uzume/S3g4Kinari.dc.html). Built in: 0.19 (player comfort and library), 0.20 (emulation).

The `⋯` `Menu` holds what the header has no button for. Group "This game": Save state F6, Load state F8, Take screenshot F12, Settings for this game..., Reset <game>.... Group "Together": Ready check..., Pause for everyone Shift+Esc. Group "Help": Hotkeys..., Report a problem.... This resolves the review question whether the quick menu duplicates the Session tab: it adds these actions.

### 3g-5 In game · load a state

Boards: [`S3g5`](../source/uzume/S3g5.dc.html) · [`S3g5Kinari`](../source/uzume/S3g5Kinari.dc.html). Built in: 0.20 (emulation).

Saves tab plus `Dialog` "Load state 1 from today 17:02?": replaces the running game (play time shown); progress is kept as Auto first; undo for 6 s or load Auto any time. Checkbox "Don't ask again (F8 never asks)".

### 3g-6 In game · diagnostics

Boards: [`S3g6`](../source/uzume/S3g6.dc.html) · [`S3g6Kinari`](../source/uzume/S3g6Kinari.dc.html). Built in: 0.20 (emulation).

F3 opens the **GameStage diagnostics block** in the top right corner: Mono label "Diagnostics" with a Copy action and label/value rows (Core, FPS, Frame, Run-ahead, Input lag, Audio, Dropped with a "!" warning, Stream with viewers, Net direct/relayed with latency). One of only three overlays the empty stage allows.

### 3g-7 In game · Chat tab

Boards: [`S3g7`](../source/uzume/S3g7.dc.html) · [`S3g7Kinari`](../source/uzume/S3g7Kinari.dc.html). Built in: 0.21 (together).

In a run: header sub "Run: Ember Route", `RunTabs` Run · Chat · Saves · SoulLink. "RUN CHAT · EMBER ROUTE · 4 PEOPLE": day separator, `ChatMessage`s with person colour and time, SoulLink event lines in the log, @mentions, quick replies (Down! · 1, Gym next · 2, Wait · 3), composer (Enter sends). Hints Enter Send, Esc Back to game.

### 3g-8 In game · NDS screen layout

Boards: [`S3g8`](../source/uzume/S3g8.dc.html) · [`S3g8Kinari`](../source/uzume/S3g8Kinari.dc.html). Built in: 0.19 (player comfort and library).

Layout menu (F7) for NDS: Stacked, Side by side, Large top / small bottom, Top screen only, Bottom screen only; Swap screens (Shift+F7); Bottom screen in its own window. A Toast confirms ("Layout: Side by side").

### 3g-9 In game · volume

Boards: [`S3g9`](../source/uzume/S3g9.dc.html) · [`S3g9Kinari`](../source/uzume/S3g9Kinari.dc.html). Built in: 0.19 (player comfort and library), 0.21 (together).

`Popover` "Sound" anchored to the header volume button: `Slider`s My game (72), Voice (70), Plugin sounds (40, muted), line "Muted in the background: on", actions Mute all and Sound settings. Hints ← → Volume, ↑ ↓ Next, B Close. Voice and plugin sound channels arrive with 0.21.

### 3g-10 In game · hold Select

Boards: [`S3g10`](../source/uzume/S3g10.dc.html) · [`S3g10Kinari`](../source/uzume/S3g10Kinari.dc.html). Built in: 0.19 (player comfort and library).

The **hotkey sheet** overlay on the stage ("Hold Select +", release Select to close): Start Pause, R Save state, L Load state, Y Screenshot, X Mute, ↑ ↓ Volume, → Screen layout, R2 Speed-up, L2 Rewind, R3 Microphone, B Fullscreen, ← Split (Splits Timer plugin). Select + ↓ cannot be a plugin hotkey because Select + ↑ ↓ is volume; the plugin got a free combination (Select + ←). Plugin hotkeys never take a taken combination.

### 3g-11 In game · notices

Boards: [`S3g11`](../source/uzume/S3g11.dc.html) · [`S3g11Kinari`](../source/uzume/S3g11Kinari.dc.html). Built in: 0.19 (player comfort and library), 0.20 (emulation), 0.21 (together).

Six kinds of Toast, bottom left of the stage: volume ("Volume 72 %", replaces itself, 1.5 s), states ("State 2 saved"; "State 1 loaded" with Undo, 6 s), rewind and speed ("Rewinding · 18 s left", "Speed-up 4x", stay while held), together ("Lena paused for everyone" stays until resumed; "Jonas joined voice"), run rules ("Save states are off in this run · rule set by Max", "Rewind is off in this run", 3 s each time the key is pressed), plugins (name the plugin: "SoulLink saved a snapshot · before Gym 3", "Creature Editor changed game memory" with Undo).

### 3g-12 In game · People tab

Boards: [`S3g12`](../source/uzume/S3g12.dc.html) · [`S3g12Kinari`](../source/uzume/S3g12Kinari.dc.html). Built in: 0.21 (together).

"3 IN THIS SESSION": `Avatar` with the **voice mark** (speaking bars, mic off, muted by host; never colour alone), role (HOST / VIEWER), connection (Direct / Relayed, latency, quality received: "gets High", "picked Medium"). Host sends one stream at e.g. 6.0 Mbit/s. Actions Ready check..., Invite someone.... Hints Shift+M Mic, Select + R3 Mic (gamepad).

## 3r Multiview

All Multiview boards: `RunHeader` "Multiview · Home server · 4 people" with an audio label ("Audio · Lena"), header buttons Add tile, Mixer (`Popover` with a `Slider` per person led by the person square and a "Mix all tiles" `Toggle`), Layout `Segmented` (2 · 3 · 2 x 2 · Focus), Leave. Tiles are `MultiviewTile`: 40 x 3 px person rule and 8 px square with the name, system chip, game name, a right-side status (YOU, MUTED, PAUSED BY LENA, ROUTE 110 · 4 ALIVE, LOW · RELAYED), the Yamabuki AUDIO frame and tag on the one tile you hear, voice marks. Built in: 0.21 (together) for voice marks and mixer; the layouts restyle the existing Multiview.

### 3r-1 Multiview · 2 tiles

Boards: [`S3r1`](../source/uzume/S3r1.dc.html) · [`S3r1Kinari`](../source/uzume/S3r1Kinari.dc.html). Built in: 0.21 (together).

Two tiles side by side: your own (YOU) and another person's session with the speaking mark.

### 3r-2 Multiview · 3 tiles

Boards: [`S3r2`](../source/uzume/S3r2.dc.html) · [`S3r2Kinari`](../source/uzume/S3r2Kinari.dc.html). Built in: 0.21 (together).

Three tiles; one tile muted locally (right side MUTED).

### 3r-3 Multiview · 2 × 2

Boards: [`S3r3`](../source/uzume/S3r3.dc.html) · [`S3r3Kinari`](../source/uzume/S3r3Kinari.dc.html). Built in: 0.21 (together).

Four tiles in a grid; states PAUSED BY LENA and mic off visible on tiles.

### 3r-4 Multiview · focus and strip

Boards: [`S3r4`](../source/uzume/S3r4.dc.html) · [`S3r4Kinari`](../source/uzume/S3r4Kinari.dc.html). Built in: 0.21 (together).

Focus puts the audio tile big and the rest in a strip; keys 1 to 4 pick the audio tile. Strip tiles show run info (ROUTE 110 · 4 ALIVE), MUTED BY HOST voice mark, LOW · RELAYED.

### 3r-5 Multiview · states

Boards: [`S3r5`](../source/uzume/S3r5.dc.html) · [`S3r5Kinari`](../source/uzume/S3r5Kinari.dc.html). Built in: 0.21 (together).

Six states: add a tile (list "Running sessions · 3 of 4 tiles" with Added tags and Add; private sessions are not listed; sound plays from one tile), full (Remove a tile...), nobody sharing (empty), tile frame legend (person rule + square always with the name, AUDIO frame, VOICE bars / MIC OFF / MUTED BY HOST, right-side text), waiting (ensō "Waiting for Sam", Stop waiting removes the tile, right side NOT STARTED), layout switch (Segmented in the header).

## Emulation and Controllers

### 3e-1 Emulation · NDS · Display

Boards: [`S3e1`](../source/uzume/S3e1.dc.html) · [`S3e1Kinari`](../source/uzume/S3e1Kinari.dc.html). Built in: 0.19 (player comfort and library), 0.20 (emulation).

PLAY group page "Emulation" ("Saved on this PC only. Cores come from Systems & cores."). Second column "Defaults" with a changed count: GB · mGBA, GBC · mGBA, GBA · mGBA, NDS · melonDS DS (2 changed), 3DS · Azahar. Page: system, core and version, "Reset all to default" (ghost), `Tabs` (Display ...). Rows the core offers: Picture (Integer scaling `Toggle`, Aspect ratio `Segmented`, Screen layout `Select`), Run-ahead and rewind (Run-ahead 0 / 1 / 2 frames with "CPU +35 %", Rewind length "About 180 MB memory at 30 s"), Renderer (melonDS DS offers OpenGL only). Changed rows show the changed marker and a reset.

### 3e-2 Emulation · 3DS · Display

Boards: [`S3e2`](../source/uzume/S3e2.dc.html) · [`S3e2Kinari`](../source/uzume/S3e2Kinari.dc.html). Built in: 0.19 (player comfort and library), 0.20 (emulation), 0.24 (platforms).

Only rows the core offers: Run-ahead is off for 3DS ("too heavy to run twice"), Rewind length with a danger tag "High memory" (about 1.1 GB at 10 s), Renderer OpenGL / Vulkan with a "Next start" tag (Vulkan lands in 0.24). One-screen systems (GB, GBC, GBA) have no Screen layout row and no Renderer (mGBA draws in software); Aspect "Core" is 10:9 for GB and 3:2 for GBA.

### 9a-1 Controllers · mapping

Boards: [`S9a1`](../source/uzume/S9a1.dc.html) · [`S9a1Kinari`](../source/uzume/S9a1Kinari.dc.html). Built in: 0.19 (player comfort and library).

PLAY group page "Controllers" ("Mapping is saved per controller on this PC."). Second column: connected controllers with player number (P1), Keyboard, "Add a controller...". Page: `Tabs` Mapping / Gamepad; `Select` Profile (Standard) and System (GBA), Reset mapping. Table game button × gamepad (`GamepadHint`) × keyboard (`Kbd`); "Press a button..." state for the row being mapped (Esc cancels). Note: Select is also the hotkey modifier. Built from the AppNav item, the pad map and the hotkey rules (the old files had no such screen).

### 9a-2 Controllers · gamepad

Boards: [`S9a2`](../source/uzume/S9a2.dc.html) · [`S9a2Kinari`](../source/uzume/S9a2Kinari.dc.html). Built in: 0.19 (player comfort and library).

Gamepad tab rows: Left stick as D-pad (`Toggle`), Dead zone (15 %), Rumble, Touch screen for NDS and 3DS (right stick moves a pointer, R3 taps), Player (which player this controller is), Button labels in hints (Automatic or a pad type).

## Settings › This PC

The second column (This PC): My account (name), Appearance, Audio & video, Hotkeys, Library folders, Plugins, Voice, Notifications, Storage, Servers, Updates, Diagnostics (with counts); "Only affects this PC and your account." All rows are `SettingsRow`; help in the `HelpColumn` of the open column.

### 9b-1 Settings › This PC › Hotkeys

Boards: [`S9b1`](../source/uzume/S9b1.dc.html) · [`S9b1Kinari`](../source/uzume/S9b1Kinari.dc.html). Built in: 0.19 (player comfort and library).

Gamepad modifier row ("One modifier for every gamepad hotkey. Tapped alone, Select goes to the game."). Table action × keyboard × gamepad: Pause Esc / Select + Start; Speed-up Space / Select + R2; Volume + − / Select + ↑ ↓; Mute M / Select + X; Save state F6 / Select + R; Load state F8 / Select + L; Rewind (hold) Backspace / Select + L2; Screenshot F12 / Select + Y; Screen layout F7 / Select + →; Panel F4 / Select twice; Fullscreen F11 / Select + B; Microphone Shift+M / Select + R3. A plugin hotkey that asks for a taken combination is **moved** to a free one and flagged with a "Moved" tag (Split: asked Select + ↓, got Select + ←). Microphone is Select + R3 everywhere.

### 6g-1 Settings › This PC › Audio & video

Boards: [`S6g1`](../source/uzume/S6g1.dc.html) · [`S6g1Kinari`](../source/uzume/S6g1Kinari.dc.html). Built in: 0.19 (player comfort and library).

Audio: Output (`Select`), Mute when in the background (`Toggle`; sessions you watch stay audible), Volume steps (`Segmented` 5 %), Show volume on screen. Video: Fullscreen on (monitor `Select`; F11), Vertical sync.

### 9c-1 Settings › This PC › Appearance

Boards: [`S9c1`](../source/uzume/S9c1.dc.html) · [`S9c1Kinari`](../source/uzume/S9c1Kinari.dc.html). Built in: 0.18 package 3 (design system in the app).

Theme (`Segmented` Sumi / Kinari; "Sumi is dark, Kinari light."), Text size (Default ...), Reduce motion (no fades, no ensō animation, no expanding widgets), Language (English; "More languages come with community translations."), Hints at the bottom (`Select`: Last used input / Keys / Gamepad).

### 6l-1 Settings › This PC › Storage

Boards: [`S6l1`](../source/uzume/S6l1.dc.html) · [`S6l1Kinari`](../source/uzume/S6l1Kinari.dc.html). Built in: 0.19 (player comfort and library).

Label `D:\UZUME · MEASURED 18:30`, Measure again. Storage bar block (total, free space, kinds as ink tints: Downloaded games, Save cache, Save states, Screenshots, Cores and firmware). Clean-up suggestions with sizes: games not played in 60 days (they download again when you play), states of games you deleted, unused cores; each with a confirmation. Never offers saves.

### 6w-1 Settings › This PC › Voice

Boards: [`S6w1`](../source/uzume/S6w1.dc.html) · [`S6w1Kinari`](../source/uzume/S6w1Kinari.dc.html). Built in: 0.21 (together).

Label `THROUGH HOME SERVER`. Rows: Microphone (`Select` + level `Progress` with the voice-activation threshold line), Talk (`Segmented` Push-to-talk / Voice activation), Push-to-talk key (V, or Select + R3 hold; with voice activation the same keys turn the mic on or off), Noise suppression, Lower game volume while someone speaks (halves game and tiles), Join voice automatically (runs and invite-only sessions).

### 6w-2 Voice · states

Boards: [`S6w2`](../source/uzume/S6w2.dc.html) · [`S6w2Kinari`](../source/uzume/S6w2Kinari.dc.html). Built in: 0.21 (together).

Six states of the header mic control: push-to-talk (Hold V), talking (Live; your name gets the speaking mark), mic off (others see "mic off"), muted by host (only the host can undo it), no microphone (you can listen), blocked by the OS (error; Open Windows settings; on Linux the PipeWire prompt appears again).

### 6r-1 Settings › This PC › Notifications

Boards: [`S6r1`](../source/uzume/S6r1.dc.html) · [`S6r1Kinari`](../source/uzume/S6r1Kinari.dc.html). Built in: 0.21 (together).

Per kind a `Segmented` Toast / List only: Invites, Sessions started, Run events, Updates, Server problems (admins only). Toggles: Quiet in fullscreen (only @mentions show; the rest waits in the bell), Sound for toasts.

### 6r-2 Notifications · bell

Boards: [`S6r2`](../source/uzume/S6r2.dc.html) · [`S6r2Kinari`](../source/uzume/S6r2Kinari.dc.html). Built in: 0.21 (together).

`Popover` "Notifications" opening from the AppNav bell: entries with text and a Mono line (time · action): invite (WATCH), run event (OPEN RUN), app update (WHAT'S NEW), session started (WATCH), server problem (OPEN STATUS).

## 4d Plugin developer console

### 4d-1 Plugins · developer console

Boards: [`S4d1`](../source/uzume/S4d1.dc.html) · [`S4d1Kinari`](../source/uzume/S4d1Kinari.dc.html). Built in: 0.23 (plugins).

Settings › This PC › Plugins with developer mode on. Title "Developer console"; actions Load unpacked..., primary Reload; `Select` Plugin with an "Unpacked" tag and its folder; error line "Paused after an error. Your game keeps running." with Reload and resume. **Permissions** granted on load (ok tags; "Not granted: write memory"). **Log**: level `FilterChip`s (Error, Warn, Info, Debug with counts), Pause log, table time, level `Tag`, message, newest first.

### 4d-2 Developer console · states

Boards: [`S4d2`](../source/uzume/S4d2.dc.html) · [`S4d2Kinari`](../source/uzume/S4d2Kinari.dc.html). Built in: 0.23 (plugins).

Six states: load unpacked (folder + manifest check), manifest problem (nothing installed), permissions on load (unpacked plugins ask every time), paused after errors (three errors within 10 s pause the plugin; Toast "Open console"), reloaded (Ctrl+R, plugin state kept), developer mode off (`Toggle`; while on, the game header shows a Developer mode tag and Ctrl+Shift+D opens the console).

## 5 SoulLink & Nuzlocke

The run plugin is "SoulLink & Nuzlocke" (`uzume.soullink`, on `uzume.soullink-core`) with a mode (SoulLink or Nuzlocke). All plugin content sits under "DRAWN BY SOULLINK". The screens are drawn by the plugin through declarative building blocks; they are built in the plugin repositories with the 0.18 package 9 run-tab slot and the 0.23 plugin work.

### 5a-1 New run · SoulLink rules

Boards: [`S5a1`](../source/uzume/S5a1.dc.html) · [`S5a1Kinari`](../source/uzume/S5a1Kinari.dc.html). Built in: 0.18 package 9 (runs without plugin), 0.23 (plugins).

Step 2 of 3 of the Runs "Start a run" dialog (lg), label "SOULLINK MODE". Games: a `Select` per person from each person's own library (hints "In your Library", "Twin version · mixes fine", "Picks when joining"). Rules (defaults from Settings › Plugins) as `Checkbox`es: First encounter only, Dupes clause, Shiny clause, Hordes count as first, Linked pairs differ in type, Only linked pairs in the team, Nickname required, Level caps as hints.

### 5a-2 New run · summary

Boards: [`S5a2`](../source/uzume/S5a2.dc.html) · [`S5a2Kinari`](../source/uzume/S5a2Kinari.dc.html). Built in: 0.18 package 9 (runs without plugin), 0.23 (plugins).

Step 3 of 3 "Start Ember Route?": creates the run on the server and a save slot for each person. Summary rows RUN (name · mode), PEOPLE (with game version), RULES, SAVES ("New slot 'Run: Ember Route' per person · Main stays untouched"). Warning when a person has not joined yet (start now and they join later, or wait).

### 5b-1 In game · SoulLink tab

Boards: [`S5b1`](../source/uzume/S5b1.dc.html) · [`S5b1Kinari`](../source/uzume/S5b1Kinari.dc.html). Built in: 0.23 (plugins).

Plugin tab of the in-game panel: run name, Open run; current route with the "Encounter now" tag, the wild creature, "first on this route", Doesn't count (Ctrl+D); the other runners' state on this route (caught, no encounter yet, missed · fled); YOUR TEAM · 5 / 6 with HP `Progress` and linked partners (two-letter placeholders); footer "Graveyard 3 · Badges 4 / 8 · next cap Lv 24". Hints Ctrl+D, Ctrl+H (HUD).

### 5b-2 SoulLink · panel states

Boards: [`S5b2`](../source/uzume/S5b2.dc.html) · [`S5b2Kinari`](../source/uzume/S5b2Kinari.dc.html). Built in: 0.23 (plugins).

Location/route status tags (No encounter yet, Encounter now, Caught <name>, Missed, Not a route); plugin paused after an error (Restart plugin, Details; the game keeps running); HUD widget (compact corner "SL · Route 104", expands for 3 s on a death, off with Reduce motion); a friend's tile in Multiview (selecting it shows their team read only); notices (death, catch, new route); scope note that everything under "Drawn by SoulLink" comes from the plugin.

### 5e-1 SoulLink run · map

Boards: [`S5e1`](../source/uzume/S5e1.dc.html) · [`S5e1Kinari`](../source/uzume/S5e1Kinari.dc.html). Built in: 0.23 (plugins).

Run page, tab SoulLink, summary line `DEATHS 7 · ROUTES 9 / 31 · BADGES 4`, inner tabs Map · Routes · Team & box · Graveyard · Event log. Map on the stage: route points and hairlines, land comes from the player's own game file and is not drawn (label "MARISOL · MAP FROM YOUR GAME FILE"); a `Segmented` Show (All people / one person); legend (square = caught by owner, ✕ dead, dashed = missed, number = link group); Zoom in, Fit. A route point opens a `Popover` route card (people, state, "A group forms when everyone has caught here or the route is closed."). Hints D-pad next route · A open · L / R zoom.

### 5e-2 SoulLink run · routes

Boards: [`S5e2`](../source/uzume/S5e2.dc.html) · [`S5e2Kinari`](../source/uzume/S5e2Kinari.dc.html). Built in: 0.23 (plugins).

Routes table with `FilterChip`s (All · 12, Open · 3, Complete · 9) and Add by hand...: route × one column per person (creature placeholder + nickname, open, missed, dead) × link group × status `Tag` (Linked, Group dead, Open, Gift · no link, Broken).

### 5e-3 SoulLink run · team and box

Boards: [`S5e3`](../source/uzume/S5e3.dc.html) · [`S5e3Kinari`](../source/uzume/S5e3Kinari.dc.html). Built in: 0.23 (plugins).

Per person (avatar, game version, current route): TEAM (up to 6, placeholder, nickname, level) and BOX (count and small placeholders).

### 5e-4 SoulLink run · graveyard

Boards: [`S5e4`](../source/uzume/S5e4.dc.html) · [`S5e4Kinari`](../source/uzume/S5e4Kinari.dc.html). Built in: 0.23 (plugins).

Table creature (✕ placeholder, nickname, species and level), owner, route, cause ("fainted to ...", "linked partner died", "undone, then fainted again"), when, link group, **Undo...**. Linked deaths are grouped by time. Undo asks for a reason that everyone sees in the log.

### 5e-5 SoulLink run · event log

Boards: [`S5e5`](../source/uzume/S5e5.dc.html) · [`S5e5Kinari`](../source/uzume/S5e5Kinari.dc.html). Built in: 0.23 (plugins).

Filters All / Automatic / Manual · 6, people `Select`, Export. Table when, who, event, kind `Tag` (Automatic neutral, Manual accent), source ("Lena's app", "by Sam · seen by all", "Home server"). Manual corrections are always logged with a name and can be undone by others.

### 5f-1 SoulLink · corrections

Boards: [`S5f1`](../source/uzume/S5f1.dc.html) · [`S5f1Kinari`](../source/uzume/S5f1Kinari.dc.html). Built in: 0.23 (plugins).

Five dialogs: "This encounter doesn't count?" (reason `Segmented`: Dupe ...), "Undo the death of link group 2?" (reason field shown in the log; danger button), change link group (`Select` Move to; "Moving Sprig also changes who dies with it."), add by hand (species `Select`, nickname, where: Team / Box; "Replaces 'missed'. Marked manual for everyone."), mark a route as missed for you. Rule: every change says who sees it; nobody's change is silent.

### 5g-1 SoulLink · stream view

Boards: [`S5g1`](../source/uzume/S5g1.dc.html) · [`S5g1Kinari`](../source/uzume/S5g1Kinari.dc.html). Not built as a web page: Uzume keeps no browser components (Fabio, 2026-10-22). Content reference for a possible app overlay window (open).

The OBS overlay: a 1920 x 1080 transparent page shown at 61 % on the stage with a dashed frame (label "READ-ONLY LINK · REVOCABLE IN THE RUN"). Four team rows (avatar, route, creature placeholders, ✕ for dead), DEATHS counter, LAST EVENTS, run name and mode with badges. "GAME CAPTURE SHOWS THROUGH · TRANSPARENT IN OBS". Dropped as a server web page on 2026-10-22; OBS captures the app window instead.

### 5i-1 Settings › This PC › Plugins › SoulLink & Nuzlocke

Boards: [`S5i1`](../source/uzume/S5i1.dc.html) · [`S5i1Kinari`](../source/uzume/S5i1Kinari.dc.html). Built in: 0.23 (plugins).

Plugin settings (label `OFFICIAL · 1.4.0 · FROM HOME SERVER`, DRAWN BY SOULLINK): Default mode (for new runs), Linked pairs differ in primary type (`Toggle`), HUD widget (Compact ...), Notices while playing (`Select`, "Deaths in your link groups always show."). **Game data** ("STAYS ON THIS PC"): table game file, map status `Tag` (Read / No map), routes, species, read time, Read again. Data is read from the player's own game files; nothing is uploaded; without a map the Routes tab comes first.
