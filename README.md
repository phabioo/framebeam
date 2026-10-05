# FrameBeam

FrameBeam ist eine selbst gehostete Retro-Gaming-Plattform. Der **FrameBeam Hub** verwaltet zentrale ROM-Library, versionierte Spielstände, Benutzer und Geräte. Der **FrameBeam Player** emuliert lokal und teilt laufende Sessions per WebRTC direkt mit anderen Playern. Der Hub emuliert, encodiert und rendert nie.

## Stand

Phasenplan: [Arbeitsweise](docs/arbeitsweise.md#phasenplan).

| Phase | Status | Inhalt |
|---|---|---|
| 0 Fundament | erledigt | Monorepo, CLAUDE.md, Architektur, CI (Linux/Windows), Build-Gerüst, Prüfskripte |
| 1 Protokoll und Hub-Grundlagen | erledigt | OpenAPI `/api/v1`, WSS-Schemas, Hub mit SQLite, Admin-Setup, TLS, Pairing, Tokens, Library, ROM-Download, Webinterface |
| 2 Spielbarer Durchstich | erledigt | Player-Kern (Profil, Pairing, Library, ROM-Cache), melonDS DS per Libretro, minimale Qt-Oberfläche |
| 3 Saves | geplant | Sync, Versionen, Konfliktmodell |
| 4 Session-Sharing und Multiview | geplant | Presence, Signaling, WebRTC, Multiview |
| 5 Rest und Politur | geplant | Firmware-Pfad, Benutzer, restliche Seiten, Paketierung |

## Was funktioniert

**FrameBeam Hub** (`server/`)

- Admin-Setup per `setup-admin` und über `/setup` im Webinterface (nur Loopback).
- HTTPS mit selbstsigniertem Zertifikat (oder eigenes Zertifikat); Fingerprint steht beim Start im Log.
- Webinterface mit Login: Library, Clients, Settings.
- Pairing neuer Geräte mit Allow/Deny; Tokens ausstellen und widerrufen (Revoke).
- ROM-Upload im Webinterface.
- ROM-Download per API mit Range und ETag.
- Info-Endpunkt `/.well-known/framebeam` und Handshake mit `protocol_version`.

**Protokoll** (`protocol/`)

- OpenAPI 3.0.3 für `/api/v1` und WSS-Nachrichtenschemas; `protocol_version` ist 1.

**FrameBeam Player** (`client/`, [ADR 0003](docs/adr/0003-player-phase2.md))

- Connection-Screen mit Hub-Profilen und Auto-Connect.
- Hub-Identifikation mit Fingerprint-Bestätigung beim Erstkontakt (TOFU); bei Abweichung blockiert die Verbindung.
- Pairing per Freigabe-Anfrage, Token-Erneuerung und Revoke.
- Library mit Suche und Filter.
- Hashgeprüfter ROM-Cache mit fortsetzbarem Download.
- NDS-Spiele lokal starten mit melonDS DS: Bild, Ton über Qt Multimedia, Tastatur, Touch per Maus.
- Credentials unter Windows im Credential Manager, unter Linux nur im Speicher (nach Neustart neues Pairing).

Es fehlen noch: Saves/Sync (Phase 3), Sessions (Phase 4), Gamepads, Firmware-Pfad und Einstellungsseiten (Phase 5).

## Bauen und Starten

Voraussetzungen: Go 1.24 (laut `server/go.mod`); für den Client CMake, ein C++-Compiler und Qt >= 6.4 (nicht über vcpkg): Linux per apt (Paketliste `QT_PKGS` in `.claude/hooks/session-start.sh`), Windows Qt 6.8.

```sh
make check          # Hub- und Client-Prüfung, leise
make check-hub      # nur Hub
make build-hub      # server/dist/framebeam-hub-linux-{amd64,arm64}
make generate       # Go-Codegen aus OpenAPI
```

Hub starten (Details: [server/README.md](server/README.md)):

```sh
framebeam-hub setup-admin -username <name>   # Passwort als eine Zeile von stdin
framebeam-hub -data-dir <verzeichnis>        # HTTPS, Default-Listen :8443
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # Entwicklung: HTTP statt HTTPS
```

Das Datenverzeichnis (`-data-dir`, Default `/var/lib/framebeam`) enthält Datenbank und Zertifikat. Weitere Flags: `-listen`, `-name`, `-tls-cert`, `-tls-key`; jeweils auch per `FRAMEBEAM_*`.

Player (Details: [client/README.md](client/README.md)):

```sh
scripts/bootstrap-vcpkg.sh                 # einmalig
make fetch-core                            # melonDS DS (gepinnt) bauen, gibt den .so-Pfad aus
make check-client                          # Preset über CLIENT_PRESET, Default linux-debug; ohne Core werden Core-Tests übersprungen
client/build/linux-debug/app/framebeam_player [--data-dir <pfad>] [--dev-allow-http]
```

`--data-dir` ersetzt AppData; `--dev-allow-http` erlaubt HTTP-Hubs außerhalb von localhost (nur Entwicklung). `scripts/e2e-player-hub.sh` prüft den Player-CLI gegen einen lokal gebauten Hub.

Windows-Testpaket: CI-Artefakt `framebeam-player-windows-x64` aus dem Windows-Job entpacken und `framebeam_player.exe` starten (Core unter `cores/`).

Tastatur: Pfeile, X=A, Z=B, S=X, A=Y, Q=L, W=R, Enter=Start, Rücktaste=Select, Esc=Pause.

## Repo-Struktur

- `server/`: FrameBeam Hub (Go)
- `client/`: FrameBeam Player (C++/Qt)
- `protocol/`: OpenAPI und WSS-Schemas, gemeinsam für Hub und Player
- `docs/`: Architektur, ADRs, Design, Arbeitsweise
- `scripts/`: Prüf- und Bootstrap-Skripte
- `packaging/`: Paketierung

## Dokumentation

- [Architektur (Index)](docs/architektur/README.md)
- [ADR 0001: Stack-Ergänzungen](docs/adr/0001-stack-ergaenzungen.md)
- [ADR 0002: Protokoll und Hub in Phase 1](docs/adr/0002-protokoll-und-hub-phase1.md)
- [ADR 0003: Player in Phase 2](docs/adr/0003-player-phase2.md) (vorgeschlagen)
- [Design](docs/design/README.md)
- [Arbeitsweise mit Claude Code](docs/arbeitsweise.md)
