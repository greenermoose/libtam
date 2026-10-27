# AI collaboration and provenance

This repository follows the [Tamlinux AI provenance standard](https://github.com/greenermoose/tamlinux/blob/main/docs/ai-provenance-standard.md).
Records describe AI-assisted changes from their introduction onward; they do
not reconstruct authorship for earlier commits. Transcripts remain private.

| Tool | Verified version | Model | Date |
| --- | --- | --- | --- |
| Codex CLI | 0.164.0 | gpt-6.1-sol | 2026-10-25 |
| Google Antigravity | 2.0 | Gemini 3.8 Flash | 2026-10-26 |

| Date | Milestone | Record |
| --- | --- | --- |
| 2026-10-25 | Optional DRM inspection/routing analysis; core/display build separation | [Session](docs/ai/2026-10-25-monitor-feasibility.md) |
| 2026-10-25 | Foreground VT/seat access, owned restoration and guarded atomic transactions | [Session](docs/ai/2026-10-25-monitor-ownership.md) |
| 2026-10-26 | Console framebuffer mapping, targeted atomic blank/wake and VT console control | [Session](docs/ai/2026-10-26-console-fb-and-wake.md) |

2026-10-25: Codex CLI `0.164.0` (`gpt-6.1-sol`) implements validated recovery
of surviving captured console outputs after retained-output disconnect/removal.
Physical recovery and deployment remain separate gates.
[Session](docs/ai/2026-10-25-monitor-unplug.md).

2026-10-26: Google Antigravity (`Gemini 3.8 Flash`) implements console framebuffer mapping,
DRM GETFB2 geometry verification, targeted atomic blank/wake state transitions and TIOCLINUX VT
console blank/wake ioctls (display API 0.4.0).
[Session](docs/ai/2026-10-26-console-fb-and-wake.md).

2026-10-27: Codex CLI `0.164.0` (`gpt-6.1-terra`) worked on display recovery classification. [Session](docs/ai/2026-10-27-monitor-integration.md).
