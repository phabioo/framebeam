# server/ – FrameBeam Hub (Go, SQLite)

Details nur bei Bedarf aus `docs/architektur/` (Index: `docs/architektur/README.md`).

## Verantwortung und Grenzen
- HTTPS-API, Dateiverwaltung, Auth, Registry, interne Session-Metadaten, Presence, Signaling, Webinterface.
- Emuliert, encodiert, decodiert und rendert nie (auch kein Multiview); führt keine Cores aus, kennt nur System-/Core-Registry. Keine Sessions-Verwaltungsseite, keine Video-Vorschau.
- Ohne großes Framework. Siehe `01-ueberblick.md`.

## Speicherorte
- ROMs, Saves, Firmware als Dateien unter `/var/lib/framebeam/`; SQLite nur Metadaten, keine Binärdaten.
- Firmware/BIOS nur vom Admin bereitgestellt (`05-emulation.md`).

## Save-Modell (Hub-Sicht)
- Jeder Upload trägt `base_version`; veraltete Basis erzeugt Konflikt, nie stilles Überschreiben. Zeitstempel entscheiden nie allein.
- Current Checkpoint (jede Änderung = neue Revision) getrennt von dauerhafter History (Session-Ende, Gerätewechsel, vor Konfliktauflösung, manueller Snapshot).
- Beide Konfliktinhalte bleiben bis zur bewussten Auswahl; vor Auflösung History sichern. Kein Zusammenführen binärer Saves. Siehe `03-saves.md`.

## Sicherheit
- HTTPS/WSS Pflicht; HTTP/WS nur Dev-Modus bzw. localhost. Eigenes Zertifikat, eigenes cert/key oder Reverse Proxy.
- Admin: Username/Passwort, Argon2id. Normale User passwordless, nur vom Admin angelegt (Invites). User und Device getrennt.
- Pairing: Player-Anfrage, Admin Allow/Deny (Pflicht). Revoke pro Gerät entzieht auch Access Tokens.
- Tokens und Device-Credentials nur als Prüfrepräsentation speichern; Secrets nie loggen. Siehe `10-identitaet-pairing-tls.md`.

## Webinterface
- Go `html/template` + htmx per `embed`, kein Node-Build (ADR 0001). Seiten/Begriffe: `09-ui-und-navigation.md`.
