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
      "version": "0.3.0-beta.57",
      "commit": "<40 hex>",
      "published_at": "2026-10-07T10:00:00Z",
      "notes_url": "https://github.com/phabioo/framebeam/releases/tag/v0.3.0-beta.57",
      "protocol_version": 1,
      "min_protocol_version": 1,
      "artifacts": [
        {"platform": "linux-arm64", "kind": "deb", "name": "framebeam-hub_0.3.0~beta.57_arm64.deb",
         "size": 123, "sha256": "<64 lowercase hex>", "url": "https://github.com/.../framebeam-hub_0.3.0~beta.57_arm64.deb"}
      ]
    }
  ]
}
```

- `product`: `hub` or `player`. `channel`: `stable` or `beta`. `notes_url` is optional.
- Platforms and kinds: hub `linux-amd64`, `linux-arm64` with `deb` and `binary`; player `windows-x64` with `installer` and `zip`.
- Artifact URLs must be https (`file://` only when the index itself was loaded from `file://`, for tests). Integrity comes from size and SHA-256 in the signed index.
- Unknown fields are ignored, an unknown schema is an error, invalid releases are skipped and reported, a duplicate (product, channel, version) rejects the whole index.
- Versions are SemVer 2.0; a consumer picks the highest release of its product, channel, platform and kind that is strictly newer than the running version and protocol-compatible. Maintained with `framebeam-sign release-add` (newest 5 per product and channel).

