# ADR 0030: One app, one server, new name Uzume (0.18)

- Status: Accepted. Name, no web admin, server commands instead of an emergency web, accounts for all users, runs without a plugin and the milestone order were decided by Fabio on 2026-10-22. Fabio decided on 2026-10-22 (later the same day) that browser watching goes too and the code keeps no browser components. The timing of the web admin removal (D2), the plugin IDs (D7) and the waves without their own design round (D8) follow the recommendations of the 0.18 plan, taken on 2026-10-22; Fabio may still change them.
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
- Package 2 of 0.18 renames the identifiers (narrowed by the amendment below): Go module path `github.com/phabioo/uzume`, programs `Uzume` and `uzume-server`, Windows service `UzumeServer`, data folders with a copying migration (the old folder stays in place, as in [ADR 0004](0004-player-portable-data.md)), environment variables `UZUME_*` (`FRAMEBEAM_*` are still read, with a log hint), links `uzume://` (`framebeam://` stays registered), Docker image `ghcr.io/<owner>/uzume-server` (`framebeam-hub` is still published for a few versions, then frozen with a notice), invite codes `UZ-` (`FB-` stays valid), update and plugin index URLs on the new repository.
- What stays: the product keys `player` and `hub` in the signed update index (old installations must keep finding updates), the plugin API namespace `fb.*`, the release signing key, the `.fbplugin` package format.
- Never create a repository with an old name again: that would break GitHub's redirect, which old installations use to reach the update index.

#### Amendment (package 2, 2026-10-22)

Package 2 renamed everything that is code, build or user-visible text. It deliberately did not touch what an installed system or an older app or server of the other side depends on. The bullet on package 2 above is narrowed accordingly. What stays, and why:

- **Wire protocol** between app and server: the headers `X-FrameBeam-*`, the info path `/.well-known/framebeam`, message names, JSON fields and error codes. Old apps must talk to new servers and the reverse during the upgrade.
- **Signed data**: the signature contexts `framebeam-updates-index-v2`, `framebeam-plugin-index-v2`, `framebeam-cores-index-v2`, the plugin signature prefix `framebeam-plugin-v1`, the product keys `player` and `hub`, the release key and the GitHub secret `FRAMEBEAM_SIGNING_KEY` (workflows pass it to the tool as `UZUME_SIGNING_KEY`). Changing a context would make every installed app and server reject the index.
- **Persisted names**: the database file `framebeam.db` (with `-wal`, `-shm`, `.restoring`) and the app's option keys `framebeam.*`. They are stored on users' machines; a rename would need its own data migration without any gain.
- **Installed-system identity moves in package 11**: Windows service names `FrameBeamHub` and `FrameBeamHubUpdater` (and the `NT SERVICE\FrameBeamHub` ACLs), the MSI UpgradeCode, install folders, registry keys, `%ProgramData%\FrameBeam\...`, the MSI marker files, the AppUserModelId; on Linux the `.deb` package `framebeam-hub` with its file name, the units `framebeam-hub.service` and `framebeam-hub-update.{path,service}`, the user `framebeam`, `/etc/framebeam`, `/var/lib/framebeam`, `/run/framebeam` and the server's default data directories. Reason: the old `.deb` prerm stops the update unit, and that unit is the process that runs `dpkg` during a self-update. A package switch inside the self-update would therefore abort half way. Windows has the same problem with the updater service. Package 11 gets its own tested transition (Replaces/Conflicts, service swap, data taken over) instead of a rename that only works on a fresh install. Display text (unit descriptions, MSI names, shortcuts, firewall rule names) already says Uzume.
- **Compatibility copies**: the launcher is also installed as `framebeam_player.exe` (pinned shortcuts, relaunch after an update by an older app); the `.deb` and `install-hub.sh` add the symlink `/usr/bin/framebeam-hub` (`/usr/local/bin/framebeam-hub`) to `uzume-server`; the image is also pushed as `ghcr.io/<owner>/framebeam-hub` with the same tags until a later release freezes it.
- **No `framebeam://` scheme**: it never existed in the code, so nothing needs to stay registered. Package 7 adds `uzume://` directly.
- **Data folder migration** with copying (old folder stays) exists for the app's AppData only (Qt organization and application name changed), plus the credential target `Uzume/<server id>/<device id>` with a fallback to `FrameBeam/...`. The server's data directories do not move, see above.
- **Environment**: `UZUME_*` names (`HUB` becomes `SERVER`, `PLAYER` becomes `APP` inside the name) are read first; the exact old `FRAMEBEAM_*` name is the fallback and logs one deprecation line per name. Build and test variables (`UZUME_SKIP_CORES`, `UZUME_REQUIRE_TESTCORE`, ...) have no fallback. Invite codes: new ones are `UZ-XXXX-XXXX`, `FB-` stays valid. Metrics are `uzume_*`; backups are named `uzume-server-<name>-<UTC timestamp>.tar` and the old `framebeam-hub-` names are still read. The development cache path `$HOME/.cache/framebeam/` and the CI cache keys keep their names so the Windows vcpkg cache stays warm.

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

### D6 Install modes and "On this PC"

- One installer per platform with three options: the app only, the app with a server on this PC, the server only (Windows MSI as in ADR 0021; Linux packages `uzume` and `uzume-server` with Replaces/Conflicts on the old packages; Docker for the server).
- **Only on this PC** replaces the standalone mode of ADR 0021 D7: the app starts the server as a child process on loopback, per user, with data in the user's folder, without a service and without UAC.
- **Share with others** turns it into the system service once (one UAC prompt, data moved, never overwritten); **Stop sharing** returns to "On this PC" and keeps accounts and saves.
- The MSI upgrade from FrameBeam stops and removes the `FrameBeamHub` service, installs `UzumeServer` and takes the data over; the UpgradeCode stays.

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
- **Rename the update index product keys.** Old installations would stop finding updates.
- **Plugin IDs `io.github.phabioo.*`.** Provably ours but long; `uzume.*` fits the reverse-DNS rule of the plugin API (at least two parts).

## Consequences

- The Hub web code (handlers, templates, JavaScript, static assets, tests) is deleted in package 12; nothing browser-facing stays in the server.
- The admin API grows the OpenAPI contract considerably; the app's admin area is the largest UI work of the milestone.
- Accounts change the identity model of architecture 10 (passwords for all, sign-in on new devices, user switching). The migration keeps existing users and devices working without a password.
- Docker users change the image name once (`framebeam-hub` to `uzume-server`); everyone else updates without manual steps through the redirect of the old repository.
- Docs use the new terms from this PR on; identifiers in docs follow the code and change with package 2. ADRs before 0030 keep the old terms as historical records.
