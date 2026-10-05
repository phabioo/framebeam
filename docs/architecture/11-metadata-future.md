# Architektur: Zentrale Metadaten (Future)

## 15. Zentrale Spielmetadaten und Artwork [Future – außerhalb des PoC]

Der spätere **Hub Metadata Service** beschafft und normalisiert Spielinformationen zentral. Eine **Metadata-Provider-Abstraktion** kapselt externe APIs, Matching und die Übersetzung der Ergebnisse. ScreenScraper und IGDB sind lediglich mögliche Provider-Beispiele; kein Anbieter ist fest verdrahtet oder Voraussetzung für FrameBeam. Auswahl und konkrete Integration erfolgen später anhand der verfügbaren Schnittstellen und Nutzungsbedingungen.

```text
Hub-Library / ROM-Analyse
          ↓
Hub Metadata Service
          ↓
Provider-Abstraktion → optionale externe Provider
          ↓
Normalisiertes FrameBeam-Modell + manuelle Overrides
          ↓
Hub: SQLite-Metadata-Cache + Artwork-Dateicache
          ↓
Player: Library / Spielansicht über Hub-API
```

### Internes Metadata-Modell und Matching

Das interne Modell ist unabhängig von Provider-Datenformaten und ergänzt die bestehenden technischen ROM-/Library-Daten. Der erste spätere Metadata-Ausbau umfasst:

| Feld | Inhalt |
|---|---|
| Display Title | Anzeigename des Spiels |
| Release Date / Year | Erscheinungsdatum oder nur Jahr entsprechend der bekannten Genauigkeit |
| Developer / Publisher | Entwickler und Publisher |
| Genre | Ein oder mehrere normalisierte Genres |
| Kurzbeschreibung | Sprachabhängige kurze Spielbeschreibung |
| Region | Region der zugeordneten Veröffentlichung beziehungsweise ROM-Ausgabe |
| Boxart | Referenz auf ein vom Hub bereitgestelltes Artwork-Asset |

Zusätzlich hält der Hub Provider-Referenzen, Herkunft, Sprache/Region, Matching-Status und Aktualisierungszeit fest. Fehlende Werte bleiben zulässig. Screenshot, Fanart, Logos, Ratings, Videos und weitere Felder sind keine Voraussetzung für diesen ersten Ausbau.

Matching erfolgt möglichst **hashbasiert** und berücksichtigt System sowie ROM-Ausgabe. SHA-256 bleibt der Integritäts-Hash von FrameBeam; zusätzliche Matching-Hashes wie CRC32, MD5 oder SHA-1 können bei der späteren ROM-Analyse nach Provider-Bedarf berechnet werden. Nicht jeder Provider muss Hash-Matching unterstützen. Hashlose oder erfolglose Zuordnung kann auf System-/Titel-/Regionssuche zurückfallen; mehrdeutige Treffer erfordern eine manuelle Auswahl statt einer stillen falschen Zuordnung.

Library-Einträge können Zustände wie „Matched“, „No match“ und „Multiple matches“ besitzen. Manuelle Auswahl und feldweise **Overrides** erlauben Korrekturen. Overrides haben Vorrang vor Provider-Daten und bleiben bei einem Refresh erhalten, bis sie bewusst entfernt werden. Technische ROM-Identität und Hashes werden durch Metadata-Änderungen nicht verändert.

### Cache, Präferenzen und Oberfläche

Normalisierte Metadaten werden im Hub gespeichert; Artwork liegt im Hub-Dateicache, beispielsweise unter `/var/lib/framebeam/metadata/artwork/`. Cache-/Refresh-Regeln berücksichtigen die jeweiligen Provider-Bedingungen. Bereits gecachte Inhalte können ohne erneuten externen Zugriff bereitgestellt werden, soweit diese Regeln es erlauben.

Der Player bezieht **Metadata und Artwork ausschließlich über seinen aktiven Hub** und kennt keine externen Provider-Credentials. Die Hub-API liefert das normalisierte Modell und Hub-eigene Asset-Referenzen statt externer Provider-URLs. Provider-Ausfälle verhindern keinen Start vorhandener ROMs; einfache Library-Bezeichnungen und Artwork-Platzhalter bleiben verfügbar.

Unter **Hub Settings → Metadata** liegen Provider-Konfiguration und Credentials, Verbindungstest sowie bevorzugte Sprache/Region und deren Fallbacks. Die tatsächliche ROM-/Veröffentlichungsregion bleibt von einer bevorzugten Darstellungsregion unterscheidbar. Verfügbare passende Varianten werden bevorzugt; fehlende Varianten fallen kontrolliert auf die konfigurierten Alternativen zurück.

Direkt im **Library-Eintrag** liegen Matching-Status und Aktionen wie „Refresh“, „Edit Metadata“ und „Change Match“. Es gibt keine neue Hub-Hauptseite für Metadata. Der Player zeigt später Boxart und Basis-Metadaten in Library und Spielansicht; diese Darstellung erweitert seine Hauptnavigation nicht.

**Der gesamte Metadata Service einschließlich Provider-Anbindung, Matching, Overrides, Präferenzen und Artwork-/Metadata-Cache liegt ausdrücklich außerhalb von FrameBeam 0.1 / PoC.** Im PoC genügen die bestehenden technischen Library-Daten, eine einfache Spielbezeichnung und Platzhalter. Es sind weder externe Provider-Zugriffe noch zusätzliche Matching-Hashes oder Metadata-Verwaltungsaktionen für den PoC erforderlich.
