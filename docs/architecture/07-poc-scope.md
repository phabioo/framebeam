# Architecture: PoC scope

## 7. Clearly delimited PoC scope

| Area | FrameBeam 0.1 / PoC |
|---|---|
| Client | Windows x86-64 |
| Server | Linux x86-64 and ARM64; Raspberry Pi 5 as the intended server target |
| System/core | Exclusively Nintendo DS with melonDS DS via Libretro; core included in the Windows Player |
| Registry | Hub knows system, core ID and expected version; no automatic core distribution |
| UI | Dark/light mode for Player and Hub; optional diagnostics |
| Settings | Player pages Emulation and Controllers; only the options actually needed for melonDS in the PoC must work completely; generic core options and hierarchy prepared |
| Library | Central ROM library with technical library data and a simple game title; no external metadata providers or box art sourcing |
| ROM access | Download and local, hash-based cache |
| Saves | Start sync, changed periodic auto checkpoints, final sync; current checkpoint separate from history; local pending sync; `base_version` also for checkpoints; conflict model and UI state |
| Emulation | Locally on the client |
| Input | Controller; DS touch via mouse |
| Sessions | Publish, discover and share; Private and Hub users functional; provide Invite only with ACL and Join/Decline flow in the data model/PoC; viewers without input rights |
| Streaming | Direct WebRTC/P2P with video and audio |
| Multiview | Local plus one remote Session; PiP and side-by-side |
| Hub connections | Multiple stored Hub profiles, add address, identify, select, switch and remove Hubs; exactly one active Hub per connected Player instance |
| User management | Admin/user; initial admin with username/password; admin-controlled short-lived onboarding invites; normal users passwordless; users and devices separate |
| Device approval | Player request → admin Allow/Deny as the default; Trusted/Revoked and Revoke access; short-lived access token plus long-term revocable device/refresh credential in the OS credential store |
| Permissions | Library management admin-only; optional setting Allow users to upload games; central uploads with `uploaded_by`, no management of others' entries |
| Firmware | Admin provisioning, system/core assignment, metadata/hash validation, delivery and local cache; Firmware required/missing |
| Handshake | Separate `protocol_version`, product versions and capability negotiation; clear compatibility errors |
| Transport | HTTPS/WSS; own initial TLS certificate, own cert/key or reverse proxy; Player TOFU/pinning; HTTP/WS only in explicit dev mode or on localhost |
| Connection start | Start/connection screen; optional auto-connect to the last used Hub |

> Superseded since the PoC: automatic core distribution ([ADR 0010](../adr/0010-cores-from-the-hub.md)), installers and the integrated updater ([ADR 0011](../adr/0011-automatic-updates.md)) and the TURN fallback ([ADR 0012](../adr/0012-internet-sessions-and-save-comfort.md)) are delivered; see [roadmap](../roadmap.md).

**Outside the PoC:** Linux and macOS clients, Windows/macOS server builds, further emulators, hosted emulation, installers for all platforms and an integrated updater, automatic core distribution, complete game-override UI and TURN fallback. Also outside: metadata service with external providers, automatic or manual provider matching, external base metadata and artwork/metadata cache; simultaneous multi-Hub use, Hub federation, cross-Hub Sessions, OAuth and central accounts. A web client is not planned. Remote control, emulator netplay, save states, friends list, public Session links, guest access, email/password recovery and a custom ACME client are not part of the agreed PoC.

In the long term, Windows/Linux/macOS remains the platform goal. The PoC should already create the reusable basis of Go server, C++ client, emulator abstraction, ROM/save protocol and WebRTC Session model.

### Proof of the PoC

Two Windows Players on a Linux Hub start an NDS ROM locally from the central library; a repeated start uses the ROM cache. A changed save is periodically secured as the current checkpoint, reconciled immediately on pause/stop/clean exit and made available on the other device. Auto checkpoints do not create history every time; relevant events do. If the Hub is down, pending sync stays locally assigned to the original Hub. An upload with a stale `base_version` shows the conflict state instead of overwriting.

A Session with video and audio appears via direct P2P in PiP and side-by-side; the Hub does not emulate or render. Private prevents a foreign join, Hub users lets authenticated users of the same Hub join; viewers send no input. Invite-only ACL, owner rights, Join/Decline and expiry at Session end are provided in the model/flow.

The first Hub start sets up an admin with username/password. An admin invite creates a Hub-local user without a permanent password and can authorize its first device. A further device, after entering the Hub address, creates a pending request with device name, platform and Player version; the admin demonstrates Allow and Deny. After a restart, login happens via the device/refresh credential and a new short-lived access token. A single Revoke removes this device's access. Library management is locked for users; with the upload option enabled, their own uploads land with `uploaded_by` in the same library, without management of others' entries.

Handshake and compatibility errors are checked. The firmware path demonstrates admin provisioning, validation, Player cache and Firmware required/missing for a required but missing artifact. HTTPS/WSS and the stored certificate fingerprint work; a later mismatch produces a warning/error.

Two Hub profiles can be stored and identified by address. On switching, exactly the selected Hub stays active; library, saves and Sessions are assigned to this Hub. Removing a profile removes its local credentials. Metadata providers and box art remain completely outside this proof.
