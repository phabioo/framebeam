# client – FrameBeam Player

C++ (C++20, siehe ADR 0001), CMake + vcpkg, Qt 6/QML, SDL3, libdatachannel, FFmpeg. Emuliert lokal, cached ROMs/Firmware, synchronisiert Saves, teilt und zeigt Sessions.
Architektur: `docs/architektur.md` Abschnitte 2, 3, 5, 6, 10-12, 14.

Unterordner:
- `app/` – Einstiegspunkt, Anwendungsstart, Hub-Profile und Verbindungsablauf
- `core/` – Anwendungslogik: Save-Sync, Caches, Einstellungs-Hierarchie, Manifeste
- `emulation/` – `EmulatorBackend`, `LibretroBackend`, melonDS DS
- `media/` – Encoder/Decoder, Audio, WebRTC-Medienpfad, Multiview-Rendering
- `network/` – Hub-Client (HTTPS/JSON, WSS), TLS-Pinning, Signaling
- `ui/` – Qt/QML-Oberfläche (Library, Emulation, Controllers, Settings)
