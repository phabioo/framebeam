# Plugin API reference (API 1)

For authors of FrameBeam Player plugins. Design background: `docs/architecture/12-plugins.md`, `docs/adr/0023-plugins.md`.
This page describes what the Player runtime implements today. Parts marked **coming in a later package** are not available yet.

A plugin is a folder with a `manifest.json`, a Lua entry file and optional `i18n/` and `assets/`. It runs in a sandboxed Lua 5.4 state on the plugin thread of the Player, never on the emulation or UI thread. A plugin can read the game's memory, but it can never slow the game down.

## Manifest

```json
{
  "id": "net.example.tracker",
  "name": "Tracker",
  "version": "1.0.0",
  "api": 1,
  "type": "script",
  "requires": { "net.example.core": "^1.2" },
  "provides": ["tracker.team"],
  "games": [{ "system": "nds", "codes": ["IPK*", "IPG?"] }],
  "permissions": ["memory.read", "rom.read"],
  "entry": "main.lua",
  "assets": ["icons/"],
  "settings": {}
}
```

| Field | Rules |
|---|---|
| `id` | Reverse-DNS, lowercase letters, digits, `-` and `.`, at least two parts (`net.example.tracker`). |
| `name`, `version` | Display name; SemVer (`1.0.0`). |
| `api` | Must be `1`. Otherwise the plugin is rejected: "`<name>` needs plugin API `<n>`; this Player supports API 1". |
| `type` | `"script"` (default). `"tool"` is rejected: "tool plugins are not supported yet". |
| `requires` | Map of plugin id to version range: `^1.2` (compatible, below the next major; `^0.2` stays below `0.3`), `~1.2` (below the next minor), `>=1.0`, `1.2.3` (exact), `*`. Required plugins start first. A required plugin must be installed and match the range. |
| `provides` | Interface names this plugin offers to others with `fb.provide`. |
| `games` | List of `{ "system", "codes" }`. `codes` are globs (`*` any run, `?` one character, case-insensitive); no `codes` = every game of that system. An empty or missing list = all games. |
| `permissions` | `memory.read`, `rom.read`, `ui`, `run.state`. `memory.write` and `tool.exec` are rejected ("permission `<p>` is not available yet"), unknown names are errors. |
| `entry` | Relative path of the `.lua` entry file inside the plugin folder (no `..`, must exist). |
| `assets` | Relative paths of bundled files or folders. |
| `settings` | Kept as raw JSON; the schema is not defined yet. |

## Permissions

| Permission | Unlocks | Status |
|---|---|---|
| `memory.read` | `fb.memory.*`, `fb.dev.watch` | available |
| `rom.read` | `fb.rom.*` | available |
| `ui` | declarative UI building blocks | **coming in a later package** |
| `run.state` | per-player run state and run events | **coming in a later package** |

Calling a function without its permission raises the Lua error `permission <p> not granted`. Everything else (`fb.log`, `fb.events`, `fb.provide`/`fb.call`, `fb.cache`, `fb.i18n`, `fb.clock`, `fb.on_tick`) needs no permission.

## Sandbox and limits

- Libraries: base, `string`, `table`, `math`, `utf8`, `coroutine`. There is no `io`, `os`, `package` or `debug`. `load`, `loadstring`, `dofile`, `loadfile`, `require` and `string.dump` do not exist. Only text source is accepted, never precompiled bytecode.
- `collectgarbage("count")` returns the memory in use in KiB; every other option does nothing.
- `print(...)` writes an info line to the plugin log (same as `fb.log.info`).
- Each plugin has its own Lua state, so plugins cannot see each other's globals.

| Limit | Value |
|---|---|
| Memory per plugin | 64 MiB |
| Instructions per entry call | 20,000,000 |
| Wall-clock time per entry call | 500 ms |
| One watched memory range | 4 MiB |
| All watched ranges of one plugin | 16 MiB (at most 256 ranges) |
| `fb.rom.read` length | 16 MiB per call |
| Plugin cache | 256 MiB per plugin; `fb.cache.read` returns files up to 32 MiB |
| Values passed between plugins | depth 32, 1 MiB |

An *entry call* is anything the host starts: loading the entry file, a tick, an event handler, or a call made with `fb.call`. When a plugin exceeds its budget or memory limit, or raises an error that it does not catch, **only that plugin is paused**: the error and its traceback appear in the developer console, its watched ranges are released, and the game and all other plugins keep running. Fix the file and reload the plugin to start it again. A plugin cannot hide a budget overrun with `pcall`.

Note: long-running native library calls (for example a pathological `string.find` pattern on a huge string) are not interrupted by the instruction budget; they are bounded by the memory limit only.

## API

Everything lives in the global table `fb`.

### Information

- `fb.api` is `1`.
- `fb.plugin` is `{ id, version }`.
- `fb.game` is `{ system, code, sha256, title, language }`. Any field may be `nil`.
- `fb.clock()` returns monotonic milliseconds (integer).

### Logging

`fb.log.debug(...)`, `fb.log.info(...)`, `fb.log.warn(...)`, `fb.log.error(...)` join their arguments with tabs (like `print`) and write to the plugin log shown in the developer console.

```lua
fb.log.info("loaded", fb.plugin.version)
```

### Memory (`memory.read`)

Plugins read a **snapshot** that the emulation copies after frames. They register the ranges they need; the data appears from the next snapshot on.

- `fb.memory.watch(key, addr, len)` registers a range under a name of your choice (the same key replaces the range). `fb.memory.unwatch(key)` removes it.
- `fb.memory.u8/u16/u32/u64/s8/s16/s32(addr)` read little-endian integers. `fb.memory.bytes(addr, len)` returns a string (`len` up to 4 MiB). `u64` wraps into the signed Lua integer range.
- All reads return `nil` when the address is not in the latest snapshot. `fb.memory.frame()` returns the frame number of the latest snapshot or `nil` if there is none yet.
- **Late memory maps:** a core may re-send its memory map while the game runs (for example when the game maps its heap later). The Player picks up the new regions at once: the console's MEMORY MAP section updates, and addresses that were unreadable become readable from the next snapshot on (ranges you already watch need no re-registration).
- **Pointer following:** read a pointer, then watch the target. The target is readable from a later snapshot.

```lua
fb.memory.watch("party", 0x02234000, 0x1D8)
local followed = false
fb.on_tick(function()
  local ptr = fb.memory.u32(0x02234000)
  if not ptr then return end
  if not followed then fb.memory.watch("target", ptr, 64) followed = true end
  local value = fb.memory.u32(ptr)        -- nil in the first tick, set afterwards
end)
```

### Ticks

`fb.on_tick(fn)` sets the function that is called on the plugin thread whenever a new snapshot exists, at most `tickHz` times per second (host setting, default 30). `fn` receives the snapshot frame number. `fb.on_tick(nil)` removes it.

### Events

- `fb.events.emit(name, value)` publishes an event.
- `fb.events.on(pluginId, name, fn)` subscribes; `pluginId` is a plugin id or `"*"` for any. `fn(value, sourcePluginId, name)` runs on a later host iteration, never inside `emit`.

Values are deep-copied between states: `nil`, booleans, numbers, strings and tables of these (depth up to 32, up to 1 MiB). Functions, userdata and tables used as keys are rejected with a Lua error. Emitted events also show in the developer console.

```lua
fb.events.emit("team.changed", { slot = 1, species = 25 })
fb.events.on("net.example.tracker", "team.changed", function(v, from) fb.log.info(from, v.slot) end)
```

### Interfaces between plugins

- `fb.provide(name, tbl)` publishes a table of functions under `name`, which must be listed in the manifest's `provides`.
- `fb.call(name, fn, ...)` calls `tbl[fn](...)` of the plugin that provides `name`. That plugin must be listed directly in this plugin's `requires`. The call is synchronous on the plugin thread; arguments and results are deep-copied (same rules as events, no shared Lua memory). The callee's budget applies. An error in the callee becomes a Lua error in the caller (catch it with `pcall`). A callee that exceeds its budget or memory is paused.

```lua
-- provider (provides = ["tracker.team"])
fb.provide("tracker.team", { size = function() return 6 end })
-- consumer (requires the provider)
local n = fb.call("tracker.team", "size")
```

### Developer watches (`memory.read`)

`fb.dev.watch(label, addr, kind)` registers the range and shows label, address and live value in the WATCHED list of the developer console. `kind` is `u8`, `u16`, `u32`, `s32` or `hex:<len>` (1 to 64 bytes). `fb.dev.unwatch(label)` removes it.

```lua
fb.dev.watch("party count", 0x02234000, "u8")
fb.dev.watch("rng seed", 0x02100000, "hex:4")
```

### Game file (`rom.read`)

- `fb.rom.size()` returns the size of the game file (or `nil` if there is none).
- `fb.rom.read(offset, len)` returns up to `len` bytes (at most 16 MiB) as a string, shorter at the end of the file, `nil` at or after the end.

- `fb.rom.romfs(path)` reads one file from the RomFS of a decrypted 3DS game (`.cxi`, `.3ds`/`.cci`), e.g. `"/a/0/1"`; up to 256 MiB. Returns the data or `nil, message`. Encrypted games fail with a clear message.
- `fb.rom.romfs_list(dir)` returns an array of `{ name, dir, size }` (`dir` is a boolean) for a RomFS directory, or `nil, message`.

### Format helpers (`fb.codec`)

Pure functions on binary strings, no permission needed. Input is treated as untrusted. Failures never raise a Lua error: they return `nil, message`; only a wrong argument type raises. Size limits are the ones of the C++ helpers (decompressed data up to 64 MiB, images up to 4096 x 4096). All indexes are 1-based.

- `fb.codec.lz_decompress(s)` decompresses Nintendo LZ10/LZ11 data.
- `fb.codec.garc(s)` parses a GARC container (versions 0x0400 and 0x0600) and returns `{ count, data_offset, files }`; `files[i][sub]` is `{ offset, length }` relative to `data_offset`.
- `fb.codec.garc_file(s, i, sub)` returns the bytes of entry `i` (1-based), sub-file `sub` (1-based, default 1).
- `fb.codec.ctr_texture(s, width, height, format)` decodes a 3DS GPU texture (formats 0 RGBA8, 1 RGB8, 2 RGBA5551, 3 RGB565, 4 RGBA4, 5 LA8, 6 HILO8, 7 L8, 8 A8, 9 LA4, 10 L4, 11 A4, 12 ETC1, 13 ETC1A4).
- `fb.codec.bclim(s)` decodes a BCLIM image (footer at the end of the file).
- `fb.codec.etc1(s, width, height, alpha)` decodes plain ETC1 (`alpha` false) or ETC1A4 (`alpha` true) block data.
  The three image decoders return `{ width, height, rgba }` (RGBA8888, straight alpha, top-down) or `nil, message`.
- `fb.codec.png(img)` encodes such an image table as a PNG string.
- `fb.codec.pk6_decrypt(s)` / `fb.codec.pk6_encrypt(s)` decrypt or encrypt a Gen 6 Pokemon structure (232 or 260 bytes) and return `data, checksum_ok` (`checksum_ok` says whether the stored checksum matches the decrypted data; encrypting recomputes nothing), or `nil, message`.

```lua
local raw = fb.memory.bytes(slot_addr, 260)
local plain, ok = raw and fb.codec.pk6_decrypt(raw)
if plain and ok then species = string.unpack("<I2", plain, 9) end
```

### Cache

A per-plugin folder for data you derive from the game, such as extracted assets. Names use `A-Z a-z 0-9 . _ - /`, no `..`, no leading `/`; anything else raises an error.

- `fb.cache.write(name, data)` returns `true`; the 256 MiB quota per plugin is enforced (error `cache quota exceeded`).
- `fb.cache.read(name)` returns the data or `nil`.
- `fb.cache.exists(name)` returns a boolean.

### Translations

`fb.i18n.t(key, vars)` looks `key` up in `i18n/<lang>.json` of the plugin, where `<lang>` is the game language (`de`, `de-DE`, ...); it falls back to `i18n/en.json`, then to the key itself. `{name}` placeholders are replaced from `vars`. Files are flat JSON objects; nested objects are addressed as `a.b`. English is the source language, German the first translation.

```lua
-- i18n/en.json: { "team.title": "Team of {player}" }
fb.log.info(fb.i18n.t("team.title", { player = "Ash" }))
```

### UI and run state

**Coming in a later package:** declarative UI building blocks (`ui` permission) and run state (`run.state`).

## Developer mode

Plugins that are not signed ("Local file") only run while **Developer mode** is on. In Developer mode you can:

- install a plugin from a folder (the Player copies it),
- link a folder (`linkDevFolder`): the plugin runs straight from your folder, and saving a file reloads it after about 300 ms,
- read the developer console: status, WATCHED values, EVENTS, LOG, and the error with traceback when a plugin was paused.

A new install is turned off. A plugin whose requirements are missing, too old or cyclic cannot be turned on; the Player shows the reason (for example "needs Creature Core ^2.0 · you have 1.2.0", "needs net.example.core, not installed", "dependency cycle: a -> b -> a").

## Minimal example

`manifest.json`:

```json
{
  "id": "net.example.hello",
  "name": "Hello",
  "version": "0.1.0",
  "api": 1,
  "permissions": ["memory.read"],
  "entry": "main.lua"
}
```

`main.lua`:

```lua
fb.log.info(fb.i18n.t("hello", { title = fb.game.title or "?" }))

fb.memory.watch("counter", 0x02000000, 4)
fb.dev.watch("counter", 0x02000000, "u32")

local last
fb.on_tick(function()
  local v = fb.memory.u32(0x02000000)
  if v and v ~= last then
    last = v
    fb.events.emit("counter.changed", { value = v })
  end
end)
```

`i18n/en.json`: `{ "hello": "Hello from {title}" }`
