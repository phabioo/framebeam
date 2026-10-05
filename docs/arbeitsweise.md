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

**Opus ist ausschließlich Orchestrator** und schreibt keinen Produktcode. Er bündelt Aufgaben zu sinnvollen Paketen, delegiert per Brief (Vorlage unten) an Sonnet-5.5-Agents in `.claude/agents/`, prüft das Ergebnis und nimmt ab oder gibt es mit konkreten Korrekturen an denselben Agent zurück.

Ablauf:

1. Aufgabe in 2–3 sinnvolle Pakete zerlegen.
2. Brief schreiben (Vorlage unten).
3. Subagent setzt um und meldet im Rückmeldeformat; er committet nicht.
4. Opus prüft über `git diff --stat`, gezielten Diff und Testergebnis.
5. Abnahme, oder Korrektur per `SendMessage` an denselben Agent.
6. Commit/PR durch Opus.

| Agent | Modell | Zuständigkeit |
|---|---|---|
| `hub-implementer` | Sonnet | Go-Hub (`server/`) |
| `player-implementer` | Sonnet | C++/Qt-Player (`client/`) |
| `protocol-implementer` | Sonnet | `protocol/` |
| `build-ci-implementer` | Sonnet | CMake/vcpkg, Go-Build, GitHub Actions, `packaging/` |
| `docs-writer` | Sonnet | `docs/`, ADRs |
| `scout` | Haiku | read-only: suchen, Logs/CI-Ausgaben lesen, zusammenfassen |

## Brief-Vorlage

```text
Ziel: <ein Satz>
Kontext: <docs/architektur/NN-datei.md, Abschnitt X; ggf. kurzes Zitat>
Dateien/Pfade: anlegen/ändern: <...>; sonst nichts anfassen
Akzeptanzkriterien:
- <...>
Prüfbefehl: <Befehl, Ausgabe gekürzt>
Rückmeldung: geänderte Dateien; Befehle + Ergebnis je 1 Zeile; offene Punkte.
Keine Volltext-Logs, keine Dateiinhalte zurückgeben. Nicht committen.
```

## Token-Sparregeln

- Aufgaben nicht zu fein zerlegen: lieber 2–3 sinnvolle Pakete als viele Mini-Aufträge, da jeder Agent kalt startet.
- Agents erkunden nicht frei; sie bekommen Pfade und die Spec-Stelle.
- Nur die relevante Architekturdatei lesen (Index: `docs/architektur/README.md`), nie alle.
- Opus prüft über `git diff --stat`, gezielten Diff und Testergebnis, nicht durch erneutes Lesen ganzer Dateien.
- Befehlsausgaben immer filtern/kürzen (`| tail -n 30`, nur Fehler). Lange Logs liest `scout` und fasst zusammen.
- Bei Korrekturen den Agent per `SendMessage` fortsetzen statt neu starten; der Kontext bleibt erhalten.
- Ein Arbeitspaket pro Thread; neue Threads statt langer Verläufe.
- Geplant in Phase 0: leise Prüfskripte (`make check` o. ä., nur Fehler + Zusammenfassung), Dependency-Cache im SessionStart-Hook, Codegen aus OpenAPI.

## Pakete und PRs

- Je Paket ein PR mit grüner CI, ein Thema pro PR.

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
