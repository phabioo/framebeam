# Sessions: LAN and internet

A Session shares a running game from one Player with other Players. Audio and video flow directly between the Players (WebRTC, H.264 + Opus); the Hub only handles presence, signaling and, optionally, a relay. Decisions: [ADR 0006](../adr/0006-sessions-phase4.md), [ADR 0012](../adr/0012-internet-sessions-and-save-comfort.md).

## Test Sessions on a LAN

Two Players on the LAN, both paired to the same Hub. With only the admin, both devices belong to the admin: Private (own devices only), Hub users and Invite only can be tested. With a second Hub user (create an invite, see [walkthrough.md](walkthrough.md)) Private rejects the foreign user and Hub users lets them in. STUN (`-ice-servers`) is not needed on a LAN. `scripts/e2e-session.sh` runs the same flow headless with two CLI processes against a local Hub; it also runs a second round with forced relay against a Hub with TURN on loopback.

## Sessions over the internet

Players on the same LAN need nothing (see above). For Players behind other routers the Hub embeds a STUN/TURN relay (off by default; ADR 0012). Media goes directly between Players when possible and through the Hub otherwise.

Requirements:

- A public IPv4 address at the Hub's router. DS-Lite and CGNAT (shared addresses) do not work; ask your provider or compare the router's WAN address with what a "what is my IP" site shows.
- A DNS name for that address, for example a DynDNS name such as `hub.example.org`. The Hub resolves its A record at start and every 5 minutes, so address changes are followed.
- Router port forwards to the Hub host:
  - TCP `<hub port>` (default 8443; the Hub's HTTPS/WSS port)
  - UDP and TCP `3478` (STUN/TURN)
  - UDP `49160-49199` (relay range, 40 allocations)

Switch it on in the web interface (Settings → Network, then "Restart hub now"), or in `/etc/framebeam/hub.env` and restart the service:

```sh
FRAMEBEAM_TURN=1
FRAMEBEAM_PUBLIC_HOST=hub.example.org
# optional: FRAMEBEAM_TURN_PORT=3478, FRAMEBEAM_TURN_RELAY_PORTS=49160-49199, FRAMEBEAM_TURN_RELAY_IP=<fixed public IPv4>
```

Values saved in Settings → Network override `hub.env` and the flags (reset per field to return to the `hub.env` value); `hub.env` only gives the initial values. This also applies to the port and the save retention values. The web form refuses ports below 1024; set those with `install-hub.sh --port` (note that a port saved in the web form then still wins until reset). The Network page shows the state (on/off, resolved IPv4, ports, active allocations) and the forwards needed. Players pair with the DNS name; credentials are issued automatically and expire after 12 hours. Relay is IPv4 only; direct IPv6 paths still work when both Players allow them. The Hub does not configure the router.
## Supported setups

LAN, internet with a router port forward (recommended, this page), or a VPN such as WireGuard or Tailscale. There is no external TURN server in 0.4 (ADR 0012 D1).

## Testing the relay

Set `FRAMEBEAM_FORCE_RELAY=1` (CLI `--force-relay`) on a Player to force the relay path. The diagnostics show the connection type (direct / relay) and the target bitrate; the host adapts the encoder bitrate to the worst viewer (AIMD).

Hub flags: `-turn`, `-public-host`, `-turn-port`, `-turn-relay-ports`, `-turn-relay-ip` (env `FRAMEBEAM_TURN*`, `FRAMEBEAM_PUBLIC_HOST`), see [hub-configuration.md](hub-configuration.md).
