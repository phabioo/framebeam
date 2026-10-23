# Design specification

At a glance: the valid design is the Uzume design (from 0.18 on), specified in [uzume/](uzume/README.md); read its design system once, then only the spec of the screen you build. The older FrameBeam design (Rounds 1 to 4) is a legacy reference in [legacy/README.md](legacy/README.md).

## Uzume design (valid from 0.18)

The **Uzume design** ([uzume/README.md](uzume/README.md)) is the valid design from milestone 0.18 on. It supersedes the FrameBeam design Rounds 1 to 4 ([legacy](legacy/README.md)) in look (tokens, type, components, themes Sumi and Kinari) and in every screen it draws: first start, join, accounts, the admin area inside the app, runs, the running game, Multiview, Settings, the plugin screens and the installers. Implementation agents read the design system once and then only the spec of the screen they build:

| File | Content |
|---|---|
| [uzume/README.md](uzume/README.md) | Overview: source, how to read the boards, sheet table, roadmap packages |
| [uzume/design-system.md](uzume/design-system.md) | Tokens for Sumi and Kinari, type, spacing, sizes, focus ring, markers, components, layout rules |
| [uzume/frame.md](uzume/frame.md) | Sheet 1: frame (0-4), first start (8a), join (8b), invite link (8c) |
| [uzume/accounts-and-server.md](uzume/accounts-and-server.md) | Sheet 2: accounts (8d-8g) and the admin screens in the app |
| [uzume/runs-and-server.md](uzume/runs-and-server.md) | Sheet 3: runs, what the server shows, sharing, On this PC, server problems, installers |
| [uzume/play-and-plugins.md](uzume/play-and-plugins.md) | Sheet 4: library and game page, running game, Multiview, Settings › This PC, developer console, SoulLink & Nuzlocke |
| [uzume/cheats.md](uzume/cheats.md) | Family 11: cheats in game, add and import, cheat database, run rule, save markers |
| [uzume/randomizer.md](uzume/randomizer.md) | Sheet 5: game variants, the Randomize dialog, randomized runs, Server › Tools |
| [uzume/decisions.md](uzume/decisions.md) | Decisions, accepted review results and open points |

The handoff (boards and design system) is stored unchanged in `source/uzume/`; the boards need the Claude Design runtime `support.js`, which is not in the repository, so they do not render standalone.

## FrameBeam design (legacy)

The FrameBeam design of Rounds 1 to 4 (Player dark, Hub light) is kept as a reference for code and ADRs that link to it and stays valid only for behaviour the Uzume boards do not cover. Its screen tables, revision lists and reading notes are in [legacy/README.md](legacy/README.md). The Hub part describes the removed web admin.
