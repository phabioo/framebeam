# ADR 0001: Ergänzungen zum Stack

- Status: angenommen
- Datum: 2026-10-05
- Entscheider: Fabio

## Kontext

`docs/architektur/` (Entwurf 0.1, Index: `README.md`) legt den Grundstack fest, lässt aber Encoder-/Decoder-Anbindung, Hub-Webinterface, Paketmanagement und CI offen. Dieser ADR ergänzt diese Punkte.

## Entscheidungen

- **Encode/Decode:** FFmpeg (libavcodec). Windows: NVENC/QSV/AMF; später VideoToolbox/VAAPI. Software-Fallback für H.264: OpenH264 (Lizenz unkritischer als x264).
- **WebRTC:** libdatachannel bleibt. Jitter-Buffer und einfache Bitratenanpassung sind Eigenleistung. libwebrtc und GStreamer sind verworfen (Build- und Verteilungsaufwand).
- **Sprache Player:** C++20 als Basis. C++23-Features nur, wenn MSVC, GCC und AppleClang sie unterstützen. Das weicht bewusst von „C++23“ in `docs/architektur/01-ueberblick.md` ab.
- **Hub-Webinterface:** Go `html/template` + htmx, per `embed` im Hub-Binary. Kein Node-Build.
- **C++-Abhängigkeiten:** vcpkg im Manifest-Modus, Build über CMake Presets.
- **CI:** GitHub Actions. Linux-Jobs bei jedem Push, Windows-Jobs bei Pull Requests gegen `main` und manuell (`workflow_dispatch`). Das Repo ist öffentlich.

## Verworfen

- **Rust statt Go/C++:** Ein Wechsel würde Qt-, SDL3-, FFmpeg- und libdatachannel-Anbindung über FFI erzwingen und den bestehenden Entwurf neu aufsetzen, ohne den PoC voranzubringen.
- **Electron/Tauri statt Qt/QML:** Mehrere GPU-beschleunigte Video-Surfaces, nativer Decoder-Zugriff und Emulator-Frameloop sind in einer Web-Runtime aufwendiger und weniger leichtgewichtig als in Qt.

## Folgen

- Die Architekturdokumente bleiben unverändert; Abweichungen gelten über diesen ADR.
- Lizenzen von FFmpeg-Build und OpenH264 sind bei der Paketierung zu prüfen.
