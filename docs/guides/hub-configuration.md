# Hub configuration

Reference for running the FrameBeam Hub binary: commands, flags and environment variables, certificates. For installation see [hub-install.md](hub-install.md).

## Commands

```sh
framebeam-hub [flags]                          # run the server (HTTPS, self-signed certificate under <data-dir>/tls/)
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # development: HTTP instead of HTTPS
framebeam-hub setup-admin -username <name>     # password as a single line from stdin; refused if an admin already exists
framebeam-hub renew-cert [-data-dir <dir>]     # renew the self-generated certificate now (refused with own cert/key)
framebeam-hub import-cores <dir>               # import libretro buildbot core zips offline
framebeam-hub version [--json]                 # version info
framebeam-hub update check|stage [-channel stable|beta]   # see updates.md
```

On a systemd install use the script instead of calling the binary for `renew-cert` and `import-cores`: `sudo packaging/linux/install-hub.sh renew-cert` / `import-cores <dir>` (it uses the service's data dir and user and restarts the service where needed), see [hub-install.md](hub-install.md).

Without an admin the Hub still starts; the web interface then redirects to `/setup` (the form is accepted from loopback clients only). A request counts as local only if it comes from loopback and carries none of `Forwarded`, `X-Forwarded-For`, `X-Real-IP`, `X-Forwarded-Host`; behind a reverse proxy that sets these headers, do the setup directly on the host (`setup-admin` or a direct loopback connection).

## Flags and environment

A flag wins over the environment variable. Under systemd the variables live in `/etc/framebeam/hub.env`. Values saved in the web interface (Settings → Network) win over both, see below.

| Flag | Environment | Default | Meaning |
|---|---|---|---|
| `-data-dir` | `FRAMEBEAM_DATA_DIR` | `/var/lib/framebeam` | Database, saves, firmware, certificate |
| `-listen` | `FRAMEBEAM_LISTEN` | `:8443` | Listen address and port |
| `-name` | `FRAMEBEAM_NAME` | empty | Hub name, first start only |
| `-tls-cert`, `-tls-key` | `FRAMEBEAM_TLS_CERT`, `FRAMEBEAM_TLS_KEY` | empty | Own certificate and key (PEM), only together |
| `-dev` | `FRAMEBEAM_DEV` | off | HTTP instead of HTTPS; excludes `-tls-cert` |
| `-ice-servers` | `FRAMEBEAM_ICE_SERVERS` | `stun:stun.cloudflare.com:3478` | Comma-separated `stun:` URLs; `none` = no external STUN |
| `-turn` | `FRAMEBEAM_TURN` | off | Embedded STUN/TURN relay; needs `-public-host` |
| `-cloudflare-turn-key-id` | `FRAMEBEAM_CLOUDFLARE_TURN_KEY_ID` | none | Cloudflare Realtime TURN key (optional relay next to `-turn`) |
| `-cloudflare-turn-token-file` | `FRAMEBEAM_CLOUDFLARE_TURN_TOKEN_FILE` | none | File holding the Cloudflare API token (alternative to the token variable) |
| (env only) | `FRAMEBEAM_CLOUDFLARE_TURN_API_TOKEN` | none | Cloudflare API token; wins over the token file |
| `-public-host` | `FRAMEBEAM_PUBLIC_HOST` | empty | DNS name or IPv4 of the Hub's public address |
| `-turn-port` | `FRAMEBEAM_TURN_PORT` | `3478` | TURN UDP and TCP port |
| `-turn-relay-ports` | `FRAMEBEAM_TURN_RELAY_PORTS` | `49160-49199` | TURN UDP relay range `min-max` |
| `-turn-relay-ip` | `FRAMEBEAM_TURN_RELAY_IP` | empty | Fixed public IPv4 for relayed addresses instead of resolving `-public-host` |
| `-library-import-dir` | `FRAMEBEAM_LIBRARY_IMPORT_DIR` | `<data-dir>/library-import` | Folder that the Library's "Rescan folder" imports ROMs from (created at start; files are only read, never moved or deleted) |
| `-save-keep-recent` | `FRAMEBEAM_SAVE_KEEP_RECENT` | `20` | Save history: newest versions per slot to keep |
| `-save-keep-daily` | `FRAMEBEAM_SAVE_KEEP_DAILY` | `30` | Save history: days of which the newest version is kept |
| `-save-keep-weekly` | `FRAMEBEAM_SAVE_KEEP_WEEKLY` | `26` | Save history: weeks of which the newest version is kept |
| `-core-buildbot-url` | `FRAMEBEAM_HUB_CORE_BUILDBOT_URL` | `https://buildbot.libretro.com/nightly` | Base URL of the libretro buildbot builds (https only) |
| `-core-info-url` | `FRAMEBEAM_HUB_CORE_INFO_URL` | `https://buildbot.libretro.com/assets/frontend/info.zip` | libretro core info archive (https only) |
| `-core-index-url` | `FRAMEBEAM_HUB_CORE_INDEX_URL` | none | Deprecated and ignored since 0.8 (the signed core index was retired) |
| `-core-trust-key` | `FRAMEBEAM_HUB_CORE_TRUST_KEYS` | none | Extra trusted Ed25519 public key (base64) for the updates index only (cores are not signed); flag repeatable, env comma-separated |
| `-update-index-url` | `FRAMEBEAM_HUB_UPDATE_INDEX_URL` | FrameBeam's `updates-index` release | Signed updates index (https, or `file://` for tests) |
| `-update-request-dir` | `FRAMEBEAM_HUB_UPDATE_REQUEST_DIR` | `/run/framebeam` | Directory of the update request file read by the root helper |

`0` for a `-save-keep-*` rule means unlimited for that rule. Invalid combinations (for example `-turn` without `-public-host`, a `-public-host` containing `/` or spaces, a non-`stun:` ICE URL, a non-IPv4 `-turn-relay-ip`) stop the start with an error.

### TURN security

**STUN and ICE updates.** Without a configured STUN list the Hub hands Players `stun:stun.cloudflare.com:3478`, so they learn their public address even when the Hub itself sits in a LAN. Set `-ice-servers none` (or "Use no external server" in Settings → Network) for a purely local setup; adding a server in Settings replaces the default. Whenever the effective ICE configuration changes (STUN list, embedded TURN on/off, Cloudflare credentials refreshed) the Hub sends every connected Player an `ice_update` message (feature `ice_update_v1`), so no reconnect is needed.

**Cloudflare TURN (optional).** As a relay without port forwarding: create a TURN key in the Cloudflare dashboard (Realtime → TURN), set `FRAMEBEAM_CLOUDFLARE_TURN_KEY_ID` and put the key's API token in `FRAMEBEAM_CLOUDFLARE_TURN_API_TOKEN` or in a file readable only by the Hub (`-cloudflare-turn-token-file /etc/framebeam/cloudflare-turn.token`, mode 0600). The token is never accepted as a flag value, never logged, never written to the database or shown in Settings. The Hub requests 24 h credentials, refreshes them at half the TTL and offers them to Players in addition to the embedded TURN; URLs on port 53 are dropped. On errors it logs one warning per failure streak (without token or response body), retries with backoff and keeps the embedded TURN working. It stays off while Network sharing is off.

With `-turn`, forwarding the TURN ports exposes only the TURN port (`-turn-port`, 3478 UDP and TCP by default) and the UDP relay range (`-turn-relay-ports`). The relay works only with short-lived (12 h) credentials that the Hub issues to paired, non-revoked devices; every TURN authentication re-checks the device. Relay targets in internal address ranges (private LAN, CGNAT, ULA, loopback, link-local and similar) are refused, except the address of a Player currently connected to the Hub, so a remote Player can still reach a Player in the Hub's LAN. Limits: UDP allocations only, at most 10 concurrent allocations per device and 64 concurrent TURN TCP connections. Decisions: [ADR 0012](../adr/0012-internet-sessions-and-save-comfort.md).

Recommendations for a home network:

- Forward only the TURN port and the relay range (for example `hub.example.com`, 3478 and 49160-49199), nothing else.
- Keep SSH and the Hub web port unforwarded unless you need them from outside.
- Keep the Hub machine updated.
- Optionally put the Hub host in a separate guest or DMZ network or VLAN.
- Revoke devices you no longer trust.

When the Hub's HTTPS port is forwarded too, the web login and the Player API are reachable from the internet. The Hub limits sign-in attempts per address (IPv6 per /64) and per username (addresses in the local network and addresses that signed in successfully in the last 30 days are exempt from the username limit), runs at most two password checks at once, requires a CSRF token on every form, applies a 30 s read deadline to ordinary requests and caps open connections at 512. Pairing requests, invite redemptions and WebSocket messages are rate limited. New passwords need at least 12 characters; use a long, unique admin password.

## Settings in the web interface

Settings → Network edits port, embedded TURN, public host, TURN port, relay range and IP, STUN servers and save retention. These values live in the Hub database and win over flags and `hub.env`, which only give the initial values (reset per field to return to the `hub.env` value). Changes that need it are applied by a restart the Hub triggers itself (no root). The web form refuses ports below 1024; set those with `install-hub.sh --port` (a port saved in the web form still wins until reset). Startup fallbacks keep the Hub reachable after a bad setting. Decisions: [ADR 0015](../adr/0015-hub-ui-pass.md).

## Certificate

The certificate's SHA-256 fingerprint is printed to the log at startup (`journalctl -u framebeam-hub | grep -i fingerprint`); Players confirm it on first contact.

- The Hub creates a self-signed certificate (ECDSA P-256, 10 years) under `<data-dir>/tls/`. At startup it renews it when it is expired or expires within 30 days; the previous pair stays as `cert.pem.prev` / `key.pem.prev`. The Settings page shows "Expires within 30 days".
- `framebeam-hub renew-cert [-data-dir <dir>]` renews on demand and prints the certificate directory it renewed. It refuses when `-tls-cert` / `-tls-key` are set. Own certificates are never modified.
- A renewed certificate has a new fingerprint, which Players must confirm ([ADR 0009](../adr/0009-finish-poc.md)); see [player.md](player.md).
- On a systemd install do not call `framebeam-hub renew-cert` directly: another user or `-data-dir` renews a different certificate, and root would create files the service cannot read. Use `install-hub.sh renew-cert`.

## Core packages

The Hub downloads cores RetroArch-style from the libretro buildbot (nightly channel) and serves them to Players; Players never contact the buildbot. The catalog comes from the libretro core info archive (`info.zip`) and the buildbot's `.index-extended`; it is refreshed at start, every 24 h and on "Check source now" on Systems & Cores. Installing a core is always an admin action. With "Automatic updates" (On by default, toggle in the source panel of Systems & Cores) the Hub updates every installed core that has a newer build after each catalog refresh; turning it on triggers a check. A failing core does not stop the others, and the last auto-update error is shown on Systems & Cores. When off, updates are manual (see below).

- On Systems & Cores the Cores tab lists the installed cores per system with Update (when the buildbot has a newer build and automatic updates are off or have not run yet; updating keeps the old build until the new one is cached), Remove and Make default, and "Available from the libretro buildbot" with Install. A core without a FrameBeam Player profile runs as experimental in the Player (marked "experimental" there). A system without an installed core shows a notice; Players cannot start its games until a core is installed.
- A download is checked against the CRC32 of `.index-extended` and must contain exactly one library file; the Hub pins the SHA-256 of that file at install and serves exactly it. The buildbot signs nothing; trust equals HTTPS to the buildbot.
- Versions are build ids `YYYY.MM.DD`, with `.N` appended for a second install on the same day. There is no expected-version selector; all Players of a Hub run the build the Hub serves.
- The Hub needs HTTPS egress to `buildbot.libretro.com`. Offline: `framebeam-hub import-cores <dir>` (systemd: `install-hub.sh import-cores <dir>`) with the layout `<dir>/<platform>/<core>_libretro.<suffix>.zip` (platform `linux-x64` or `windows-x64`, suffix `.so` or `.dll`) plus an optional `<dir>/info.zip` and `<dir>/<platform>/.index-extended` (the CRC32 is checked only when that file is present). The SHA-256 is pinned at import.
- Nintendo 3DS (0.10, [ADR 0022](../adr/0022-nintendo-3ds-with-azahar.md)): the Hub knows system `3ds` but has no default core. Install `azahar` from "Available from the libretro buildbot" on Systems & Cores, then Make default if needed. It is a profiled core. No firmware or system files are required or shipped; use decrypted dumps only. Players need OpenGL Core >= 3.3.
- Hubs upgraded from 0.7: packages from the retired signed source (origin `framebeam`, e.g. `melonds_ds`) are removed on start (database rows and stored files; one info log line per core; saves are untouched). A removed default core is replaced by another installed core of the system, else the system has no core and the Systems page asks the admin to install one from the libretro buildbot (`import-cores` on an offline Hub). Players cannot start games of a system without a core.

Details: [ADR 0020](../adr/0020-cores-from-the-libretro-buildbot.md).

## Windows

Install with the FrameBeam MSI (feature Hub; see [packaging.md](packaging.md)), usually through "Set up a Hub on this PC" in the Player, or manually: `msiexec /i framebeam.msi /qn ALLUSERS=1 INSTALL_HUB=1 INSTALL_PLAYER=0 HUB_PORT=8443 NETWORK_SHARING=1`. The binary `framebeam-hub-windows-amd64.exe` alone is for manual and test use. Decisions: [ADR 0021](../adr/0021-one-windows-installer.md).

- Service `FrameBeamHub` runs as `NT SERVICE\FrameBeamHub` with `-listen :<port> -network-sharing=<true|false>` from the MSI properties. A restart from the web interface re-runs the Hub in-process.
- Defaults: data `%ProgramData%\FrameBeam\Hub`, import dir `<data>\library-import`, update request dir `<data>\update-request`. Logs when running as a service: `<data>\logs\hub.log` (rotated at 10 MB, one old file kept).
- Updates: service `FrameBeamHubUpdater` (LocalSystem), see [updates.md](updates.md).
- `-network-sharing` (env `FRAMEBEAM_NETWORK_SHARING`, default `true`): initial value only; after the first start the stored setting wins. Off = the Hub listens only on 127.0.0.1 and ::1 (standalone mode, TURN off). Changing it restarts the server in-process.
- Settings card (web Settings): "Network sharing" and "Import folder" are stored settings. `-library-import-dir` is only the initial value or fallback. The import folder must be an absolute, existing folder the Hub can read (else `import_dir_unreadable`).
- `framebeam-hub grant-folder <path>` (Windows, run as administrator): gives `NT SERVICE\FrameBeamHub` read and list access (inherited) to a ROM folder outside the Hub's data directory. Other systems print "not supported" and exit 2.
- Local API (`/api/v1/local/status|setup|pair|settings`, feature `local_setup_v1`): loopback only, admin credentials needed, used by the Player's "Set up a Hub on this PC". Five wrong passwords lock local pairing for a minute. See [protocol.md](../reference/protocol.md).

## Data directory

Default `/var/lib/framebeam`: `framebeam.db` (SQLite, metadata only), `saves/`, `firmware/<system>/`, `tls/`, `cores/`, `updates/`, and `backups/` (database backups before schema migrations, newest 5). ROMs, saves and firmware are files; they are never stored in SQLite.
