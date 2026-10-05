# Architektur: Controllers

## 12. Controllers und lokale Input-Profile

Der Player erhält eine eigene Seite **„Controllers“**, getrennt von Emulation/Core Settings. Sie bietet lokale Controllerprofile, Geräte-/Spielerzuordnung, Remapping, Zurücksetzen und einen Input-Test. Gamepads und Tastatur können eigene Profile haben; DS-Touch über Maus bleibt Teil des PoC.

```text
Physischer Controller / Tastatur
              ↓
Lokales Controllerprofil / Remapping
              ↓
FrameBeam Input
              ↓
Systemprofil-Mapping, z. B. nds
              ↓
Libretro Input
```

Das Systemprofil übersetzt die einheitlichen FrameBeam-Eingaben in die Eingaben des emulierten Systems. Controllerprofile und gerätespezifische Zuordnungen bleiben **lokal im Player und werden nicht zentral synchronisiert**. Sie sind keine Hub-Verwaltungsfunktion.
