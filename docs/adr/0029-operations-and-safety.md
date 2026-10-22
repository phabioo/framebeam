# ADR 0029: Operations and safety

- Status: Accepted for D1; further decisions (backup and restore, update rollback, problem reports, connection assistant, Hub health) follow in later 0.17 PRs
- Date: 2026-10-22
- Deciders: Fabio

## Context

Roadmap 0.17 ([roadmap.md](../roadmap.md)) collects the operations items that matter before a test evening. The Hub so far installs as a Debian package or with `install-hub.sh` on a systemd host and updates itself through a root helper ([ADR 0011](0011-automatic-updates.md)). Many self-hosters run everything in Docker (NAS, home server) and expect an image.

## Decisions

### D1 Docker image for the Hub

- **Registry and tags.** `ghcr.io/<owner>/framebeam-hub` (GitHub Container Registry, same repository and `GITHUB_TOKEN` as the releases; no extra account or secret). Tags: `X.Y.Z` for every beta and release, `beta` for the newest `main` build, `stable` and `latest` for the promoted release. `release.yml` pushes `X.Y.Z` and `beta` from the binaries CI built and tested (Dockerfile target `hub-prebuilt`, no rebuild, same number as the `.deb`). `promote.yml` adds `stable` and `latest` to the existing manifest with `docker buildx imagetools create`, also without a rebuild. CI builds the image for `linux/amd64` and `linux/arm64` and starts the amd64 one on every change, but never pushes.
- **Image.** `packaging/docker/Dockerfile`, multi-stage. The Go stage runs on the build host and cross-compiles with `CGO_ENABLED=0` (pure-Go SQLite), so arm64 needs no emulation. The runtime is `gcr.io/distroless/static` (no shell, no package manager, CA certificates included). Version, channel and commit are baked in with the same ldflags as `make build-hub`.
- **Volumes.** One volume `/data` (`FRAMEBEAM_DATA_DIR`) holds all state: database, saves, firmware, ROM library, certificate, cores, plugins, backups. Backup is a copy of that volume. Nothing else in the container is state.
- **Non-root.** The container runs as uid/gid `65532`. The `/data` directory in the image is owned by that user so new named volumes work; bind mounts need a `chown`.
- **Ports.** Web `8443/tcp`; with the embedded TURN relay `3478/udp+tcp` and the relay range (default `49160-49199/udp`). The user publishes what the configuration needs.
- **No self-update in the container.** The image sets `FRAMEBEAM_HUB_CONTAINER=1`. The updater's package check (`updates.IsPackaged`) then returns false, so the Hub still checks the index and shows "Update available", but never stages or installs a `.deb`; Settings shows how to update (pull the new image and recreate the container). Automatic install is off for the same reason. There is no root helper or systemd path unit in the image to begin with.
- **No `HEALTHCHECK`** for now: the image has no shell or curl and the Hub has no health command. The Hub health page (later in 0.17) can add one.

## Rejected

- **Docker Hub as registry:** needs an account, a secret and pull rate limits; GHCR is already where the project lives.
- **Rebuilding the image in `release.yml` or `promote.yml`:** the stable image must be exactly the tested beta (ADR 0011); `imagetools create` re-tags the manifest.
- **Alpine or Debian runtime:** a shell and a package manager add attack surface and size; the Hub is one static binary.
- **Letting the container update itself** (for example by replacing the binary in the writable layer or in `/data`): the change would be lost on the next recreate and bypasses the image as the unit of upgrade and rollback.

## Consequences

- Fabio must set the GHCR package `framebeam-hub` to public after the first push (package settings), otherwise `docker pull` needs a login.
- Docker users update with `docker compose pull && docker compose up -d`; the Hub keeps its database copy in `/data/backups` before a schema migration.
- The guide is [hub-docker.md](../guides/hub-docker.md).
