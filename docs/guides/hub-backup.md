# Hub backup and restore

One archive holds everything a FrameBeam Hub needs to come back on the same or a new machine. Decisions: [ADR 0029](../adr/0029-operations-and-safety.md) D2. Design of the web page: [design/hub.md](../design/hub.md#6b-backups), decision 26 in [design/decisions.md](../design/decisions.md).

Three ways to use it, all with the same archive:

- **Command line**: `framebeam-hub backup`, `backup list`, `backup config`, `backup verify`, `restore`.
- **Admin API**: `/api/v1/admin/backups` (admin device token, see [protocol/openapi](../../protocol/openapi/framebeam.yaml)).
- **Web**: Settings → Backups (status card, schedule, list, restore confirmation). Kept plain; the other two are the main interfaces.

## What is inside

One plain `.tar` file named `framebeam-hub-<hubname>-<UTC timestamp>[-safety].tar`, for example `framebeam-hub-home-hub-20261014T010000Z.tar`.

| Member | Content |
| --- | --- |
| `database/framebeam.db` | A consistent SQLite snapshot (`VACUUM INTO`), taken while the Hub keeps running: users, devices, library metadata, runs, plugin settings, all Hub settings (including the backup settings) |
| `data/saves/` | Current saves, the version history and snapshots |
| `data/states/` | Save states, if the Hub has that directory |
| `data/plugins/` | Installed plugin files |
| `data/firmware/` | Firmware the admin provided |
| `data/tls/` | The self-generated TLS certificate and key |
| `data/roms/` | The ROM library, **only with "Include ROMs"** |
| `config/` | Files outside the data folder: an own TLS certificate and key (`-tls-cert`, `-tls-key`) and `/etc/framebeam/hub.env` on Linux, when the Hub can read them |
| `manifest.json` | Last member: Hub version, database schema version, creation time, contents and the SHA-256 of every file |

Not inside: cores (the Hub downloads them again from the libretro buildbot, ADR 0020), logs, temporary files, the database copies the Hub makes before a schema migration (`backups/`), the update staging area.

**The archive contains secrets**: the TLS private key, `hub.env` (which may hold TURN secrets), and the password hashes and token hashes in the database. Keep it as private as the data folder, and never put it where others can read it. The Hub never logs these values.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| Backup folder | not set | Where the files go. A folder the Hub can write to, ideally on another disk. Not set: backups are off. It cannot be inside `saves/`, `plugins/`, `firmware/`, `tls/`, `roms/` or `backups/` of the data folder |
| Schedule | off | `off`, `daily` or `weekly` (with a weekday) |
| Time | 03:00 | In the Hub's local time zone. A run that was missed while the Hub was down is not made up |
| Keep last | 7 | Regular backups kept (1 to 365) |
| Include ROMs | off | Adds the whole ROM library to every backup. With 38 GB of ROMs and 7 kept that is about 270 GB |

A backup runs in the background while Players keep playing; saves that arrive during the run go into the next backup. Only one backup or restore runs at a time, and a running backup can be canceled (the partial file is removed).

### Retention and full disks

- The Hub **never deletes a backup because the disk is full**. Before it starts it checks the free space of the backup folder; if there is not enough it fails with `Not enough space on /mnt/backup: needs 1.4 GB, 0.3 GB free.` and keeps everything. The Status page and the sidebar show the problem ("Backup failed", amber) until the next backup succeeds.
- Only "Keep last N" removes the oldest regular backups, and only **after a new backup succeeded**.
- **Safety backups** (made automatically before a restore, name ends in `-safety`) are never removed by the retention. Delete them yourself when you no longer need them.
- A folder the Hub cannot write to shows "Cannot write" (also on the Status page). "Check again" tests it again.

The Status page has a "Disk · backup" card with the free space of the backup folder.

## Command line

All commands take the usual `-data-dir` (and the other flags of [hub-configuration.md](hub-configuration.md)). On a systemd install run them as the service user so that files stay readable for the Hub, for example `sudo -u framebeam framebeam-hub backup`.

```
framebeam-hub backup [--out DIR] [--roms | --no-roms]     make a backup now (the Hub may keep running)
framebeam-hub backup list [--dir DIR]                     list the backups of the backup folder
framebeam-hub backup verify <file>                        check every checksum of a backup file
framebeam-hub backup config                               show the settings
framebeam-hub backup config --folder /mnt/backup/framebeam --schedule daily --time 03:00 --keep 7 --roms off
framebeam-hub backup config --schedule weekly --weekday sun
framebeam-hub restore <file> [--safety-dir DIR | --no-safety]     restore (the Hub must be stopped)
```

- `backup` into the configured folder behaves like a manual "Back up now" (the result shows on the Status card, retention runs afterwards). `--out DIR` writes a plain copy somewhere else: nothing is recorded and nothing is deleted.
- `backup config` writes the settings into the database; a running Hub applies them within 5 minutes. Without flags it only shows them.
- `restore` refuses while a Hub runs on the data folder (the Hub holds a lock file `hub.lock`). It makes a **safety backup** of the current state first (into `--safety-dir`, else the backup folder; `--no-safety` skips it, for a broken database or a fresh machine), verifies every checksum, unpacks to `restore-staging/` inside the data folder, then swaps everything in. If anything fails, the data folder is left exactly as it was. Run as root it gives the restored files the owner of the data folder.
- Exit status is non-zero on any failure, so the commands work in scripts and cron jobs. Example: `framebeam-hub backup --out /mnt/nas/framebeam` from cron for an extra copy.

## Admin API

Admin device token, `/api/v1/admin/backups` (tag `admin` in the OpenAPI file): `GET` list, `POST` start, `GET status` (progress of the running job: phase, bytes done and total, estimated seconds left; last result; next run; result of the last restore), `POST cancel`, `GET`/`PUT settings`, `DELETE {name}`, `GET {name}/download` (streamed, range support), `POST {name}/restore`, `DELETE restore-result`.

## Restore

What a restore does, in this order:

1. **Safety backup** of the current state into the backup folder. If it fails, nothing is changed.
2. **Verify and unpack** the chosen backup into `restore-staging/` (every file is compared with the manifest while it is read). A damaged file ("checksum mismatch in saves") or a backup made by a **newer** Hub (database schema newer than this Hub knows) stops here; nothing is changed and the Hub runs as before.
3. **Swap**: the Hub restarts itself; before it opens the database it moves the current database and data folders aside, moves the staged ones in place and deletes the old ones. A crash in the middle is repaired at the next start (rolled back, or finished if it was already committed).

The restart replaces the process in place (`exec` on Linux, an in-process restart on Windows), so no service-manager setting is needed: systemd (`Restart=on-failure` in `packaging/linux/framebeam-hub.service`) and the Windows service keep running the same process, and in Docker the container keeps its PID 1 and needs no `restart:` policy for this.

Consequences of a restore: everything that changed since the backup is gone (users, devices and runs added later, saves uploaded later, plugin and Hub settings). Connected Players are disconnected for about a minute and running Sessions end. Web sessions of the admin may be gone; sign in again. ROM files are only replaced when the backup contains ROMs; without them the ROM files now on the Hub stay. Files of the `config/` group (own TLS certificate and key, `hub.env`) are not written back to their old places; they are unpacked to `<data folder>/restored-config/` for you to copy where they belong.

After a web restore the Backups page shows "Restored" with the name of the safety backup, or "Not restored" with the reason. To undo a restore, restore the safety backup.

Rolling back the Hub itself is a separate topic: the Hub refuses a database from a newer version, so to go back to an older Hub version restore a backup made by that version.

## Docker

The image keeps all state in the volume `/data`. Backups should not live on the same disk as the data:

```yaml
services:
  hub:
    image: ghcr.io/<owner>/framebeam-hub:stable
    volumes:
      - framebeam-data:/data
      - /mnt/backup/framebeam:/backup     # a second volume or bind mount for the backups
```

Then set the folder to `/backup` (`docker compose exec hub framebeam-hub backup config --folder /backup --schedule daily`, or in Settings → Backups). The container user (uid 65532) needs write access to the host folder. The CLI works the same inside the container; `restore` needs the Hub stopped, so run it in a one-off container that mounts the same volumes: `docker compose run --rm --no-deps hub restore /backup/<file>` (stop the `hub` service first). The restore through the web or API needs no restart policy, see above.

## Restore on a new machine

1. Install the same or a newer Hub version ([hub-install.md](hub-install.md) or [hub-docker.md](hub-docker.md)), do not create an admin and do not start it yet (or stop it).
2. Copy the backup file to the machine.
3. `framebeam-hub restore <file> --no-safety` (as the service user, or as root).
4. Copy anything from `<data folder>/restored-config/` that you need (own TLS certificate and key, `hub.env`), set the public host and ports as on the old machine, start the Hub.
5. Players keep working if the address stays the same. A self-generated certificate comes with the backup, so its fingerprint is unchanged; a different address needs a new pairing only if the certificate changes.
6. Set the backup folder again if the path differs (`framebeam-hub backup config --folder ...`); the old value was restored with the database.

## Limits

- A file that is deleted while the backup runs (for example by the daily save history sweep) is left out and counted; the next backup is complete again.
- Backups are not encrypted and not compressed (saves and ROMs hardly compress). Use an encrypted disk or encrypt the files you copy off the machine.
- No upload of backups to cloud storage and no incremental backups yet.
