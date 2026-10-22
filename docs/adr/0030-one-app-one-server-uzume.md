# ADR 0030: One app, one server, new name Uzume (0.18)

- Status: Accepted. Name, no web admin, server commands instead of an emergency web, accounts for all users, runs without a plugin and the milestone order were decided by Fabio on 2026-10-22. Fabio decided on 2026-10-22 (later the same day) that browser watching goes too and the code keeps no browser components. The timing of the web admin removal (D2), the plugin IDs (D7) and the waves without their own design round (D8) follow the recommendations of the 0.18 plan, taken on 2026-10-22; Fabio may still change them. On the same day he decided that the rename has no backward compatibility (amendment of D1).
- Date: 2026-10-22
- Deciders: Fabio

## Context

Up to 0.17 the product had three faces: the FrameBeam Player (Qt app), the FrameBeam Hub (Go server) and the Hub web interface for administration. Almost every planned feature of the waves 0.18 to 0.22 would have been built twice, once in the Player and once in the Hub web (favourites, notifications, storage, reports, run recap, health). Administration needed a browser, a web login and the certificate dialog of a self-signed Hub, although every user already has the app with a pinned certificate and a device token.

On 2026-10-22 Fabio decided: one app that also administers the server, no web admin and no admin from a phone, emergency and first setup through commands on the server, accounts with passwords for every user, and a new name. The name is **Uzume**; the GitHub repositories were renamed the same day (`phabioo/framebeam` is now `phabioo/uzume`, the plugin repositories are `phabioo/uzume-plugin-*`; GitHub redirects the old names). The design for the whole milestone and the later waves was redrawn in the new design system "Uzume" (Round 5 restyle, [design](../design/uzume/README.md)).

This ADR supersedes parts of [ADR 0015](0015-hub-ui-pass.md) (Hub web UI), [ADR 0021](0021-one-windows-installer.md) D6 and D7 (local setup, standalone mode), [ADR 0023](0023-plugins.md) (sidebar slot, plugin IDs) and of the architecture files [09-ui-and-navigation.md](../architecture/09-ui-and-navigation.md) (product terms) and [10-identity-pairing-tls.md](../architecture/10-identity-pairing-tls.md) (admin web login, users without passwords, approval as the only way onto a new device).

## Decisions

### D1 Name and terms

- The product is **Uzume**. The app (formerly FrameBeam Player) is called "Uzume", the server (formerly FrameBeam Hub) "Uzume server", in running text "the server". The terms Hub and Player leave the UI and the docs; the code directories `client/` and `server/` keep their names.
- "Session" stays the term for a shared running game.
- Package 2 of 0.18 renames every identifier, with no compatibility to FrameBeam (see the amendment below): Go module path `github.com/phabioo/uzume`, programs `Uzume` and `uzume-server`, Windows services `UzumeServer` and `UzumeServerUpdater`, install and data folders, environment variables `UZUME_*` (`FRAMEBEAM_*` are not read), links `uzume://`, Docker image `ghcr.io/<owner>/uzume-server` only, invite codes `UZ-` only, wire names, database file `uzume.db`, signed-data contexts, update and plugin index URLs on the new repository.
- What stays: the product keys `player` and `hub` in the signed update index (they are not FrameBeam strings), the plugin API namespace `fb.*`, the release signing key, the `.fbplugin` package format.
- Never create a repository with an old name again: that would break GitHub's redirect for old links.

#### Amendment (package 2, 2026-10-22): no compatibility

Fabio decided on 2026-10-22 that the rename has no backward compatibility: he sets up his server fresh and the few app installs reinstall once. Package 2 therefore renames everything, in two rounds. Round 1 kept some names for installed systems; round 2 removed them. Nothing of FrameBeam is read, migrated or kept alive: not the `FRAMEBEAM_*` variables, `FB-` invite codes, the `framebeam-hub` image, old backups, old data folders, the `FrameBeam/` credential targets, the wire names, the signature contexts or the installed-system names.

New names, as built:

- **Wire protocol**: headers `X-Uzume-Base-Revision`, `X-Uzume-Content-SHA256`, `X-Uzume-Sync-Reason`, `X-Uzume-Save-Revision`, `X-Uzume-Expected-Revision`; info path `/.well-known/uzume`; core origin `legacy-mirror`; TURN realm `uzume`. OpenAPI 1.27.0.
- **Signed data**: contexts `uzume-updates-index-v1`, `uzume-plugin-index-v1`, `uzume-cores-index-v1`, plugin signature prefix `uzume-plugin-v1`. The release key stays.
- **Persisted names**: database `uzume.db` (with `-wal`, `-shm`, `.restoring`, `.rollback`), app option keys `uzume.*`, backups `uzume-server-<name>-<UTC timestamp>[-safety].tar` with the members `database/uzume.db` and `config/server.env`, dev cache `$HOME/.cache/uzume/`.
- **Linux**: package `uzume-server` (`uzume-server_<version>_<arch>.deb`), units `uzume-server.service` and `uzume-server-update.{path,service}`, user `uzume`, `/etc/uzume/server.env`, `/var/lib/uzume`, `/run/uzume`, script `packaging/linux/install-server.sh`. No `framebeam-hub` symlink.
- **Windows**: services `UzumeServer` and `UzumeServerUpdater` (`NT SERVICE\UzumeServer`), `%ProgramFiles%\Uzume\App` and `\Server`, `%LOCALAPPDATA%\Programs\Uzume`, registry `Software\Uzume\App` and `\Server`, `%ProgramData%\Uzume\Server` and `\ServerUpdater`, markers `uzume-app.msi-installed` and `uzume-server.msi-installed`, AppUserModelId `Uzume.App`. The MSI UpgradeCode stays (a GUID), so the MSI replaces the installed FrameBeam product.
- **Docker**: only `ghcr.io/<owner>/uzume-server`.
- **Links**: `uzume://` only; `framebeam://` never existed.
- **Environment**: only `UZUME_*` (`HUB` became `SERVER`, `PLAYER` became `APP` inside the name). Invite codes `UZ-XXXX-XXXX` only. Metrics `uzume_*`.

The only name that keeps FrameBeam is the GitHub secret `FRAMEBEAM_SIGNING_KEY` (workflows pass it to the tool as `UZUME_SIGNING_KEY`): a secret can only be renamed by re-entering the private key.

Consequences:

- Old apps and servers cannot verify the new updates index (other signature context) and do not find updates. They are reinstalled once from the release page.
- The MSI replaces the old product through the same UpgradeCode, but it does not take over the old app data folder. The app is paired again.
- The plugin index and the plugins must be signed again with "Publish plugin" so that they verify again; old signatures do not.
- Docker users switch to the `uzume-server` image and `UZUME_*` variables, with a new volume.
- Old backups (`framebeam-hub-*.tar`) cannot be restored.

### D2 One app, no web admin

- Every administration task moves into the app: AppNav groups PLAY, SERVER (admins only) and THIS PC; Settings have a This PC / Server switch. Admin actions on the library and on saves appear inline where a member sees the same list.
- The Hub web admin is removed at the end of the milestone (package 12), after the app covers everything it does. Until then it keeps working, so every beta stays usable. No new web pages for administration are built.
- No web pages remain and the code keeps no browser components (Fabio, 2026-10-22): no admin web, no watching a Session in the browser (dropped from 0.23), and no web stream view for OBS (design 5g is not built as a web page; OBS captures the app window; whether the app gets its own overlay window for that stays open). The server only serves the API, WebSocket signaling and file transfer. Invites have no landing page; they are a code or an `uzume://` link the app opens.
- Admin from a phone or a browser is not supported. A server on a Raspberry Pi or in Docker is administered from a PC with the app; a Linux-only user waits for the Linux app (0.23) and uses the server commands (D4) until then.

### D3 Admin API

- Everything the web admin can do gets a REST endpoint under `/api/v1/admin/...` in the OpenAPI contract: users and invites, devices, systems and cores, firmware, plugins, reports, library, saves of all users, server settings, status. The handshake announces it with a feature flag.
- The web handlers and the API call the same functions in `server/internal/hub`; nothing in the domain layer is web-specific.

### D4 Server commands for setup and emergencies

- `uzume-server` gets the commands `setup-code`, `code`, `reset-admin`, `status`, `update`, `backup`, `restore` and `reset` on Windows, Linux and in Docker (`backup` and `restore` exist since [ADR 0029](0029-operations-and-safety.md) D2).
- A server without a screen is set up from the app with a **setup code**: the server prints it at first start (terminal, journal, `docker logs`, installer last page) and on `setup-code`. The code is single use, short-lived and rate-limited; it creates the first admin from the app.
- There is no emergency web page. A forgotten admin password is reset with `reset-admin` on the server.

### D5 Accounts for every user

- Every user can have a password; admins must have one. Signing in on a new device with user name and password replaces waiting for the admin's approval; approval stays the way for users without a password.
- A shared PC can hold several signed-in users; switching needs no new pairing and can be protected with a PIN.
- Dangerous steps (delete, remove a user, server update, restore) ask "Confirm it's you" again, valid for a short time.
- Each user sees their own devices and can sign them out; admins see all devices.
- Forgot password: an admin resets it in the app, or `reset-admin` for admins. No email recovery (still out of scope).
- Devices keep their own revocable credentials in the OS credential store and the pinned certificate (architecture 10, [ADR 0009](0009-finish-poc.md)); the password only replaces the approval step.

#### Amendment (package 4, 2026-10-22): as built

- **Sign-in**: `POST /api/v1/auth/sign-in` (user name + password) trusts a new device directly (sign-in kind `password`) and answers like an approved pairing request. Every failure is the same 401 `invalid_credentials`, also for unknown names and users without a password. 10 attempts a minute per IP; 15 min lock of a user name after 10 failures (unknown names count too). Approval stays for users without a password.
- **Devices and PCs**: apps send an optional `install_id`; several users on one PC are one device row each with their own device id and the shared `install_id`. Invite redeem and pairing take `install_id`, redeem also an optional password. The PIN for switching is app-side only and never sent (package 7).
- **Passwords**: at least 12 characters; admins cannot remove theirs. The policy `require_passwords` only blocks removing a password for members; it forces nothing on existing users. An admin reset (`POST /api/v1/admin/users/{id}/password-reset`) sets a temporary password and the user must change it. Demoting an admin keeps the password. No email recovery.
- **Confirm it's you**: `POST /api/v1/account/confirm` is valid 120, 300 or 900 s per user and device; 3 wrong passwords lock confirming for 5 min on that device. Dangerous admin steps answer 403 `confirmation_required` without it (hub helper `RequireConfirmed`, `s.requireConfirmed(ctx)` in `httpapi`): backup restore and delete, update install and rollback, password reset, accounts settings, delete a game, turn off or remove a user, delete or restore another person's saves, and the network and external TURN settings (who can join and sharing). OpenAPI 1.28.0 is the admin API (package 5), 1.29.0 the accounts (package 4).
- **Devices**: `GET/DELETE /api/v1/account/devices[/{id}]` and `sign-out-all` for the user's own devices; handshake feature `accounts_v1`; new codes `confirmation_required`, `password_required`, `password_not_set`, optional `retry_after`.
- The web admin and its cookie login stay until package 12.

### D6 Install modes and "On this PC"

- One installer per platform with three options: the app only, the app with a server on this PC, the server only (Windows MSI as in ADR 0021; Linux packages `uzume` and `uzume-server`; Docker for the server).
- **Only on this PC** replaces the standalone mode of ADR 0021 D7: the app starts the server as a child process on loopback, per user, with data in the user's folder, without a service and without UAC.
- **Share with others** turns it into the system service once (one UAC prompt, data moved, never overwritten); **Stop sharing** returns to "On this PC" and keeps accounts and saves.
- The MSI keeps the UpgradeCode, so it replaces an installed FrameBeam product. It does not take over data: a FrameBeam install is not migrated (amendment of D1).

### D7 Runs without a plugin

- Runs are always visible under PLAY, also without a run plugin. A run has its own save slot "Run: <Name>"; the main slot stays untouched.
- A run plugin is chosen under "Rules" when creating the run; its screens become run tabs. Plugins get no sidebar entries any more; the plugin API's sidebar slot becomes a run-tab slot.
- Plugin IDs: `uzume.soullink-core` and `uzume.soullink` (later `uzume.pokemon-editor`, `uzume.cheats`). There are no open runs, so the IDs change without a migration; the plugin index is republished without the old IDs. SoulLink and Nuzlocke become one plugin "SoulLink & Nuzlocke" with a mode.

### D8 Design

- The Uzume design ([docs/design/uzume/](../design/uzume/README.md), design system "Uzume" with the themes Sumi and Kinari, IBM Plex) is the valid design from 0.18 on. Rounds 1 to 4 stay in `docs/design/` as a legacy reference for behaviour the new boards do not cover.
- The boards already draw the screens of the later waves (save states, rewind, chat, voice, mixer, notifications, storage, developer console, controllers). The waves 0.19 to 0.22 build from them without their own design round; a missing screen is a small addition in the Uzume canvas.

### D9 Milestone

- One milestone 0.18 "Uzume" with twelve packages in the order of the [roadmap](../roadmap.md#018-uzume-one-app-one-server-new-name). Every merge is a beta and must stay usable.
- The waves move by one: 0.19 Player comfort and library, 0.20 Emulation, 0.21 Together, 0.22 Plugins, 0.23 Platforms. 1.0 is unchanged.

## Rejected

- **Keep "Hub" as the server name.** It is short and in every ADR, but a second product word next to the app name; "server" says what it is.
- **A thin emergency web** (first setup and admin login in the browser): more attack surface on an internet-facing server for a rare case that a server command covers.
- **Keep the OBS stream view or browser watching as the only web pages.** Both would keep a web stack (templates, static assets, browser WebRTC) in the server for one feature each; Fabio wants no browser components in the code.
- **Remove the web admin only with the Linux app (0.23).** The web would be maintained for five more milestones for Linux-only admins; the server commands cover emergencies until then.
- **Two milestones** (0.18 name, server and accounts; 0.19 admin in the app and installer): the packages depend on each other closely, and the "every beta stays usable" rule keeps one milestone manageable.
- **Rename the update index product keys `player` and `hub`.** They are not FrameBeam strings; the index format keeps them.
- **Plugin IDs `io.github.phabioo.*`.** Provably ours but long; `uzume.*` fits the reverse-DNS rule of the plugin API (at least two parts).

## Consequences

- The Hub web code (handlers, templates, JavaScript, static assets, tests) is deleted in package 12; nothing browser-facing stays in the server.
- The admin API grows the OpenAPI contract considerably; the app's admin area is the largest UI work of the milestone.
- Accounts change the identity model of architecture 10 (passwords for all, sign-in on new devices, user switching). The migration keeps existing users and devices working without a password.
- There is no upgrade path from FrameBeam: the server is set up fresh, the app is installed again and paired again, Docker users switch to the `uzume-server` image (amendment of D1).
- Docs use the new terms from this PR on; identifiers in docs follow the code and change with package 2. ADRs before 0030 keep the old terms as historical records.
