# ADR 0006: Sessions, signaling and multiview in phase 4

- Status: accepted
- Date: 2026-10-05
- Decided by: Fabio (proposal by the orchestrator, accepted on 2026-10-06)

## Context

Phase 4 adds Session sharing: a FrameBeam Player shares the game it is running, other Players of the same Hub watch and listen over direct WebRTC, and a Player shows a local and a remote Session as picture-in-picture or side-by-side. The architecture (`docs/architecture/04-sessions-and-multiview.md`, sections 4 and 5; `07-poc-scope.md`) fixes the principles: the Hub manages metadata, visibility, presence and signaling and never touches media. This ADR fixes data model, protocol, media path and UI behavior for the PoC. Design: `docs/design/player.md` screens 3c (Sessions on this Hub), 3g, 3h, 3i.

## Decisions

### D1 Session model

- A Session belongs to one owner device (and thereby its user, `devices.user_id`) and one game. A device has at most one active Session; publishing a new one ends the previous one. Reason: crash recovery without manual cleanup.
- Visibility: `private` | `hub_users` | `invite_only`.
  - `private`: only devices of the owner user may join (watching your own game on a second device). Reason: until users arrive in phase 5 there is only the admin user, so this is the only way to test with two devices; a foreign user can never join.
  - `hub_users`: every authenticated device of the active Hub.
  - `invite_only`: owner user devices plus users in `allowed_user_ids` (table `session_invites`).
- Viewers get fixed permissions `view_video=true`, `hear_audio=true`, `send_input=false`; there is no API to change them in the PoC. Viewers cannot invite, re-share or change anything.
- Max 4 viewers per Session (`session_full`). Reason: one software encoder, per-viewer upload on a home uplink.
- Lifetime: a Session ends on owner `DELETE`, on a new publish from the same device, on device revoke, or 30 s after the owner's WSS connection dropped without reconnect. Ending a Session ends all viewers and expires all its invites.
- Storage: SQLite tables `sessions`, `session_invites`, `session_viewers` (migration `0003_sessions.sql`); ended Sessions keep `ended_at` for diagnostics. Live connection state stays in memory.

### D2 REST API (OpenAPI 1.2.0, tag `sessions`)

All under `/api/v1`, Bearer auth:

| Method | Path | Who | Purpose |
|---|---|---|---|
| POST | `/sessions` | any device | publish `{game_id, visibility}` -> 201 Session |
| GET | `/sessions` | any device | active Sessions the caller may join or owns, plus Sessions inviting the caller |
| GET | `/sessions/{session_id}` | allowed | one Session |
| PATCH | `/sessions/{session_id}` | owner device | `{visibility}`; revokes viewers no longer allowed |
| DELETE | `/sessions/{session_id}` | owner device | end Session |
| PUT | `/sessions/{session_id}/invites/{user_id}` | owner device | invite a user of this Hub |
| DELETE | `/sessions/{session_id}/invites/{user_id}` | owner device | withdraw; revokes that user's viewers |
| POST | `/sessions/{session_id}/decline` | invited user | decline the invite |
| POST | `/sessions/{session_id}/join` | allowed | -> `{viewer_id, permissions, ice_servers}`; ACL checked here |
| DELETE | `/sessions/{session_id}/viewers/{viewer_id}` | owner device or that viewer | remove / leave |
| GET | `/users` | any device | users of this Hub `{id, display_name, online}` for the invite field |

Session object: `session_id, game_id, game_title, owner {user_id, display_name, device_name}, visibility, created_at, viewer_count, viewers[] (owner only: viewer_id, user display name, device name), invites[] (owner only: user_id, display_name, state invited|declined|joined, online), is_owner, invited` (caller has an open invite).

New error codes: `session_not_found` (404), `session_forbidden` (403, ACL), `session_full` (409), `session_ended` (410). Codec check: a device whose handshake reports `video.h264_encode=false` cannot publish, one with `h264_decode=false` cannot join (`capability_missing`). The handshake response adds feature `sessions_v1`. `protocol_version` stays 1 (additive).

### D3 WSS (`GET /api/v1/ws`)

- Bearer access token in the upgrade request (header); the connection stays valid until the device is revoked (Hub closes it). Envelope `{type, id?, payload}` as in `protocol/schemas`.
- Client -> Hub: `hello {protocol_version, device_id}` (first message, else close), `signal`, `presence_update {state: online|in_game, game_id?}`.
- Hub -> client: `hello_ack {protocol_version, features, ice_servers}`, `presence_update {user_id, device_id, state, game_id?}`, `session_update {session}` (created/changed, to every device that may see it), `session_ended {session_id, reason}`, `session_invite {session}`, `viewer_joined {session_id, viewer_id, user display name, device_name}` (to the owner device), `viewer_left {session_id, viewer_id, reason: left|removed|revoked|disconnected}` (to owner and viewer), `signal`, `error`.
- `signal {session_id, viewer_id, kind: offer|answer|candidate, sdp?, candidate?, mid?}`: the Hub relays only between the owner device and that viewer's device and only while the viewer is authorized; anything else gets `error session_forbidden`. The Hub never parses SDP.
- Keepalive: WebSocket ping every 20 s; a viewer whose WSS drops is removed.
- Revocation: on visibility change, invite withdrawal, viewer removal or Session end, the Hub sends `viewer_left`/`session_ended` to both sides; both Players close the PeerConnection immediately, so an established media path is cut too.
- Go library: `github.com/coder/websocket`.

### D4 ICE

- Direct P2P only (no TURN in the PoC). Hub config `ice_servers` (list of `stun:` URLs), default empty: host candidates suffice on a LAN. Delivered in `hello_ack` and the join response. Settable via config file/flag; no web UI in phase 4.

### D5 Media path (Player)

- WebRTC: libdatachannel 0.24.5 (media enabled, own WebSocket off). The owner offers one `sendonly` H.264 video and one Opus audio track per viewer; the viewer answers `recvonly`. Packetization and depacketization, RTCP SR/RR and PLI via libdatachannel handlers.
- Encoding happens once per Session, not per viewer; encoded packets fan out to all viewer tracks. Keyframe on viewer join and on PLI. The encoder runs only while at least one viewer is connected (architecture: encoder off when playing alone).
- Video: libavcodec; encoder preference `h264_nvenc`, `h264_qsv`, `h264_amf`, `libopenh264`, `libx264` (first that opens; hardware encoders are compiled in where available but only verified locally by Fabio). Low-latency settings, baseline/constrained profile, no B-frames, GOP 2 s, fixed 2 Mbit/s at native core resolution (DS: 256 x 384, both screens stacked), 60 fps. Decoder: libavcodec `h264`. Bitrate adaptation is a follow-up (fixed bitrate in phase 4).
- Audio: core audio resampled to 48 kHz stereo, libopus 20 ms frames, 96 kbit/s; viewer decodes with libopus into a small jitter buffer (target 60 ms) before the audio output.
- Dependencies: Linux (cloud and CI) uses apt `libavcodec-dev libswscale-dev libopus-dev qt6-websockets-dev` and builds libdatachannel from git (`scripts/fetch-libdatachannel.sh`, pinned, cached under `$HOME/.cache/framebeam/deps`). Windows uses vcpkg (`libdatachannel`, `ffmpeg[avcodec,swscale,openh264]`, `opus`, platform-qualified in `client/vcpkg.json`) and install-qt-action module `qtwebsockets`. Reason: cloud sessions cannot download vcpkg source archives, only git clone. Ubuntu's FFmpeg has `libx264` but no `libopenh264`; both are GPL-compatible with the GPL-3.0 Player (melonDS DS).
- WSS client: Qt WebSockets with the existing leaf-fingerprint pinning (`QSslConfiguration` from the Hub connection).

### D6 Multiview and UI

- Library (3c): section "SESSIONS ON THIS HUB" from `GET /sessions` plus live `session_update`; "Watch Session", or "Join"/"Decline" for invites. Detail pane: "Play and share Session" publishes with the last chosen visibility (default `hub_users`).
- In game (3g): tabs Session | Multiview | Diagnostics. The Session tab shows visibility, invited users and viewers with Remove/Withdraw, the user search over `GET /users`, and "Share Session"/"Stop sharing". Diagnostics are a tab and also collapsible in the Session tab (design deviation 1 accepted as drawn: optional, never required).
- Multiview: exactly local Session plus one remote Session. Modes side-by-side (3h) and PiP (3i, fixed position bottom right, "Swap" and "Remove"). A viewer without a running game sees the remote Session alone in the main surface. Exactly one surface has audio: local by default, switched with "Audio here"; the other is muted.
- Diagnostics per surface: fps, encoder name, codec, video bitrate, Opus bitrate (local); fps, connection type (direct host/srflx), RTT, bitrate, packet loss if available (remote).
- Hub failure: if the WSS drops, the Player reconnects with backoff; a running media path continues until the Hub ends the Session (30 s grace).

## Verification in the cloud

- Hub: Go tests for ACL per visibility, revoke on visibility change/withdraw/remove, signaling relay restrictions and the 30 s owner grace, over a real WSS (`httptest`).
- Player: loopback test with two PeerConnections in one process (synthetic frames and tone through encoder, WebRTC, decoder); `scripts/e2e-session.sh` runs two `framebeam_player_cli` processes (one sharing synthetic frames, one watching) against a real Hub on loopback and checks that decoded frames and audio arrive.
- Locally by Fabio: two devices on the LAN, real core, PiP/side-by-side, audio switching, hardware encoder.

## Consequences

- New Linux build prerequisite (libdatachannel script) and larger Windows vcpkg build (FFmpeg); both cached in CI.
- Users and real foreign-user tests arrive with phase 5; the Hub tests cover foreign users already via `CreateUser`.

## Implementation notes (phase 4 PR)

Choices made where this ADR was silent.

### Hub

- `capability_missing` is HTTP 409. Devices that never reported capabilities are not blocked, only an explicit `false`.
- In `invite_only`, a declined invite removes access (`session_ended` with reason `no_longer_visible` to that user's devices); a new invite reopens it. Withdraw always removes that user's viewers (they may rejoin if still allowed).
- `session_ended` reasons: `ended|replaced|owner_disconnected|device_revoked|no_longer_visible`. A Session end sends `session_ended`, not per-viewer `viewer_left`.
- `is_owner` and owner-only fields are per owner device.
- A viewer or owner without WSS gets the 30 s grace, then removal. Active Sessions survive a Hub restart if the owner reconnects within the grace.
- Rejoin by the same device returns the same `viewer_id`; joining your own Session is 400.
- `presence_update` goes to all connected devices, plus a snapshot after `hello_ack`.
- A newer WSS of the same device replaces the older one (close 1008). Hello timeout 10 s, read limit 64 KiB.
- `signal` to an offline peer returns `not_found`.
- `ice_servers` only via flag/env; `stun:` is enforced.
- `GET /users` lists all users including admin.

### Player

- The encoder starts with the first frame after a viewer's PeerConnection is connected and stops with the last viewer.
- Packet loss on the viewer is derived from RTP sequence numbers, n/a on the host. RTT is n/a (libdatachannel reports RTT only with an SCTP channel); follow-up.
- Viewers close their PeerConnection on the Hub's `viewer_left`/`session_ended`. Early offers are buffered until the join response.
- Encoding runs on the UI thread (cheap at 256 x 384; to verify locally with the real core).
- Each surface draws the combined DS frame.
- No automatic rejoin after a failed PeerConnection.
- Linux CI/cloud uses libx264, Windows libopenh264; hardware encoders untested.
