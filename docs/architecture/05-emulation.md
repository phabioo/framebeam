# Architecture: emulation, cores, settings

## 6. Extensibility for further emulators

FrameBeam is not coupled directly to melonDS. The common emulator interface separates application logic and UI from the respective backend:

```text
FrameBeam → EmulatorBackend
              ├── LibretroBackend
              │      ├── melonDS DS  [PoC]
              │      ├── mGBA        [possible later]
              │      └── Snes9x      [possible later]
              └── StandaloneBackend [later option]
```

Systems and cores are described data-driven via manifests instead of spreading console-specific launch logic through the application. An NDS manifest contains, for example, system ID `nds`, core assignment `melonds_ds`, file extension `.nds`, BIOS/firmware details, input profile `nds` and display profile `dual_screen`.

New Libretro systems should generally be added through core, manifest and matching profiles. Additional backend work remains possible if an emulator has special requirements. Concrete BIOS/firmware requirements are described per system/core in the manifest. For the PoC, melonDS DS ships with the Windows Player; automatic core distribution via the Hub follows later.

### BIOS/firmware via the Hub [PoC]

BIOS/firmware is provided centrally by the **admin** and assigned to a system/core. FrameBeam does not automatically ship proprietary BIOS/firmware files and does not download them from the internet on its own. Normal users do not manage these files.

The Hub validates expected metadata and hashes according to system/core requirements and delivers required, provided files to authorized Players. The Player validates and caches them locally, separate from ROMs and cores. If a core needs an unavailable file, it clearly shows **"Firmware required/missing"** and prevents the affected launch. Cores without BIOS/firmware needs work without this path. The concrete requirements of the PoC core and manifest/file formats remain to be specified; the provisioning and error path is part of the PoC.


## 10. Systems & Cores: registry and package provisioning

The Hub page **"Systems & Cores"** is retained. It manages the system registry and core registry: system ID, assigned or preferred cores, core ID, expected version and supported platforms. The Hub never runs cores.

For FrameBeam 0.1: `nds → melonds_ds`; the Player reports platform and available core versions so that compatibility can be checked. A missing or unsuitable core version is reported visibly. Since 0.2 installers ship no cores; they come from the Hub (ADR 0010).

Since 0.2 the Hub has a **core package cache**. Packages carry core ID, version, platform, SHA-256 per file, origin and licence information. The Hub fetches them from FrameBeam's GitHub Releases through an Ed25519-signed index and verifies the signature; the Player checks size and SHA-256 (ADR 0010). "Cached on Hub" denotes a stored package, not an emulation installed or run there.

The flow (built in 0.2) is: Player requests the intended core → Hub delivers package metadata → Player checks local version, platform and hash → download only if needed → hash/version check → inclusion in the local core cache → local execution. Multiple versions can be cached in parallel; a later version-bound game assignment remains possible.

ROM cache and core cache remain separate, for example:

```text
Player Data/cache/
├── roms/<sha256>.nds
└── cores/<core-id>/<version>/<platform>/
```

### Visible cache and readiness states

Library and emulation show understandable states for the respective ROM or core needed. Availability, transfer and validation are tracked separately so that a package can be present on the Hub and faulty locally at the same time.

| State | Meaning / UI action |
|---|---|
| Cached locally / ready | Local file is present and validated; a legacy `<app-dir>/cores` core is still found |
| Only on Hub | ROM or core package is on the Hub, missing locally |
| Download required | Required local file is missing; offer download or trigger it on launch |
| Download running / failed | Progress or an understandable error with a retry option |
| Hash mismatch | Local file does not match the expected SHA-256; do not use, fetch again |
| Version mismatch | Core version does not match the intended version; matching version required (a different major version blocks the launch, other differences warn; ADR 0017 D1) |
| Core unavailable / incompatible | No matching core for the Player platform; launch not possible |

The status refers to the respective device and artifact. The Hub registry can display reported client compatibility; it does not imply core execution on the Hub. Full hashes and further technical details can live in a detail view.

## 11. Emulation and settings hierarchy in the Player

The Player gets its own **"Emulation"** page with systems, available cores, version and readiness state as well as their configuration. It covers general, graphics, audio and core-specific options, as far as the respective core supports them.

FrameBeam's own options such as fullscreen, UI scaling, presentation and multiview layout remain distinguishable from emulator options such as internal resolution, renderer and core-specific audio processing.

Core settings should be generated **dynamically from Libretro core options** wherever possible. Reported categories, descriptions, permitted values and defaults serve as the basis. Enumerated options appear as selection fields; toggle, number field, slider or file picker are used only when suitable type/validation information is available. Libretro options do not automatically provide a free number or path field for every option. Additional FrameBeam metadata or backend adapters can supplement the presentation. Unsupported options are not invented; necessary restarts or delayed effect must be indicated.

The configuration provides the following hierarchy:

```text
Global → system/core → game override
```

A more specific level overrides only explicitly set values. Unset values are inherited; removing an override restores inheritance. The data model therefore stores partial overrides with system/core or game assignment instead of complete configuration copies. The UI should make the origin and effective value recognizable. Global defaults apply only as far as the selected core supports them; core-specific option keys remain assigned to the core.

In the PoC, emulation settings remain local. The hierarchy is prepared conceptually and in the data model; a complete game-override UI is not a prerequisite for the PoC. An optional later synchronization of emulation settings via the Hub is open.
