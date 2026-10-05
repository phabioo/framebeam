# Architecture: identity, pairing, TLS

## 14. Hub connections, identity and pairing [PoC]

### Roles, user creation and identity

FrameBeam has no global account infrastructure, no public registration and no global user ID. The Hub manages **Hub-local user IDs**; the same person can have different identities on different Hubs.

At least **admin** and **user** are provided. The admin manages library, users, clients, systems/cores including firmware, Hub settings and pairings. Normal users use library, their saves and Sessions according to Session permissions. Library management is admin-only by default. The optional Hub setting **"Allow users to upload games"** permits normal users their own ROM uploads into the same central library. Entries receive `uploaded_by` as a Hub-local user ID. This permission grants no general library management and in particular does not allow deleting or managing existing entries of others.

On the first Hub start, an admin account with **username + password** is set up. The admin uses a classic web login; the Hub stores a secure password hash, for example Argon2id. Further users are created exclusively under admin control, preferably via short-lived invite codes/invite links. The user redeems the invite in the Player and chooses a display name; the Hub creates the local user and can directly authorize the first device in the process. User onboarding links are not Session share links. Invite lifetimes and concrete redemption endpoints remain to be specified.

Normal users need **no permanent password**. User and device are separate entities; a user can own multiple devices. The Player generates the device ID and device name locally. This identity as well as the local profile/display name serve UX and do not constitute authentication. The admin assigns a pairing request to an existing Hub-local user; a freely entered user ID is not blindly accepted. No email/password recovery system in the PoC.

### Hub profiles and device credentials

Multiple Hub profiles can be stored locally. A profile contains Hub ID, display name, address, Hub user ID, local device ID, credential reference, pinned certificate fingerprint and time of the last connection.

Each authorized device has its own **Hub-specific credentials**: a short-lived access token and a long-term, revocable device/refresh credential, bound to Hub, user and device. The long-term secret lives exclusively in the **OS credential store** (Windows Credential Manager in the PoC, later macOS Keychain/Linux Secret Service), never in profile files or logs. The Hub stores a secure verification representation instead of plaintext. Exact lifetimes and renewal rules remain open. A lost device can be revoked individually; revocation must also withdraw further access with its access tokens.

**Exactly one Hub is active per connected Player instance.** Before a connection or after disconnecting, no Hub can be active. Library, saves, Sessions and core registry come exclusively from the selected Hub; profiles create neither parallel connections nor a merged library. Local Hub data and pending sync stay separated by Hub ID and, where applicable, Hub user ID, even with identical ROM hashes.

### Connection and approval pairing [PoC]

1. On the connection screen, the user enters the Hub address or selects a profile.
2. The Player identifies the Hub via a FrameBeam info endpoint, for example `GET /.well-known/framebeam`, with Hub ID, name, Hub version and protocol information; the concrete path remains open. This is address-based identification, not a necessary automatic network discovery. TLS trust is checked according to the following section; the capability handshake happens on connect.
3. Without valid device credentials, the Player issues a **pairing request**. In the Hub web interface a **pending request** appears with device name, platform and Player version.
4. The admin assigns the user and clicks **Allow / Deny**. Only Allow registers and authorizes the device. Requests/attempts are rate-limited; the info endpoint and a pending request grant no rights. A valid admin onboarding invite can directly authorize the first device.
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
