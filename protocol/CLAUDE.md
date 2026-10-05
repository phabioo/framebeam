# protocol/ – shared protocol definition

Details only as needed from `docs/architecture/02-protocols-and-rom-cache.md` (handshake), `03-saves.md`, `10-identity-pairing-tls.md`.

- HTTPS/JSON under `/api/v1/...`; WSS for presence, Session updates, WebRTC signaling; WebRTC (libdatachannel) P2P with H.264 + Opus.
- `protocol_version` is separate from the Hub/Player product version; compatibility is determined by it. Breaking changes only with a bump.
- Handshake with capability negotiation; error states: Player too old, Hub too old, core missing, core version mismatch, codec/capability missing.
- No gRPC, no RPC layer.
- OpenAPI (`openapi/`) is the source for generated code (Go codegen planned); JSON schemas in `schemas/`.
- Do not decide anything marked as open; report it as a proposal. No secrets in examples.
- Protocol changes always together with Hub and Player changes.
