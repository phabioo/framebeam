# Architecture: controllers

## 12. Controllers and local input profiles

The Player gets its own **"Controllers"** page, separate from emulation/core settings. It offers local controller profiles, device/player assignment, remapping, reset and an input test. Gamepads and keyboard can have their own profiles; DS touch via mouse remains part of the PoC.

```text
Physical controller / keyboard
              ↓
Local controller profile / remapping
              ↓
FrameBeam input
              ↓
System profile mapping, e.g. nds
              ↓
Libretro input
```

The system profile translates the uniform FrameBeam inputs into the inputs of the emulated system. Controller profiles and device-specific assignments remain **local in the Player and are not synchronized centrally**. They are not a Hub management function.

> Refined by [ADR 0014](../adr/0014-player-ui-pass.md): the Controllers page was rebuilt in 0.6; built-in profiles are read-only.

**Hotkeys.** Player hotkeys (fullscreen, diagnostics overlay, save snapshot; Esc is fixed and always pauses since 0.14, [ADR 0026](../adr/0026-ui-gaps-and-multiview.md)) are configurable on the Hotkeys tab of the Controllers page. They are local, stored in `controllers.json` under `hotkeys` (only non-default entries), work on the keyboard and, per action, on a gamepad (below) and are never sent to the core. They are global (not per device or profile); a key that is both a hotkey and mapped in the keyboard profile triggers the hotkey (the hotkey wins).

**Gamepad hotkeys.** Each action has an optional gamepad binding, stored under `padHotkeys` (`{action: "pad:<button>" | "solo:pad:<button>" | "none"}`, only non-default entries) and `padHotkeyModifier` (default `pad:back`). `pad:<button>` = modifier held plus the button, `solo:` = the button alone. Defaults: modifier + RB speed-up, + LB snapshot, + R3 diagnostics. Only P1 triggers them. `PadHotkeyDetector` (`input/padhotkeys.h`, pure) fires once per press and only if the modifier was already held; the fired button is hidden from the core until released (solo buttons while held), the modifier itself still reaches the core. During a binding capture and in the same poll after it nothing fires. A button is used at most once (also not the modifier); the check is in `ControllerProfiles`.
