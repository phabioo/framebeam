# ADR 0015: Hub UI pass (0.7)

- Status: accepted (Fabio, 2026-10-08)
- Date: 2026-10-07
- Decided by: Fabio (precedence and restart decided on 2026-10-07; self-restart without root, ports below 1024 refused on the web and reachability derived from configuration accepted on 2026-10-07)

## Context

Roadmap 0.7 (`docs/roadmap.md`) brings the FrameBeam Hub web interface to the v4 design (`docs/design/hub.md`, 3j-3s), makes navigation swap content instead of loading whole pages, updates the UI live, and replaces editing `hub.env` for network and retention values with a Settings form. Before 0.7 the sidebar loaded full pages, Clients polled every 15 s and TURN, port and retention were configured only by flags and `hub.env` (ADR 0012).

## Decisions

### D1 Fragment navigation

- Sidebar links use htmx (`hx-get`, `hx-target="#main"`, `hx-push-url`).
- A GET with `HX-Request` and `HX-Target: main` renders only the main content plus an out-of-band nav and the title. Direct loads stay full pages, so every URL works without JavaScript state.
- The nav refreshes via `GET /nav-fragment` on the `fb:badges` event.

### D2 Live updates: event bus and Server-Sent Events

- `hub.Service` has an in-process event bus. Topics: `clients`, `saves`, `library`, `systems`, `users`, `updates`. Events carry no payload and are published after successful service calls. Slow subscribers coalesce instead of blocking.
- `GET /events` (admin session) is an SSE stream: at most one event per topic per 500 ms, plus `badges` and a heartbeat every 25 s.
- `static/app.js` turns events into htmx events `fb:<topic>`. Live regions re-fetch themselves (`hx-trigger="fb:<topic> from:body"`, `hx-swap="outerHTML"`); the fragment is chosen by the `HX-Target` id. The 15 s Clients polling is removed.
- No htmx SSE extension and no Node build step.

### D3 Settings sub-pages and autosave

- Settings follow design 3q: sub-pages Updates, General, Network, Security at `/settings/{section}`.
- Each field saves on its own through htmx (autosave per field).

### D4 Network settings: precedence and storage

- Precedence (Fabio, 2026-10-07): web settings win over `hub.env` and flags; `hub.env` only gives the initial value.
- Storage: rows `net.<key>` in the `settings` table. Keys: `listen_port`, `public_host`, `turn`, `turn_port`, `turn_relay_ports`, `turn_relay_ip`, `ice_servers` (JSON), `save_keep_recent`, `save_keep_daily`, `save_keep_weekly`.
- At startup `main` applies the rows over the flag/`hub.env` configuration. For the listen address only the port is replaced; the host part is kept.
- Per field, "Reset to hub.env value" deletes the override. An empty saved value is a valid override.
- Live without restart: save retention and external STUN servers (running Sessions keep theirs). Restart needed: listen port, public host, TURN on/off, TURN port, relay range, relay IP. The page shows a "Restart required to apply" card with "Restart hub now".

### D5 Self-restart

- `POST /settings/network/restart` (admin, CSRF) starts a graceful shutdown as on SIGTERM, then re-executes the own binary with `syscall.Exec` (same PID). This works under systemd `Type=simple` and from a shell. A " (deleted)" suffix on the executable path, left by an update that replaced the binary, is stripped.
- If exec fails, the Hub exits non-zero and systemd `Restart=on-failure` restarts it. On non-unix platforms the Hub exits non-zero.
- No root and no `systemctl` call (accepted by Fabio, 2026-10-07).
- Unchanged port: the page polls and reloads. Changed port: the page shows a link to the new address, because the CSP `connect-src 'self'` forbids polling another origin.

### D6 Validation

- Ports 1024-65535. Ports below 1024 are refused on the web; use `install-hub.sh --port` (accepted by Fabio, 2026-10-07).
- Listen port and TURN port differ.
- Relay range: min <= max, at most 1000 ports, excludes both ports.
- TURN needs a public host. Relay IP must be IPv4.
- ICE servers: `stun:` URLs only, at most 10, no duplicates.
- Save retention values 0-100000.
- A new listen port is test-bound; failure shows "Port N is in use".

### D7 Startup fallbacks

- A saved port that cannot be bound: the Hub falls back to the `hub.env`/flag address and shows an error card.
- TURN fails with saved values: the Hub runs without TURN and shows the error.
- The combined configuration is invalid: the `hub.env`/flag configuration is used, with an error card.
- The Hub always comes up, so a bad web setting cannot lock the admin out.

### D8 Reachability derived from configuration

- The reachability card derives from the running configuration: "Prepared for the internet" when TURN runs and a public host is set, otherwise "Only in local network", naming what is missing.
- No external probe (accepted by Fabio, 2026-10-07).
- The TURN status and the router port forward list from 0.4 move into the Network page.

### Pages (v4 design)

Library (3j, saves column and conflict marker), Saves (3k/3s: slot tabs, history timeline, snapshot filter, inline restore confirmation, retention box built from the real rules), Systems (3l: list and detail tabs Firmware, Clients, Core & manifest), Clients (3m), Users (3n). Signal Cyan accent; cyan is also the warning color. The nav badge "firmware" is renamed "{n} issues" and counts firmware problems.

## Rejected

- htmx SSE extension or a JS/Node build: more dependencies; a small `app.js` bridge is enough.
- Polling for live updates: load and delay; the 15 s Clients polling is removed.
- Event payloads: fragments are re-fetched, which keeps one rendering path and avoids stale diffs.
- Restart through `systemctl` or a polkit rule: needs root or extra setup and does not work from a shell.
- Writing `hub.env` from the web UI: the service user cannot (and should not) write `/etc/framebeam`.
- Probing reachability from outside: needs an external service; derived state is enough.
- Ports below 1024 on the web: binding needs capabilities the service does not have.

## Consequences

- Network and retention values have two sources; the Network page marks overridden fields and offers per-field reset. `hub.env` edits to an overridden key have no effect until reset.
- Settings live in the database, so they survive reinstalls that keep the data dir.
- Slow or many SSE clients are bounded by coalescing; each admin tab holds one open connection.
- Open points left out of 0.7: Rescan folder, "+ New slot", deleting a snapshot, thinned-out history marker, invite "Copy link", metadata actions, "Player too old" marker in the Clients table, Library "of total" disk capacity, external TURN servers with login (only the STUN list), certificate "Renew now", reachability probe from outside, core packages not filtered per system, transport options "Custom cert/key" and "Reverse proxy".
