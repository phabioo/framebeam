# client/ – FrameBeam Player (C++20, Qt 6/QML, SDL3)

Details nur bei Bedarf aus `docs/architektur/` (Index: `docs/architektur/README.md`).

## Verantwortung
- Library-Zugriff, ROM-/Core-/Firmware-Cache (getrennt, ROM hashbasiert), Save-Sync, Session-Manager, Emulation, Rendering inkl. Multiview, Audio, Encoder/Decoder, Input. Siehe `01-ueberblick.md`, `02-protokolle-und-rom-cache.md`.

## Emulation
- `EmulatorBackend` -> `LibretroBackend` (später ggf. `StandaloneBackend`). Systeme/Cores datengetrieben über Manifeste (z. B. `nds` -> `melonds_ds`), keine konsolenspezifische Startlogik.
- Core Options dynamisch aus Libretro Core Options, keine erfundenen Optionen. Siehe `05-emulation.md`.
- Einstellungen lokal: Global -> System/Core -> Game Override, nur partielle Overrides. Controllerprofile lokal (`06-controllers.md`).

## Hub-Bindung
- Genau ein aktiver Hub; Library, Saves, Sessions, Registry nur von ihm.
- Lokale Daten und pending sync getrennt nach Hub-ID (und Hub-User-ID), auch bei gleichen ROM-Hashes. Keine globalen Accounts.
- Credentials nur im OS-Credential-Store, nie in Profil-Dateien/Logs. TOFU/Pinning: Zertifikatsabweichung warnt, wird nie still übernommen. Siehe `10-identitaet-pairing-tls.md`.

## Save-Sync (Player-Sicht)
- Upload nur bei geändertem Hash; Debounce ca. 10-15 s, periodisch frühestens alle ca. 60 s; Pause/Stop/sauberes Beenden = sofortiger Final-Sync.
- Fehlgeschlagene Uploads als pending sync (Hub, User, Spiel/Slot, Basisversion), nur zum Ursprungs-Hub wiederholen. Save States getrennt, nicht im PoC. Siehe `03-saves.md`.

## Medien und Sprache
- FFmpeg (libavcodec) mit NVENC/QSV/AMF, OpenH264 als Software-Fallback; WebRTC mit libdatachannel (ADR 0001, `04-sessions-und-multiview.md`).
- C++20 als Basis; C++23 nur, wenn MSVC, GCC und AppleClang es unterstützen. vcpkg (Manifest) + CMake Presets.
- Firmware fehlt: „Firmware required/missing“, Start blockieren.
- Design: Player-UI `docs/design/README.md`, `docs/design/player.md`, `docs/design/tokens.md`.
