# Publishing an official plugin

For plugin maintainers with access to the FrameBeam release signing key. Background: [12-plugins.md](../architecture/12-plugins.md#1610-distribution), [ADR 0023](../adr/0023-plugins.md). Hub side: [hub-configuration.md](hub-configuration.md#plugins).

An official plugin is a `.fbplugin` file (zip with `manifest.json` at the root) that carries a `SIGNATURE.json` signed with the FrameBeam release key. A plugin index lists signed files so that a Hub admin can install or update them with one click. The tool is `server/cmd/framebeam-sign`. The signing key is read from the environment variable `FRAMEBEAM_SIGNING_KEY` (base64 seed); never write it to a file in a repository or to logs. Plugin repositories keep it as a CI secret.

1. **Build the zip.** Zip the plugin folder (manifest, entry file, assets, i18n) as `x.fbplugin`. Limits: 32 MiB packed, 64 MiB unpacked, 2000 entries, relative `/` paths only, no symlinks.
2. **Sign it.**
   ```
   framebeam-sign plugin-sign -in x.fbplugin -out x-signed.fbplugin
   ```
   Validates the file and adds or replaces `SIGNATURE.json` (SHA-256 of every file plus an Ed25519 signature). `-out` may equal `-in`.
3. **Add it to the index.**
   ```
   framebeam-sign plugin-add -index plugin-index.json -plugin x-signed.fbplugin -url https://example.com/releases/x-signed.fbplugin
   ```
   `-url` is where the signed file will be published (https). The command refuses unsigned or invalid files, creates the index when missing, and adds or replaces the entry for the manifest's id and version; other entries stay. It records name, version, `api`, description, permissions, SHA-256 and size from the file.
4. **Sign the index.**
   ```
   framebeam-sign sign -index plugin-index.json
   ```
   Writes `plugin-index.json.sig` next to the index (same scheme as the updates index).
5. **Publish** the signed `.fbplugin` at the URL from step 3, and `plugin-index.json` plus `plugin-index.json.sig` side by side at one location.

A Hub admin then sets `-plugin-index-url` (env `FRAMEBEAM_PLUGIN_INDEX_URL`, https or `file://`, for example `https://hub.example.com/plugin-index.json`) and installs the plugin from the Plugins page. The default is empty: without it the Hub has no catalog, and signed or unsigned files can still be uploaded or imported with `framebeam-hub import-plugins <dir>`.

Checks: the Hub accepts the signature only for the release key or a key given with `-core-trust-key`; a file that fails validation or whose SHA-256 differs from the index is not installed. The index URL of the official plugins is not decided yet (**open**).
