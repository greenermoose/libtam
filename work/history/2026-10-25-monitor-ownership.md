# Native console monitor ownership and guarded transactions

- **Date**: 2026-10-25
- **CLI Tool**: Codex CLI `0.164.0`
- **Model**: `gpt-6.1-sol`
- **Transcript**: Retained privately by the author.
- **Authorship**: AI-assisted implementation directed by Fred.
- **Commit**: The commit introducing this record contains this repository's changes.

## Guiding prompts

> Continue improving tam-monitor using the approved plan at tamlinux/docs/plans/console-monitor-control.md. The native C inspection library and CLI are implemented and tested. Next, work on authorized foreground VT/device access and owned restoration, then guarded atomic display transactions. Keep the installed command working. Do not deploy or move stages without my instruction. Leave my unfinished tam command/corpus work untouched. Ask if you have questions.

> Does it seem the C version uses less memory and is more capable than the python or shell version of tam-monitor? Does writing in C seem like a good strategy for this type of command?

## Result

Implemented foreground VT verification, an optional direct-device/session-observation
adapter, owned framebuffer/blob restoration and guarded single-device atomic
transactions. The optional display API is 0.2.0; core and portal APIs/versions
are unchanged. The product command adds an uninstalled bounded C `preview`
prototype; the installed Python command and its install target remain intact.

The recovery process owns its VT_PROCESS handoff, session-observation connection,
duplicated DRM descriptor and retained state. It stays in the controlling
terminal session with a separate process group, preserving KD_TEXT and keyboard
mode. Existing seat permissions authorize the direct card open; occupied DRM
master and wrong-seat devices are refused. A private readiness handshake, monotonic deadline, signals,
UI death, session events and kernel hotplug notifications govern its lifetime.
Authorization, owner liveness, deadline and hotplug are rechecked immediately
before actual writes. Every selection and restore request passes TEST_ONLY;
uncertain interrupted commits are restored rather than retried. Shared CRTC
routing preserves the chosen connector and its planes. Capture proves that
owned blobs and cloned framebuffer IDs form a kernel-accepted restore request;
release uses non-destructive CLOSEFB, never RMFB on active scanout.

The evaluated logind takeover was rejected after reviewing
[session_set_controller](https://github.com/systemd/systemd/blob/v262/src/login/logind-session.c#L1493)
and [vt_restore](https://github.com/systemd/systemd/blob/v262/src/basic/terminal-util.c#L1654):
TakeControl mutes the keyboard and enters graphics mode; release resets tty
ownership. The final adapter invokes neither TakeControl nor TakeDevice.
This uses the direct-device option already allowed by the approved plan.
Physical access and owned VT restoration still require package Test evidence.

## Verification

- `make test-display test-session` passes with ASan/UBSan on library objects.
  Fake devices cover reference retention/cleanup, shared framebuffer/CRTC
  routing, missing authorization, stale state, kernel test/write rejection,
  uncertain commits, failed restore, bounds, VT ownership and session events.
- Nine native CLI tests, sixteen real-process recovery tests, kernel event
  parsing checks and the existing 25 Python monitor tests pass. Recovery tests
  cover UI SIGKILL/SIGSTOP, guard SIGKILL/SIGSTOP/termination, signals, expiry
  during preparation, failed commits/restores, hotplug, VT handoff and session loss.
  Fake interfaces contact neither DRM hardware nor a real session bus. Tests
  needing private socket sends and LeakSanitizer run outside the Codex sandbox.
- Explicit-prefix and pkg-config builds work; no-op builds stay unchanged;
  either archive, headers and flags trigger rebuild/relink. Missing/old
  development metadata fails with a useful message. Separate display/session
  install targets provide headers, archives and pkg-config data without portal.
- GCC static analysis reports no diagnostics in the new access, restoration,
  session, transaction and event/recovery sources. Debug information and
  symbol tables remain present. The full prototype directly needs libc and
  libsystemd; this machine also resolves libgcc_s. A display-only consumer
  links with libc only, without those optional adapter dependencies.
- The actual installed Python command returns valid JSON for
  `list --json --no-probe`. No installed endpoint or script was changed.

The final local unstripped build is 174,296 bytes; its debug
sections total 113,868 bytes. The display and session archives are
135,336 and
36,010 bytes. These are
local development artifacts, not a package closure or live-memory measurement.

## Startup measurement and strategy

Seven warm `--help` process launches per implementation, measured on the same
machine with a small C fork/exec/wait4 runner and CLOCK_MONOTONIC, gave:

| Implementation | Median peak RSS | Median elapsed |
| --- | ---: | ---: |
| Installed Python | 21,700 KiB | 80.328 ms |
| C prototype | 2,108 KiB | 1.035 ms |

The runner source is kept in
[tam-monitor tests](https://github.com/greenermoose/tamlinux/blob/develop/commands/tam-monitor/tests/startup-measure.c).
This is about 90% lower startup peak RSS. It measures help/startup only, not
hardware enumeration or an active owner/guard pair. It is not cold-cache data,
combined proportional memory, closure size, energy or a constrained-machine
benchmark. No equivalent shell implementation was measured. C fits the direct
kernel APIs and explicit resource lifetime, but increases memory-safety and
cleanup obligations; sanitizer and failure tests remain required. The Python
version still has the complete installed TUI, brightness and idle controls.

## Open gates

Actual console framebuffer mapping, three-monitor fbcon rendering and physical
standby remain unproved. Unexpected hotplug identities fail closed; recovery
choosing another available output after retained-output unplug is not yet
implemented. Multi-GPU compensation, individual wake, sustained `keep --shell`,
UI parity and resource acceptance remain open. The proposed combined 4 MiB
proportional-memory budget has not been tested with the real adapter/snapshots.
The kernel fallback after guard death (or both processes dying) remains
unknown; a userspace timer cannot bound a hung kernel ioctl. Session/device loss can prevent restoration and must be reported as such.

No live display mutation, candidate preparation, package pin change,
deployment, service installation, stage movement or tam command/corpus edit
occurred. Physical work must follow the recorded package route on Fred's
separate instruction. The approved product plan records the next actions.
