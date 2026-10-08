# ADR 0018: Player speed-up (fast-forward)

- Status: Proposed
- Date: 2026-10-08
- Deciders: Fabio

## Context

Fabio asked for SpeedUp/FastForward in the FrameBeam Player, visible when the emulator core offers it natively. The user-facing name is "Speed-up"; "fast-forward" stays the technical (libretro) term. Libretro has no capability flag for it; it is a frontend feature. The core only learns about it through environment calls (`RETRO_ENVIRONMENT_GET_FASTFORWARDING`, `RETRO_ENVIRONMENT_GET_THROTTLE_STATE`) and can restrict it with `RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE`. Scope: roadmap 0.7.x ([roadmap.md](../roadmap.md)); emulation background in [05-emulation.md](../architecture/05-emulation.md).

## Decisions

### D1 Frontend feature, availability from the core

- The Player runs the core unthrottled up to the selected speed and answers `GET_FASTFORWARDING` and `GET_THROTTLE_STATE`.
- A core can forbid it with `SET_FASTFORWARDING_OVERRIDE` (`inhibit_toggle`).
- "Supported natively" therefore means: the backend is libretro and the core did not inhibit it. Otherwise the control is hidden.

### D2 Speed

- Selectable: 1.5x, 2x, 3x, 4x, 6x, 8x. Default 2x.
- The user's choice wins over a ratio the core gives via the override. A core's inhibit still hides the feature.
- Frames to the screen are limited to the base frame rate.

### D3 Audio

- Setting "Audio during speed-up", default on: audio is resampled to real time, so the pitch rises. Off: audio is dropped (no underrun counting).

### D4 UI in the game screen

- Header button "Speed-up" with key hint Space; Space toggles it. Space is never forwarded to the core.
- A speed select in the Session panel ("Speed-up speed") changes the speed for the running game only. Below 1360 px the header button shrinks to "»".
- While on, the indicator "Speed-up ×N" is shown.

### D5 Persistent settings

- In the Emulation screen, with the usual global > system > game hierarchy ([05-emulation.md](../architecture/05-emulation.md) section 11):
  - `framebeam.speedup_ratio` ("Speed-up speed", default 2x),
  - `framebeam.speedup_on_start` ("Speed-up on start", default off): permanent speed-up, the game starts sped up and stays so until toggled,
  - `framebeam.speedup_audio` ("Audio during speed-up", default on).

### D6 Shared Sessions

- Speed-up stays allowed while the own Session is shared and can be toggled and changed during play (header toggle and Session panel speed select).
- Viewers see the sped-up game at the normal stream frame rate: frames are capped at the base frame rate, audio is resampled to real time or dropped per the audio setting (D3).

### D7 Save sync

- Unaffected: the normal debounce and periodic rules apply.

## Rejected

- A fixed speed cap (4x): rejected by Fabio on 2026-10-08; the speed is selectable.
- Always-muted audio during speed-up: rejected by Fabio on 2026-10-08; it is a setting.
- Blocking speed-up while the Session is shared (button disabled, turned off when sharing starts): rejected by Fabio on 2026-10-08.
- Showing the control for every core: libretro offers no capability flag and a core may inhibit it.

## Consequences

- The key becomes configurable in Controllers -> Hotkeys (separate work package); a gamepad hotkey also comes later. Both are open.
- Space is reserved by the Player like F3, F5 and F11 ([ADR 0014](0014-player-ui-pass.md) D4).
- Three new Player settings keys under `framebeam.` (D5), controlled by FrameBeam and not core options.
