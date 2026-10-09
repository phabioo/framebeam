# packaging

Release artifacts of FrameBeam.

- `linux/`: Hub as a systemd service. `install-hub.sh` (any systemd Linux), the Debian package build `build-deb.sh` with its units and maintainer scripts (`deb/`), and the service and update units. Guide: [docs/guides/hub-install.md](../docs/guides/hub-install.md); packaging checks run in `make check-hub`.
- `windows/`: Player installer `framebeam-player.iss` (Inno Setup, per-user), built in the Windows CI job. Details: [docs/guides/packaging.md](../docs/guides/packaging.md).
- `macos/`: empty placeholder; a macOS Player is planned in the [roadmap](../docs/roadmap.md) (0.12).

Versions, channels and the release pipeline: [docs/development.md](../docs/development.md), [docs/guides/updates.md](../docs/guides/updates.md).
