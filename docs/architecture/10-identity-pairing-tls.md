# Architecture: identity, pairing, TLS

## 14. Hub connections, identity and pairing [PoC]

### Roles, user creation and identity

FrameBeam has no global account infrastructure, no public registration and no global user ID. The Hub manages **Hub-local user IDs**; the same person can have different identities on different Hubs.

At least **admin** and **user** are provided. The admin manages library, users, clients, systems/cores including firmware, Hub settings and pairings. Normal users use library, their saves and Sessions according to Session permissions. Library management is admin-only by default. The optional Hub setting **"Allow users to upload games"** permits normal users their own ROM uploads into the same central library. Entries receive `uploaded_by` as a Hub-local user ID. This permission grants no general library management and in particular does not allow deleting or managing existing entries of others.

On the first Uzume server start, an admin account with **username + password** is set up (from the app, with a setup code on a headless server; [ADR 0030](../adr/0030-one-app-one-server-uzume.md) D4). The server stores a secure password hash (Argon2id). Further users are created exclusively under admin control, preferably via short-lived invite codes/invite links. The user redeems the invite in the app, chooses a display name and optionally a password; the server creates the local user and can directly authorize the first device in the process. User onboarding links are not Session share links. Invite lifetimes and the redemption endpoint are in the protocol reference.

**Every user can have a password; admins must** (built in 0.18 package 4, handshake feature `accounts_v1`, ADR 0030 D5). A password has at least 12 characters. Admins cannot remove theirs; the admin setting `require_passwords` blocks removing a password for members and forces nothing on existing users. A user without a password still gets onto a new device by approval. User and device are separate entities; a user can own multiple devices. The app generates the device ID and device name locally. This identity as well as the local profile/display name serve UX and do not constitute authentication. The admin assigns a pairing request to an existing local user; a freely entered user ID is not blindly accepted.

- **Sign-in on a new device**: `POST /api/v1/auth/sign-in` with user name and password trusts the device directly and replaces waiting for the admin. Every failure is one generic `invalid_credentials` (unknown names too). Limits: 10 attempts a minute per IP; 15 min lock of a user name after 10 failures.
- **Several users on one PC**: the app sends an `install_id`. Each user on that PC is its own device row with its own device ID and credentials; the rows share the `install_id`. Switching needs no new pairing. The optional PIN for switching is app-side only and never reaches the server.
- **Confirm it's you**: before dangerous steps (backup restore and delete, update rollback, password reset, accounts settings; the admin API adds more) the app asks for the password again (`POST /api/v1/account/confirm`, valid 120, 300 or 900 s per user and device). Without it the server answers 403 `confirmation_required`. 3 wrong passwords lock confirming for 5 min on that device.
- **Own devices**: each user lists their devices, signs one out or all out (`/api/v1/account/devices`); admins see all devices under Clients.
- **Forgot password**: an admin resets it (`password-reset`: temporary password, the user must change it); `reset-admin` on the server for admins. There is no email recovery.

### Hub profiles and device credentials

Multiple Hub profiles can be stored locally. A profile contains Hub ID, display name, address, Hub user ID, local device ID, credential reference, pinned certificate fingerprint and time of the last connection.

Each authorized device has its own **Hub-specific credentials**: a short-lived access token and a long-term, revocable device/refresh credential, bound to Hub, user and device. The long-term secret lives exclusively in the **OS credential store** (Windows Credential Manager in the PoC, Linux Secret Service with the Linux Player after 1.0; macOS is not planned), never in profile files or logs. The Hub stores a secure verification representation instead of plaintext. Exact lifetimes and renewal rules remain open. A lost device can be revoked individually; revocation must also withdraw further access with its access tokens.

**Exactly one Hub is active per connected Player instance.** Before a connection or after disconnecting, no Hub can be active. Library, saves, Sessions and core registry come exclusively from the selected Hub; profiles create neither parallel connections nor a merged library. Local Hub data and pending sync stay separated by Hub ID and, where applicable, Hub user ID, even with identical ROM hashes.

### Connection and approval pairing [PoC]

1. On the connection screen, the user enters the Hub address or selects a profile.
2. The Player identifies the Hub via a FrameBeam info endpoint, for example `GET /.well-known/uzume`, with Hub ID, name, Hub version and protocol information; the concrete path remains open. This is address-based identification, not a necessary automatic network discovery. TLS trust is checked according to the following section; the capability handshake happens on connect.
3. Without valid device credentials, the Player issues a **pairing request**. In the Hub web interface a **pending request** appears with device name, platform and Player version.
4. The admin assigns the user and clicks **Allow / Deny**. A user with a password can skip this by signing in (see above); a pairing request and an invite carry the optional `install_id`. Only Allow registers and authorizes the device. Requests/attempts are rate-limited; the info endpoint and a pending request grant no rights. A valid admin onboarding invite can directly authorize the first device.
5. After successful approval, the device receives Hub ID, user assignment and its own access/device credentials. Further connections use short-lived access tokens; the device/refresh credential enables their renewal.
6. **Clients** shows **Trusted / Revoked** and **Revoke access**. On revocation or invalid credentials, the Player returns to the required approval.

A short-lived pairing code may be provided as an alternative; **Player request → admin Allow/Deny is the preferred default and the mandatory PoC flow**. A code-only flow does not replace it.

Optionally the Player connects to the last used Hub at startup. If it is unreachable, the connection selection appears with an understandable error and a retry option.

### Transport security and certificate trust [PoC]

In production operation, **HTTPS/WSS is mandatory** for API, signaling, authentication and file transfer. HTTP/WS is allowed exclusively explicitly in dev mode or on localhost.

For simple self-hosted/LAN use, the Hub can generate its own TLS certificate on first start. Your own cert/key and operation behind a reverse proxy are also possible. On first pairing, the Player uses **trust-on-first-use / certificate pinning**: show/confirm the certificate fingerprint and store it in the Hub profile. Later mismatches produce a warning/error and are not silently accepted as new trust. Concrete certificate management including renewal and confirmed pin change remains to be specified. A custom ACME client is outside the PoC. WebRTC is separately encrypted independently of this.

### Switching and removing

A Hub switch first ends the old connection including presence and signaling. Running local/shared Sessions are ended before the switch; pending saves are uploaded to the previous Hub or unambiguously secured for this Hub for later retry. If reconciliation fails, the UI must allow a deliberate decision. An upload must never be redirected to the newly selected Hub. Only then is the new Hub connected.

When removing a profile, the Player deletes the local profile assignment and its credentials; open saves are resolved beforehand. Hub account, ROMs and central saves are not deleted by this. Device credentials can be revoked in the Hub under **Clients**; local removal alone does not replace a server-side revocation.

The PoC covers this Hub profile/pairing base model. Simultaneous multi-Hub use, federation, cross-Hub Sessions, OAuth and central FrameBeam accounts are excluded. Shared Sessions in the PoC connect users of the same active Hub.
