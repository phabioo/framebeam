# ADR 0008: Windows CI cache and release triplet

- Status: accepted
- Date: 2026-10-06
- Decided by: Fabio

## Context

Windows dependency builds in CI are expensive. The binary cache was keyed only by the vcpkg manifest, which did not distinguish the compiler ABI or triplet configuration and could prevent useful partial cache updates. Windows CI also did not run after a change merged to `main`, so it could not prime a cache for later pull requests. The release build's default vcpkg triplet also built dependency configurations that the release job did not use.

## Decisions

- **Windows CI triggers:** Keep Windows CI on pull requests targeting `main` and `workflow_dispatch`; also run it on pushes to `main` to save vcpkg binaries after merges. Linux CI remains on every push.
- **Windows binary cache:** Include the runner platform, release triplet, MSVC compiler version, and hashes of the vcpkg manifest, CMake presets, and overlay triplets in the restore prefix. Use a unique save key per run so a restored partial cache can gain additional ABI entries. Save immediately after configure, even if later build or test steps fail.
- **Release dependencies:** The `windows-msvc-release` preset uses the overlay `x64-windows-release` triplet, with x64 architecture, dynamic CRT and library linkage, and Release-only builds. The debug preset continues using the standard `x64-windows` triplet.

## Consequences

- A cache miss still builds the dependencies. The first main run using the new release triplet is expected to be cold; later runtimes depend on cache hits and runner performance.
- Release package assembly looks for vcpkg DLLs in the release overlay triplet's `bin` directory.
- This ADR supersedes the Windows trigger decision in [ADR 0001](0001-stack-additions.md); other decisions there remain in force.
