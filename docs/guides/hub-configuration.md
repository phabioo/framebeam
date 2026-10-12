# Hub configuration

Reference for running the FrameBeam Hub binary: commands, flags and environment variables, certificates. For installation see [hub-install.md](hub-install.md).

## Commands

```sh
framebeam-hub [flags]                          # run the server (HTTPS, self-signed certificate under <data-dir>/tls/)
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # development: HTTP instead of HTTPS
framebeam-hub setup-admin -username <name>     # password as a single line from stdin; refused if an admin already exists
framebeam-hub renew-cert [-data-dir <dir>]     # renew the self-generated certificate now (refused with own cert/key)
framebeam-hub import-cores <dir>               # import libretro buildbot core zips offline
framebeam-hub import-plugins <dir>             # offer every *.fbplugin file of a folder to Players (offline, see "Plugins")
framebeam-hub diagnostics <out.zip>            # write the diagnostics bundle without the web interface (see "Diagnostics bundle")
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
| `-turn-external` (repeatable) | `FRAMEBEAM_TURN_EXTERNAL` (one server per line) | none | External TURN server (own coturn or a third-party provider), see "External TURN servers" below |
| (env only) | `FRAMEBEAM_TURN_EXTERNAL_<n>_SECRET`, `FRAMEBEAM_TURN_EXTERNAL_<n>_PASSWORD` | none | Shared secret (mode `secret`) or password (mode `static`) of the n-th external TURN server (1-based); wins over its `secret-file` / `password-file` |
| `-public-host` | `FRAMEBEAM_PUBLIC_HOST` | empty | DNS name or IPv4 of the Hub's public address, optionally `host:port` (IPv6 in brackets, `[2001:db8::1]:8443`). The port is the external port a router forwards to the Hub; it is used for invite links and the Player address, while TURN/STUN URLs use only the host part with their own ports. Without a port, invite links use the Hub's listen port (nothing for 443) |
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
| `-plugin-index-url` | `FRAMEBEAM_PLUGIN_INDEX_URL` | FrameBeam's `plugin-index` release | Signed plugin index `plugin-index.json` (+ `.sig` next to it), https or `file://`; `none` (or empty) turns the index off for offline Hubs, the Plugins page then has no catalog. Checked at start, every 24 h and with "Check now"; a failure is shown on the page and never stops the Hub. Trusted keys are the same as for the updates index (`-core-trust-key`) |
| `-update-request-dir` | `FRAMEBEAM_HUB_UPDATE_REQUEST_DIR` | `/run/framebeam` | Directory of the update request file read by the root helper |

`0` for a `-save-keep-*` rule means unlimited for that rule. Invalid combinations (for example `-turn` without `-public-host`, a `-public-host` that is not `host[:port]` (scheme, path, spaces, port outside 1 to 65535, IPv6 without brackets), a non-`stun:` ICE URL, a non-IPv4 `-turn-relay-ip`) stop the start with an error.

### TURN security

**STUN and ICE updates.** Without a configured STUN list the Hub hands Players `stun:stun.cloudflare.com:3478`, so they learn their public address even when the Hub itself sits in a LAN. Set `-ice-servers none` (or "Use no external server" in Settings → Network) for a purely local setup; adding a server in Settings replaces the default. Whenever the effective ICE configuration changes (STUN list, embedded TURN on/off, Cloudflare credentials refreshed) the Hub sends every connected Player an `ice_update` message (feature `ice_update_v1`), so no reconnect is needed.

**Cloudflare TURN (optional).** As a relay without port forwarding: create a TURN key in the Cloudflare dashboard (Realtime → TURN), set `FRAMEBEAM_CLOUDFLARE_TURN_KEY_ID` and put the key's API token in `FRAMEBEAM_CLOUDFLARE_TURN_API_TOKEN` or in a file readable only by the Hub (`-cloudflare-turn-token-file /etc/framebeam/cloudflare-turn.token`, mode 0600). The token is never accepted as a flag value, never logged, never written to the database or shown in Settings. The Hub requests 24 h credentials, refreshes them at half the TTL and offers them to Players in addition to the embedded TURN; URLs on port 53 are dropped. On errors it logs one warning per failure streak (without token or response body), retries with backoff and keeps the embedded TURN working. It stays off while Network sharing is off.

**External TURN servers (optional).** Any standard TURN server can be a relay provider: your own coturn on a VPS ([guide](coturn-on-a-vps.md)) or a third-party provider. It runs next to the embedded TURN and Cloudflare TURN; Players get the credentials of every active provider (`turn_servers` in `hello_ack`, the join response, `viewer_joined` and `ice_update`) and ICE picks a path. Admins can manage the servers in the web interface (Settings → Network → External TURN servers: add, edit, delete; see below) or with the flags. One `-turn-external` per server, as `key=value` fields separated by `;`:

| Field | Meaning |
| --- | --- |
| `urls` | Required. Comma-separated `turn:` / `turns:` URLs with optional `?transport=udp` or `?transport=tcp` |
| `mode` | `secret` (default) or `static` |
| `secret-file` | Mode `secret`: file holding the shared secret (alternative: `FRAMEBEAM_TURN_EXTERNAL_<n>_SECRET`) |
| `username`, `password-file` | Mode `static`: the login (not secret) and the file holding its password (alternative: `FRAMEBEAM_TURN_EXTERNAL_<n>_PASSWORD`) |
| `name` | Optional label for Settings, diagnostics and logs (default `external-<n>`) |

- **Shared secret (TURN REST API, coturn `use-auth-secret`).** The Hub never sends the secret. For each Player it issues time-limited credentials: username `<expiry-unix>:<device-id>`, password `base64(HMAC-SHA1(secret, username))`, valid 12 h like the embedded TURN, renewed by an `ice_update` push every 6 h.
- **Static login.** One fixed username and password for every Player (for providers that give you only that). The credentials carry no expiry, so anyone with a paired Player can read them from the Hub; use a shared secret where the server supports it.

```
FRAMEBEAM_TURN_EXTERNAL="name=vps;urls=turn:turn.example.org:3478?transport=udp,turn:turn.example.org:3478?transport=tcp,turns:turn.example.org:5349?transport=tcp;secret-file=/etc/framebeam/turn-vps.secret"
# or as flags (repeatable):
#   -turn-external 'mode=static;username=framebeam;urls=turn:relay.example.net:3478;password-file=/etc/framebeam/relay.password'
FRAMEBEAM_TURN_EXTERNAL_2_PASSWORD=...   # alternative to password-file for the second server
```

Secrets and passwords of flag-configured servers come only from the environment or a file (mode 0600, readable only by the Hub), never from a flag value; the Hub does not log them and does not show them. A flag server whose secret is missing is listed as "not set", is not handed to Players and logs one warning.

**Editing in the web interface.** Settings → Network → External TURN servers lets an admin add, edit and delete servers (name, URLs, mode `secret` or `static`, username for static, secret or password). The same rule as for the other Network settings applies: what is saved in the web interface wins, flags and `hub.env` are only the initial value. As long as nothing is saved, the list from `-turn-external` applies. The first add, edit or delete copies that list into the Hub database (names, URLs, modes and usernames, but not the secrets: a copied server keeps using its environment/file secret until you enter a new one in the form, also after a rename) and from then on the saved list is the only list; "Use hub flags again" deletes it. Secrets and passwords entered in the web interface are stored in the Hub database settings like the embedded TURN secret and are write-only: never shown again, not in HTML, logs, the diagnostics bundle or API responses; the page only shows "set" or "not set". Leave the field empty on edit to keep the stored value, tick "Clear" to remove it (a server without a secret is not used); changing the mode requires entering the new secret. Changes apply at once without a restart: new joins and `hello_ack` use the new list and connected Players receive it by `ice_update`. External servers stay off while Network sharing is off (the page says so; the list can still be edited, and applies after sharing is on and the Hub restarted). Settings → Network and the diagnostics bundle (`network.external_turn` in `hub-info.json`, with `source` `web` or `flags` and `active`) list the configured servers with URLs and mode and whether a secret is set, never the secret.

With `-turn`, forwarding the TURN ports exposes only the TURN port (`-turn-port`, 3478 UDP and TCP by default) and the UDP relay range (`-turn-relay-ports`). The relay works only with short-lived (12 h) credentials that the Hub issues to paired, non-revoked devices; every TURN authentication re-checks the device. Relay targets in internal address ranges (private LAN, CGNAT, ULA, loopback, link-local and similar) are refused, except the address of a Player currently connected to the Hub, so a remote Player can still reach a Player in the Hub's LAN. Limits: UDP allocations only, at most 10 concurrent allocations per device and 64 concurrent TURN TCP connections. Decisions: [ADR 0012](../adr/0012-internet-sessions-and-save-comfort.md).

Recommendations for a home network:

- Forward only the TURN port and the relay range (for example `hub.example.com`, 3478 and 49160-49199), nothing else.
- Keep SSH and the Hub web port unforwarded unless you need them from outside.
- Keep the Hub machine updated.
- Optionally put the Hub host in a separate guest or DMZ network or VLAN.
- Revoke devices you no longer trust.

When the Hub's HTTPS port is forwarded too, the web login and the Player API are reachable from the internet. The Hub limits sign-in attempts per address (IPv6 per /64) and per username (addresses in the local network and addresses that signed in successfully in the last 30 days are exempt from the username limit), runs at most two password checks at once, requires a CSRF token on every form, applies a 30 s read deadline to ordinary requests and caps open connections at 512. Pairing requests, invite redemptions and WebSocket messages are rate limited. New passwords need at least 12 characters; use a long, unique admin password.

## Settings in the web interface

Library page (admin): each row has Upload save, Rename (inline form, display title only, up to 200 bytes of UTF-8) and Delete (inline confirmation). Deleting removes the entry and the ROM file; saves of the game are kept and stay on the Saves page.

Settings → Network edits port, embedded TURN, public host, TURN port, relay range and IP, STUN servers and save retention. These values live in the Hub database and win over flags and `hub.env`, which only give the initial values (reset per field to return to the `hub.env` value). Changes that need it are applied by a restart the Hub triggers itself (no root). The web form refuses ports below 1024; set those with `install-hub.sh --port` (a port saved in the web form still wins until reset). Startup fallbacks keep the Hub reachable after a bad setting. Decisions: [ADR 0015](../adr/0015-hub-ui-pass.md).

## Diagnostics bundle

When something does not work, hand over one file instead of describing it: Settings → Diagnostics → "Download diagnostics bundle" (admin only; a form POST with the CSRF token, answered with `framebeam-hub-diagnostics-<UTC timestamp>.zip`). The bundle contains:

| Entry | Content |
|---|---|
| `README.txt` | what is in the bundle and what was removed |
| `hub-info.json` | Hub version, channel, commit, protocol; OS, architecture, uptime, free space of the data directory; effective network configuration (listen, public host, TLS, TURN on/off and ports, STUN list); core source state and installed cores per system with versions; counts (users, devices, games, running Sessions); paired Players with Player version, status, connected and last seen; feature list |
| `logs/hub.log`, `logs/hub.log.1`, `logs/updater.log` ... | the Hub's log files in `<data-dir>/logs/` (Windows service), newest first, capped at about 50 MiB in total (the oldest file that does not fit is cut to its newest lines) |
| `logs/recent.log` | the Hub's last 5000 log lines (at most 4 MiB) kept in memory since the start, so logs exist even without a log file or journal access (web bundle only) |
| `logs/journal.txt` | Linux: the last 5000 lines of `journalctl -u framebeam-hub` when `journalctl` is available and the Hub user may read it |

Everything in it passes a redaction pass: TURN secret and credentials (`username`/`credential`), tokens, passwords, invite codes, `Authorization` headers and session cookies are replaced by `[redacted]`. IP addresses and host names stay because connection problems cannot be debugged without them; the page says so ("contains IP addresses and host names, no passwords or tokens"). Redaction is pattern based: look through the files before you post them publicly.

If the web interface does not come up, `framebeam-hub diagnostics out.zip [-data-dir <dir>]` writes the same layout from the command line (as the service user, for example `sudo -u framebeam framebeam-hub diagnostics /tmp/out.zip -data-dir /var/lib/framebeam`). It never opens the database, so a running Hub is not disturbed; `hub-info.json` then has `"mode": "offline"` and lacks cores, counts and Players, and the network values come from `hub.env`/flags, not the saved web settings. It refuses to overwrite an existing file.

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

## Plugins

Plugins (0.11, [ADR 0023](../adr/0023-plugins.md)) are `.fbplugin` files (zip with `manifest.json`, Lua code and assets, at most 32 MiB packed, 64 MiB unpacked, 2000 entries; optional `SIGNATURE.json`). The Hub stores them under `<data-dir>/plugins/<plugin_id>/<version>.fbplugin` (metadata in the database) and offers every stored version to all Players (`GET /api/v1/plugins`, handshake feature `plugins_v1`). The Hub never runs plugin code.

- Origin: `official` when `SIGNATURE.json` lists exactly the files of the zip and carries a valid Ed25519 signature of a trusted key (the FrameBeam release key plus `-core-trust-key`); otherwise `unverified` (a present but invalid signature is reported as such and never counts as official).
- Plugins page (admin): table of the offered versions, detail with permissions, download and remove. Upload shows name, version, origin and one tick per requested permission; "Offer to Players" works only with every tick set (checked on the server). The same id and version with a different file is refused; remove the old version first. Removal keeps the copies Players already installed and is refused (409) while a run that is not finished or abandoned uses the version.
- Official catalog: the page lists the signed official plugin index (default, `-plugin-index-url`; `none` disables it) with Install / Update. Install downloads the file, checks size and SHA-256 against the signed index and the embedded signature against the trusted keys, and stores it as `official` (source `index`).
- Offline: `framebeam-hub import-plugins <dir>` offers every `*.fbplugin` file directly in `<dir>` (source `import`; running the command is the confirmation of the permissions; unsigned files become `unverified`). Run it as the service user with the service's `-data-dir`.
- Publishing (see [plugin-publishing.md](plugin-publishing.md)): `framebeam-sign plugin-sign -in x.fbplugin -out signed.fbplugin` (seed from `FRAMEBEAM_SIGNING_KEY`), `framebeam-sign plugin-add -index plugin-index.json -plugin signed.fbplugin -url https://.../signed.fbplugin` (refuses unsigned plugins), then `framebeam-sign sign -index plugin-index.json` writes `plugin-index.json.sig`.

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
