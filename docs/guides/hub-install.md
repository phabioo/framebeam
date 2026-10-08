# Install the Hub on Linux

Two ways to run the FrameBeam Hub as a systemd service: the Debian package (recommended on Debian / Raspberry Pi OS, the only type that can update itself) or the script `packaging/linux/install-hub.sh` (any systemd Linux). For flags and environment variables see [hub-configuration.md](hub-configuration.md); for Players outside your LAN see [sessions-over-the-internet.md](sessions-over-the-internet.md).

## Install the Debian package (recommended)

Download `framebeam-hub_<version>_<amd64|arm64>.deb` from the [GitHub releases](https://github.com/phabioo/framebeam/releases) (or the CI artifact `framebeam-hub-linux`) and install it:

```sh
sudo apt install ./framebeam-hub_<version>_arm64.deb
```

The package installs
`/usr/bin/framebeam-hub`, the units `framebeam-hub.service`, `framebeam-hub-update.path` and
`framebeam-hub-update.service` (in `/lib/systemd/system`) and is the only install type the Hub can update itself
(Settings > Updates). Configure flags (port, admin) in `/etc/framebeam/hub.env`, which an existing install keeps. Create the admin account as described under [Admin account](#admin-account).

Package details:

- Published betas and releases have a plain version (`framebeam-hub_0.8.3_arm64.deb`). The control Version field uses `~` for other pre-release versions (`0.8.3~dev.57`, legacy `0.3.0~beta.57`), so they sort before the release; file names keep the SemVer because GitHub rewrites special characters in asset names.
- The postinst creates the `framebeam` user, `/var/lib/framebeam` (only if missing) and `/etc/framebeam/hub.env` (only
  if missing: an existing file is never overwritten), then enables and (re)starts the Hub and the update path unit.
- Migration from `install-hub.sh`: the old unit in `/etc/systemd/system` is moved to
  `/etc/framebeam/framebeam-hub.service.pre-deb`, `/usr/local/bin/framebeam-hub` is removed; drop-ins in
  `framebeam-hub.service.d/`, `hub.env` and the data directory are kept.
- Remove keeps config and data; purge removes `/etc/framebeam` only. The data directory (database, saves, ROM library)
  and the `framebeam` user are never deleted by the package.
- Updates: the Hub (user `framebeam`) stages a verified `.deb` and creates `/run/framebeam/update-request`; the path
  unit starts `framebeam-hub update apply-staged` as root, which verifies the staged package again and runs `dpkg -i`.
- Build (dpkg-deb only): `packaging/linux/build-deb.sh --binary PATH --arch amd64|arm64 --version X.Y.Z[-pre] --out DIR`;
  all of it: `make build-hub package-hub-deb HUB_VERSION=...`. Checked by `make check-hub` (control, contents, units,
  shellcheck of the maintainer scripts in `deb/`).
- `scripts/e2e-hub-update.sh` installs and updates a Hub on a real systemd host (CI only; skips without root/systemd).

## Install with `install-hub.sh`

`install-hub.sh` installs the Hub as a persistent service. Primary target: Raspberry Pi 5 (64-bit Raspberry Pi OS / Debian, aarch64); amd64 works too. It installs a local binary and downloads nothing.

## 1. Get the binary

- CI: download the artifact `framebeam-hub-linux` from a workflow run of `.github/workflows/ci.yml` (contains `framebeam-hub-linux-arm64` and `-amd64`).
- Or build: `make build-hub` produces `server/dist/framebeam-hub-linux-{amd64,arm64}`.

Copy the binary and the files of `packaging/linux/` to the Pi, for example:

```sh
scp framebeam-hub-linux-arm64 packaging/linux/install-hub.sh packaging/linux/framebeam-hub.service pi@raspberrypi:
```

## 2. Install

```sh
sudo ./install-hub.sh install --binary ./framebeam-hub-linux-arm64 --name "Living Room Hub" --admin alice
```

> **Port 8443 already in use? Pick another port: `--port 8444`**
>
> ```sh
> sudo ./install-hub.sh install --binary ./framebeam-hub-linux-arm64 --port 8444
> ```
>
> The script warns if the chosen port is busy. Change it later by re-running `install` with `--port N` (only the given options are updated in `hub.env`, then restart: `sudo systemctl restart framebeam-hub`).

Options: `--binary PATH`, `--port N` or `--listen ADDR`, `--data-dir DIR`, `--name NAME` (first start only), `--admin USER`, `--no-start`; see `--help`. If `--binary` is omitted, `framebeam-hub-linux-<arch>` next to the script or in the current directory is used.

At the end the script prints the service status, the URL (`https://<ip>:<port>/`) and the TLS fingerprint.

## Admin account

`--admin USER` asks for the password twice (or reads one line from stdin when not a terminal, e.g. `echo ... | sudo ./install-hub.sh ...`). It runs `framebeam-hub setup-admin` as the `framebeam` user and is skipped if an admin already exists. On a headless Pi this is needed because the web setup form only accepts connections from localhost. Without the script (for example after the `.deb` install) run `setup-admin` as the `framebeam` user with the data directory from `hub.env`, for example `sudo runuser -u framebeam -- env FRAMEBEAM_DATA_DIR=/var/lib/framebeam framebeam-hub setup-admin -username <name>` (use the `FRAMEBEAM_DATA_DIR` value from `/etc/framebeam/hub.env` if you changed it), or open `/setup` in a browser on the Hub host itself.

## TLS fingerprint

The Hub creates a self-signed certificate under `<data dir>/tls/` and logs its SHA-256 fingerprint at startup. The Player asks you to confirm it on first contact:

```sh
journalctl -u framebeam-hub | grep -i fingerprint
```

## Upgrade

```sh
sudo ./install-hub.sh upgrade --binary ./framebeam-hub-linux-arm64
```

Replaces the binary atomically, restarts the service and prints the old and new version. Data and config are untouched.

## Renew the certificate

```sh
sudo ./install-hub.sh renew-cert
```

Renews the self-generated TLS certificate as the `framebeam` user in the service's data dir (read from `/etc/framebeam/hub.env`), restarts the service and prints the new fingerprint. Refused when `FRAMEBEAM_TLS_CERT`/`FRAMEBEAM_TLS_KEY` are set. Do not call `framebeam-hub renew-cert` directly on a service install: another user or `-data-dir` renews a different certificate, and root would create root-owned files the service cannot read.

## Import cores offline

```sh
sudo ./install-hub.sh import-cores /path/to/dir
```

For a Hub that cannot reach the FrameBeam core source. `dir` holds `cores-index.json`, `cores-index.json.sig` and the package files (download them from the `cores-index` and `core-*` releases on GitHub). The script copies the directory to a temporary location the service user can read and runs `framebeam-hub import-cores` as the `framebeam` user in the service's data dir (read from `/etc/framebeam/hub.env`, like `renew-cert`). The index signature must match a trusted key: the built-in FrameBeam key or keys in `FRAMEBEAM_HUB_CORE_TRUST_KEYS` of `/etc/framebeam/hub.env`. The service keeps running.

## Uninstall

```sh
sudo ./install-hub.sh uninstall            # keeps /etc/framebeam and the data dir
sudo ./install-hub.sh uninstall --purge    # also deletes them (asks first; --yes skips)
```

The `framebeam` system user is kept. `status` shows the service state and URL.

## Files

| Path | Purpose |
| --- | --- |
| `/usr/local/bin/framebeam-hub` | Hub binary |
| `/etc/framebeam/hub.env` | Config (`FRAMEBEAM_LISTEN`, `FRAMEBEAM_DATA_DIR`, optional `FRAMEBEAM_NAME`), `0640 root:framebeam` |
| `/var/lib/framebeam` | Default data dir (database, saves, `tls/`), `0750 framebeam:framebeam` |
| `/etc/systemd/system/framebeam-hub.service` | Service unit |
| `/etc/systemd/system/framebeam-hub.service.d/data-dir.conf` | Drop-in, only for a non-default data dir or ports below 1024 |

## Migrating an existing manual data dir

Do not point `--data-dir` into `/home` or `/root` (the script refuses it): home directories are typically `0700`, so the `framebeam` user cannot reach them, and `ProtectHome` applies as well. Copy the data into the default location instead:

```sh
# 1. stop the old, manually started Hub process
sudo ./install-hub.sh install --binary ./framebeam-hub-linux-arm64 --no-start
sudo cp -a /home/pi/framebeam-data/. /var/lib/framebeam/
sudo ./install-hub.sh install --binary ./framebeam-hub-linux-arm64   # fixes ownership, starts the service
```

Keep the old directory as a backup until the service works. `--data-dir DIR` remains available for directories outside `/home` and `/root`, e.g. `/srv/framebeam`; the script chowns them to `framebeam` and never deletes or overwrites their content.

