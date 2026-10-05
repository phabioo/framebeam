# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

FrameBeam ist eine selbst gehostete Retro-Gaming-Plattform (Monorepo): zentrale ROM-Library und versionierte Saves, Emulation lokal, Sessions per WebRTC zwischen Playern. Leitsatz: **„Der Hub verwaltet. Der Player emuliert. Audio und Video laufen möglichst direkt zwischen Playern.“**

## Begriffe

- **FrameBeam Hub** (`server/`, Go) und **FrameBeam Player** (`client/`, C++/Qt).
- **Session** = laufende/geteilte Spielsitzung; „Stream“ nur in Diagnostics.

## Arbeitsweise

- Opus orchestriert nur und schreibt keinen Produktcode; Implementierung durch die Sonnet-Agents in `.claude/agents/`, Suchen und Log-Lesen durch Haiku-Agent `scout`.
- Briefs nach Vorlage in `docs/arbeitsweise.md` (dort auch Token-Sparregeln, Phasenplan, Cloud-Grenzen).
- Opus prüft per `git diff --stat`, gezieltem Diff und Testergebnis; Commit/PR durch Opus. Ein Arbeitspaket pro Thread, ein Thema pro PR.

## Harte Regeln

- Keine ROMs, BIOS oder Firmware im Repo oder in Tests; nur Homebrew-ROMs bzw. Dummy-Dateien.
- Saves nie still überschreiben, nie an einen anderen Hub senden.
- Secrets nie in Dateien, Profilen oder Logs.

## Status und Befehle

- `scripts/bootstrap-vcpkg.sh`: vcpkg (gepinnt) nach `$HOME/.cache/framebeam/vcpkg`; Voraussetzung für den Client-Build.
- `make check`: Hub- und Client-Prüfung, leise; `make check-hub` / `make check-client` einzeln (Preset via `CLIENT_PRESET`, Default `linux-debug`).
- `make build-hub`: Hub-Binaries `server/dist/framebeam-hub-linux-{amd64,arm64}` (`HUB_VERSION` setzbar).
- `make generate`: Go-Codegen (oapi-codegen) neu erzeugen.

CI (`.github/workflows/ci.yml`): Linux bei jedem Push, Windows bei PRs gegen `main` und manuell. Der SessionStart-Hook `.claude/hooks/session-start.sh` bereitet nur Cloud-Sessions vor (vcpkg, Go-Module; ohne Qt).

## Wegweiser

- Architektur: `docs/architektur/README.md` (Index, nur die nötige Datei lesen); Abweichungen: `docs/adr/`. Bereichsregeln: `server/`, `client/`, `protocol/` je `CLAUDE.md`.
