# libtam roadmap

`libtam` is Tamlinux's static C17 foundation library (`libtam.a` core on
libc only, plus `libtam-render.a`, `libtam-portal.a`, and optional
experimental display archives).

- **Current version:** `0.15.1` (branch `dev`)
- **Progress:** 2 of 3 phases done (P2 and P1 done; P3 deferred on Fred's word)
- **Versioning:** Every code change bumps `VERSION` in the same commit:
  minor for a new module or function, patch for a fix. Consumers pin an
  exact `libtam` version.

## Phase summary

| Phase | Versions | Goal | Plan | Status |
| --- | --- | --- | --- | --- |
| **P2** | 0.1.0–0.8.0 | Core foundation, static archives, tree/hash/record primitives, terminal stack, cultivar helpers | [P2-core-foundation.md](plans/P2-core-foundation.md) | Done |
| **P1** | 0.9.2–0.15.1 | Tamlinux 0.7 refactor: core fixes, shared primitives, `libtam-render.a`, C17, consumer migrations | [P1-0.7-refactor.md](plans/P1-0.7-refactor.md) | Done (2026-11-26) |
| **P3** | 0.16.0+ | Queued requests: kitty keyboard decoding, shared Wayland/evdev, display hardware gates | [P3-queued-requests.md](plans/P3-queued-requests.md) | Deferred |

## Phases and milestones

### Phase P2: Core foundation (0.1.0–0.8.0) — [P2-core-foundation.md](plans/P2-core-foundation.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **0.1.0–0.3.0** | Core foundation (`tam_str`, `tam_exec`, `tam_fs`, `tam_path`, `tam_ini`, `tam_term`) and static `libtam-portal.a` | Done |
| **0.4.0** | Experimental `libtam-display.a` (DRM/KMS, fbcon, `tam_vt`) and `libtam-display-session.a` | Done |
| **0.5.0–0.5.1** | `tam_sha256`, `tam_walk`, `tam_rec`, `tam_fs_tree`, and reproducible debug paths | Done |
| **0.6.0** | Shared startup guard and repository-local defect tracking | Done |
| **0.7.0–0.7.1** | Terminal foundation (`tam_uni`, `tam_key`, `tam_tty`), `VERSION` file, plain-text READMEs | Done |
| **0.8.0** | Retire `tam_bundle_guard`; add `tam_cultivar` helpers | Done |

### Phase P1: The 0.7 refactor (0.9.2–0.15.1) — [P1-0.7-refactor.md](plans/P1-0.7-refactor.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **0.9.2** | `tam_pager` hardening and header comment fixes | Done (2026-11-26) |
| **0.10.0** | Breaking core semantics fixed in place (`tam_str`, `tam_fs`, `tam_key_feed`, `tam_exec`, error convention) with callers migrated | Done (2026-11-26) |
| **0.11.0** | Shared process, I/O, socket, time, base64, and locale primitives | Done (2026-11-26) |
| **0.12.0** | Terminal suspend/resume, crash restore, paste/mouse events, key names | Done (2026-11-26) |
| **0.13.0** | `libtam-render.a` sharing `tam-view`'s Markdown/HTML/Mermaid renderer | Done (2026-11-26) |
| **0.14.0** | Process options (stdin, output, kept fd, env) and secret-safe socket line reader | Done (2026-11-26) |
| **0.14.1** | `tam_tty_job_stop` pauses a session leader (`SIGSTOP` on orphaned process group) | Done (2026-11-26) |
| **0.14.2** | `tam_exec` waits on a `pidfd` instead of 5 ms polling | Done (2026-11-26) |
| **0.15.0** | `tam.h` sets no feature macros; `tests/check-headers.sh` compiles each public header standalone | Done (2026-11-26) |
| **0.15.1** | Build `libtam` explicitly as C17 (`-std=c17`); all consumer migrations complete | Done (2026-11-26) |

### Phase P3: Queued requests (0.16.0+) — [P3-queued-requests.md](plans/P3-queued-requests.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **TBD** | Kitty keyboard protocol decoding in `tam_key` (CSI u, release events, flags query) | Deferred |
| **TBD** | Shared Wayland wire and evdev keymaps for `tam-pane` and `tam-last` (not before Tamlinux 0.7.0) | Deferred |
| **TBD** | Console display hardware gates (`docs/display.md`) requiring Fred at the hardware | Deferred |

## How a milestone is done

A milestone is done when its acceptance checks in the phase plan pass
(`make test` under ASan/UBSan and `tests/check-headers.sh`) and Fred has
accepted it. Bump `VERSION`, update the phase plan, and update this
roadmap in the same change.
