# Architecture: UI and navigation

## 9. Product terms, navigation and presentation

In the user interface, a running or shared game session is consistently called a **Session**: "Share Session", "Watch Session" and "Add to multiview". **Stream** denotes only the technical transmission path in diagnostics. Technical terms in implementation and protocol remain permitted.

Player and Hub each support a selectable **dark/light presentation** with a common design language. The choice is available in the respective settings.

| FrameBeam Hub | FrameBeam Player |
|---|---|
| Library | Library |
| Saves including history and conflict state | Emulation |
| Systems & Cores | Controllers |
| Clients (pending requests, Trusted/Revoked, Revoke access) | Settings |
| Users (admin-controlled users and onboarding invites) | — |
| Settings | During play: running Session, multiview, optional diagnostics |

Before the main navigation, the Player gets a **start/connection screen** with stored Hubs, connection status, "Add Hub" and "Connect". Without a successful connection, this screen remains reachable; optional auto-connect to the last used Hub leads directly to the library on success. A compact Hub switcher as well as **Settings → Hubs** offer switching, removal and the auto-connect setting. No additional main navigation page is created. On switching, running Sessions are ended first and pending save uploads are secured or resolved (see section 14).

The existing Hub pages remain; user management supplements them. The admin manages library, users, clients/pairings, systems/cores/firmware and Hub settings. Normal users use library, their saves and Sessions; the visible actions follow their permissions. Pending requests appear on Clients with device name, platform, Player version and Allow/Deny. "Allow users to upload games" lives in Hub settings. Firmware required/missing, pairing, certificate and compatibility errors are actionable states and remain directly visible. Later provider configuration lives under **Settings → Metadata**; metadata actions live directly in the library entry (see section 15).

The Hub gets **no Sessions management page**, no live Sessions dashboard and no video preview. Its Session metadata remains internal for presence, visibility/permissions and signaling. Session discovery, sharing, watching and multiview belong in the Player. The existing Session protocol is retained for this.

FPS, latency, encoder, codec, bitrate, WebRTC/streaming statistics and technical debug data appear exclusively in an optionally shown or collapsible diagnostics area. They do not form the primary UX. Actionable states such as download needed and save conflicts, by contrast, remain immediately visible.
