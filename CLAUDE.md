@AGENTS.md

# Claude Code

## Working rules (Claude)

- Opus only orchestrates and writes no product code; implementation is done by the Sonnet agents in `.claude/agents/`, searching and log reading by the Haiku agent `scout`.
- Opus verifies via `git diff --stat`, a targeted diff and the test result; commit/PR by Opus.

## Pointers

- Claude roles (subagents) are defined in `.claude/agents/`.
