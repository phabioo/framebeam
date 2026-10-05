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
