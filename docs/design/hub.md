# Design: FrameBeam Hub Webinterface (Go html/template + htmx, hell)

Quelle: `quelle/framebeam-designs-v3.dc.html`, Zeilenbereiche je Screen = Screen-Container (ohne Caption). Mock-Daten im Script ab Zeile 825. Tokens: `tokens.md`. Maße in px bei 1440 x 900. Der Hub hat keine Sessions-Seite (Architektur 09).

## Gemeinsame Shell (3j-3o)

- Grid: Sidebar 232 | Inhalt. Sidebar: Fläche `#efeee9`, Rahmen rechts `#e3e1dc`, Padding 24 16, Gap 28.
- Logo: 22px Quadrat (Radius 5, `#1b1b1d`) mit Akzent-Quadrat innen + "FrameBeam Hub" 16/600.
- Navigation (Reihenfolge, Item Padding 9 10, 14px, Label links, Badge rechts 12/500): Library; Saves (Badge "1 Konflikt", warn); Systeme & Cores (Badge "2 Firmware", error); Clients (Badge "1 Anfrage", warn); Benutzer; Settings. Aktiv: Fläche `#fff`, Ring 1px `#e3e1dc`, 500; inaktiv `#55544f`. Badges sind Zähler offener, handlungsrelevanter Zustände.
- Fuß der Sidebar: "admin · Admin" (13/500), Mono 12 "hub.local · v0.1.0", "Raspberry Pi 5 · arm64".
- Seitenkopf: Titel 26/600 + Untertitel 14 `#6b6a66`. Tabellen: Karte `#fff`, Radius 10, Kopfzeile Fläche `#faf9f7` mit Mono-11-Eyebrows, Zeilen Trenner `#efeee9`.
- Status-Pills (Radius 999, 12/600): ok grün, neutral grau, warn/error siehe Tokens.

## 3j Hub Library
Zeilen 492-540. Phase 1 (Library, ROM-Up-/Download).

- Layout: Sidebar | Main (Padding 32 40, Gap 24); Library aktiv.
- Kopf: Titel "Library", Untertitel "42 ROMs · 3,1 GB von 118 GB belegt"; rechts Buttons "Ordner neu scannen" (Outline) und "ROM hochladen" (Primär dunkel), je 38 hoch.
- Filterzeile: Pills "Alle Systeme" (aktiv, dunkel), "Nintendo DS · 42"; rechts Suchfeld 260 "Titel oder Hash suchen…".
- Tabelle, Spalten `2.2fr | 70 | 90 | 150 | 160 | 80`: TITEL (Cover-Platzhalter 34 + Titel 500), SYSTEM (Mono "nds"), GRÖSSE (Mono), SHA-256 (Mono, gekürzt "1b7e…04c9"), HOCHGELADEN VON ("{user} · {TT.MM}"), SAVES (Mono Zahl + "▲" warn bei Konflikt).
- Mock (9 Zeilen, Script ab Zeile 878): u. a. "Clocktower Kids" 64,0 MB 3 Saves; "Harbor Rally" 32,0 MB 6 Saves mit Konflikt-Marker; "Velvet Arcade" hochgeladen von "lena · 03.10", sonst "admin".
- Offen: Zeilenaktionen (Löschen, Metadata; Architektur 09 nennt Metadata-Aktionen im Library-Eintrag, später), Upload-Dialog, Scan-Fortschritt, Fehlerzustände (Hash/Duplikat), Paging nicht gezeichnet.

## 3k Hub Saves
Zeilen 546-633. Phase 3. Konflikt neben der Versionshistorie.

- Grid: Sidebar | Liste 320 (Fläche `#faf9f7`, Rahmen rechts, Padding 32 20) | Detail (Padding 32 40, Gap 22).
- Liste: Titel "Saves" (22/600); Filter "Benutzer: Max ▾"; Spiele (Cover 36, Titel 14/500, Zeile 12): ausgewählt Fläche `#fff` + Ring; Konflikt "▲ Konflikt · nicht aufgelöst" (warn) bzw. "Checkpoint · {Zeit}". Mock: Harbor Rally (Konflikt, ausgewählt), Lumen Drift (heute 18:42), Tide & Lantern (28.09.), Clocktower Kids (02.10.), Stylus Knights (21.09.).
- Detail-Kopf: Spieltitel "Harbor Rally" 26/600, Benutzer "Max".
- Leiste "CURRENT CHECKPOINT": "Rev 41 · Laptop-Büro · heute 19:10 · Auto-Checkpoint", rechts "Herunterladen".
- Konfliktkarte (Rahmen `#e8d3a6`, Padding 22): Titel "▲ Konflikt: Upload basiert auf Rev 40, aktuell ist Rev 41" (16/600, `#6e4a08`), Text "Beide Inhalte bleiben erhalten und werden vor der Auflösung in der History gesichert."; zwei Vergleichskarten (Label-Spalte 110: Gerät, Zeitpunkt, Basis-Revision, Inhalts-Hash Mono):
  - "Current Checkpoint auf dem Hub": "Rev 41", Laptop-Büro, "heute, 19:10", Basis "Rev 40", Hash "a3f1…08cc".
  - "Gesicherter Upload · Sync ausstehend": "Lokaler Save", Desktop-Wohnzimmer, "heute, 19:24", Basis "Rev 40", Hash "6e02…d911".
  - Aktionen (38 hoch): "Hub-Version verwenden", "Lokalen Save als neue aktuelle Version übernehmen" (Outline), "Beide behalten, später entscheiden" (Primär dunkel).
- "HISTORY · DAUERHAFTE VERSIONEN": Zeitleiste (Spalte 24 mit vertikaler Linie und Knoten | Text | Aktionen). Knoten aktuell = gefüllt Akzent mit Ring, alt = hohler Kreis. Zeile: Mono "v{n}", Gerät 500, Badge "AKTUELL" (nur aktuell), Meta "{Zeit} · {Anlass}"; Aktionen "Herunterladen" (immer), "Wiederherstellen" (nur alte). Mock (alle alt): v6 Desktop-Wohnzimmer gestern 22:30 "Session-Ende"; v5 Laptop-Büro gestern 18:05 "Gerätewechsel"; v4 Desktop-Wohnzimmer 03.10. 21:12 "Manueller Snapshot".
- Offen: "Wiederherstellen" im Hub ist in der Architektur nicht belegt (siehe README Abweichungen); Leerzustand ohne Konflikt, Bestätigung von "Wiederherstellen", Revisionsnummerierung (Rev 41 vs. v6) uneinheitlich.

## 3l Hub Systeme und Cores
Zeilen 639-700. Phase 5. Registry, Kompatibilität, Firmware.

- Layout: Sidebar | Main (Padding 28 40, Gap 14). Titel "Systeme & Cores", Untertitel "Registry: welches System welchen Core in welcher Version erwartet. Der Hub führt keine Cores aus."
- Systemkarte (2 Spalten, Padding 24, Trennlinie): links "Nintendo DS" (20/600) + Mono "nds"; Felder (Label 150): Bevorzugter Core "melonDS DS" (Mono "melonds_ds"), Erwartete Version "1.2.0", Plattformen "windows-x86_64", Bereitstellung "Im Windows-Player enthalten". Rechts "MANIFEST": Dateiendung ".nds", Input-Profil "nds", Display-Profil "dual_screen", "BIOS / Firmware" "laut Manifest · siehe unten".
- "VON CLIENTS GEMELDET": Tabelle Spalten `1.2fr | 150 | 260 | 1.4fr`: Gerät, Plattform (Mono), "Player {Version} · melonDS DS {Version}" (Mono), Status "● kompatibel" (grün) oder Fehlertext (error/500). Mock: Desktop-Wohnzimmer (Player 0.1.0, Core 1.2.0, kompatibel); Lenas Gaming-PC (Core 1.1.3, "Core-Version mismatch · 1.2.0 erwartet"); Notebook Jonas (Player 0.0.9, "Player zu alt · Protokoll v1 nötig").
- "BIOS / FIRMWARE · NDS" + Text "vom Admin bereitgestellt, nicht mit FrameBeam ausgeliefert", rechts Button "Datei bereitstellen" (dunkel, 32 hoch). Tabelle Spalten `1fr | 120 | 1.5fr | 150 | 150`: DATEI (Anzeigename + Dateiname Mono), BEDARF ("erforderlich"), SHA-256 ERWARTET / VORHANDEN (gekürzt, Mono 12, zwei Zeilen), STATUS (Pill), Aktion.
  - Zeile 1 (ARM7-BIOS): "✓ Valid" (grün), keine Aktion ("—").
  - Zeile 2 (ARM9-BIOS): "✕ Hash mismatch" (voll rot, Zeile `#fbf1ef`, abweichender Hash rot), Aktion "Ersetzen".
  - Zeile 3 (DS-Firmware): "○ Missing" (gestrichelt rot, Zeile `#fdf8f6`, vorhanden "—"), Aktion "Bereitstellen".
  - Dateinamen in der Quelle sind nicht übernommen (Regel: keine echten BIOS-Namen); Anzeigenamen "ARM7 BIOS", "ARM9 BIOS", "DS Firmware" stehen so in der Quelle.
- "Core Package Cache" (gestrichelt, Badge "SPÄTER"): "Hier werden später Core-Pakete mit Version, Plattform, SHA-256, Herkunft und Lizenz gespeichert und an Player verteilt. In 0.1 nicht aktiv."
- Offen: Datei-Upload-Dialog, Entfernen einer Firmware, weitere Systeme; die Nav-Badge "2 Firmware" entspricht den Zeilen 2 und 3.

## 3m Hub Clients
Zeilen 706-742. Phase 1 (Pairing Allow/Deny, Revoke).

- Layout: Sidebar | Main (Padding 32 40, Gap 26). Titel "Clients", Untertitel "Geräte, die auf diesen Hub zugreifen dürfen".
- "AUSSTEHENDE ANFRAGEN · 1" (Eyebrow): Karte (Rahmen `#e8d3a6`, Padding 20 22; Spalten `1fr | 220 | auto`): "Gerätename" / "Lenas Gaming-PC" (20/600), Mono "Windows x86-64 · Player 0.1.0 · Protokoll v1", "angefragt vor 2 min"; Select "Benutzer zuordnen" (Wert "Lena"); Buttons "Ablehnen" (Outline) und "Erlauben" (Primär dunkel).
- "GERÄTE": Tabelle Spalten `1.3fr | 110 | 140 | 90 | 140 | 100 | 150`: GERÄT, BENUTZER, PLATTFORM (Mono), PLAYER (Mono), ZULETZT AKTIV, STATUS (Pill), Aktion.
  - Trusted (grün) mit Aktion "Zugriff widerrufen" (rot, Textlink): Desktop-Wohnzimmer (Max, 0.1.0, "gerade eben"), Laptop-Büro (Max, 0.1.0, "heute 19:10"), Notebook Jonas (Jonas, 0.0.9, "gestern"), Desktop admin (admin, 0.1.0, "02.10.").
  - Revoked (grau, Zeile gedämpft, Aktion "—"): Alter Laptop (Max, 0.0.9, "12.09.").
- Fußhinweis: "Widerrufen entzieht dem Gerät sofort den Zugriff, auch mit noch gültigen Access Tokens."
- Offen: Bestätigungsdialog für Widerrufen, Abgelehnt-Zustand, Umbenennen, Wiedereinsetzen eines Revoked-Geräts, Zertifikat-/Kompatibilitätsfehler pro Client (nur in 3l) nicht gezeichnet.

## 3n Hub Benutzer
Zeilen 748-785. Phase 5. Hub-lokale Konten und Onboarding-Einladungen.

- Grid: Sidebar | Main | Seitenleiste 420 (Fläche `#faf9f7`, Rahmen links, Padding 32 28, Gap 18).
- Main: Titel "Benutzer", Untertitel "Konten gelten nur auf diesem Hub. Normale Benutzer haben kein Passwort." Tabelle Spalten `1.2fr | 80 | 70 | 110 | 110 | 110`: NAME (Avatar-Initiale 28 + Name), ROLLE, GERÄTE (Mono Anzahl), ERSTELLT, STATUS (Pill Active/Disabled), Aktion.
  - Mock: admin (Admin, 1, 14.08.2026, Active, "—"); Max (User, 2, 14.08.2026, Active, "Deaktivieren" rot); Lena (User, 1, 20.08.2026); Jonas (User, 1, 02.09.2026); Sam (User, 1, 11.09.2026, Disabled, Zeile gedämpft, Aktion "Aktivieren").
  - Fußhinweis: "Deaktivierte Benutzer können sich mit keinem Gerät anmelden. Saves und Uploads bleiben erhalten."
- Seitenleiste "Onboarding-Einladungen" (18/600): Text "Die eingeladene Person löst den Code im Player ein und wählt einen Anzeigenamen. Das ist keine Session-Einladung."; Button "Einladung erstellen" (dunkel, 36).
  - Aktive Einladung (Karte): "Aktiv", "läuft ab in 42 min" (warn); Code Mono 24 "FB-7KQ2-M9XD"; Toggle (an) "Erstes Gerät direkt freigeben"; "Einmalig nutzbar"; Buttons "Link kopieren" (Outline), "Widerrufen" (rot).
  - Verlauf (gedämpft): "FB-2HC9-…" "eingelöst von Jonas · 02.09."; "FB-Q81M-…" "abgelaufen · 30.09.".
- Offen: Dialog "Benutzer anlegen" (kein Button gezeichnet), Rollenwechsel, Löschen, Ablaufdauer wählbar (unklar); Beispielcodes sind Mock.

## 3o Hub Settings
Zeilen 791-819. Phase 1 (Hub-Name, Adresse, Zertifikat/TLS, Admin-Konto); Phase 5 (Darstellung, Upload-Option; "Allow users to upload games" setzt Benutzer voraus).

- Layout: Sidebar | Main (Padding 32 40, Gap 24). Titel "Settings", Untertitel "Nur für Admins". Zwei Spalten (Gap 20), Karten (Padding 18 22 6, Radius 10, Titel 16/600); Zeilen: Label-Block links (14/500 + Hilfetext 12), Steuerelement rechts (Feld 200 x 34 oder Segment/Toggle), Trenner oben.
- Linke Spalte:
  - "Allgemein": "Hub-Name" (Hilfe "Wird Playern bei der Identifikation angezeigt", Wert "Zuhause"); "Adresse für Player" (Wert "hub.local:8443").
  - "Darstellung": "Modus" (Hilfe "Gilt für dieses Webinterface"), Segment "Hell" (aktiv) | "Dunkel" | "System".
  - "Library": "Allow users to upload games" + Badge "Inaktiv", Text "Aktuell können nur Admins ROMs hochladen.", Hilfe "Eigene Uploads landen in derselben Library und werden mit uploaded_by gekennzeichnet. Verwalten oder Löschen fremder Einträge bleibt Admins vorbehalten."; Toggle aus.
- Rechte Spalte:
  - "Transport & Zertifikat": "Zertifikat" (Hilfe "HTTPS/WSS aktiv. HTTP nur im Dev-Modus oder auf localhost."), Segment "Selbst erzeugt" (aktiv) | "Eigenes cert/key" | "Reverse Proxy"; "Fingerprint": SHA-256 (Mono 12, zwei Zeilen) + "Player vergleichen diesen Wert beim ersten Verbinden · gültig bis 05.10.2027".
  - "Admin-Konto": "admin" (Hilfe "Web-Login mit Benutzername und Passwort"), Button "Passwort ändern".
  - "Metadata" (gestrichelt, Badge "SPÄTER"): "Provider, Sprache und Region".
- Offen: Konfiguration bei "Eigenes cert/key" und "Reverse Proxy", Speichern-Mechanik (kein Speichern-Button; bei htmx evtl. sofort, unklar), Dark-Variante des Hub-Designs (nur Hell gezeichnet).
