# packaging/linux

Hub on Linux (systemd): the installer script, the Debian package build and the units.

| File | Purpose |
|---|---|
| `install-hub.sh` | `install`, `upgrade`, `renew-cert`, `import-cores`, `status`, `uninstall` from a local binary (nothing is downloaded); run `--help` |
| `framebeam-hub.service` | Service unit used by the script |
| `build-deb.sh` | Builds `framebeam-hub_<version>_{amd64,arm64}.deb` (dpkg-deb only): `make build-hub package-hub-deb HUB_VERSION=...` |
| `deb/` | Maintainer scripts of the package (`postinst`, `prerm`, `postrm`) |
| `framebeam-hub-update.path`, `framebeam-hub-update.service` | Root helper that applies a staged Hub update |

```sh
sudo ./install-hub.sh install --binary ./framebeam-hub-linux-arm64 --admin <name>   # add --port 8444 if 8443 is taken
```

Full guide (Debian package, options, admin account, TLS, upgrade, migration, uninstall, files): [docs/guides/hub-install.md](../../docs/guides/hub-install.md).
