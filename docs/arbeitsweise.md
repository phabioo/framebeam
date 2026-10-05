# Arbeitsweise mit Claude Code

## Hybrider Modus

- Entwicklung in Claude-Code-Cloud-Sessions.
- Windows-Build und -Tests laufen über GitHub Actions.
- Fabio testet Meilensteine lokal (echtes Windows, GPU, Ton, Gamepads, P2P zwischen zwei Rechnern, Raspberry Pi 5 als Hub) oder per Remote-Control-Session auf seinem Rechner.

## Was in der Cloud prüfbar ist

Prüfbar:
- Hub komplett: Go-Server, SQLite, API, WSS-Signaling, Pairing, Tokens, TLS, Save-Konflikte, Firmware-Pfad, Webinterface; Linux-Builds x86-64 und ARM64 (Cross-Compile).
- Protokoll: OpenAPI, JSON-Schemas, Handshake, Contract-Tests.
- Player-Kern ohne GUI unter Linux (Hub-Client, Caches, Save-Sync, Hub-Profile, Einstellungs-Hierarchie, Manifeste).
- Libretro-Backend headless mit melonDS DS und Homebrew-ROM (Frame-Hashes).
- WebRTC zwischen zwei Prozessen auf einem Rechner (Loopback).
- QML unter Xvfb mit Software-Rendering (Qt 6.4 aus apt).
- Windows-Build und -Tests über GitHub Actions.

Nicht prüfbar (nur lokal bei Fabio):
- Windows Credential Manager, NVENC/QSV/AMF, Installer, interaktive Bedienung.
- GPU, Ton, Gamepads, Spielgefühl, Latenz.
- Echtes P2P (NAT, Firewall, zwei Geräte im LAN).
- Lauf auf Raspberry Pi 5 (Cloud: nur ARM64-Build bzw. QEMU-Smoke-Test).
- Echte ROMs/BIOS: dürfen nicht in Repo oder Cloud.
- Container sind kurzlebig: Abhängigkeiten brauchen Setup-Skript bzw. Caching.

## Rollen

**Opus ist ausschließlich Orchestrator.** Opus zerlegt größere Aufgaben in kleine, klar abgegrenzte Teilaufgaben und gibt sie an Subagents mit Sonnet 5.5 (Agents in `.claude/agents/`). Opus prüft jedes Ergebnis selbst (Diff lesen, Build und Tests selbst ausführen) und nimmt ab oder gibt es mit konkreten Korrekturen an denselben Agent zurück. Opus schreibt keinen Produktcode selbst.

Ablauf:

1. Aufgabe zerlegen.
2. Brief schreiben: Ziel, betroffene Pfade, Akzeptanzkriterien, Testbefehl.
3. Subagent setzt um und liefert Zusammenfassung (geänderte Dateien, Befehle mit Ergebnis, offene Punkte); er committet nicht.
4. Opus prüft: Diff lesen, Build/Tests selbst ausführen.
5. Abnahme, oder Rückgabe mit konkreten Korrekturen an denselben Agent.
6. Commit/PR durch Opus.

| Agent | Zuständigkeit |
|---|---|
| `hub-implementer` | Go-Hub (`server/`) |
| `player-implementer` | C++/Qt-Player (`client/`) |
| `protocol-implementer` | `protocol/` |
| `build-ci-implementer` | CMake/vcpkg, Go-Build, GitHub Actions, `packaging/` |
| `docs-writer` | `docs/`, ADRs |

## Pakete und PRs

- Ein Arbeitspaket pro Projekt-Thread.
- Je Paket ein PR mit grüner CI.
- Ein Thema pro PR.

## Phasenplan

- **Phase 0 – Fundament:** Monorepo-Struktur, CLAUDE.md, Architektur nach `docs/`; CI (Linux, Windows, ARM64-Cross-Build Hub); SessionStart-Hook für Abhängigkeiten.
- **Phase 1 – Protokoll und Hub-Grundlagen:** OpenAPI `/api/v1`, WSS-Nachrichten, `protocol_version`, Handshake, Fehlercodes; Hub mit SQLite-Schema, Admin-Setup, TLS, Info-Endpunkt, Pairing, Tokens, Revoke, Library, ROM-Up-/Download.
- **Phase 2 – Spielbarer Durchstich:** Player-Kern (Hub-Profil, TOFU, Pairing, Library, ROM-Cache); `LibretroBackend` mit melonDS DS; minimale Qt-Oberfläche. Erster lokaler Windows-Test durch Fabio.
- **Phase 3 – Saves:** Start-/Auto-/Final-Sync, Current Checkpoint vs. History, `base_version`, Konfliktmodell, pending sync; Hub-Saves-Seite, Player-Konfliktdialog.
- **Phase 4 – Session-Sharing und Multiview:** Presence, Signaling, Sichtbarkeit/ACL, WebRTC mit Software-H.264/Opus, PiP/Side-by-Side, Diagnostics; danach Hardware-Encoder (nur lokal testbar).
- **Phase 5 – Rest und Politur:** Firmware-Pfad, Benutzer/Invites, Systeme & Cores, Emulation- und Controllers-Seiten, SDL3-Gamepads, Dark/Light, restliche Hub-Seiten, Paketierung (Windows-Installer, systemd-Unit).

## Regeln

- Keine echten ROMs, BIOS oder Firmware im Repo oder in Tests; nur Homebrew-ROMs bzw. Dummy-Dateien.
- melonDS DS ist GPL-3.0; das betrifft die spätere Verteilung des Players.
