# protocol/ – gemeinsame Protokolldefinition

Details nur bei Bedarf aus `docs/architektur/02-protokolle-und-rom-cache.md` (Handshake), `03-saves.md`, `10-identitaet-pairing-tls.md`.

- HTTPS/JSON unter `/api/v1/...`; WSS für Presence, Session-Updates, WebRTC-Signaling; WebRTC (libdatachannel) P2P mit H.264 + Opus.
- `protocol_version` ist getrennt von Hub-/Player-Produktversion; Kompatibilität richtet sich nach ihr. Breaking Changes nur mit Anhebung.
- Handshake mit Capability Negotiation; Fehlerzustände: Player zu alt, Hub zu alt, Core fehlt, Core-Version mismatch, Codec/Capability fehlt.
- Kein gRPC, keine RPC-Schicht.
- OpenAPI (`openapi/`) ist Quelle für generierten Code (Go-Codegen geplant); JSON-Schemas in `schemas/`.
- Als offen Markiertes nicht festlegen; als Vorschlag melden. Keine Secrets in Beispielen.
- Protokolländerungen immer zusammen mit Hub- und Player-Anpassung.
