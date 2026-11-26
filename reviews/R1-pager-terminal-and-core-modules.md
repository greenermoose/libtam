# R1 — Pager helpers, terminal and core modules

- **Date:** 2026-11-22
- **Reviewer:** agy 1.5.14, Claude Opus 5.5 (High)
- **Revision:** `72318d09d25ec859e8a275d906dee9cd4250fe6d` (0.9.0). The
  next commit, `930aa50`, changes only the install prefix.
- **Scope:** `tam_pager` (new in 0.9.0), `tam_uni`, `tam_tty` raw mode and
  writes, `tam_exec`, `tam_str`, `tam_path`, `tam_cultivar`, and the
  library concerns raised in tam's dependency assessment. Done alongside
  tam-view's R1, which consumes `tam_pager`.

## Summary

The core is in good shape: `make test` passes under ASan and UBSan.
`tam_exec` uses one monotonic deadline, and `tam_xdg_path` ignores
relative `XDG_*` values. The new `tam_pager` module needs the most
attention. Its external opener cannot report failure (tam-view B3).
`file://` URIs with a remote host resolve as local relative paths. The
OSC 8 check lets non-ASCII bytes through. None of the findings is a
memory-safety defect.

## Findings

### 1. The external opener cannot report failure (medium)

- **Where:** [tam_uri_open_external](../src/tam_pager.c#L368-L400)
- **What:** The function double-forks and returns 0 once the intermediate
  child exits, whether or not `xdg-open` ran. The opener stays in the
  caller's session and process group, and inherits every descriptor the
  caller left open without close-on-exec.
- **Evidence:** Source inspection. In tam-view, a missing `xdg-open` is
  reported as "Opened:" (tam-view B3).
- **Fix:** Pass the exec result back through a close-on-exec pipe, call
  `setsid()` in the grandchild, and document that callers must confirm.
- **Status:** mostly fixed in `ec98a07` and `a3e0901` (report pipe,
  `setsid`, signal reset). The opener still inherits the caller's
  descriptors that lack close-on-exec; P1 0.10.0.

### 2. `file://host/path` resolves as a relative path (low)

- **Where:** [tam_uri_resolve_local](../src/tam_pager.c#L302-L310)
- **What:** Only an empty host or `localhost` is stripped. Any other host
  becomes the first part of a relative path.
- **Evidence:** With base `/home/u/docs/index.md`,
  `file://example.com/etc/hosts` gives `/home/u/docs/example.com/etc/hosts`
  and success.
- **Fix:** Refuse a non-local host.
- **Status:** fixed in `a3e0901`

### 3. The fragment contract is unstated, and long paths truncate silently (low)

- **Where:** [tam_uri_resolve_local](../src/tam_pager.c#L292-L318)
- **What:** The function returns the fragment already percent-decoded, but
  the header does not say so. tam-view decodes it again (tam-view R1,
  finding 5). A decoded path longer than the 4 KiB work buffer is cut
  short, and the call still succeeds.
- **Evidence:** Source inspection, plus a direct call with
  `other.md#a%2520b`, which returns `a%20b`.
- **Fix:** Document the decoded fragment, and fail on truncation.
- **Status:** fixed in `a3e0901`

### 4. The OSC 8 URL check accepts non-ASCII bytes (low)

- **Where:** [tam_osc8_url_safe](../src/tam_pager.c#L27-L40)
- **What:** Only C0 and DEL are refused, so a URL can carry UTF-8 C1
  controls such as U+009B (CSI). The OSC 8 convention limits URIs to bytes
  32–126. Whether a terminal acts on them varies.
- **Evidence:** `tam_osc8_url_safe("http://a\xC2\x9B" "31mX", 0)` returns
  true.
- **Fix:** Accept only bytes 0x20–0x7E, or percent-encode the rest.
- **Status:** fixed in `a3e0901` (only 0x20-0x7E accepted); the
  `tam_pager.h` comment is still stale, P1 0.9.2

### 5. The `tam_install_pending` header describes removed behaviour (low)

- **Where:** [tam.h](../include/tam.h#L34-L37)
- **What:** The comment says NULL checks both `/var/lib/tamlinux/pending.json`
  and `/var/lib/tamlinux/source/pending.json`. Since `725dd1f` (D279), the
  code checks only the first path, which is the one the installer writes.
- **Evidence:** [tam_cultivar.c](../src/tam_cultivar.c#L18-L23).
- **Fix:** Correct the comment.
- **Status:** open; P1 0.9.2

### 6. A failed `tam_str` append invites a leak (low)

- **Where:** [tam_str_make_room](../src/tam_str.c#L57-L83)
- **What:** On allocation failure, the append returns NULL and the old
  string stays allocated, so the natural `s = tam_str_cat(s, …)` loses it.
  The size arithmetic has no overflow check. tam's dependency assessment
  noted the first part.
- **Evidence:** Source inspection.
- **Fix:** Document the pattern in `tam.h`, and check `len + addlen` and the
  doubling for overflow.
- **Status:** open; became R2 finding 1

### 7. Width depends on the caller's locale (low)

- **Where:** [tam_uni_width](../src/tam_uni.c#L89-L97)
- **What:** Widths come from `wcwidth`. A program that has not called
  `setlocale`, or that runs in the C locale, gets width 1 for wide
  characters, so its columns drift. tam-view sets `LC_CTYPE`.
- **Evidence:** Source inspection.
- **Fix:** Document the requirement in `tam_uni.h`, or use built-in width
  tables.
- **Status:** documented in `tam_uni.h`; P1 0.11.0 adds `tam_locale_init`

## tam's concerns that do not hold

- `tam_exec` already uses one monotonic deadline for the whole call
  ([tam_exec.c](../src/tam_exec.c#L147)).
- `tam_install_pending` returns −1 for a probe error and 1 for a pending
  transaction. The merged diagnostic is in tam's command.
- `tam_xdg_path` ignores relative `XDG_*` values.
- `tam_path_join_under` is lexical, as its documentation says. Resolving
  symlinks is left to the caller.

## Limits

- `tam_key`, most of `tam_tty`, `tam_fs`, `tam_fs_tree`, `tam_ini`,
  `tam_rec`, `tam_walk`, `tam_sha256` and `tam_portal` were not reviewed
  line by line. They pass their suites under ASan and UBSan.
- The experimental display archives and `tam_vt` were not reviewed.
- `tam_pager` findings were checked with a small program linked against
  `libtam.a`, built from an exported copy of the revision.
