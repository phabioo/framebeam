# Architecture: Sessions and multiview

## 4. Session sharing and WebRTC/P2P

When playing alone, video and audio go directly to local output; the streaming encoder stays off. Only when sharing a Session is the media path additionally activated:

```text
Emulator ──→ local video/audio output
        └──→ encoder → WebRTC → remote client → decoder → output
```

The server manages Session metadata, visibility and presence and mediates connection setup via signaling. It does not transport or process any video or audio itself.

Direct P2P connections form the basis. Since 0.4 ([ADR 0012](../adr/0012-internet-sessions-and-save-comfort.md)) the Hub can embed an optional STUN/TURN relay (off by default, `-turn` / `-public-host`) for connections that NAT or firewalls prevent. A relay is an additional media path; the Hub still only manages and signals. Supported setups: LAN (host candidates), internet with port forwarding and the embedded relay, and VPN; no external TURN server.

- ICE: the Hub hands out `ice_servers` (`stun:` URLs from `-ice-servers`, plus its own STUN with TURN on) and `turn_servers` with short-lived credentials (12 h, TURN REST scheme) in `hello_ack` and the join response. Players prefer the join response and fall back to `hello_ack`.
- Diagnostics show the connection type per PeerConnection (direct host/srflx/prflx or relay udp/tcp).
- Bitrate adaptation: the viewer reports loss and received kbit/s over `fb-diag`; the host adapts the single encoder to the worst viewer (AIMD, 300-4000 kbit/s, start 2000).

Session sharing initially means the transmission of video and audio. Remote control, synchronized multiplayer emulation or NDS link/WLAN emulation are thus not promised.

### Session visibility and invitations

| Visibility | Access rule | FrameBeam 0.1 / PoC |
|---|---|---|
| Private | Only the Session owner | Functional |
| Hub users | All authenticated users of the same active Hub may join | Functional |
| Invite only | Explicit `allowed_user_ids` / Session ACL of the same Hub | Provide data model and flow in the PoC; full implementation not required |

The Hub checks visibility and permissions on join and on changes. Viewers initially receive only `view_video=true`, `hear_audio=true`, `send_input=false`. Only the Session owner may change invitations or the ACL and remove viewers; a viewer must not re-share the Session. Changes must revoke ongoing access accordingly, even if the media path is already established.

The planned invite-only flow is: owner selects already known users of the active Hub → Hub maintains `allowed_user_ids` → invited Player receives **Join / Decline** → on join, the Hub checks the existing authentication and Session ACL. The invitation does not authenticate a new user. An invite applies only to this specific Session and expires when it ends; offline users can receive it as long as the Session is still running. A Session invite is separate from a user onboarding invite (section 14).

No friends list, public share links, guest access, guest codes, cross-Hub invites or cross-Hub Sessions in the PoC.

## 5. Multiview

The client combines the local Session with received remote Sessions in multiple video surfaces: up to 4 (local game plus up to 3 remote Sessions, or 4 remote Sessions without a local game; each remote surface is its own viewer join). Layouts: **side-by-side** (2), **grid 2 x 2** (3 or 4), **picture-in-picture (PiP)** (one main surface, others as small tiles). Details: ADR 0012 D8.

Layout, scaling, decoding and rendering take place entirely on the client. The server produces no composited image. Audio focus: exactly one surface is audible ("Audio here" moves it); default is the local game, and when the focused surface is removed the focus goes to the local game, else the first remaining surface. No mixing. The exact DS screen arrangement is still to be defined.
