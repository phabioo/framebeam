# ADR 0002: Protokoll und FrameBeam Hub in Phase 1

- Status: angenommen
- Datum: 2026-10-05
- Entscheider: Fabio (Vorschlag des Orchestrators, bestätigt am 2026-10-05)

## Kontext

`docs/architektur/08-repo-und-offene-punkte.md` lässt API-Endpunkte, Nachrichtenformate, Kompatibilitätsregeln, Token-Format und -Laufzeiten, Widerrufsdetails sowie die TLS-Zertifikatsverwaltung offen. Phase 1 braucht dafür Defaults. Dieser ADR hält sie fest, bis Fabio sie bestätigt oder ändert.

## Entscheidungen

- **Protokollversion:** `protocol_version` ist eine Ganzzahl ab 1, unabhängig von Produktversionen. FrameBeam Hub und FrameBeam Player melden je `protocol_version` und `min_protocol_version`. Player unter dem Minimum des Hub: `player_too_old`. Hub unter dem Minimum des Player: `hub_too_old`.
- **Info-Endpunkt:** `GET /.well-known/framebeam` liefert `hub_id`, `name`, `hub_version`, `protocol_version`, `min_protocol_version`, `api_base`. Ohne Auth, verleiht keine Rechte. Alle anderen Endpunkte liegen unter `/api/v1`.
- **Fehlerformat:** `{"error":{"code","message"}}` mit festen Codes, u. a. `player_too_old`, `hub_too_old`, `core_missing`, `core_version_mismatch`, `capability_missing`, `device_revoked`, `pairing_*`.
- **Tokens:** opak (Zufallsbytes, base64url) mit Präfix `fba_` (Access Token), `fbd_` (Device Credential), `fbp_` (Poll-Token). Der Hub speichert nur den SHA-256-Hash; wegen hoher Entropie ist Argon2 nicht nötig. Access Token: 15 min. Device Credential: gültig bis Widerruf. Der Widerruf eines Geräts invalidiert sofort alle seine Access Tokens. Das Admin-Passwort wird mit Argon2id gehasht.
- **Pairing:** Eine Anfrage per POST ohne Auth liefert `request_id` und `poll_token`. Der Admin erlaubt oder verweigert im Webinterface und ordnet die Anfrage einem bestehenden User zu (Phase 1: nur Admin vorhanden). Der Player pollt; das Credential wird genau einmal ausgeliefert. Offene Anfragen verfallen nach 10 min. Die Zahl offener Anfragen ist begrenzt (HTTP 429).
- **Handshake:** `POST /api/v1/handshake` nach Auth. Phase 1 prüft nur die Protokollversionen. Core- und Codec-Prüfungen folgen mit der Core-Registry (Phase 5) bzw. den Sessions (Phase 4).
- **ROMs:** Upload in Phase 1 nur über das Admin-Webinterface. Download per API mit Range und ETag. Dateien liegen im Datenverzeichnis nach SHA-256 abgelegt, SQLite hält nur Metadaten.
- **WSS:** In `protocol/schemas` entstehen nur die Nachrichtenschemas (Envelope, `hello`, `hello-ack`, `error`, `presence-update`). Die Implementierung folgt ab Phase 4.
- **TLS:** Der Hub erzeugt beim ersten Start ein selbstsigniertes Zertifikat (ECDSA P-256) im Datenverzeichnis. Alternativ eigenes cert/key per Konfiguration oder Betrieb hinter einem Reverse Proxy. HTTP nur mit explizitem Dev-Flag oder auf localhost.
- **SQLite:** Reiner Go-Treiber `modernc.org/sqlite`; der Hub baut mit `CGO_ENABLED=0`.

## Offen

- Zertifikatserneuerung und bestätigter Pin-Wechsel.
- Weitere Punkte aus Abschnitt 8 (Save-Retention, Cache-Limits, ICE/STUN/TURN, Medienparameter, Core-/Firmware-Manifeste) sind von diesem ADR nicht berührt.

## Verworfen

- **Refresh Tokens in Phase 1:** Das Device Credential übernimmt diese Rolle; ein zweiter Token-Typ ist unnötig.
- **Argon2 für Tokens:** Bei Zufallsbytes mit hoher Entropie bringt es keinen Gewinn.
- **SQLite über cgo (`mattn/go-sqlite3`):** erschwert Cross-Builds (`CGO_ENABLED=0`, amd64/arm64).

## Folgen

- Die Architekturdokumente bleiben unverändert; Abweichungen gelten über diesen ADR.
- OpenAPI und Schemas in `protocol/` sowie der Hub in `server/` folgen diesen Festlegungen; Änderungen nur über einen neuen ADR.
