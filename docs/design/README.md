# Design-Spezifikation

Zweck: token-sparsame Aufbereitung des UI-Prototyps, damit Implementierungs-Agents pro Screen nur die nötige Stelle lesen. Der Prototyp ist ein Mock-up; bei Widersprüchen gilt die Architektur (`../architektur/09-ui-und-navigation.md`), Abweichungen siehe unten.

Quelle: Claude Design, Export "FrameBeam Designs v2"; `quelle/framebeam-designs-v3.dc.html` (v3) ist der aktuelle Stand, ältere Versionen sind nicht übernommen. Die Datei bleibt unverändert und rendert nicht standalone (Runtime `support.js` fehlt im Repo). 15 Screens à 1440 x 900; Mock-Daten im Script ab Zeile 825.

Lesehinweis: nur die nötige Datei bzw. den nötigen Abschnitt lesen. Die Quell-HTML nur für Details im angegebenen Zeilenbereich öffnen (Zeilen = Screen-Container von `data-screen-label` bis Ende; Anker `#3x` = Block-ID in der Quelle).

- `tokens.md`: Farben, Typografie, Abstände, Radien, Schatten, Layoutmaße (Player dunkel, Hub hell).
- `player.md`: Screens 3a-3i (FrameBeam Player, Qt 6 QML).
- `hub.md`: Screens 3j-3o (FrameBeam Hub Webinterface, `html/template` + htmx).

## Screens

| Screen | Spezifikation | Quelle (Anker, Zeilen) | Phase |
|---|---|---|---|
| 3a Player Connection | [player.md](player.md#3a-player-connection) | `#3a`, 30-52 | 2 |
| 3b Player Pairing | [player.md](player.md#3b-player-pairing) | `#3b`, 58-72 | 2 |
| 3c Player Library | [player.md](player.md#3c-player-library) | `#3c`, 78-175 | 2 |
| 3d Player Save-Konflikt | [player.md](player.md#3d-player-save-konflikt) | `#3d`, 181-209 | 3 |
| 3e Player Emulation | [player.md](player.md#3e-player-emulation) | `#3e`, 215-276 | 5 |
| 3f Player Controllers | [player.md](player.md#3f-player-controllers) | `#3f`, 282-344 | 5 |
| 3g Player Session | [player.md](player.md#3g-player-session) | `#3g`, 350-391 | 4 |
| 3h Player Side-by-Side | [player.md](player.md#3h-player-side-by-side) | `#3h`, 397-438 | 4 |
| 3i Player PiP | [player.md](player.md#3i-player-pip) | `#3i`, 444-476 | 4 |
| 3j Hub Library | [hub.md](hub.md#3j-hub-library) | `#3j`, 492-540 | 1 |
| 3k Hub Saves | [hub.md](hub.md#3k-hub-saves) | `#3k`, 546-633 | 3 |
| 3l Hub Systeme und Cores | [hub.md](hub.md#3l-hub-systeme-und-cores) | `#3l`, 639-700 | 5 |
| 3m Hub Clients | [hub.md](hub.md#3m-hub-clients) | `#3m`, 706-742 | 1 |
| 3n Hub Benutzer | [hub.md](hub.md#3n-hub-benutzer) | `#3n`, 748-785 | 5 |
| 3o Hub Settings | [hub.md](hub.md#3o-hub-settings) | `#3o`, 791-819 | 1 (Teile), 5 |

Phasen nach `../arbeitsweise.md` (Phasenplan). Grenzfälle: 3o Phase 1 nur Hub-Name, Adresse, Transport und Zertifikat, Admin-Konto; Darstellung (Dark/Light) und "Allow users to upload games" Phase 5 (setzt Benutzer voraus). 3m ist Phase 1 (Allow/Deny, Revoke); die Zuordnung zu einem bestehenden User (Architektur 10) gehört ebenfalls dazu, setzt aber angelegte User voraus (bis Phase 5 nur Admin). 3k und 3d gehören beide zu Phase 3, obwohl 3k Hub-Seite ist. 3l Phase 5 (Firmware-Pfad); Nav-Badges der Hub-Shell (Konflikte, Firmware, Anfragen) folgen den jeweiligen Phasen. Die Hub-Shell (Sidebar, Seitenkopf, Tabellen) entsteht mit der ersten Hub-Seite in Phase 1.

## Abweichungen zur Architektur

Nur notiert, nicht aufgelöst; es gilt die Architektur. Geprüft gegen `09-ui-und-navigation.md`, `03-saves.md` und `10-identitaet-pairing-tls.md`; übrige Dateien nicht gelesen (Punkte dort: unklar).

1. Diagnostics erscheint in 3g-3i als gleichrangiger Tab neben Session und Multiview. Architektur: nur optional einblendbar/einklappbar, nicht primäre UX. In 3g zusätzlich "▸ Diagnostics einblenden".
2. Hub Settings (3o): Darstellung "Hell | Dunkel | System"; Architektur: "Dark-/Light-Darstellung wählbar". Option "System" ist zusätzlich. Gezeichnet sind nur Player dunkel und Hub hell; die jeweils andere Palette fehlt.
3. Fingerprint-Bestätigung "im Hub-Webinterface" bei geändertem Zertifikat (3a): Architektur 10 (Z. 40) lässt den bestätigten Pin-Wechsel zu spezifizieren; offen.
4. 3b zeigt "Zertifikat vertraut" ohne Bestätigung. Architektur 10 (Z. 40) verlangt beim ersten Pairing, den Fingerprint anzuzeigen und zu bestätigen. Echte Abweichung, Architektur gilt.
5. Nicht gezeichnet, aber von der Architektur genannt: Player Settings und Settings → Hubs, Hub-Switcher-Dialog (nur Karte "wechseln"), Admin-Setup und Login im Hub, Metadata-Aktionen im Library-Eintrag (später), Firmware-Blockade im Library-Detail (3c zeigt nur Erfolgsfall), Multiview-Aktion "Zu Multiview hinzufügen".
6. Begriffe: UI-Texte nutzen "Session"; "Stream" kommt nicht vor. Englische Labels ("Allow users to upload games", "Private", "Hub users", "Invite only", "Current Checkpoint", "Game Override", "Trusted/Revoked") sind aus der Quelle übernommen; Einheitlichkeit Deutsch/Englisch unklar.
7. Prototyp-intern uneinheitlich: Revisionen "Rev 41" (Konflikt) vs. "v6" (History) in 3k; Filter "Achtung nötig · 2" in 3c ohne eindeutige Zählung.
8. Hub Saves (3k) zeigt "Wiederherstellen" in der History; ob das im Hub vorgesehen ist, ist offen (`03-saves.md` nennt die Konfliktaktionen, nicht die Wiederherstellung).
