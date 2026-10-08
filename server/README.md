# server: FrameBeam Hub

Go service with SQLite: ROM library, versioned saves, users and devices, Session signaling, optional STUN/TURN relay, core package cache, updater and a web interface (`html/template` + htmx, admins only). It never emulates, encodes or renders. Rules for agents: `CLAUDE.md`.

## Build, run, test

```sh
make check-hub                                   # gofmt, vet, staticcheck, tests, codegen freshness, packaging checks
make build-hub                                   # server/dist/framebeam-hub-linux-{amd64,arm64}
make generate                                    # Go code from protocol/openapi (oapi-codegen)
framebeam-hub -dev -listen 127.0.0.1:8443 -data-dir /tmp/fb   # development: HTTP instead of HTTPS
framebeam-hub setup-admin -username <name>       # first admin (password as one line from stdin)
```

Requires Go >= 1.25 (`go.mod`). `cmd/framebeam-hub` is the Hub, `cmd/framebeam-sign` signs core and update indexes.

## Documentation

- Install on Linux (`.deb`, systemd script): [docs/guides/hub-install.md](../docs/guides/hub-install.md)
- Flags, environment, certificates, data directory: [docs/guides/hub-configuration.md](../docs/guides/hub-configuration.md)
- Updates and releases: [docs/guides/updates.md](../docs/guides/updates.md); Sessions and the relay: [docs/guides/sessions-over-the-internet.md](../docs/guides/sessions-over-the-internet.md)
- Code layout, web interface, users/invites, firmware, update internals: [docs/reference/hub-internals.md](../docs/reference/hub-internals.md)
- API: [protocol/README.md](../protocol/README.md); decisions: [ADR 0002](../docs/adr/0002-protocol-and-hub-phase1.md), [0005](../docs/adr/0005-saves-phase3.md), [0006](../docs/adr/0006-sessions-phase4.md), [0010](../docs/adr/0010-cores-from-the-hub.md), [0011](../docs/adr/0011-automatic-updates.md), [0012](../docs/adr/0012-internet-sessions-and-save-comfort.md), [0015](../docs/adr/0015-hub-ui-pass.md)
