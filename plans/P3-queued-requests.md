# P3: Queued requests (on Fred's word)

- **Status:** not started (deferred until Fred chooses what comes next)
- **Versions:** 0.16.0+ (unassigned)

## Goal

Track candidate `libtam` additions requested by Tamlinux programs that
wait for Fred's direction before work begins.

## Versions

- **Kitty keyboard protocol in `tam_key`:** CSI u decoding, release
  events, and flags query so Tamlinux terminal programs can receive every
  key cleanly from `tam-last`.
- **Shared Wayland wire and evdev keymaps:** Candidate shared module for
  `tam-pane` (server side) and `tam-last` (client side) if Fred places it
  in `libtam` rather than each program. Not before Tamlinux 0.7.0 (Fred,
  2026-11-25).
- **Console display hardware gates (`docs/display.md`):** Physical
  multi-monitor trial and physical fbcon/VT/desktop handoff requiring
  Fred at the hardware.

## Acceptance

Defined per item when Fred authorizes it; every new module must pass
`make test` (ASan/UBSan) and `tests/check-headers.sh`.

## Dependencies

Core additions must keep `libtam.a` libc-only; anything needing external
libraries goes in its own archive.

## Risks

Prematurely extracting shared wire/keymap code before `tam-pane` and
`tam-last` stabilize their interfaces.

## Open points

- Which queued item (if any) Fred wants started next.
