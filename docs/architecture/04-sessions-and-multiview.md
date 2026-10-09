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

### GPU-direct branch of the media path (ADR 0019)

Since 0.7.x ([ADR 0019](../adr/0019-gpu-direct-nvenc.md)), for a hardware-rendered game on NVIDIA the encoder can take the frame without a CPU readback. The readback path above stays the default and the fallback:

```text
Emulator (GL texture) ──→ readback (CPU) ──→ h264 encoder (any)            default, fallback
        └──→ encode texture ──CUDA-GL──→ CUDA frame ──→ h264_nvenc         GPU-direct
```

- Scope: NVIDIA only (CUDA-GL interop into FFmpeg CUDA frames). The player-side modules are the interface in `framebeam_emulation`, the CUDA producer in `framebeam_media` and the adapter in `framebeam_ui`; the Hub, the protocol and the viewer are unaffected.
- Start: the CPU encoder starts as before. GPU-direct takes over only after `h264_nvenc` has encoded one CUDA frame; only then is the CPU encoder closed. Each encoder run therefore opens NVENC twice and sends two keyframes.
- Allowed only for a hardware-rendered game with a running encoder, `h264_nvenc` in the probed encoders and FFmpeg CUDA input support. The kill switch `FRAMEBEAM_DISABLE_GPU_ENCODE=1` and a forced `FRAMEBEAM_H264_ENCODER` keep readback ([player guide](../guides/player.md)).
- Size and timing: the encode texture is the frame fitted into 1280x1920 (the same cap as the readback share size), independent of the window. The copy runs on the emulation thread at the start of the next frame, so the latency equals the display path. The display readback remains, sized to the view.
- Fallback: any GPU-path failure (interop, copy, GPU encoder open or encode, no GPU frame for 60 frames and 1 s) switches the rest of this Session to readback frames. It does not stop the Session, does not set the sticky encoder failure and shows no error. A fatal CUDA error also removes `h264_nvenc` from the CPU encoder order until the Player restarts. A software-rendered game, a missing driver or a non-NVIDIA GPU never creates the GPU path.
- Diagnostics: the host line ends with ` · GPU-direct` or, after a failure, ` · readback (GPU-direct off)`; log lines and the Frame row `GPU copy` are listed in [ADR 0019](../adr/0019-gpu-direct-nvenc.md) D11.
- Open: QSV and AMF (need an OpenGL to D3D11 bridge) and direct GPU display ([roadmap](../roadmap.md)). Verified only locally on real hardware.

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
