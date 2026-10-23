# ADR 0026: UI gaps and Multiview (0.14)

- Status: accepted
- Date: 2026-10-12
- Deciders: Fabio, via the Claude Design "Round 3" handoff (decisions 20-23 in [design/legacy/decisions.md](../design/legacy/decisions.md)). Details marked "default" are working defaults that Fabio can still change.
- Supersedes in part: [ADR 0014](0014-player-ui-pass.md) D4 (Esc) and refines D13 (Default Multiview).

## Context

The 0.14 UI scan (`docs/design/`, Round 3) found that Esc does not pause in fullscreen, that viewers cannot see a paused game, and that Multiview uses Grid 2x2 with an empty slot for three Sessions. Round 3 specifies the screens ([design/README.md](../design/legacy/README.md#round-3-screens-2026-10-12-014-ui-gaps-and-multiview)).

## Decisions

### D1 Esc and fullscreen

Esc always pauses the game and opens the pause overlay, in a window and in fullscreen. The overlay has "Resume" and "Leave fullscreen". F11 toggles fullscreen and is the only key that leaves fullscreen. This replaces the fixed role of ADR 0014 D4 ("leave fullscreen, else pause"). Gamepad: Select + Start pauses, Select + Y toggles fullscreen (design 3f-4, 3y-2).

### D2 Multiview layouts

The layout "Side by side" is renamed "Row" (it stays the name of the DS screen layout). Row is used for 2-3 Sessions, Grid 2x2 from 4 Sessions, PiP is unchanged. Grid 2x2 is disabled ("Grid 2×2 · from 4") below 4 Sessions and never shows an empty slot. The Default Multiview options become PiP, Row, Grid 2x2 (refines ADR 0014 D13).

### D3 Paused state for viewers

Viewers see that a game is paused: "Paused by <name>" on the frozen remote tile in Row, Grid, PiP and fullscreen. This overrides design open point v. The host sends a small state signal (paused yes/no and the pausing user's display name) to its viewers. It carries state only, no picture or input. If it is an optional field of an existing message, no protocol version bump is needed. The transport choice (existing control channel or session message) is decided in the implementation PR, not here.

### D4 Cross-cutting UI rules

Dates, row actions, pill colors (amber = needs your judgment, cyan = information or in progress, green ok, grey neutral, red destructive text only), add-to-list pattern, content widths, plurals and the responsive Hub follow sheet 0-2 as recorded in [design/legacy/decisions.md](../design/legacy/decisions.md) decisions 20-23 and the open-point answers bg-bk.

### D5 Small rules

- Toggles sit right in the control column everywhere, also in the Connect screen 3a-3 ("one look"; finding P4).
- The save slot is displayed as "Main" in the Hub and the Player; the stored slot name is unchanged.

## Consequences

- Implementation in the 0.14 work packages (`docs/roadmap.md`): Hub pass, Player library/settings/plugins/runs pass, Player in-game and Multiview pass.
- The pause signal (D3) extends the Session protocol with an optional field; `protocol/` documents it in the implementation PR.
