# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projekt

FrameBeam ist eine selbst gehostete Retro-Gaming-Plattform (Monorepo): zentrale ROM-Library und versionierte Saves im Hub, Emulation lokal im Player, Sessions per WebRTC direkt zwischen Playern. Leitsatz: **„Der Hub verwaltet. Der Player emuliert. Audio und Video laufen möglichst direkt zwischen Playern.“**

- Produktbegriffe in UI und Doku: **FrameBeam Hub** (Server) und **FrameBeam Player** (Client). Intern heißen die Verzeichnisse `server/` und `client/`.
- In der UI heißt eine laufende/geteilte Spielsitzung **Session**; „Stream“ nur in Diagnostics.
- Maßgeblich: `docs/architektur.md` (Entwurf 0.1, Deutsch). Abweichungen und Ergänzungen: `docs/adr/`.

## Arbeitsweise (wichtig)

- Opus ist ausschließlich Orchestrator und schreibt keinen Produktcode selbst. Er zerlegt Aufgaben in kleine Teilaufgaben, delegiert per Brief (Ziel, Pfade, Akzeptanzkriterien, Testbefehl) an die Sonnet-5.5-Agents in `.claude/agents/` (`hub-implementer`, `player-implementer`, `protocol-implementer`, `build-ci-implementer`, `docs-writer`), prüft das Ergebnis selbst (Diff lesen, Build/Tests selbst ausführen) und nimmt ab oder gibt es mit konkreten Korrekturen an denselben Agent zurück. Commit/PR macht Opus.
- Ein Arbeitspaket pro Projekt-Thread, ein PR mit grüner CI, ein Thema pro PR.
- Details, Phasenplan, was in der Cloud prüfbar ist: `docs/arbeitsweise.md`.

## Status und Befehle

Es gibt noch kein Build-System und keinen Code. Build-, Test- und Einzeltest-Befehle werden hier ergänzt, sobald Phase 0 sie anlegt. Keine Befehle erfinden.

## Architektur im Überblick

**Hub (`server/`, Go, SQLite):** HTTPS-API, Dateiverwaltung, Auth, Registry, interne Session-Metadaten, Presence, Signaling, Webinterface. Er emuliert, encodiert, decodiert und rendert nie (auch kein Multiview). ROMs, Saves und Firmware liegen als Dateien (`/var/lib/framebeam/`); SQLite hält nur Metadaten, keine Binärdaten. Der Hub hat keine Sessions-Verwaltungsseite und keine Video-Vorschau.

**Player (`client/`, C++/Qt 6/QML, SDL3):** Library-Zugriff, ROM-/Core-/Firmware-Cache (getrennt), Save-Sync, Session-Manager, Emulation, Rendering inkl. Multiview, Audio, Encoder/Decoder, Input. Controllerprofile und Emulations-Einstellungen sind lokal (Hierarchie Global → System/Core → Game Override, nur partielle Overrides).

**Protokolle:** HTTPS/JSON unter `/api/v1/...`; WSS für Presence, Session-Updates und WebRTC-Signaling; WebRTC (libdatachannel) P2P mit H.264 + Opus. `protocol_version` ist getrennt von Hub- und Player-Produktversion; Kompatibilität richtet sich nach ihr (Handshake mit Capability Negotiation). Kein gRPC. Details in `protocol/`.

**Emulation:** `EmulatorBackend` → `LibretroBackend` (später ggf. `StandaloneBackend`). Systeme und Cores werden datengetrieben über Manifeste beschrieben (z. B. `nds` → `melonds_ds`), keine konsolenspezifische Startlogik in der App. Der Hub führt Cores nie aus; er kennt nur System-/Core-Registry. Core-Optionen dynamisch aus Libretro Core Options, keine erfundenen Optionen.

**Hub-Bindung:** Genau ein aktiver Hub pro verbundener Player-Instanz; Library, Saves, Sessions und Registry stammen nur von ihm. Lokale Daten und pending sync werden nach Hub-ID (und Hub-User-ID) getrennt, auch bei gleichen ROM-Hashes. Hub-User-IDs sind hub-lokal; keine globalen Accounts.

**Save-Modell:**
- Current Checkpoint (Auto-Checkpoints, jede Änderung = neue Revision) ist getrennt von der dauerhaften History (Session-Ende, Gerätewechsel, vor Konfliktauflösung, manueller Snapshot).
- Jeder Upload trägt `base_version` (beim Abgleich gelesene Checkpoint-Revision). Veraltete Basis erzeugt einen Konflikt statt Überschreiben; Zeitstempel entscheiden nie allein. Beide Inhalte bleiben bis zur bewussten Auswahl erhalten; vor Auflösung wird die History gesichert.
- Uploads nur bei geändertem Hash; Debounce ca. 10-15 s, periodisch frühestens ca. alle 60 s; Pause/Stop/sauberes Beenden lösen sofortigen Final-Sync aus.
- Fehlgeschlagene Uploads werden lokal als pending sync (Hub, User, Spiel/Slot, Basisversion) gepuffert und nur zum ursprünglichen Hub wiederholt. Kein automatisches Zusammenführen binärer Saves; Save States sind getrennt und außerhalb des PoC.

**Sicherheit:**
- HTTPS/WSS Pflicht; HTTP/WS nur expliziter Dev-Modus bzw. localhost. Eigenes TLS-Zertifikat, eigenes cert/key oder Reverse Proxy; Player macht TOFU/Pinning, Abweichung warnt und wird nie still übernommen.
- Admin mit Username/Passwort (z. B. Argon2id); normale User passwordless, nur Admin-kontrolliert angelegt (Onboarding-Invites). User und Device sind getrennte Entitäten.
- Pairing: Player-Anfrage → Admin Allow/Deny (verpflichtender Standard); Revoke pro Gerät muss auch Access Tokens entziehen.
- Kurzlebiges Access Token + langfristiges Device/Refresh Credential; Letzteres nur im OS-Credential-Store, nie in Profil-Dateien oder Logs. Der Hub speichert nur eine Prüfrepräsentation.
- Firmware/BIOS stellt nur der Admin über den Hub bereit; fehlt sie, zeigt der Player „Firmware required/missing“ und blockiert den Start.

## PoC-Grenzen (0.1)

Player nur Windows x86-64; Hub Linux x86-64/ARM64 (Raspberry Pi 5); nur Nintendo DS mit melonDS DS über Libretro (im Player enthalten); P2P ohne TURN. Außerhalb: Linux-/macOS-Player, weitere Emulatoren, automatische Core-Verteilung, Metadata/Boxart, Save States, Remote-Steuerung/Netplay, Multi-Hub, OAuth, Webclient. Vollständig: `docs/architektur.md` Abschnitt 7.

## Stack-Ergänzungen

Gelten zusätzlich zum Architekturdokument (`docs/adr/0001-stack-ergaenzungen.md`): FFmpeg (libavcodec) mit NVENC/QSV/AMF und OpenH264 als Software-Fallback; C++20 als Basis (C++23 nur wenn MSVC, GCC und AppleClang es unterstützen); Hub-Webinterface mit Go `html/template` + htmx per `embed`, kein Node-Build; vcpkg (Manifest) + CMake Presets; GitHub Actions (Linux bei jedem Push, Windows bei PRs gegen `main` und manuell).

## Harte Regeln

- Keine ROMs, BIOS oder Firmware im Repo oder in Tests; nur Homebrew-ROMs bzw. Dummy-Dateien. melonDS DS ist GPL-3.0.
- Save-Uploads dürfen nie still überschreiben und nie an einen anderen Hub gehen.
- Secrets nie in Profil-Dateien oder Logs.
