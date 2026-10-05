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

Noch kein Build-System und kein Code; Befehle folgen in Phase 0. Keine Befehle erfinden.

## Wegweiser

- Architektur: `docs/architektur/README.md` (Index, nur die nötige Datei lesen); Abweichungen: `docs/adr/`. Bereichsregeln: `server/`, `client/`, `protocol/` je `CLAUDE.md`.
