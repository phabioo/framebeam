# server – FrameBeam Hub

Go hub with SQLite: library, saves, users, devices, signaling, web interface. Rules for agents: `CLAUDE.md`.

## Start and setup

```sh
framebeam-hub setup-admin -username <name>   # password as a single line from stdin; refused if an admin already exists
framebeam-hub [flags]                        # server (HTTPS, self-signed certificate under <data-dir>/tls/)
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # development: HTTP
```

Flags and environment (flag wins): `-data-dir`/`FRAMEBEAM_DATA_DIR` (default `/var/lib/framebeam`), `-listen`/`FRAMEBEAM_LISTEN` (`:8443`), `-name`/`FRAMEBEAM_NAME` (first start only), `-tls-cert`+`-tls-key` (`FRAMEBEAM_TLS_CERT`/`_KEY`, own certificate), `-dev`/`FRAMEBEAM_DEV` (HTTP instead of HTTPS). The certificate's SHA-256 fingerprint is printed to the log at startup. Without an admin the hub still starts; the web interface then redirects to `/setup` (form accepted from loopback clients only).

Layout of `internal/`: `config`, `store` (SQLite, migrations), `auth` (Argon2id, tokens), `tlsutil`, `hub` (service layer for the API and web interface), `httpapi` (OpenAPI implementation; tests validate against `protocol/openapi/framebeam.yaml`).

Web interface (`internal/web`, `html/template` + htmx via `embed`, no external resources): setup, login (cookie `fb_session`, admins only), library, clients, settings. htmx 2.0.4 (0BSD) lives unmodified at `internal/web/static/htmx.min.js`. ROM upload limit: `web.DefaultMaxUploadBytes` (4 GiB).
