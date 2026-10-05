# FrameBeam

FrameBeam ist eine selbst gehostete Retro-Gaming-Plattform. Der **FrameBeam Hub** verwaltet zentrale ROM-Library, versionierte Spielstände, Benutzer und Geräte. Der **FrameBeam Player** emuliert lokal und teilt laufende Sessions per WebRTC direkt mit anderen Playern. Der Hub emuliert, encodiert und rendert nie.

## Stand

Phasenplan: [Arbeitsweise](docs/arbeitsweise.md#phasenplan).

| Phase | Status | Inhalt |
|---|---|---|
| 0 Fundament | erledigt | Monorepo, CLAUDE.md, Architektur, CI (Linux/Windows), Build-Gerüst, Prüfskripte |
| 1 Protokoll und Hub-Grundlagen | erledigt | OpenAPI `/api/v1`, WSS-Schemas, Hub mit SQLite, Admin-Setup, TLS, Pairing, Tokens, Library, ROM-Download, Webinterface |
| 2 Spielbarer Durchstich | in Arbeit | Player-Kern, Libretro-Anbindung, minimale Oberfläche |
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

**FrameBeam Player** (`client/`)

- Bisher nur Build-Gerüst (CMake, vcpkg-Presets); noch kein lauffähiger Player.

Saves, Session-Sharing und Emulation sind noch nicht umgesetzt.

## Bauen und Starten

Voraussetzungen: Go 1.24 (laut `server/go.mod`); für den Client CMake, ein C++-Compiler und Qt.

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

Client (vcpkg einmalig bootstrappen, Details: [client/README.md](client/README.md)):

```sh
scripts/bootstrap-vcpkg.sh
make check-client   # Preset über CLIENT_PRESET, Default linux-debug
```

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
- [Design](docs/design/README.md)
- [Arbeitsweise mit Claude Code](docs/arbeitsweise.md)
