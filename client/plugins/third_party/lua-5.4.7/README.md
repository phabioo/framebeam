# Vendored Lua 5.4.7

- Project: Lua, https://www.lua.org/ (git: https://github.com/lua/lua, tag `v5.4.7`)
- Version: 5.4.7 (`LUA_VERSION_RELEASE "7"`)
- Licence: MIT (full text in the copyright notice at the end of `lua.h`; Copyright (C) 1994-2024 Lua.org, PUC-Rio)
- Used for: the FrameBeam Player plugin runtime (`client/plugins/`), compiled as C++ so that `lua_error` unwinds with C++ exceptions and destructors run.

## Provenance

The intended source is `https://codeload.github.com/lua/lua/tar.gz/refs/tags/v5.4.7`. That host was blocked in the
authoring environment (GitHub access not enabled for the session), so the unmodified Lua 5.4.7 library sources
were taken from the `lua-src` 547.0.0 crate (`https://static.crates.io/crates/lua-src/lua-src-547.0.0.crate`,
directory `lua-5.4.7/`), which bundles the official 5.4.7 sources.

- SHA-256 of the crate actually used: `1edaf29e3517b49b8b746701e5648ccb5785cde1c119062cbabbc5d5cd115e42`
- SHA-256 of the official tarball (`lua.org/ftp` / codeload): not recorded, the host was unreachable.
- Cross-check: all 54 vendored files are byte-identical to the Lua 5.4.7 copy bundled in the `lupa` 2.4 sdist on PyPI
  (`third-party/lua54/`), an independent second source.

## Contents

Only the library sources and headers. Left out: `lua.c`, `luac.c`, `onelua.c`, `ltests.*`, `testes/`, `manual/`, and the
libraries the sandbox never opens (`linit.c`, `liolib.c`, `loslib.c`, `loadlib.c`, `ldblib.c`).
The files are unmodified except for one commented patch in `lstrlib.c` (search "FrameBeam patch"): `match()` calls
`fbMatchBudget(L)` every 1024 steps, so the sandbox can abort pattern matching that runs for seconds inside one C call
(`client/plugins/lua_sandbox.cpp`). Keep the patch when updating Lua.
