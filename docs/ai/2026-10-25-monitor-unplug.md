# Owned console-output unplug recovery

- **Date**: 2026-10-25
- **CLI Tool**: Codex CLI `0.164.0`
- **Model**: `gpt-6.1-sol`
- **Transcript**: Retained privately by the author.
- **Authorship**: AI-assisted implementation directed by Fred.
- **Commit**: The commit introducing this record contains this repository's changes.

## Guiding prompt

> Continue tam-monitor from tamlinux/docs/plans/console-monitor-control.md. Implement retained-output unplug recovery next. Preserve the installed command and my unfinished tam work. Do not deploy or move stages.

## Implementation

The optional display API is 0.3.0; core and portal APIs are unchanged. Owned
restoration now accepts captured connectors disconnecting or disappearing
from enumeration on the retained device. It restores surviving captured
outputs with their retained framebuffer backing and owned mode/property blobs.
Disconnected connectors are detached, vanished connector assignments are
omitted, and CRTCs/planes without surviving baseline routes are disabled.
Shared CRTCs are preserved or re-enabled for surviving captured outputs.

Validation permits EDID disappearance only on a disconnected output and narrow
kernel zeroing of a pipeline whose sole selected route was lost. An output
that was deliberately detached can therefore recover on that same shared
pipeline. An unrelated still-retained pipeline cannot be silently disabled.
New connectors, unknown connection state, changed nonzero identities, foreign
routing, replacement CRTC/plane objects and revoked devices still refuse replay.
If no captured working output survives, no recovery modeset is issued; the
owner releases control and reports restoration failure.

A complete recovery request passes atomic TEST_ONLY. A second observation
must match the tested state, then the authorization callback runs immediately
before the sole write. Another unplug/replug, changed IDs or lost authorization
prevents the write. Recovery has no retry loop or periodic output reapplication.
Hotplug ends the bounded trial even after successful recovery; the command
continues to report that interruption rather than silently resuming selection.

## Verification

- ASan/UBSan instrumented library objects pass `make test-display test-session`.
  The owned-state fixtures exercise every retained choice and nonempty loss
  subset for both disconnected and removed connectors, shared CRTCs and shared
  framebuffers, kernel detachment/disablement, no remaining output, identities,
  foreign routing, test/write failures, uncertain writes and validation races.
- Nine native CLI checks, eighteen recovery-process checks, kernel-event parser
  checks and all 25 existing Python monitor tests pass. Process checks include
  no-survivor and failed-hotplug-recovery release paths. Private socket tests
  and LeakSanitizer run outside the Codex sandbox; six existing Python guard
  tests failed before their simulated action inside it and pass outside it.
- The command builds with explicit-prefix and pkg-config discovery of matching
  0.3.0 display/session development metadata. Older metadata is rejected.
  GCC static analysis reports no diagnostics in the changed transaction source.
- The installed command returns valid `list --json --no-probe` metadata.
  The Python command and production install target remain unchanged. No live
  display mutations, deployment, candidate preparation or stage moves occurred.
  The separate `tam` repository was not edited.

## Remaining acceptance

Fixture success is not physical fbcon, standby or unplug-recovery proof. The
actual console framebuffer mapping, real session/device handoff, kernel
fallback after guard death, resource budget, multi-GPU compensation and
sustained work mode remain open. Drivers that change CRTC/plane identities or
unrelated routing during hotplug remain unsupported by this recovery path.
The final enumeration and synchronous ioctl cannot exclude a hardware change
inside that interval; kernel rejection is reported and is not retried.
