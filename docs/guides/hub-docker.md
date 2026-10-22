# Run the Hub in Docker

The official image runs the FrameBeam Hub as a non-root container for `linux/amd64` and `linux/arm64` (Raspberry Pi 5 included). Decisions: [ADR 0029](../adr/0029-operations-and-safety.md) D1. For flags and environment variables see [hub-configuration.md](hub-configuration.md); for Players outside your LAN see [sessions-over-the-internet.md](sessions-over-the-internet.md). The Debian package ([hub-install.md](hub-install.md)) stays the recommended install on Debian; use the image when you already run Docker (NAS, home server).

## Image

`ghcr.io/phabioo/framebeam-hub`

| Tag | Meaning |
|---|---|
| `X.Y.Z` | One exact version (every beta and release). Pin this for repeatable setups. |
| `stable` and `latest` | The newest promoted release ("Promote to release", no rebuild). |
| `beta` | The newest build of `main`. Not for production. |

The image is `distroless/static` with the Hub binary only: no shell, no package manager. It runs as user `65532` and keeps all data in the volume `/data`. The image sets `FRAMEBEAM_DATA_DIR=/data`, `FRAMEBEAM_LISTEN=:8443` and `FRAMEBEAM_HUB_CONTAINER=1`.

## docker run

```sh
docker run -d --name framebeam-hub --restart unless-stopped \
  -v framebeam-data:/data \
  -p 8443:8443 \
  ghcr.io/phabioo/framebeam-hub:stable
```

Open `https://<docker-host>:8443`. The Hub creates a self-signed certificate (accept it once; the Player pins it). A container is not "local" for the setup page, so create the admin account with the CLI instead (password as a single line from stdin):

```sh
printf '%s\n' 'a long password' | docker exec -i framebeam-hub framebeam-hub setup-admin -username alice
```

Other commands work the same way, for example `docker exec -i framebeam-hub framebeam-hub import-cores /data/import` (put the zips in the volume first) and `docker exec framebeam-hub framebeam-hub diagnostics /data/diagnostics.zip`.

## docker compose

```yaml
services:
  hub:
    image: ghcr.io/phabioo/framebeam-hub:stable
    container_name: framebeam-hub
    restart: unless-stopped
    environment:
      FRAMEBEAM_NAME: "Living Room Hub"        # first start only
      # Sessions over the internet (embedded TURN relay, see sessions-over-the-internet.md):
      # FRAMEBEAM_TURN: "1"
      # FRAMEBEAM_PUBLIC_HOST: "hub.example.com"
    ports:
      - "8443:8443"                  # web interface and API (TCP)
      # With FRAMEBEAM_TURN=1 (same numbers as -turn-port and -turn-relay-ports):
      # - "3478:3478/udp"
      # - "3478:3478/tcp"
      # - "49160-49199:49160-49199/udp"
    volumes:
      - framebeam-data:/data
      # Optional: a ROM folder for "Rescan folder" (read only). Set FRAMEBEAM_LIBRARY_IMPORT_DIR=/roms.
      # - /srv/roms:/roms:ro

volumes:
  framebeam-data:
```

Bridge networking is fine for the web port. For the TURN relay the Hub announces the address from `FRAMEBEAM_PUBLIC_HOST` (or `FRAMEBEAM_TURN_RELAY_IP`), so publish exactly the relay range you configured and forward the same ports on the router. If relaying is unreliable behind NAT, `network_mode: host` removes Docker's port translation (then drop the `ports` section).

## Volumes and permissions

- Back up to a second volume, not into `/data`: see [hub-backup.md](hub-backup.md#docker).
- `/data` holds everything: `framebeam.db`, `saves/`, `firmware/`, the ROM library, `tls/`, `cores/`, plugins and `backups/`. Nothing else in the container is state.
- A new named volume starts owned by the container user. For a bind mount (`-v /srv/framebeam:/data`) make the folder writable for uid `65532` first: `sudo chown 65532:65532 /srv/framebeam`.
- Own certificate: mount the PEM files read only (for example `-v /srv/certs:/certs:ro`) and set `FRAMEBEAM_TLS_CERT=/certs/hub.pem`, `FRAMEBEAM_TLS_KEY=/certs/hub.key`.

## Upgrade

The Hub never updates itself inside a container (`FRAMEBEAM_HUB_CONTAINER=1`): Settings → Updates shows that a newer version exists and tells you to pull the image. Sessions end while the container restarts.

```sh
docker compose pull && docker compose up -d
# or, with docker run: docker pull ghcr.io/phabioo/framebeam-hub:stable, then
# docker rm -f framebeam-hub and run it again with the same -v option
```

The Hub migrates its database at start and keeps a copy of the old one in `/data/backups`. To go back, start the older `X.Y.Z` tag with a restored copy (the Hub refuses a database from a newer version, see [Data directory](hub-configuration.md#data-directory)).

## Backup

Back up the `/data` volume. For a consistent copy stop the container first:

```sh
docker stop framebeam-hub
docker run --rm -v framebeam-data:/data:ro -v "$PWD":/backup alpine tar czf /backup/framebeam-data.tar.gz -C /data .
docker start framebeam-hub
```

Restore into an empty volume with the same command in reverse (`tar xzf ... -C /data`, then `chown -R 65532:65532 /data`). Saves are never sent to another Hub; keep the archive private.

## Build the image yourself

From the repository root (the context must be the root):

```sh
docker build -f packaging/docker/Dockerfile -t framebeam-hub \
  --build-arg HUB_VERSION=0.17.0 --build-arg HUB_CHANNEL=stable .
docker buildx build --platform linux/amd64,linux/arm64 -f packaging/docker/Dockerfile .
```

The Go build stage always runs on the build host and cross-compiles, so arm64 needs no emulation. CI builds both platforms on every change; `release.yml` pushes `X.Y.Z` and `beta` from the CI binaries and `promote.yml` adds `stable` and `latest` to the promoted version without a rebuild.
