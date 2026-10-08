# Hub configuration

Reference for running the FrameBeam Hub binary: commands, flags and environment variables, certificates. For installation see [hub-install.md](hub-install.md).

## Commands

```sh
framebeam-hub [flags]                          # run the server (HTTPS, self-signed certificate under <data-dir>/tls/)
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # development: HTTP instead of HTTPS
framebeam-hub setup-admin -username <name>     # password as a single line from stdin; refused if an admin already exists
framebeam-hub renew-cert [-data-dir <dir>]     # renew the self-generated certificate now (refused with own cert/key)
framebeam-hub import-cores <dir>               # import signed core packages offline
framebeam-hub version [--json]                 # version info
framebeam-hub update check|stage [-channel stable|beta]   # see updates.md
```

On a systemd install use the script instead of calling the binary for `renew-cert` and `import-cores`: `sudo packaging/linux/install-hub.sh renew-cert` / `import-cores <dir>` (it uses the service's data dir and user and restarts the service where needed), see [hub-install.md](hub-install.md).

Without an admin the Hub still starts; the web interface then redirects to `/setup` (the form is accepted from loopback clients only).

## Flags and environment

A flag wins over the environment variable. Under systemd the variables live in `/etc/framebeam/hub.env`. Values saved in the web interface (Settings → Network) win over both, see below.

| Flag | Environment | Default | Meaning |
|---|---|---|---|
| `-data-dir` | `FRAMEBEAM_DATA_DIR` | `/var/lib/framebeam` | Database, saves, firmware, certificate |
| `-listen` | `FRAMEBEAM_LISTEN` | `:8443` | Listen address and port |
| `-name` | `FRAMEBEAM_NAME` | empty | Hub name, first start only |
| `-tls-cert`, `-tls-key` | `FRAMEBEAM_TLS_CERT`, `FRAMEBEAM_TLS_KEY` | empty | Own certificate and key (PEM), only together |
| `-dev` | `FRAMEBEAM_DEV` | off | HTTP instead of HTTPS; excludes `-tls-cert` |
| `-ice-servers` | `FRAMEBEAM_ICE_SERVERS` | none | Comma-separated `stun:` URLs; a LAN needs none |
| `-turn` | `FRAMEBEAM_TURN` | off | Embedded STUN/TURN relay; needs `-public-host` |
| `-public-host` | `FRAMEBEAM_PUBLIC_HOST` | empty | DNS name or IPv4 of the Hub's public address |
| `-turn-port` | `FRAMEBEAM_TURN_PORT` | `3478` | TURN UDP and TCP port |
| `-turn-relay-ports` | `FRAMEBEAM_TURN_RELAY_PORTS` | `49160-49199` | TURN UDP relay range `min-max` |
| `-turn-relay-ip` | `FRAMEBEAM_TURN_RELAY_IP` | empty | Fixed public IPv4 for relayed addresses instead of resolving `-public-host` |
| `-save-keep-recent` | `FRAMEBEAM_SAVE_KEEP_RECENT` | `20` | Save history: newest versions per slot to keep |
| `-save-keep-daily` | `FRAMEBEAM_SAVE_KEEP_DAILY` | `30` | Save history: days of which the newest version is kept |
| `-save-keep-weekly` | `FRAMEBEAM_SAVE_KEEP_WEEKLY` | `26` | Save history: weeks of which the newest version is kept |
| `-core-index-url` | `FRAMEBEAM_HUB_CORE_INDEX_URL` | FrameBeam's `cores-index` release | Signed core index (https) |
| `-core-trust-key` | `FRAMEBEAM_HUB_CORE_TRUST_KEYS` | none | Extra trusted Ed25519 public key (base64) for the core and updates index; flag repeatable, env comma-separated |
| `-update-index-url` | `FRAMEBEAM_HUB_UPDATE_INDEX_URL` | FrameBeam's `updates-index` release | Signed updates index (https, or `file://` for tests) |
| `-update-request-dir` | `FRAMEBEAM_HUB_UPDATE_REQUEST_DIR` | `/run/framebeam` | Directory of the update request file read by the root helper |

`0` for a `-save-keep-*` rule means unlimited for that rule. Invalid combinations (for example `-turn` without `-public-host`, a `-public-host` containing `/` or spaces, a non-`stun:` ICE URL, a non-IPv4 `-turn-relay-ip`) stop the start with an error.

## Settings in the web interface

Settings → Network edits port, embedded TURN, public host, TURN port, relay range and IP, STUN servers and save retention. These values live in the Hub database and win over flags and `hub.env`, which only give the initial values (reset per field to return to the `hub.env` value). Changes that need it are applied by a restart the Hub triggers itself (no root). The web form refuses ports below 1024; set those with `install-hub.sh --port` (a port saved in the web form still wins until reset). Startup fallbacks keep the Hub reachable after a bad setting. Decisions: [ADR 0015](../adr/0015-hub-ui-pass.md).

## Certificate

The certificate's SHA-256 fingerprint is printed to the log at startup (`journalctl -u framebeam-hub | grep -i fingerprint`); Players confirm it on first contact.

- The Hub creates a self-signed certificate (ECDSA P-256, 10 years) under `<data-dir>/tls/`. At startup it renews it when it is expired or expires within 30 days; the previous pair stays as `cert.pem.prev` / `key.pem.prev`. The Settings page shows "Expires within 30 days".
- `framebeam-hub renew-cert [-data-dir <dir>]` renews on demand and prints the certificate directory it renewed. It refuses when `-tls-cert` / `-tls-key` are set. Own certificates are never modified.
- A renewed certificate has a new fingerprint, which Players must confirm ([ADR 0009](../adr/0009-finish-poc.md)); see [player.md](player.md).
- On a systemd install do not call `framebeam-hub renew-cert` directly: another user or `-data-dir` renews a different certificate, and root would create files the service cannot read. Use `install-hub.sh renew-cert`.

## Core packages

The Hub fetches an Ed25519-signed core index and the packages from FrameBeam's GitHub Releases (at startup, every 24 h, and on "Check source now" in Systems & Cores) and serves them to Players. It needs internet access to github.com. Offline: `framebeam-hub import-cores <dir>` (systemd: `install-hub.sh import-cores <dir>`) with a directory holding `cores-index.json`, `cores-index.json.sig` and the package files. Extra trusted keys: `-core-trust-key`. Details: [ADR 0010](../adr/0010-cores-from-the-hub.md).

## Data directory

Default `/var/lib/framebeam`: `framebeam.db` (SQLite, metadata only), `saves/`, `firmware/<system>/`, `tls/`, `cores/`, `updates/`, and `backups/` (database backups before schema migrations, newest 5). ROMs, saves and firmware are files; they are never stored in SQLite.
