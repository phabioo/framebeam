# Architektur: Überblick und Tech-Stack

Stand: 05.10.2026 · Architekturentwurf 0.1  
Grundlage: Architekturgespräch „Konzept für Retro Streaming“.

## 1. Ziel und Grundprinzip

FrameBeam verbindet eine zentrale ROM-Bibliothek und versionierte Spielstände mit lokaler Emulation und dem Teilen laufender Sessions. FrameBeam Player und FrameBeam Hub sollen leicht, leistungsfähig und langfristig unter Windows, Linux und macOS nutzbar sein. Weitere Emulatoren sollen ohne grundlegenden Umbau hinzukommen.

> **Der Hub verwaltet. Der Player emuliert. Audio und Video laufen möglichst direkt zwischen Playern.**

```text
                   FrameBeam Hub
          Library · Saves · Auth · Registry
          Presence / Session-Metadaten intern
                 HTTPS/JSON + WSS
                    /           \
             Player A         Player B
             Emulator         Emulator
             Rendering        Rendering
                    \           /
                     WebRTC/P2P
                     Video + Audio
```

## 2. Verantwortlichkeiten und Tech-Stack

| Bereich | Entscheidung | Aufgabe |
|---|---|---|
| Hub | Go, ohne großes Framework | HTTPS-API, Dateiverwaltung, Authentifizierung, Registry, interne Session-Metadaten und Signaling |
| Verwaltungsdaten | SQLite | Spiele, ROM-Hashes, Hub-lokale Nutzer, Geräte, Token-Zuordnungen, Save-Versionen, Save-Konflikte und Registry; Session-Metadaten intern |
| Dateispeicher | Normale Dateien | ROMs, Spielstände und Admin-bereitgestellte Firmware; keine Binärdaten in SQLite |
| Player | C++23 + CMake | Emulation, Medienverarbeitung, Netzwerk und Anwendungslogik |
| Oberfläche | Qt 6 + QML | GPU-beschleunigte Library, Settings und mehrere Video-Surfaces |
| Emulator-Anbindung | `EmulatorBackend` → `LibretroBackend` | Austauschbare Emulator-Cores |
| Erster Core | melonDS DS über Libretro | Nintendo-DS-Emulation |
| Input/Audio | SDL3 | Plattformübergreifende Input-/Audio-Schicht, insbesondere Gamepads |
| Streaming | libdatachannel | WebRTC-Verbindungen zwischen Clients |
| Medienformate | H.264 + Opus | Video- und Audioübertragung |
| Windows-Encoding | NVENC / QSV / AMF; Software-H.264 als Fallback | Hardware-Encoding, soweit verfügbar |

Der Server führt keine Emulation, kein Encoding, kein Decoding und kein Multiview-Rendering aus. Der Client enthält Library-Zugriff, getrennte ROM-/Core-Caches, lokale Emulator-/Controller-Einstellungen, Save-Sync, Session-Manager, Emulator-Backend, Rendering, Audio, Encoder/Decoder und Input.

Der Hub verwaltet Hub-lokale Benutzer und Geräte-Pairing. Der Player verwaltet lokale Geräteidentität und gespeicherte Hub-Profile; eine verbundene Player-Instanz verwendet genau einen aktiven Hub (siehe Abschnitt 14). Ein zentraler Metadata Service mit Provider-Abstraktion und Artwork-Cache ist ausschließlich als spätere Erweiterung vorgesehen (siehe Abschnitt 15).

Der Server wird als einzelner Dienst betrieben, unter Linux beispielsweise über `systemd`, mit SQLite und Daten unter `/var/lib/framebeam/`. Docker ist optional. Node.js, Redis, PostgreSQL und Kubernetes sind für die geplante Basis nicht erforderlich.
