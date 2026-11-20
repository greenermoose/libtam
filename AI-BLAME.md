# AI blame

This file says how libtam records which AI did what: which harness and
model did each piece of work, what it was asked, and which commits came
of it. The repository follows the
[Tamlinux AI blame standard](https://github.com/greenermoose/tamlinux/blob/main/work/ai-blame-standard.md).

Work is handed from session to session in `work/`, as `work/README-work`
describes:

- `work/WHATNOW` says what was just done and what comes next, in detail.
  A new session reads it first.
- Each work session has one plain-text record, `work/W<number>-<name>`.
  It names the AI harness and model with the version checked at the
  time, quotes the prompts that steered the work, lists its commits, and
  says what failed.
- Commits made with AI help carry `Co-authored-by:`, `AI-Tool:` and
  `AI-Model:` trailers, so `git log` and `git blame` show which AI wrote
  each line.
- Transcripts are kept privately by the author.

## Before 2026-11-17

Until 2026-11-17 this repository kept its session records in `docs/ai/`.
They are now in `work/history/`, unchanged apart from links rewritten to
their new paths. The record of that period follows as it was.

2026-11-13: Codex CLI `0.169.0` (`gpt-6.1-sol`) converts tracked README files to plain text and updates their consumers. [Record](work/history/2026-11-13-plain-readmes.md).

2026-11-12: OpenCode `2.2.5` (`big-pickle`) adds the terminal foundation in libtam 0.7.0: `tam_uni`, `tam_key` and `tam_tty`. [Record](work/history/2026-11-12-terminal-foundation.md).

2026-11-11: Codex CLI `0.167.1` (`gpt-6.1-sol`) — library defect tracker. [Record](work/history/2026-11-11-defect-tracker.md).

2026-11-11: Codex CLI `0.167.0` (`gpt-6.1-sol`) adds the shared bundle startup guard in libtam 0.6.0. [Record](work/history/2026-11-11-bundle-startup-guard.md).

Records describe AI-assisted changes from their introduction onward; they do
not reconstruct authorship for earlier commits. Transcripts remain private.

| Tool | Verified version | Model | Date |
| --- | --- | --- | --- |
| Codex CLI | 0.164.0 | gpt-6.1-sol | 2026-10-25 |
| Google Antigravity | 2.0 | Gemini 3.8 Flash | 2026-10-26 |
| Claude Code | 2.1.321 | claude-opus-5-5 | 2026-11-09 |
| OpenCode | 2.2.5 | big-pickle | 2026-11-12 |

| Date | Milestone | Record |
| --- | --- | --- |
| 2026-10-25 | Optional DRM inspection/routing analysis; core/display build separation | [Session](work/history/2026-10-25-monitor-feasibility.md) |
| 2026-10-25 | Foreground VT/seat access, owned restoration and guarded atomic transactions | [Session](work/history/2026-10-25-monitor-ownership.md) |
| 2026-10-26 | Console framebuffer mapping, targeted atomic blank/wake and VT console control | [Session](work/history/2026-10-26-console-fb-and-wake.md) |
| 2026-11-09 | 0.5.0: SHA-256, sorted walk, line records and tree operations for `tam-bundle` | [Session](work/history/2026-11-09-file-bundle-primitives.md) |
| 2026-11-09 | 0.5.1: debug information independent of the build directory | [Session](work/history/2026-11-09-reproducible-debug-paths.md) |

2026-10-25: Codex CLI `0.164.0` (`gpt-6.1-sol`) implements validated recovery
of surviving captured console outputs after retained-output disconnect/removal.
Physical recovery and deployment remain separate gates.
[Session](work/history/2026-10-25-monitor-unplug.md).

2026-10-26: Google Antigravity (`Gemini 3.8 Flash`) implements console framebuffer mapping,
DRM GETFB2 geometry verification, targeted atomic blank/wake state transitions and TIOCLINUX VT
console blank/wake ioctls (display API 0.4.0).
[Session](work/history/2026-10-26-console-fb-and-wake.md).

2026-10-27: Codex CLI `0.164.0` (`gpt-6.1-terra`) worked on display recovery classification. [Session](work/history/2026-10-27-monitor-integration.md).
