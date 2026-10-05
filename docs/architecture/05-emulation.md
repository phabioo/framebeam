# Architektur: Emulation, Cores, Einstellungen

## 6. Erweiterbarkeit für weitere Emulatoren

FrameBeam wird nicht direkt an melonDS gekoppelt. Die gemeinsame Emulator-Schnittstelle trennt Anwendungslogik und UI vom jeweiligen Backend:

```text
FrameBeam → EmulatorBackend
              ├── LibretroBackend
              │      ├── melonDS DS  [PoC]
              │      ├── mGBA        [später möglich]
              │      └── Snes9x      [später möglich]
              └── StandaloneBackend [spätere Option]
```

Systeme und Cores werden datengetrieben über Manifeste beschrieben, statt konsolenspezifische Startlogik in der Anwendung zu verteilen. Ein NDS-Manifest enthält beispielsweise System-ID `nds`, Core-Zuordnung `melonds_ds`, Dateiendung `.nds`, BIOS-/Firmware-Angaben, Input-Profil `nds` und Display-Profil `dual_screen`.

Neue Libretro-Systeme sollen im Regelfall durch Core, Manifest und passende Profile hinzukommen. Zusätzliche Backend-Arbeit bleibt möglich, wenn ein Emulator besondere Anforderungen hat. Konkrete BIOS-/Firmware-Anforderungen werden je System/Core im Manifest beschrieben. Für den PoC wird melonDS DS mit dem Windows-Player ausgeliefert; automatische Core-Verteilung über den Hub folgt später.

### BIOS/Firmware über den Hub [PoC]

BIOS/Firmware wird zentral durch den **Admin** bereitgestellt und einem System/Core zugeordnet. FrameBeam liefert proprietäre BIOS-/Firmware-Dateien nicht automatisch mit und lädt sie nicht eigenständig aus dem Internet. Normale User verwalten diese Dateien nicht.

Der Hub validiert erwartete Metadaten und Hashes gemäß System-/Core-Anforderungen und liefert benötigte, bereitgestellte Dateien an autorisierte Player aus. Der Player validiert und cached sie lokal, getrennt von ROMs und Cores. Benötigt ein Core eine nicht verfügbare Datei, zeigt er klar **„Firmware required/missing“** und verhindert den betroffenen Start. Cores ohne BIOS-/Firmware-Bedarf funktionieren ohne diesen Pfad. Die konkreten Anforderungen des PoC-Cores und Manifest-/Dateiformate bleiben zu spezifizieren; der Bereitstellungs- und Fehlerpfad gehört zum PoC.


## 10. Systeme & Cores: Registry und spätere Paketbereitstellung

Die Hub-Seite **„Systeme & Cores“** bleibt erhalten. Sie verwaltet die System Registry und Core Registry: System-ID, zugeordnete beziehungsweise bevorzugte Cores, Core-ID, erwartete Version und unterstützte Plattformen. Der Hub führt Cores niemals aus.

Für FrameBeam 0.1 gilt: `nds → melonds_ds`; melonDS DS ist Bestandteil des Windows-Players. Der Player meldet Plattform und verfügbare Core-Versionen, damit die Kompatibilität geprüft werden kann. Eine fehlende oder unpassende Core-Version wird sichtbar gemeldet; der PoC enthält keinen automatischen Core-Paketmanager.

Später kommt ein **Core Package Cache** auf dem Hub hinzu. Registrierte Pakete enthalten Core-ID, Version, Plattform/Architektur, SHA-256 und Herkunft sowie die für die Verteilung erforderlichen Lizenzinformationen. Für die spätere automatische Core-Verteilung sind außerdem Paket-Signaturen beziehungsweise vertrauenswürdige Manifestquellen vorzusehen; SHA-256 allein bestätigt keine vertrauenswürdige Herkunft. Diese Vertrauensprüfung erweitert den PoC nicht. „Auf Hub gecacht“ bezeichnet ein gespeichertes Paket, keine dort installierte oder ausgeführte Emulation.

Der spätere Ablauf ist: Player fragt den vorgesehenen Core an → Hub liefert Paketmetadaten → Player prüft lokale Version, Plattform und Hash → Download nur bei Bedarf → Hash-/Versionsprüfung → Aufnahme in den lokalen Core-Cache → lokale Ausführung. Mehrere Versionen können parallel gecacht werden; eine spätere versionsgebundene Spielzuordnung bleibt möglich.

ROM-Cache und Core-Cache bleiben getrennt, beispielsweise:

```text
Player Data/cache/
├── roms/<sha256>.nds
└── cores/<core-id>/<version>/<platform>/
```

### Sichtbare Cache- und Bereitschaftszustände

Library und Emulation zeigen verständliche Zustände für die jeweils benötigte ROM beziehungsweise den Core. Verfügbarkeit, Transfer und Validierung werden getrennt erfasst, damit ein Paket zugleich auf dem Hub vorhanden und lokal fehlerhaft sein kann.

| Zustand | Bedeutung / UI-Aktion |
|---|---|
| Lokal gecacht / bereit | Lokale Datei ist vorhanden und validiert; im PoC kann der Core auch mitgeliefert sein |
| Nur auf Hub vorhanden | ROM oder späteres Core-Paket liegt auf dem Hub, lokal fehlt es |
| Download nötig | Benötigte lokale Datei fehlt; Download anbieten beziehungsweise beim Start auslösen |
| Download läuft / fehlgeschlagen | Fortschritt beziehungsweise verständlicher Fehler mit Wiederholungsmöglichkeit |
| Hash mismatch | Lokale Datei stimmt nicht mit dem erwarteten SHA-256 überein; nicht verwenden, erneut beziehen |
| Version mismatch | Core-Version entspricht nicht der vorgesehenen Version; passende Version erforderlich |
| Core nicht verfügbar / inkompatibel | Kein passender Core für die Player-Plattform; Start nicht möglich |

Der Status ist auf das jeweilige Gerät und Artefakt bezogen. Die Hub-Registry kann gemeldete Client-Kompatibilität anzeigen; sie suggeriert keine Core-Ausführung auf dem Hub. Vollständige Hashes und weitere technische Details können in einer Detailansicht liegen.

## 11. Emulation und Einstellungs-Hierarchie im Player

Der Player erhält eine eigene Seite **„Emulation“** mit Systemen, verfügbaren Cores, Version und Bereitschaftszustand sowie deren Konfiguration. Sie umfasst allgemeine, Grafik-, Audio- und core-spezifische Optionen, soweit der jeweilige Core sie unterstützt.

FrameBeam-eigene Optionen wie Vollbild, UI-Skalierung, Präsentation und Multiview-Layout bleiben von Emulator-Optionen wie interner Auflösung, Renderer und core-spezifischer Audioverarbeitung unterscheidbar.

Core-Einstellungen sollen möglichst **dynamisch aus Libretro Core Options** erzeugt werden. Gemeldete Kategorien, Beschreibungen, zulässige Werte und Defaults dienen als Grundlage. Enumerierte Optionen erscheinen als Auswahlfelder; Toggle, Zahlenfeld, Slider oder Dateiauswahl werden nur verwendet, wenn passende Typ-/Validierungsinformationen vorliegen. Libretro-Optionen liefern nicht automatisch für jede Option ein freies Zahlen- oder Pfadfeld. Zusätzliche FrameBeam-Metadaten oder Backend-Adapter können die Darstellung ergänzen. Nicht unterstützte Optionen werden nicht erfunden; nötige Neustarts beziehungsweise verzögerte Wirksamkeit sind kenntlich zu machen.

Die Konfiguration sieht folgende Hierarchie vor:

```text
Global → System/Core → Game Override
```

Eine spezifischere Ebene überschreibt nur explizit gesetzte Werte. Nicht gesetzte Werte werden geerbt; das Entfernen eines Overrides stellt die Vererbung wieder her. Das Datenmodell speichert deshalb partielle Overrides mit System-/Core- beziehungsweise Game-Zuordnung statt vollständiger Konfigurationskopien. Die UI soll Herkunft und wirksamen Wert erkennbar machen. Globale Vorgaben gelten nur, soweit der ausgewählte Core sie unterstützt; core-spezifische Optionsschlüssel bleiben dem Core zugeordnet.

Im PoC bleiben Emulations-Einstellungen lokal. Die Hierarchie wird konzeptionell und im Datenmodell vorbereitet; eine vollständige Game-Override-UI ist keine Voraussetzung für den PoC. Eine optionale spätere Synchronisierung von Emulations-Einstellungen über den Hub ist offen.
