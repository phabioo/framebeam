# Architecture: repository and open points

## 8. Repository and open implementation decisions

Client and server are two build targets of the same FrameBeam monorepo and not separate projects or repositories. The corresponding client and server artifacts are provided in the common FrameBeam release.

The binding product terms in UI and documentation are **FrameBeam Player** for the client and **FrameBeam Hub** for the server. Internally the names `client` and `server` remain.

A common monorepo keeps client, server and protocol changes together:

```text
framebeam/
├── client/       # app, core, emulation, media, network, ui
├── server/
├── protocol/     # openapi, schemas
├── packaging/    # windows, linux, macos
└── docs/
```

Still to be specified are the concrete encoder/decoder integration, API endpoints and message formats, protocol compatibility rules, token format and exact access/refresh lifetimes as well as technical revocation/renewal details within the defined model. Also open are concrete TLS certificate management including renewal/pin change, save retention and details of conflict resolution, cache limits/cleanup, ICE/STUN configuration and later TURN use, media parameters as well as core/firmware manifest and package formats. Approval pairing, roles, passwordless users and the TLS requirement are already decided. These open details introduce no additional PoC features.

### Packaging and updates (after the PoC)

Planned, not part of the PoC (the PoC packaging stays Windows installer and systemd unit, see `docs/workflow.md`). Target: the first versions after the PoC.

- **Installers:** per platform for both products. Player: Windows installer, Linux package, macOS app bundle/dmg. Hub: Linux packages including ARM64 (Raspberry Pi) with systemd unit; Windows/macOS service later.
- **Install layout:** the Player no longer ships as a loose folder of DLLs; runtime libraries live in a subfolder, not the top level. Player data stays in the portable data directory (ADR 0004).
- **Integrated updater (Hub and Player):** checks for new FrameBeam releases (common release), shows the available version, downloads, verifies integrity (hash/signature) and applies only after user confirmation. An update never touches user data, saves or ROMs. Player/Hub compatibility is checked via the existing handshake (`protocol_version`).
- **Open:** installer tooling, update channel/feed, signing and key handling, rollback behavior.
