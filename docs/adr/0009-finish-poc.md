# ADR 0009: Finish the PoC (0.1.1)

- Status: proposed
- Date: 2026-10-06
- Decided by: Fabio (proposal by the orchestrator; accepted with the PR)

## Context

The PoC left open points in ADR 0002, 0003, 0006 and 0007 and in the architecture: a changed Hub certificate blocked the connection permanently, hardware encoders were not in the Windows FFmpeg build although ADR 0006 said "compiled in", encoding ran on the UI thread, RTT showed "n/a", the multiview picker pointed to the library with "← Library" ending the game, and Hubs could only be managed on the connection screen. This ADR records the decisions of milestone 0.1.1 ([Roadmap](../roadmap.md)).

## Decisions

### D1 Confirmed pin change (Player)

- A changed leaf fingerprint still blocks the connection and sends no credential (ADR 0003).
- The Hub card shows the stored and the presented fingerprint. "Trust new certificate" followed by "Yes, trust this certificate" (two steps) replaces the stored pin with the presented fingerprint only and keeps the credential. The user compares the fingerprint with the Hub's log or Settings page.
- A further, different certificate is a mismatch again. No automatic re-pin, no pin list.
- CLI: `--accept-fingerprint <sha256>` pins the given fingerprint if it is the one presented.

### D2 Self-signed certificate renewal (Hub)

- At startup the Hub renews its self-generated certificate when it is expired or expires within 30 days. New certificate: ECDSA P-256, valid 10 years. The previous pair is kept as `cert.pem.prev` / `key.pem.prev`.
- `framebeam-hub renew-cert [-data-dir ...]` does the same on demand. It refuses when the Hub is configured with its own certificate and key.
- An own certificate and key (`-tls-cert`/`-tls-key`) are never modified.
- Settings page: badge "Expires within 30 days" with a hint.
- Renewal changes the fingerprint, so Players need the confirmation from D1.

### D3 Encoding worker thread and drop policy (Player)

- Session encoding and RTP sending run on a worker thread. The UI thread only enqueues frames.
- Bounded queue: at most 2 pending video frames; when full, the oldest is dropped and counted as a dropped frame. Audio is queued and processed in order.
- Encoder preference and parameters stay as in ADR 0006 D5.

### D4 RTT via a negotiated DataChannel

- Host and viewer open a negotiated DataChannel `fb-diag` (id 0), so SCTP is up and libdatachannel reports RTT, shown in diagnostics.
- The channel carries no application data; the signaling protocol is unchanged (no new messages, `protocol_version` stays 1).

### D5 Multiview picker

- The picker lists all Sessions in a scrollable list. The "+N more in the Library" pointer is removed, so "← Library" no longer ends the game by way of that pointer.
- Multiview stays local plus one remote Session (ADR 0006 D6); more remote Sessions are planned for 0.4 (Sessions over the internet; numbered 0.6 until 2026-10-07).

### D6 Settings → Hubs

- Settings has a Hubs section: switch, remove (with confirmation), auto-connect, current Hub marked.
- Switching ends running work and secures saves like the connection screen (saves stay per Hub, ADR 0005).

### D7 Hardware encoder features (Windows)

- `client/vcpkg.json` enables the vcpkg `ffmpeg` features `nvcodec`, `qsv` and `amf`.
- A Windows-only test checks that `h264_nvenc`, `h264_qsv` and `h264_amf` are compiled in. Encoder selection and fallback stay as in ADR 0006 D5.

### D8 Unchanged and cleanup

- A core version mismatch still only warns (ADR 0007); re-evaluated in 0.7 with a second core.
- The system display name and controller labels come from the system manifest (`nds.json`: `core_display_name`, `labels.input`/`labels.touch`) instead of hardcoded NDS texts.
- Phase-named files, tests and comments are renamed (`httpapi/phase5.go` becomes `invites.go`, `uploads.go`, `systems.go`). Migration `0004_phase5.sql` keeps its name, because applied migrations are identified by it.
- The `PlayerController` split moves to the Player UI pass (0.5; numbered 0.4 until 2026-10-07).

## Rejected

- Automatic pin replacement on a changed certificate: would defeat pinning.
- Renewing or replacing an own certificate: it is the operator's.
- Encoding on a thread per viewer: encoding happens once per Session (ADR 0006 D5).
- Unbounded frame queue: adds latency; dropping the oldest frame keeps the Session current.
- RTT over the signaling channel or the Hub: media and its measurements stay between Players.

## Consequences

- Open and verified only locally by Fabio: opening `h264_nvenc` / `h264_qsv` / `h264_amf` needs a GPU; the Windows test only proves they are compiled in. Likewise the certificate flow against a real Hub certificate, and the encoding thread with the real core on Windows.
- Windows vcpkg builds of FFmpeg grow (more features); cold builds take longer until cached (ADR 0008).
- Host and viewer must both run 0.1.1 or later; mixing with an older Player is not tested.
- Dropped frames appear in diagnostics under load.

## Update 2026-10-08 (milestone numbering)

Milestone numbers above 0.4 in this ADR use the numbering from before the roadmap renumbering of 2026-10-07: old 0.5/0.6/0.7/0.8/0.9/0.10 are now 0.6 (Player UI pass) / 0.7 (Hub UI pass) / 0.8 (3DS) / 0.9 (metadata) / 0.10 (Hub for Windows) / 0.11 (Linux and macOS Player); 0.5 is now OpenGL hardware rendering. See [roadmap.md](../roadmap.md).
