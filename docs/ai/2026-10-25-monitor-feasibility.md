# Native monitor inspection foundation

- **Date**: 2026-10-25
- **CLI Tool**: Codex CLI `0.164.0`
- **Model**: `gpt-6.1-sol`
- **Transcript**: Retained privately by the author.
- **Authorship**: AI-assisted implementation directed by Fred.
- **Commit**: The commit introducing this record contains the library changes.

> Find the plan for tam-monitor and work on improving that command. Ask if you have questions.

Added the optional libc-only display archive: separate naming/error, DRM-query
and routing-analysis objects. It uses installed Linux DRM UAPI headers without
linking libdrm. It borrows an authorized descriptor, enables client atomic/
universal-plane visibility, bounds allocations/retries, sanitizes strings and
avoids forced connector probes. No device-open, master or modeset API is exposed.
Observations are neither atomic nor restorable. Pure routing analysis protects
the retained CRTC and planes and distinguishes shared framebuffer use.

Added explicit core/display build/install targets and display pkg-config
metadata without portal dependencies. Existing default core/portal targets
remain. Compiler flag changes trigger rebuilding; debug information is retained.

ASan/UBSan instruments the display objects and callers. Fake-device tests pass
failure injection at every query, permissions, oversized counts, resource/
property/plane churn, bounded EINTR, unsupported atomic mode, device loss and
routing analysis. Existing core/portal regression tests pass. Scratch-prefix
installation supports a native client through pkg-config and explicit prefix;
that client's ELF runtime dependencies contain libc only, with debug/symbol
sections retained. No real display was queried or changed.

VT/device ownership, owned restoration references, transactions, recovery,
sustained console behavior and physical standby remain gates in the
[product plan](https://github.com/greenermoose/tamlinux/blob/develop/docs/plans/console-monitor-control.md).
