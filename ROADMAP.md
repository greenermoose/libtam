# libtam roadmap

`libtam` is the Tamlinux C foundation library. Every code change bumps
`VERSION` in the same commit: minor for a new module or function, patch
for a fix. Consumers build and test against a pinned version of
`libtam`.

## Done

- **0.1.0 – 0.3.0: core foundation and static archives.** `tam_str`,
  `tam_exec`, `tam_fs`, `tam_path`, `tam_ini`, `tam_term`, and the
  separate `libtam-portal.a` archive for D-Bus desktop portal access.
  Switched to static-only archives so core consumers need only libc.
- **0.4.0: experimental console display control.** Optional
  `libtam-display.a` (DRM/KMS inspection, console framebuffer mapping,
  owned restoration, targeted atomic blank/wake, `tam_vt`) and
  `libtam-display-session.a` (libsystemd observer and seat validation),
  kept separate from the core and portal archives.
- **0.5.0 – 0.5.1: tree, hash and record primitives.** `tam_sha256`,
  `tam_walk`, `tam_rec`, and `tam_fs_tree`, plus reproducible debug
  paths independent of the build directory.
- **0.6.0: startup guard and defect tracking.** Shared startup guard
  and repository-local defect tracking.
- **0.7.0 – 0.7.1: terminal foundation and plain-text READMEs.**
  `tam_uni`, `tam_key` and `tam_tty` for native terminal programs,
  `VERSION` as the version's source of truth, and plain-text READMEs.
- **0.8.0: cultivar helpers.** Retired `tam_bundle_guard` and added
  `tam_cultivar` (`tam_install_pending`, `tam_cultivar_read_version`).

## Planned: the 0.7 refactor ([P1](plans/P1-0.7-refactor.md))

Approved by Fred on 2026-11-25, from [review R2](reviews/R2-whole-suite-consumers.md).
Each version lands on his acceptance.

- **0.9.2 (done 2026-11-26):** version bump for the `tam_pager`
  hardening, and stale comments corrected.
- **0.10.0 (done 2026-11-26):** core semantics fixed in place, with every caller migrated
  (`tam_str`, `tam_fs`, `tam_key_feed`, `tam_exec`, one error
  convention).
- **0.11.0 (done 2026-11-26):** process, I/O, socket, time, base64 and
  locale primitives.
- **0.12.0 (done 2026-11-26):** terminal suspend/resume, crash restore,
  paste and mouse events, key names.
- **0.13.0 (done 2026-11-26):** `libtam-render.a`, holding tam-view's
  renderer.
- **0.14.0 (done 2026-11-26):** process options (stdin, kept output, a
  kept descriptor, environment) and a socket reader safe for secrets.
- **0.14.1 (done 2026-11-26):** `tam_tty_job_stop` pauses a session
  leader too (SIGSTOP when the process group is orphaned).
- **0.14.2 (done 2026-11-26):** `tam_exec` sleeps on a pidfd until the
  program exits, instead of checking every 5 ms.
- **Consumer migrations (done 2026-11-26):** every program that links
  libtam moved onto it, one commit each.

## Queued requests (on Fred's word)

These are known requests waiting for Fred to choose what comes next:

- **Kitty keyboard protocol in `tam_key`.** CSI u decoding, release
  events, and flags query so Tamlinux terminal programs can receive
  every key cleanly from `tam-last`.
- **Shared Wayland wire and evdev keymaps.** Candidate shared module
  for `tam-pane` (server side) and `tam-last` (client side), if Fred
  chooses to place it in `libtam` rather than in each program. Not
  before Tamlinux 0.7.0 (Fred, 2026-11-25).
- **Console display hardware gates (`docs/display.md`).** Physical
  multi-monitor trial and physical fbcon, VT and desktop handoff,
  requiring Fred at the hardware.
