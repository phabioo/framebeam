# Design-Tokens

Extrahiert aus `quelle/framebeam-designs-v3.dc.html` (nur tatsächlich vorkommende Werte; Rollen sind aus der Verwendung abgeleitet, die Quelle benennt keine Tokens). Gemeinsam: Fonts, Akzent, Layoutraster. Canvas-Chrome der Prototypseite (Hintergrund `#d9d8d4`, Überschriften `#3a3936`/`#5b5a56`, Links `#9a6a12`) ist kein UI und nicht aufgeführt.

## Gemeinsam

| Token | Wert | Hinweis |
|---|---|---|
| Font Sans | IBM Plex Sans, Fallback `system-ui, sans-serif` | Gewichte 400, 500, 600; Antialiasing `-webkit-font-smoothing: antialiased` |
| Font Mono | IBM Plex Mono, Fallback `monospace` | Gewichte 400, 500; Hashes, Adressen, Versionen, Eyebrow-Labels |
| Akzent | `#e9b44c` | Primärbutton und Fokus im Player; im Hub nur Logo-Marke und History-Marker |
| Text auf Akzent | `#161512` | |
| Screen-Größe | 1440 x 900 | Prototyp-Canvas, kein Responsive-Vorgabe (unklar) |
| Sidebar-Breite | 232px | Player-Hauptnavigation und Hub-Navigation |

Hinweis: Die Fonts werden in der Quelle über Google Fonts geladen. Für Hub (kein Node-Build, selbst gehostet) und Player ist die Einbettung der Fonts offen (Lizenz/Offline-Betrieb nicht entschieden).

## Typografie (beide Themes)

| Rolle | Wert |
|---|---|
| Seitentitel | Sans 26px/600, letter-spacing -.015em (Hub 3k Spaltentitel 22px) |
| Start-/Pairing-Titel (Player) | Sans 30px/600, letter-spacing -.02em |
| Dialogtitel (3d) | Sans 24px/600, -.01em |
| Kartentitel / Abschnitt | 15-17px/600 (Hub Karten 16px/600, Überschrift Systemkarte 20px/600) |
| Fließtext, Zeilen | 13-14px/400-500 |
| Meta, Hilfetext | 12px/400, Zeilenhöhe 1.5-1.55 |
| Eyebrow / Spaltenkopf | Mono 11px/500, UPPERCASE, letter-spacing .08em (Tabellenköpfe .06em) |
| Zahlen-/Hash-/Adresswerte | Mono 12-13px/400 |
| Invite-Code (Hub 3n) | Mono 24px/500, letter-spacing .04em |
| Monogramm auf Cover-Platzhalter | Sans 34-36px/600, -.02em |

## Player (dunkel)

Farben

| Rolle | Wert |
|---|---|
| Hintergrund App | `#121315` |
| Hintergrund Sidebar | `#0e0f10` |
| Hintergrund Seitenpanel (Mittel-/Detailspalte, Dialog) | `#16171a` |
| Fläche Karte / Input | `#1a1b1e` |
| Fläche erhöht / ausgewählt / Hub-Karte | `#1f2024` / Hub-Karte in Sidebar `#17181b` |
| Fläche Menü/Dialog-Kachel | `#1d1e22` |
| Hintergrund im Spiel (3g-3i) | `#0b0b0c`, Header und Seitenleiste `#111214` |
| Backdrop Dialog (3d) | `#08090a`, Muster `#0d0e0f`/`#101113` |
| Video-Platzhalter | Streifen `#17181a`/`#1b1c1f`; PiP `#1c1d20`/`#222327` |
| Rahmen Sidebar/Trenner | `#232428` (im Spiel `#1e1f22`, Tabellenzeile `#1f2024`) |
| Rahmen Karte | `#26272b` |
| Rahmen Input/Chip, Segment aktiv, Avatar | `#2c2d32` |
| Rahmen Auswahlring, Popup | `#2f3035` |
| Rahmen Button (Outline) | `#3a3b40` |
| Text primär | `#ecebe7` |
| Text sekundär | `#c9c8c4` |
| Text gedämpft | `#a3a3a8`, `#8e8e94` (Meta) |
| Text Platzhalter/Eyebrow | `#7d7d83`; auf Kacheln `#6f6f75` |
| Text deaktiviert / Offline-Punkt | `#5e5e63`; Monogramm `#45464c` |
| Status ok | `#6fd39a` (Fläche `#17291f`) |
| Status warn | `#e9b44c` (Fläche `#2a2418`) |
| Status error | `#ef8a78` (Fläche `#2a1a17`, Text darauf `#e6d6d2`) |

Radien: 4, 5 (Segment, Logo), 6, 7 (Input, Button), 8 (Karte, Primärbutton), 9, 10 (Hub-Karte), 12 (Dialog), 999 (Chip, Pill, Toggle), 50% (Avatar, Statuspunkt).

Schatten: Screen `0 30px 60px rgba(0,0,0,.25)` (nur Prototyp-Rahmen); Auswahlring `0 0 0 1px #2f3035`; Fokus/aktive Karte `0 0 0 1.5px #e9b44c`; Tab-Unterstrich `inset 0 -2px 0 #e9b44c`; Dialog `0 40px 80px rgba(0,0,0,.6)`; PiP `0 16px 40px rgba(0,0,0,.5)`; Popup `0 12px 28px rgba(0,0,0,.45)`.

Maße (wiederkehrend)

| Maß | Wert |
|---|---|
| Sidebar-Padding / Gap | 24px 16px / 28px; Nav-Item 9px 10px, Radius 6 |
| Main-Padding | 28px 32px (3e: 28px 36px) |
| Spalten | Library-Detail 392px; Mittelspalte 300px (3e/3f); Input-Test 340px; Session-Seitenleiste 340px |
| Header im Spiel | 56px hoch, Padding 0 20px |
| Buttonhöhen | 48 (Primär), 46 (Dialogoption), 44, 42, 40, 38; Input/Select 34 |
| Abstände (Gap) | 4, 6, 8, 10, 12, 14, 16, 20, 28 (häufigste: 10, 4, 8) |
| Library-Raster | 4 Spalten, Gap 20px 18px; Cover quadratisch |

## Hub (hell)

Farben

| Rolle | Wert |
|---|---|
| Hintergrund App | `#f6f5f2` |
| Hintergrund Sidebar | `#efeee9` |
| Fläche Karte, Tabelle, aktives Nav-Item | `#fff` |
| Fläche leicht (Tabellenkopf, Listenspalte, Inputs) | `#faf9f7` |
| Cover-/Avatar-Platzhalter | `#ebeae5`; Streifen `#ebeae5`/`#e2e0da` |
| Rahmen Karte/Sidebar | `#e3e1dc` |
| Rahmen Input/Button | `#d9d7d1` |
| Zeilentrenner | `#efeee9` (auch `#e9e7e2`) |
| Rahmen gestrichelt (Platzhalter "SPÄTER") | `#cfccc5` |
| Neutraler Marker (History alt) | `#b9b7b0` |
| Text primär | `#1b1b1d` |
| Text sekundär | `#55544f` |
| Text gedämpft | `#6b6a66` |
| Text Platzhalter / deaktiviert | `#8a8984` |
| Primärbutton | Fläche `#1b1b1d`, Text `#f6f5f2` |
| Status ok | Text `#1f6a3f` auf `#e2f1e7`; Text "kompatibel" `#2a7a4c` |
| Status warn | Text `#9a6a12`, dunkel `#6e4a08`, Rahmen `#e8d3a6`, Badge-Fläche `#f6e6c4` |
| Status error | Text/Fläche `#b5402f`; Zeilenhintergrund `#fbf1ef`, leichter `#fdf8f6` |
| Neutral-Badge (Revoked/Disabled/Inaktiv) | Fläche `#ebeae5`, Text `#55544f` |

Radien: 2 (Logo-Marke), 4 (Cover), 5, 6 (Nav-Item), 7 (Input, Button), 8, 10 (Karte, Tabelle), 999 (Pill, Toggle), 50% (Avatar).

Schatten: Screen `0 30px 60px rgba(0,0,0,.15)` (nur Prototyp-Rahmen); aktives Nav-Item/Segment `0 0 0 1px #e3e1dc`; aktueller History-Marker `0 0 0 4px #f6f5f2, 0 0 0 5px #e9b44c`.

Maße (wiederkehrend)

| Maß | Wert |
|---|---|
| Sidebar | 232px, Padding 24px 16px, Gap 28px; Fußzeile Mono 12px (`#6b6a66`) |
| Main-Padding | 32px 40px (3l: 28px 40px) |
| Spalten | Saves-Liste 320px; Benutzer-Seitenleiste 420px |
| Tabellenzeile | Padding 12-13px 20px, Spaltengap 16px, Kopf Padding 10-12px 20px |
| Buttonhöhen | 38 (Standard), 36 (Karte/Formular), 34 (Input), 32 (klein) |
| Abstände (Gap) | 2, 3, 4, 8, 10, 12, 16, 20, 24, 28 (häufigste: 10, 16, 4) |
| Karten | Radius 10, Rahmen `#e3e1dc`, Padding 18-24px |

## Abbildung (Hinweis, kein Code)

- Player (QML): ein Theme-Singleton mit den obigen Rollen als Properties; Dark/Light-Umschaltung (Architektur 09) über zwei Paletten mit gleichen Rollennamen. Eine helle Player-Palette ist in der Quelle nicht entworfen (offen).
- Hub (CSS): Rollen als CSS-Variablen auf `:root`; dunkle Variante per `[data-theme]`/`prefers-color-scheme` (Settings kennt Hell/Dunkel/System). Eine dunkle Hub-Palette ist in der Quelle nicht entworfen (offen); der Player-Satz kann nicht ohne Weiteres übernommen werden.
- Rollennamen vorschlagsweise gleich in beiden Themes (bg, bg-sidebar, surface, border, text, text-muted, accent, ok, warn, error), damit die Designsprache gemeinsam bleibt.
