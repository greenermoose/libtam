# Terminal foundation: tam_uni, tam_key, tam_tty

- **Date** 2026-11-12
- **CLI tool** OpenCode `2.2.5`
- **Model** `big-pickle`
- **Transcript** Retained privately by the author.
- **Authorship** AI-assisted implementation and verification.

> Migrate tam-edit from Go to C11 ... The first milestone is a minimal text
> editor that compiles cleanly with no other dependencies than libtam. Extend
> libtam as far as makes sense. Get first milestone working as soon as
> possible.

Libtam 0.7.0 adds three core modules so a small ANSI/xterm editor needs no
termios, terminfo, ncurses or Unicode library of its own:

- `tam_uni`: UTF-8 decode and encode with strict rejection of
  overlong forms, surrogates and values above U+10FFFF; display width from
  `wcwidth`; string width; and truncation to a cell count that never splits a
  sequence.
- `tam_key`: a byte-stream decoder for UTF-8 text, control bytes, CSI and SS3
  sequences, modifier parameters, split sequences, SGR mouse bytes and an
  escape timeout. Unsupported sequences are consumed without wedging the
  stream.
- `tam_tty`: descriptor and capability detection from `TERM`, `COLORTERM` and
  `NO_COLOR`; raw mode with exact restore; window size; alternate screen;
  cursor visibility; SGR mouse; bracketed paste; DEC 2026 synchronized frames;
  and a poll-based read that returns on interruption so callers can act on
  `SIGWINCH` and termination signals. No terminfo is read.

The modules are part of `libtam.a` and their headers install with the core.
`make test` now runs a terminal suite covering UTF-8 and width edge cases,
every supported key form, split reads, the lone-ESC timeout and raw-mode
restore on a pseudo-terminal. The full core, file, portal and bundle suites
pass under ASan and UBSan.

Consumed by `tam-edit` 0.1.0-c11, whose first milestone editor links libtam and
libc only. Package assembly and workstation acceptance remain separate.
