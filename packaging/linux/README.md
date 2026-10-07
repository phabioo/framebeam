# FrameBeam Hub on Linux (systemd)

`install-hub.sh` installs the Hub as a persistent service. Primary target: Raspberry Pi 5 (64-bit Raspberry Pi OS / Debian, aarch64); amd64 works too. It installs a local binary and downloads nothing.

## 1. Get the binary

- CI: download the artifact `framebeam-hub-linux` from a workflow run of `.github/workflows/ci.yml` (contains `framebeam-hub-linux-arm64` and `-amd64`).
- Or build: `make build-hub` produces `server/dist/framebeam-hub-linux-{amd64,arm64}`.

Copy the binary and this directory's files to the Pi, for example:

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

`--admin USER` asks for the password twice (or reads one line from stdin when not a terminal, e.g. `echo ... | sudo ./install-hub.sh ...`). It runs `framebeam-hub setup-admin` as the `framebeam` user and is skipped if an admin already exists. On a headless Pi this is needed because the web setup form only accepts connections from localhost.

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
