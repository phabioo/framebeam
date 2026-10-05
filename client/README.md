# client – FrameBeam Player

C++/Qt-Player: emuliert lokal, synchronisiert Saves, teilt Sessions. Stand Phase 2: Hub-Profil, Pairing, Library, ROM-Cache, Emulation (melonDS DS) und minimale Oberfläche. Festlegungen: [ADR 0003](../docs/adr/0003-player-phase2.md) (angenommen). Regeln für Agents: `CLAUDE.md`.

## Struktur

- `core/`: Profile, ROM-Cache, Credential-Store, Version (`framebeam_core`)
- `network/`: Hub-Verbindung (HTTP/TLS mit Fingerprint-Pinning), Library, ROM-Download
- `emulation/`: `EmulatorBackend`/`LibretroBackend`, Systemmanifeste (`manifests/`), Core-Locator
- `ui/`: QML-Modul `FrameBeam.Player` als statische Lib `framebeam_ui`
- `app/`: Executable `framebeam_player`
- `media/`: noch leer (ab Phase 4)

## Abhängigkeiten

- Qt >= 6.4 (Core, Network, Gui, Quick, QuickControls2, Multimedia, Test); nicht über vcpkg. Linux: apt, Windows: install-qt-action (siehe `.github/workflows/ci.yml`).
- CMake >= 3.25, Ninja; vcpkg (`scripts/bootstrap-vcpkg.sh`) für weitere Pakete.
- melonDS-DS-Core (gepinnt, `scripts/melonds-ds.pin`): `scripts/fetch-melonds-ds.sh` (Linux) bzw. `.ps1` (Windows). Kein Core im Repo.

## Bauen und Testen

```sh
make check-client                      # Preset via CLIENT_PRESET, Default linux-debug
cmake -DFRAMEBEAM_MELONDS_DS_CORE=<pfad-zum-core> ...   # aktiviert die Core-Tests
```

Ohne `FRAMEBEAM_MELONDS_DS_CORE` bauen alle Ziele; Tests mit `NEEDS_CORE` werden von ctest übersprungen. Tests laufen headless (`QT_QPA_PLATFORM=offscreen`) gegen einen Fake-Hub und eine zur Build-Zeit erzeugte Homebrew-Test-ROM.

## Datenablage

Default portabel: `<Verzeichnis der Programmdatei>/data` (ROM-Cache, `profiles.json`, `device.json`, `hubs/<id>/` inkl. Saves, `system/`, `probe/`), sofern dort ein echter Schreibtest gelingt. Sonst Rückfall auf AppData (`QStandardPaths::AppDataLocation`) mit Log-Hinweis (`framebeam.profiles`). Gibt es im portablen Ordner noch keine `profiles.json`, wird vorhandener AppData-Inhalt einmalig kopiert (Quelle bleibt unverändert, nichts wird überschrieben, `device_id` bleibt erhalten, ROM-Cache wird neu geladen). Credentials bleiben im OS-Credential-Store. `--data-dir` und `FRAMEBEAM_DATA_DIR` haben Vorrang.
