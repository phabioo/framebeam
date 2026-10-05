# server – FrameBeam Hub

Go-Hub mit SQLite: Library, Saves, Benutzer, Geräte, Signaling, Webinterface. Regeln für Agents: `CLAUDE.md`.

## Start und Setup

```sh
framebeam-hub setup-admin -username <name>   # Passwort als eine Zeile von stdin; verweigert, wenn schon ein Admin existiert
framebeam-hub [flags]                        # Server (HTTPS, selbstsigniertes Zertifikat unter <data-dir>/tls/)
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # Entwicklung: HTTP
```

Flags und Umgebung (Flag gewinnt): `-data-dir`/`FRAMEBEAM_DATA_DIR` (Default `/var/lib/framebeam`), `-listen`/`FRAMEBEAM_LISTEN` (`:8443`), `-name`/`FRAMEBEAM_NAME` (nur beim ersten Start), `-tls-cert`+`-tls-key` (`FRAMEBEAM_TLS_CERT`/`_KEY`, eigenes Zertifikat), `-dev`/`FRAMEBEAM_DEV` (HTTP statt HTTPS). Der SHA-256-Fingerprint des Zertifikats steht beim Start im Log. Ohne Admin startet der Hub trotzdem (Web-Setup folgt).

Aufbau `internal/`: `config`, `store` (SQLite, Migrationen), `auth` (Argon2id, Tokens), `tlsutil`, `hub` (Service-Schicht für API und Webinterface), `httpapi` (OpenAPI-Implementierung; Tests validieren gegen `protocol/openapi/framebeam.yaml`).
