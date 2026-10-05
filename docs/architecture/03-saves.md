# Architektur: Saves

### Save-Sync, Checkpoints und Versionierung [PoC]

```text
Start-Sync:      Hub → Current Checkpoint → Player → Emulator
Auto-Checkpoint: geänderter Save → Player → Hub → Current Checkpoint
Final-Sync:      Pause / Stop / sauberes Beenden → Player → Hub
History:        dauerhafte Version bei relevanten Ereignissen
```

Spielstände liegen zentral im Dateisystem; SQLite hält Zuordnung, Checkpoint-Revisionen und Versionsmetadaten einschließlich Herkunftsgerät und Zeitstempel. Save-Dateien werden nur bei tatsächlich geändertem Inhalt übertragen (Hash-/Dirty-Erkennung). Nach einer Änderung folgt ein kurzer Debounce, beispielsweise 10–15 Sekunden; periodische Uploads erfolgen frühestens ungefähr alle 60 Sekunden. Bei unverändertem Hash erfolgt kein Upload. Pause, Stop und sauberes App-Beenden lösen unabhängig vom periodischen Intervall einen sofortigen Final-Sync geänderter Saves aus.

Auto-Checkpoints aktualisieren den **Current Checkpoint**, ohne bei jedem Upload eine permanente History-Version zu erzeugen. Dauerhafte History-Versionen entstehen bei relevanten Ereignissen wie Session-Ende, Gerätewechsel, vor Konfliktauflösung oder manuellem Snapshot. Konkrete Retention und Ausdünnung bleiben später zu spezifizieren.

Die bestehende `base_version`-Konfliktlogik gilt auch für Checkpoints: Jede Änderung des Current Checkpoint erhält eine neue Revision für den nächsten Abgleich, selbst wenn keine dauerhafte History-Version entsteht. Ein Upload gegen eine veraltete Basis darf eine konkurrierende Änderung nicht still überschreiben (Abschnitt 13).

Bei Hub-Ausfall oder fehlgeschlagenem Upload wird der Save lokal sicher als **pending sync** mit Hub-, User-, Spiel-/Slot-Zuordnung und Basisversion gepuffert. Wiederholung erfolgt ausschließlich zum ursprünglichen Hub; niemals wird ein Save an einen anderen Hub umgeleitet. Checkpoints begrenzen möglichen Fortschrittsverlust bei einem Crash, garantieren jedoch bei Ausfällen oder ausstehenden Änderungen keine feste maximale Verlustdauer. Save States bleiben ein getrennter Mechanismus und liegen außerhalb des PoC; automatisches Zusammenführen binärer Spielstände ist nicht vereinbart.

## 13. Save-Konflikte: Datenmodell und UI

Save-Versionierung wird um einen ausdrücklich modellierten Konfliktzustand ergänzt. Ein neuer Upload verweist auf die beim Abgleich verwendete **Basisversion**. Hat sich die aktuelle Hub-Version inzwischen geändert, darf eine abweichende lokale Änderung sie nicht still überschreiben. Eine Offline-Änderung mit veralteter Basis kann so ebenso erkannt werden wie Änderungen auf zwei Geräten. Zeitstempel dienen der Anzeige, nicht als alleinige Konfliktentscheidung.

`base_version` bezeichnet auch bei Auto-Checkpoints die beim Abgleich gelesene Current-Checkpoint-Revision. Checkpoint-Revision und permanente History-Version sind getrennt; eine Konfliktauflösung sichert zuvor die konkurrierenden Inhalte in der History.

Vorgesehene Metadaten sind Save-Zuordnung (Spiel/Nutzer beziehungsweise bestehender Save-Slot), Versions-ID, Basisversions-ID, Inhalts-Hash, Herkunftsgerät und Zeitstempel. Ein Konfliktdatensatz referenziert die konkurrierenden Versionen beziehungsweise den gesicherten lokalen Upload, seinen Status und eine spätere Auflösungsentscheidung. Beide Inhalte bleiben erhalten, bis eine bewusste Auswahl erfolgt; Save-Dateien liegen weiterhin im Dateisystem.

Der Hub zeigt Konflikte auf der **Saves-Seite** neben der Versionshistorie. Der Player zeigt den Sync-/Konfliktzustand beim betroffenen Spiel und beim Start-, Checkpoint- und Final-Abgleich. Die Detailansicht macht Herkunft, Zeitpunkt und Versionen vergleichbar und sieht Aktionen wie „Hub-Version verwenden“, „Lokalen Save als neue aktuelle Version übernehmen“ und „Beide Versionen behalten / später entscheiden“ vor. Auch eine Auflösung darf keinen stillen Datenverlust erzeugen und muss gegen zwischenzeitliche Änderungen geprüft werden.

Die konkrete API, Auflösungslogik und Startentscheidung bei ungelöstem Konflikt bleiben zu spezifizieren. Für den PoC sind Datenmodell und UI-Zustand vorzusehen; automatisches Zusammenführen binärer Spielstände ist nicht vereinbart.
