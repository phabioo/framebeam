# ADR 0013: OpenGL hardware rendering (0.5)

- Status: accepted
- Date: 2026-10-07
- Decided by: Fabio (proposal by the orchestrator, accepted on 2026-10-07 with the merge of PR #35)

## Context

Roadmap 0.5 (`docs/roadmap.md`) gives the Player an OpenGL context for libretro cores. Until now the libretro backend handles only software framebuffers and answers `RETRO_ENVIRONMENT_SET_HW_RENDER` with false. Cores that need hardware rendering (Azahar/3DS in 0.8, later N64 and GameCube/Wii) cannot run, and melonDS DS has its OpenGL renderer and internal resolution option locked (`docs/design/player.md`, Emulation page). The Player's frame path is: core frame as XRGB8888 in CPU memory, then `GameView`, multiview and the Session encoder (ADR 0006).

## Decisions

### D1 OpenGL context

- One offscreen OpenGL context per running game, owned by the emulation thread (`QOpenGLContext` plus `QOffscreenSurface`; the surface is created on the GUI thread). The context is not shared with Qt Quick.
- `RETRO_HW_CONTEXT_OPENGL_CORE` requests get a core profile at the requested version. 3.3 is the tested minimum target; Azahar/3DS in 0.8 needs 3.3. `RETRO_HW_CONTEXT_OPENGL` (compatibility) gets a default/compatibility context.
- GLES, Vulkan and Direct3D requests are rejected so that cores fall back to their software path. `GET_PREFERRED_HW_RENDER` answers OpenGL Core.

### D2 Framebuffer and callbacks

- The Player owns the framebuffer: an RGBA8 color texture and, when the core requests it, a depth/stencil renderbuffer. It is sized to the core's maximum geometry and resized on geometry and AV info changes.
- `get_current_framebuffer` returns that FBO; `get_proc_address` resolves through Qt.
- `context_reset` is called after `load_game`, `context_destroy` before unload.

### D3 Presentation and Sessions

- Every hardware frame is read back (`glReadPixels`) into the existing XRGB8888 frame, flipped for the bottom-left origin. `GameView`, multiview and the Session encoder stay unchanged, so shared Sessions keep working.
- Deviation from the roadmap wording "present without a CPU copy": a zero-copy path into Qt Quick is deferred. Qt Quick renders with Direct3D 11 on Windows by default, so sharing a GL texture would mean forcing the OpenGL RHI or GL/D3D interop. The software H.264 encoders need a CPU frame anyway.
- Fabio agreed to deferring zero-copy on 2026-10-07.
- Revisit with asynchronous PBO readback or zero-copy if profiling shows that the readback costs frame time. Example: melonDS DS at 4x internal resolution is 1024x1536 x 4 B, about 6 MiB per frame.
- Update 2026-10-08 (0.7.x): measured 12.4 ms per synchronous readback at 8x (2048x3072) on an RTX 4080, which starved audio and capped speed-up ([ADR 0018](0018-player-speed-up.md)). The readback now uses double-buffered PBOs (one frame of latency, synchronous fallback, `FRAMEBEAM_SYNC_READBACK=1` forces it), and frames that are not shown during speed-up are not read back; the core then gets `GET_AUDIO_VIDEO_ENABLE` without the video bit.

### D4 Fallback

- Hardware rendering is unavailable (`SET_HW_RENDER` answers false, cores use their software path) when there is no `QGuiApplication` (the CLI), when `FRAMEBEAM_DISABLE_HW_RENDER=1` is set, or when context or FBO creation fails.
- The outcome and the GL vendor, renderer and version are logged.

### D5 melonDS DS

- The render mode is no longer locked by the manifest; the default stays Software.
- "Renderer" and "Internal resolution" appear on the Emulation page from the core's own options and apply on the next start.
- Screen layout and OpenGL filtering stay locked.
- The core decides which option values exist and are shown; the Player does not filter them (Fabio, 2026-10-07). melonDS DS v1.4.0 offers 1x to 8x internal resolution (`melonds_opengl_resolution`) and a "compute" renderer that needs GL 4.3.

### D6 Tests and CI

- A fake hardware-render core verifies context lifecycle, readback and orientation.
- Linux CI runs it with Mesa llvmpipe under xvfb. Without a GL 3.3 context (Windows CI runners) the GL tests are skipped.
- melonDS DS OpenGL output is checked by a `NEEDS_CORE` test.
- Real GPUs, drivers and frame time are verified locally by Fabio.

## Not in scope

- Vulkan (later, per roadmap), GLES.
- Shaders and post-processing.
- Zero-copy presentation (see D3).

## Rejected

- Context shared with the Qt Quick scene: ties core rendering to the GUI thread and to the RHI backend, which is Direct3D 11 on Windows.
- Reading back only for shared Sessions: two presentation paths, and `GameView` would need a GL texture path for the local case; deferred with the zero-copy work.

## Consequences

- One readback per frame costs bandwidth and some frame time; accepted for 0.5, measured later (D3).
- Cores with hardware rendering run locally and in shared Sessions without changes to the viewer path.
- The CLI and headless hosts never get hardware rendering; cores must keep a software path or be unavailable there.
- Windows CI cannot run GL tests; Windows behavior is verified locally.
