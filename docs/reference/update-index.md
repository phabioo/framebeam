# Update index (schema 1)

Release feed of the updater ([ADR 0011](../adr/0011-automatic-updates.md)). No change to `protocol_version` (stays 1) or OpenAPI. Release `updates-index` holds `updates-index.json` and `updates-index.json.sig` (default URL `https://github.com/phabioo/framebeam/releases/download/updates-index/updates-index.json`; signature URL = index URL + `.sig`). Signature as for the core index: Ed25519 over the exact bytes, one line `ed25519 <key_id> <base64 sig>`, same release key.

Schema 1:

```json
{
  "schema": 1,
  "generated_at": "2026-10-07T10:00:00Z",
  "releases": [
    {
      "product": "hub",
      "channel": "beta",
      "version": "0.8.3",
      "commit": "<40 hex>",
      "published_at": "2026-10-07T10:00:00Z",
      "notes_url": "https://github.com/phabioo/framebeam/releases/tag/v0.8.3",
      "protocol_version": 1,
      "min_protocol_version": 1,
      "artifacts": [
        {"platform": "linux-arm64", "kind": "deb", "name": "framebeam-hub_0.8.3_arm64.deb",
         "size": 123, "sha256": "<64 lowercase hex>", "url": "https://github.com/.../framebeam-hub_0.8.3_arm64.deb"}
      ]
    }
  ]
}
```

- `product`: `hub` or `player`. `channel`: `stable` or `beta`. `notes_url` is optional.
- Platforms and kinds: hub `linux-amd64`, `linux-arm64` with `deb` and `binary`; hub `windows-amd64` with `msi` and `binary`; player `windows-x64` with `msi`, `installer` and `zip`. Hub `msi` is applied by `msiexec` through the Windows updater service with `ALLUSERS=1`. Player `msi` is preferred by the Player (launcher `--apply-msi-update`); `installer` is the small Inno shell that only installs the MSI, kept so older Players can update ([ADR 0021](../adr/0021-one-windows-installer.md), [updates.md](../guides/updates.md)).
- Artifact URLs must be https (`file://` only when the index itself was loaded from `file://`, for tests). Integrity comes from size and SHA-256 in the signed index.
- Unknown fields are ignored, an unknown schema is an error, invalid releases are skipped and reported, a duplicate (product, channel, version) rejects the whole index.
- Versions are SemVer 2.0; a consumer picks the highest release of its product, channel, platform and kind that is strictly newer than the running version and protocol-compatible. Maintained with `framebeam-sign release-add` (newest 5 per product and channel).
- Forward compatibility: since 0.10.x consumers drop artifacts with a platform unknown for the product or a kind not allowed on it instead of rejecting the release (a release without remaining artifacts is skipped). `framebeam-sign release-add` stays strict for the new release.
- Legacy companion: Hubs <= 0.8.0 reject a whole release that has an unknown platform (`windows-amd64` since 0.8.1) and so saw no update. `release-add` therefore adds per hub channel, for the newest regular release only, an entry `X.Y.Z-legacy` with just the `linux-amd64`/`linux-arm64` `deb`/`binary` artifacts (skipped when the base version has a prerelease, when all artifacts are legacy already, or without a legacy `deb`). It is a prerelease, not build metadata, because the Player's parser keys duplicates without build metadata. It sorts just below `X.Y.Z`: old Hubs install it and then report `X.Y.Z`; newer Hubs prefer `X.Y.Z`; Windows Hubs never select it. Companions are regenerated on every `release-add` and not counted by `-keep`.
