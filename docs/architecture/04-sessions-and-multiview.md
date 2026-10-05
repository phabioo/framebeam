# Architecture: Sessions and multiview

## 4. Session sharing and WebRTC/P2P

When playing alone, video and audio go directly to local output; the streaming encoder stays off. Only when sharing a Session is the media path additionally activated:

```text
Emulator ──→ local video/audio output
        └──→ encoder → WebRTC → remote client → decoder → output
```

The server manages Session metadata, visibility and presence and mediates connection setup via signaling. It does not transport or process any video or audio itself.

Direct P2P connections form the basis. TURN can be added later as a separate relay service if NAT or firewalls prevent direct connections. A TURN relay would be an additional media path; the FrameBeam application server remains responsible for management and signaling. The PoC demonstration therefore needs an environment in which direct connections work. The concrete ICE/STUN configuration is still to be defined.

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

The client combines the local Session with a received remote Session in multiple video surfaces. The PoC provides a second Session, **picture-in-picture (PiP)** and **side-by-side**.

Layout, scaling, decoding and rendering take place entirely on the client. The server produces no composited image. Audio focus or mixing and the exact DS screen arrangement are still to be defined.
