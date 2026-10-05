# Architecture: protocols, handshake, ROM cache

## 3. Protocols and data flows

The API is versioned under `/api/v1/...`. In addition there is a standalone `protocol_version`, separate from the Player and Hub product versions. Compatibility is determined primarily by the supported protocol version, not by identical product versions. A complex RPC layer or gRPC is not planned.

| Connection | Content |
|---|---|
| Client ↔ server: HTTPS/JSON | Library and metadata, ROM download, save download/upload, devices and Sessions |
| Client ↔ server: WSS | Presence, Session updates and WebRTC signaling |
| Client ↔ client: WebRTC | H.264 video and Opus audio; DataChannel possibly later |

Planned API areas: `/games`, `/roms`, `/saves`, `/devices` and `/sessions` under the versioned prefix. In addition there is a public FrameBeam info endpoint for Hub identification as well as API areas for pairing and token revocation (see section 14). Exact endpoints and message formats are still to be specified.

### Protocol handshake and capability negotiation [PoC]

On connect, the Player reports at least `platform`, `arch`, Player version, `protocol_version`, available core IDs and core versions, H.264 encode/decode capability and available encoders, Opus capability and input capabilities. The Hub replies with Hub version, `protocol_version` and compatibility status. The handshake does not replace authentication or device approval.

Different product versions may work together given a compatible protocol. Clear error states distinguish "Player too old", "Hub too old", "core missing", "core version mismatch" and "codec/capability missing". Missing capabilities block the affected launch or streaming path; exact compatibility rules and message formats remain to be specified.

### Game launch and ROM cache

1. The Player connects to the selected Hub, authenticates with a short-lived access token of its authorized device and performs the handshake. It loads the library data including game title, ROM size and SHA-256. External game metadata and box art are not required in the PoC.
2. It checks whether the ROM with this hash is already in the local cache.
3. On a validated hit it uses the local file; otherwise it downloads the ROM, verifies SHA-256 and places it in the cache only after successful verification.
4. It checks core compatibility and required firmware, and if necessary downloads and validates admin-provided firmware (see section 6). It loads the current save and starts the emulator with local files.

```text
Server metadata → cache check → ROM download if needed → local emulation
Server save ──────────────────────────────────────────┘
```

ROMs are not read over a network file system during emulation. Repeated launches of a cached ROM need no further ROM transfer. Cache limit, cleanup and download error handling are still to be defined.
