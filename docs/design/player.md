# Design: FrameBeam Player (QML, dunkel)

Quelle: `quelle/framebeam-designs-v3.dc.html`, Zeilenbereiche je Screen = Screen-Container (ohne Caption). Mock-Daten stehen im Script ab Zeile 825 (`renderVals`). Tokens: `tokens.md`. Alle Maße in px bei 1440 x 900.

## Gemeinsame Shell (3c, 3e, 3f)

- Grid: Sidebar 232px | Inhalt (je Screen 1-3 weitere Spalten). Sidebar: Hintergrund `#0e0f10`, Rahmen rechts, Padding 24 16, Gap 28.
- Sidebar oben: Logo (22px Quadrat, Radius 5, Akzent) + "FrameBeam Player" 16/600.
- Navigation (Reihenfolge): Library, Emulation, Controllers, Settings. Aktiv: Fläche `#1f2024`, Radius 6, 500; inaktiv `#a3a3a8`. Item Padding 9 10, 14px.
- Unten: Hub-Switcher-Karte (Statuspunkt grün, Hubname, rechts "wechseln", darunter Mono 11 "hub.local · aktiver Hub"). Nur 3c zeigt zusätzlich den Benutzerblock (Avatar 28 "M", "Max", Gerätename "Desktop-Wohnzimmer").
- Im Spiel (3g-3i) entfällt die Sidebar; stattdessen Header 56px (siehe dort).

## 3a Player Connection
Zeilen 30-52. Start-/Connection-Screen vor der Hauptnavigation.

- Zweck: gespeicherte Hubs, Verbindungsstatus, Hub hinzufügen, Verbinden.
- Layout: Screen zentriert (`place-items:center`), Spalte 620px, Gap 28; Hintergrund `#121315`, keine Sidebar.
- Kopf: Logo + "FrameBeam Player", Titel "Mit einem Hub verbinden" (30/600).
- Hub-Karten (Spalte, Gap 10, Padding 16 18, Radius 10, Fläche `#1a1b1e`); je Karte Name 16/600 + Statuszeichen, Mono 12 Zeile mit Adresse und Details:
  - Zustand erreichbar (gewählt, Akzentring 1.5): "Zuhause", "● erreichbar" (grün), "hub.local:8443 · als Max · zuletzt heute 18:40", Button "Verbinden" (40 hoch, Akzent).
  - Zustand Zertifikat geändert: "Studio Lena", "✕ Zertifikat geändert" (error), "lena-hub.fritz.box:8443 · als Max", Link "Fingerprint prüfen"; Fehlerbox (Fläche `#2a1a17`): "Der Fingerprint stimmt nicht mehr mit dem gespeicherten überein. Die Verbindung ist blockiert, bis du den neuen Fingerprint im Hub-Webinterface geprüft und bestätigt hast." Kein Verbinden-Button.
  - Zustand Hub zu alt: "Büro", "▲ Hub zu alt" (warn), "hub.office.lan · Hub spricht Protokoll v0, Player benötigt v1", Aktion "Entfernen".
- Hub hinzufügen: Eingabefeld (44 hoch, Platzhalter "Hub-Adresse, z. B. hub.local:8443", Mono) + Button "Hub hinzufügen" (Outline).
- Fuß (Trennlinie): Toggle (an) "Beim Start automatisch mit dem zuletzt verwendeten Hub verbinden"; Mono 12 "Dieses Gerät: Desktop-Wohnzimmer · Player 0.1.0 · Windows x86-64".
- Zustände: erreichbar, Zertifikat geändert (blockiert), Hub inkompatibel (Protokoll). Offline/nicht erreichbar: nicht gezeigt (unklar).
- Entfernen nur beim inkompatiblen Hub gezeigt; ob es bei allen Karten verfügbar ist, ist unklar.

## 3b Player Pairing
Zeilen 58-72. Hub hinzufügen: Zertifikat (TOFU) und Freigabe durch Admin.

- Layout: zentriert, Spalte 640px, Gap 14. Titel "Hub hinzufügen" (30/600) + Mono 13 Adresse "hub.local:8443".
- Drei Schrittkarten (Raster 18px | Text, Gap 4 12; Statuspunkt 10px; Radius 10, Padding 16-18):
  1. "Hub erkannt": "Zuhause · FrameBeam Hub 0.1.0 · Protokoll v1 · kompatibel" (Punkt gefüllt, Akzent).
  2. "Zertifikat vertraut": SHA-256-Fingerprint in Mono 12 (zwei Zeilen, Hex-Paare), Hinweis "Gespeichert. Ändert sich das Zertifikat später, wird die Verbindung blockiert." (Punkt gefüllt).
  3. "Gerät freigeben" (aktiv, Akzentring; Punkt als Ring):
     - Segment-Umschalter: "Freigabe anfragen" (aktiv) | "Einladung einlösen".
     - Status: Spinner + "Warte auf Freigabe durch den Admin" / "Die Anfrage erscheint im Hub unter Clients."
     - Datenblock (Label-Spalte 130px): Gerätename "Desktop-Wohnzimmer", Plattform "Windows x86-64", Player-Version "0.1.0".
     - Button "Anfrage abbrechen" (38 hoch, Outline).
- Nicht gezeigt: Ansicht "Einladung einlösen" (Code-Eingabe, Anzeigename), Zustände abgelehnt/widerrufen/Fehler (unklar).

## 3c Player Library
Zeilen 78-175. Hauptscreen; Bereitschaftszustände, Save-Konflikt, Sessions entdecken.

- Grid: Sidebar 232 | Main (flexibel) | Detailspalte 392 (Fläche `#16171a`, Rahmen links, Padding 28).
- Main (Padding 28 32, Gap 28):
  - Header: Titel "Library" (26/600), Meta "42 Spiele · Nintendo DS", rechts Suchfeld 220 x 34 ("Suchen…").
  - Filter-Chips (Pills): "Alle" (aktiv), "Bereit", "Achtung nötig · 2".
  - Abschnitt "SESSIONS AUF DIESEM HUB" (Eyebrow grün mit Punkt); 2-spaltiges Raster, Gap 12; Karte: Avatar 30, "{User} · {Spiel}", Meta 12. Beispiele: "Lena · Harbor Rally", Meta "Hub users · seit 24 min", Aktion "Session ansehen"; "Jonas · Clocktower Kids", Meta "lädt dich ein", Aktionen "Ablehnen" / "Beitreten" (Akzent).
  - Spielraster: 4 Spalten, Gap 20 18. Kachel: quadratisch, Mono 11 "NDS" oben, Monogramm (Initialen, 36/600) unten; Auswahl = Akzentrahmen 2px (Offset -3, Radius 8). Darunter Titel 14/500 (Ellipsis) und Status 12.
  - Statusvarianten pro Spiel: "● Bereit"; "↓ Download nötig · {Größe}"; "▲ Save-Konflikt" (warn, 500); "✕ Hash mismatch · neu laden" (error, 500); "⟳ Save-Sync ausstehend".
  - Mock: Lumen Drift (bereit, ausgewählt), Harbor Rally (Konflikt), Paper Wizards (Download 128 MB), Clocktower Kids (bereit), Orbit Gardens (Download 16 MB), Tide & Lantern (Sync ausstehend), Copper Courier (Hash mismatch), Stylus Knights (bereit).
- Detailspalte (Auswahl):
  - Kopf: Cover 112 + Titel "Lumen Drift" (22/600) + "Nintendo DS".
  - Tabelle (Label links gedämpft, Wert rechts, Zeilen 11 Padding): ROM "Lokal gecacht · geprüft"; Core "melonDS DS · bereit"; Firmware "vom Hub · geprüft"; Spielstand "Checkpoint aktuell · 18:42"; "Zuletzt gespeichert von" "Laptop-Büro".
  - Link "Details anzeigen (Hashes, Größe, Pfade)" (12, gedämpft).
  - Abschnitt "START" (Eyebrow): Checkliste, Zeile Raster 18 | Label | Meta; Punkt gefüllt = erledigt, Ring = aktiv: "Spieldaten vom Hub" (aktuell), "ROM aus Cache geprüft" (kein Download), "Firmware vorhanden" (geprüft), "Current Checkpoint geladen" (Rev 88), "melonDS DS startet" (aktiv).
  - Unten: Primärbutton "Spielen" (48, Akzent), Sekundär "Spielen und Session teilen" (44, Outline).
- Offen/unklar: Zustände der Detailspalte bei Download, Hash mismatch und Konflikt (nur der Erfolgsfall ist gezeichnet); Suche/Filter-Verhalten; "Achtung nötig · 2" zählt laut Mock-Daten nicht eindeutig (Konflikt, Hash mismatch, Sync ausstehend sind drei Kacheln).

## 3d Player Save-Konflikt
Zeilen 181-209. Modaler Dialog beim Start-Abgleich.

- Layout: Screen `#08090a` mit abgedunkelter Library (Streifenmuster, Beschriftung "Library (abgedunkelt)" oben links, Mono 12); Dialog zentriert, 760 breit, Fläche `#16171a`, Rahmen `#2c2d32`, Radius 12, Padding 32, Gap 24.
- Kopf: Eyebrow "▲ SAVE-KONFLIKT · HARBOR RALLY" (warn), Titel "Hub und dieses Gerät haben unterschiedliche Stände" (24/600), Text "Dieses Gerät hat offline weitergespielt, während ein anderes Gerät einen neuen Checkpoint gesichert hat. Nichts wird überschrieben, bis du entscheidest. Beide Stände werden vorher in der History gesichert."
- Zwei Vergleichskarten (2 Spalten, Fläche `#1d1e22`, Radius 9):
  - "Auf dem Hub": "Current Checkpoint · Laptop-Büro"; "heute, 19:10 · Rev 41"; "Basis: Rev 40".
  - "Auf diesem Gerät": "Lokal · Desktop-Wohnzimmer"; "heute, 19:24 · Sync ausstehend"; "Basis: Rev 40".
- Aktionen (Spalte, 46 hoch, Gap 8; rechts jeweils Hinweis 12):
  - "Hub-Version verwenden" (Outline; "lokaler Save bleibt gesichert").
  - "Lokalen Save als neue aktuelle Version übernehmen" (Outline; "wird neuer Checkpoint").
  - "Beide behalten, später entscheiden" (Primär, Akzent; hervorgehoben als Standard).
- Fußhinweis: "Später entscheiden: Der Konflikt bleibt beim Spiel und auf der Saves-Seite im Hub sichtbar."
- Offen: Bestätigungsschritt vor überschreibender Aktion, Fehlerfall beim Sichern, Tastaturbedienung nicht gezeichnet.

## 3e Player Emulation
Zeilen 215-276. Cores, Bereitschaft, Einstellungs-Hierarchie.

- Grid: Sidebar (Emulation aktiv, ohne Benutzerblock) 232 | Systemliste 300 (Fläche `#16171a`) | Main.
- Systemliste: Eyebrow "EMULATION"; Karte (ausgewählt, Fläche `#1f2024`): "Nintendo DS", "melonDS DS · 1.2.0", "● Bereit · im Player enthalten" (grün), "Firmware vom Hub · geprüft"; Platzhalterkarte gestrichelt "Weitere Systeme" / "Kommen später über Core, Manifest und Profile hinzu."
- Main (Padding 28 36, Gap 22): Titel "Nintendo DS · melonDS DS", Untertitel "Einstellungen gelten lokal für dieses Gerät".
- Ebenen-Umschalter (Segment): "Global" | "System / Core" (aktiv) | "Game Override · später" (deaktiviert); Hinweis daneben "Global → System/Core → Game Override · nur explizit gesetzte Werte überschreiben".
- Optionsgruppen (Titel 15/600 + Untertitel 12, Trennlinie); Zeile = Raster `1fr | 220 | 200`, Gap 20:
  - Spalte 1: Label 14 (+ Badge "Neustart nötig", warn) und Beschreibung 12.
  - Spalte 2: Select-Feld 34 hoch (Wert + "▾").
  - Spalte 3: Herkunft: "geerbt · {Quelle}" (gedämpft) oder "● hier gesetzt" (warn-Farbe) + Link "zurücksetzen".
- Gruppe "FrameBeam" (Untertitel "Darstellung im Player"): "Vollbild beim Start" (Aus, geerbt Global), "DS-Bildschirm-Anordnung" (Übereinander, hier gesetzt), "Standard-Multiview" (Picture-in-Picture, geerbt Global).
- Gruppe "melonDS DS" (Untertitel "aus Libretro Core Options · nur vom Core gemeldete Optionen"): "Renderer" (OpenGL, hier gesetzt, Neustart nötig), "Interne Auflösung" (3× (768×576), hier gesetzt), "Konsolentyp" (DS, geerbt Core-Default, Neustart nötig), "Audio-Interpolation" (Keine, Core-Default), "Touch-Modus" (Maus, Core-Default).
- Offen: Ansichten "Global" und "Game Override", Zustand "Firmware fehlt" (Architektur: Start blockieren) nicht gezeichnet.

## 3f Player Controllers
Zeilen 282-344. Profile, Remapping, Input-Test.

- Grid: Sidebar (Controllers aktiv) 232 | Geräteliste 300 | Main | Input-Test 340 (Fläche `#16171a`).
- Geräteliste: Eyebrow "GERÄTE"; Einträge (Radius 8, Padding 12; ausgewählt Fläche `#1f2024`): Name 14/500, rechts Slot (Mono 11), darunter Profil 12. Mock: "Xbox Wireless Controller" P1 "Standard Gamepad" (ausgewählt); "Tastatur" Slot "—" "Tastatur · Standard"; "Maus" Slot "Touch" "DS-Touch". Fuß: "Profile bleiben lokal auf diesem Gerät und werden nicht synchronisiert."
- Main: Titel "Xbox Wireless Controller", rechts Profil-Select "Profil: Standard Gamepad ▾" (34 hoch).
- Tabelle Raster `1fr | 180 | 120`: Kopf (Eyebrow Mono 11) "FRAMEBEAM INPUT" | "BELEGUNG" | "NDS". Zeilen (Padding 8): Input | Belegungsfeld (30 hoch) | NDS-Ziel (Mono). Mock: A→"Ⓑ  B"→A; B→"Ⓐ  A"→B; X→"Ⓨ  Y"→X; Y→"Ⓧ  X"→Y; L→"LB"→L (Zustand Lauschen: Rahmen Akzent, Text "Taste drücken…"); R→"RB"→R; Start→"Menu"→START; Select→"View"→SELECT; Steuerkreuz→"D-Pad / linker Stick"→D-PAD; "Lid schließen"→"nicht belegt"→LID.
- Aktionen unten: "Auf Standard zurücksetzen", "Profil duplizieren" (Outline, 38 hoch).
- Input-Test: Eyebrow "INPUT-TEST", Text "Drücke Tasten am Controller — aktive Eingaben leuchten auf."; Raster 4 Spalten, Kacheln 44 hoch: A, B, X, Y, L, R, ▲, ▼, ◀, ▶, ST, SE; aktive Kachel = Akzentfläche (Mock: A und ▶). Abschnitt "DS-TOUCH": Fläche 150 hoch mit Text "Maus auf unterem Bildschirm", Cursor-Ring; Hinweis "Linke Maustaste = Stylus".
- Offen: Analogsticks/Trigger-Darstellung, Slot-Zuweisung und Profil-Verwaltung (Löschen/Umbenennen) nicht gezeichnet; SDL3-Mapping-Details nicht Teil des Designs.

## 3g Player Session
Zeilen 350-391. Im Spiel, Session geteilt, Sichtbarkeit "Invite only" mit Nutzerauswahl.

- Grid: Zeilen 56 | 1fr; Spalten 1fr | 340; Hintergrund `#0b0b0c`; keine Sidebar.
- Header (über beide Spalten, Fläche `#111214`, Padding 0 20): "← Library", Trenner, Spieltitel "Lumen Drift" (15/600), Pill grün "Session geteilt · 1 sieht zu"; rechts Tab-Segment "Session" (aktiv) | "Multiview" | "Diagnostics".
- Spielfläche: zwei DS-Bildschirme übereinander (je 480 x 360, Platzhalter "oberer DS-Bildschirm" / "unterer Bildschirm · Touch per Maus", unterer gestrichelt umrandet).
- Seitenleiste 340 (Fläche `#111214`, Padding 24, Gap 20):
  - "SICHTBARKEIT": Segment "Private" | "Hub users" | "Invite only" (aktiv).
  - "EINGELADEN · 2" mit Hinweis rechts "nur du kannst ändern"; Liste: "Lena" (grüner Punkt, "sieht zu", Aktion "Entfernen"), "Jonas" (grauer Punkt, "eingeladen · offline", Aktion "Zurückziehen").
  - Einladungsfeld (Akzentrahmen, Eingabe "Sa", Hinweis rechts "Nutzer dieses Hubs") mit Ergebnisliste: "Sam" ("online"), "Sarah" ("offline · erhält die Einladung, solange die Session läuft"), je Button "Einladen" (Akzent).
  - Hinweis: "Eingeladene sehen und hören nur. Sie senden keine Eingaben und können nicht weiter einladen."
  - Unten: "Checkpoint gesichert vor 40 s · Final-Sync bei Pause oder Beenden"; Buttons "Teilen beenden" (Outline), "Spiel beenden und speichern" (Fläche `#1f2024`); einklappbar "▸ Diagnostics einblenden".
- Offen: Ansichten für Sichtbarkeit "Private" und "Hub users", Verhalten wenn Hub oder Verbindung ausfällt, Kopf-Pill ohne Session unklar.

## 3h Player Side-by-Side
Zeilen 397-438. Multiview, Side-by-Side, Diagnostics ausgeklappt.

- Grid: Zeilen 56 | 1fr | auto; Hintergrund `#0b0b0c`.
- Header: "← Library", Trenner, Titel "Multiview", Modus-Segment "PiP" | "Side-by-Side" (aktiv); rechts Tab-Segment "Session" | "Multiview" (aktiv) | "Diagnostics" (Akzent-Unterstrich, da ausgeklappt).
- Mitte: zwei gleich breite Spalten (Gap 2, Hintergrund `#1e1f22` als Trenner); je Spalte: Kopf mit Avatar 28, "{Wer} · {Spiel}", Meta 12 und Ton-Button ("Ton aktiv" Akzent oder "Ton hierher" Outline); darunter zwei Bildschirme (je 360 x 270, Platzhalter "{Label} · oben/unten").
- Mock (Script, Zeilen 871-878): Surface 1 "Du · Lumen Drift", Meta "lokal", Ton aktiv; Surface 2 "Lena · Harbor Rally", Meta "Session von Lena", Button "Ton hierher".
- Diagnostics-Mock: "lokal" 60.0 fps · Encoder NVENC · H.264 · 6,0 Mbit/s · Opus 128 kbit/s; "Lena" 59.9 fps · WebRTC direkt · RTT 14 ms · 5,8 Mbit/s · Verlust 0,1 %.
- Diagnostics-Panel unten (Fläche `#111214`, Rahmen oben): Titel "▾ Diagnostics" + "technische Details · optional"; zwei Spalten, je Teilnehmer Mono-12-Zeile (Name + vier Werte `a` bis `d`).
- Genau ein Surface hat Ton; Wechsel über "Ton hierher".

## 3i Player PiP
Zeilen 444-476. Multiview, Picture-in-Picture.

- Layout wie 3h-Header, Modus-Segment mit "PiP" aktiv; Tab "Multiview" aktiv, "Diagnostics" nicht hervorgehoben.
- Hauptbild: lokale Session, zwei Bildschirme (480 x 360, Platzhalter "lokal · oben/unten") zentriert.
- PiP-Fenster unten rechts (Abstand 28, Breite 248, Fläche `#16171a`, Radius 10, Padding 8): Kopfzeile grüner Punkt, "Lena · Harbor Rally", rechts "stumm"; zwei Remote-Bildschirme (je 174 hoch, "remote · oben/unten"); Buttons "Tauschen" und "Entfernen" (je 50 %).
- Offen: Verschieben/Größe des PiP-Fensters, mehrere PiPs, Ton-Steuerung im PiP (nur Status "stumm") nicht gezeichnet.
