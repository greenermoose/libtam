# R2: libtam against the whole Tamlinux C suite

- **Date:** 2026-11-25
- **Reviewer:** Claude Code 2.1.332, Claude Opus 5.5
- **Revision:** libtam `a3e0901` (VERSION 0.9.1). Consumers: tam
  `f361bcb`, tam-clam `9aadef9`, tam-data `c3c6368`, tam-edit `2d2ce33`,
  tam-hide `ce57726`, tam-knox `dd7a8da`, tam-last `ec39c8c`, tam-pane
  `418bd62`, tam-view `3444bb1`, tamlinux-tools `8446495`, tamlinux
  `36aca8a`.
- **Scope:** libtam's public API, and how the consumers use it, work
  around it or copy it. The question was whether libtam can be refactored
  or improved so the programs in the 0.7.x Northern Torch cultivar are
  more robust and shorter. In tamlinux, the review covered the C commands
  start, tam-monitor, tam-meter, tam-boot, tam-hardware, tam-open and
  tam-launch-journal. The plan that follows from it is
  [P1](../plans/P1-0.7-refactor.md).

## Summary

The answer is yes. Most of the duplication across programs is not
copying. It comes from gaps and unsafe semantics in libtam, which each
program then works around in its own way.

The biggest problems are in the core:
- a failed `tam_str` append leaks;
- `tam_fs_read_file` hangs on a FIFO and returns nothing for /proc;
- `tam_fs_write_atomic` forces the file mode and replaces symlinks;
- `tam_key_feed` rejects chunks larger than 64 bytes;
- `tam_exec` children inherit the caller's blocked signals.

Missing process, I/O, socket and terminal primitives are written again in
every program. One true copy exists: tam-view's renderer, about 4,100
lines, is byte-identical in tam-knox.

None of the libtam findings is a memory-safety defect in libtam itself.
Consumer bugs found along the way are listed at the end.

## Findings

### 1. A failed `tam_str` append leaks the string (medium)

- **Where:** [tam_str_make_room](../src/tam_str.c#L57-L72)
- **What:** When `realloc` fails, `make_room` returns NULL and leaves the
  old block allocated. Every consumer writes `s = tam_str_cat(s, ...)`,
  which loses the only pointer to it. The size arithmetic has no overflow
  check. `tam_str_cat_json` leaks the same way when it fails partway, and
  `tam_str_trim` on an empty string computes `s - 1`. This is R1
  finding 6, which is still open.
- **Evidence:** `tam_str.c:66-69`.
- **Fix:** Free the old block and return NULL, so the chained idiom is
  leak-free. Check `len + addlen` and the doubling for overflow. Test with
  an allocation-failure hook.
- **Status:** fixed in 0.10.0 by keeping the old string valid on failure
  (overflow checks, `cat_json` grows once, `trim` of ""); see P1 for
  why libtam does not free it

### 2. `tam_fs_read_file` is unsafe for anything but a small regular file (high)

- **Where:** [tam_fs_read_file](../src/tam_fs.c#L16-L40)
- **What:** It opens with `fopen` and no close-on-exec, and sizes the file
  with `fseek` and `ftell`, whose returns it does not check. The effects:
  - /proc and sysfs files report size 0, so it returns "".
  - A pipe fails.
  - A FIFO blocks forever.
  - A short read still succeeds.
  - There is no size cap.

  tam-edit, tam-knox and tam-data hang when asked to open a FIFO. The
  tamlinux probes, tam-pane, tam-clam and tam-data each write their own
  reader because of this.
- **Evidence:** Source inspection. tam-view already fixed it locally in
  `source_open_file` (tam-view `src/source.c:9-25`): it opens non-blocking
  and accepts only a regular file.
- **Fix:** Use an fd-based read with `O_CLOEXEC|O_NONBLOCK`. It refuses
  anything but a regular file, reads until EOF, and takes a size cap. Add
  a separate `tam_fd_read_all` for pipes and stdin.
- **Status:** fixed in 0.10.0 (`tam_fs_read`, `tam_fd_read_all`)

### 3. `tam_fs_write_atomic` forces the mode and replaces symlinks (high)

- **Where:** [tam_fs_write_atomic](../src/tam_fs.c#L63-L99)
- **What:** The function has four problems:
  - It `fchmod`s the temporary file to the caller's mode and ignores the
    umask.
  - It renames over the path even when the path is a symlink.
  - `mkstemp` gives a descriptor without close-on-exec, and the temporary
    path is a fixed 4096-byte buffer.
  - It returns false when the directory fsync fails, even though the file
    has already been replaced.

  tam-edit saves every file as 0644 (tam-edit `src/buffer.c:435`), and
  tam-data does the same (`src/csv.c:766`, `json.c:899`, `options.c:330`).
  As a result, scripts lose `+x`, 0600 files become world-readable, and a
  symlinked file becomes a regular file.
- **Evidence:** Source inspection.
- **Fix:**
  - Keep the existing mode by default, and apply the umask to a new file.
  - Write through a final symlink unless the caller asks to replace it.
  - Use `mkostemp(O_CLOEXEC)` and build the temporary path dynamically.
  - Report "replaced, not durable" separately from failure.
- **Status:** fixed in 0.10.0 (flags `TAM_FS_KEEP_MODE` and
  `TAM_FS_REPLACE_LINK`); tam-edit and tam-data now keep modes

### 4. `tam_key_feed` drops a whole chunk over 64 bytes (high)

- **Where:** [tam_key_feed](../src/tam_key.c#L26-L33)
- **What:** The parser buffer is 64 bytes. Any feed that does not fit is
  rejected whole with -1. tam-knox feeds 256-byte reads and ignores the
  result (tam-knox `src/workbench.c:3279,3293`), so a fast paste or burst
  of keys vanishes. tam-view sizes each read to the free space, and
  tam-edit feeds one byte at a time.
- **Evidence:** Source inspection of both sides.
- **Fix:** Return the number of bytes accepted, and have callers feed the
  rest after draining `tam_key_next`.
- **Status:** fixed in 0.10.0; tam-knox 1.0.7 feeds in a loop

### 5. `tam_exec` children inherit blocked and ignored signals (medium)

- **Where:** [tam_exec.c](../src/tam_exec.c#L119-L124)
- **What:** The function has four problems:
  - It blocks SIGCHLD with `sigprocmask` and does not restore the mask or
    signal dispositions in the child. A compositor or a signalfd program
    therefore starts children with signals blocked. tam-pane's own
    `spawn.c:28-30` clears the mask.
  - Exit 127 means both "not found" and "the program exited 127".
  - An allocation failure is reported as `truncated`.
  - It polls every 5 ms while it waits.
- **Evidence:** Source inspection.
- **Fix:**
  - Reset the mask and dispositions in the child.
  - Report the exec errno through a close-on-exec pipe.
  - Report out-of-memory separately from truncation.
- **Status:** fixed in 0.10.0 (child signal state, `error` field); the 5 ms
  wait poll remains

### 6. The error conventions disagree (low)

- **Where:** `include/tam.h` throughout
- **What:**
  - Functions return in at least eight ways: `bool` (`tam_fs_*`), NULL, 0
    or -1 without errno (`tam_tty_*`), 0 or -1 with errno (`tam_fs_tree`,
    `tam_sha256`), a callback's value, a line number (`tam_ini`), magic
    numbers (`tam_portal` status 0/1/2), and a struct field
    (`tam_exec`).
  - Public enums are anonymous.
  - `tam_log_*` and `tam_str_cat_printf` have no printf format attributes.
  - `tam.h` defines `_GNU_SOURCE` and `_POSIX_C_SOURCE` for every
    consumer.
- **Evidence:** Header inspection.
- **Fix:** For each new or changed function, return 0 or a count, or -1
  with errno set. A pointer result is NULL with errno set. Name the enums,
  add the format attributes, and move the feature macros into each
  consumer's CFLAGS.
- **Status:** partly fixed in 0.10.0 (format attributes, named enums, one
  convention for changed functions); feature macros deferred

### 7. Other silent truncation and parsing gaps (low)

- **Where:**
  - [tam_ini.c](../src/tam_ini.c#L29-L47): section names over 127 bytes
    are cut, and lines without `=` are skipped.
  - [tam_cultivar.c](../src/tam_cultivar.c#L25-L30): a line over 255
    bytes comes back as a prefix that still counts as success, and the
    token is not validated.
  - [tam_portal_uri_to_path](../src/tam_portal.c#L17-L47): the remote-host
    bug fixed in `tam_pager` for R1 #2 is still here, and `%00` decodes to
    a NUL byte.
- **Evidence:** Source inspection.
- **Fix:** Fail on each of these with a line number or errno.
- **Status:** fixed in 0.10.0

### 8. Primitives every program writes again (medium)

- **Where:** across the consumers; counts at the revisions above
- **What:** libtam lacks these, so each program writes its own:

  | Missing primitive | Copies | Notes |
  | --- | --- | --- |
  | PATH lookup | 4 | Two of them in start |
  | Detached spawn with an error report | 5 | `tam_uri_open_external` takes only URLs |
  | Running a foreground child | 6 | |
  | A write-all loop | 10+ | Two never end on a zero-byte write |
  | A monotonic-milliseconds clock | 13 | Two inside libtam |
  | Read-all from a descriptor, or one trimmed line | 9 | |
  | XDG base directories, and runtime-directory owner and mode checks | 7 | |
  | UTF-8 previous character and validation | 6 | |
  | base64 for OSC 52 | 3 | |
  | Key-name parse and format | 2 | About 160 lines each, about 85% the same, disagreeing on Shift |
  | Terminal suspend/resume around a child or Ctrl+Z | 5 | Crash-time restore exists only in tam-data |
  | Bracketed paste and SGR mouse decoding | 3 | `tam_tty` enables them; `tam_key` decodes neither |
  | A private Unix socket server with a peer-uid check | 3 | |
  | The "interrupted update pending" message | 8 | |
  | JSON escaping | 3 | All in tam-monitor; they corrupt UTF-8 |

- **Evidence:** The consumer file:line lists in P1's Phase 5 table.
- **Fix:** Add the `tam_proc`, `tam_io`, `tam_time`, `tam_sock` and
  `tam_b64` modules, and extend `tam_path`, `tam_uni`, `tam_tty`,
  `tam_key` and `tam_cultivar` (P1 Phases 2 and 3).
- **Status:** fixed: primitives in 0.11.0 to 0.14.0, consumers migrated
  2026-11-26 (P1)

### 9. tam-view's renderer is copied verbatim into tam-knox (medium)

- **Where:** tam-view `src/render.c`, `render_mermaid.c`, `layout.c`,
  `render_md.c`, `render_html.c` and `include/render_internal.h`, with
  the same files in tam-knox.
- **What:** About 4,100 lines are byte-identical, as tam-knox's
  `UPSTREAM.md` declares (taken from tam-view `3119315`). `source.c` and
  `view.h` have already diverged: tam-view added safe opening, snapshots
  and watching that tam-knox's copy lacks.
- **Evidence:** `cmp` of each pair.
- **Fix:** Fred decided on 2026-11-25 to move the renderer into a new
  optional `libtam-render.a` that both programs link (P1 Phase 4).
- **Status:** fixed in 0.13.0; tam-view 0.4.3 and tam-knox 1.0.8 link
  `libtam-render.a`

### 10. The display archives do not provide what a compositor needs (low)

- **Where:** `include/tam_display.h`, `tam_display_session.h`, `tam_vt.h`
- **What:** The display archives inspect the console and hand it off.
  They take no device control (no TakeControl/TakeDevice and no
  KD_GRAPHICS). They have no buffer allocation, page flips, vblank events,
  cursor planes, mode setting, hotplug or input. tam-pane's P6 said that
  libtam "already has … seat handling".
- **Evidence:** Header comments, and tam-pane's use of wlroots for all
  DRM, input and VT work (tam-pane `src/server.c:61,115`).
- **Fix:** Correct tam-pane's P6, which was done with this review. A
  compositor mode would be a new libtam layer, and it is not part of the
  0.7 refactor.
- **Status:** tam-pane P6 corrected

### 11. Pins and version floors are stale or missing (low)

- **Where:** consumer Makefiles
- **What:**
  - Commit `a3e0901` changed code without bumping VERSION, so "0.9.1 or
    later" cannot tell whether the hardened opener is present.
  - tam-monitor requires libtam 0.6.0 or later, but calls
    `tam_install_pending`, which needs 0.8.0.
  - tam-clam, tam-edit, tam-last and five tamlinux commands have no
    version floor.
  - Those five tamlinux commands also do not relink when `libtam.a`
    changes.
  - tam's `source-build.json` pins libtam `725dd1f`, while
    `tamlinux/components.lock` pins `a3e0901`.
- **Evidence:** Makefile inspection, and `git log -- VERSION`.
- **Fix:**
  - Release `a3e0901` as 0.9.2.
  - Install a `libtam.pc` file, and have every consumer check
    `--atleast-version` for what it actually uses.
  - Make the archive a link prerequisite.
  - Reconcile tam's pin.
- **Status:** fixed for every migrated program (floors, relink where the
  build allows, tam's source-build pin); components.lock moved with the
  migrations

## Consumer bugs found along the way

All fixed in the 2026-11-26 migrations (P1), except tam-hide's
0666 auth-socket default (kept: every user's lock screen must reach it and
handlers check the peer uid), VT_WAITACTIVE without a timeout, and
tam-meter's version fallbacks (Fred's call). See each program's work record.

These belong to each program's own bug records. They are listed here so
the migration slices in P1 fix them while replacing the local code.

- **tam-hide:**
  - The daemon reads a request one byte at a time with a 2-second timeout
    per byte, and serves one client at a time (`src/controller.c:755-783`,
    `src/tam-hided.c:152`).
  - The auth socket defaults to mode 0666 (`tam-hided.c:119-120`), so any
    local user can stall it.
  - The passcode store does not sync the directory after its rename
    (`passcode_store.c:254`).
- **tam-knox:**
  - Pasted text arrives as commands: bracketed paste is enabled but not
    handled. (Since libtam 0.12.0 it arrives as paste events, which
    tam-knox ignores, so it no longer runs as commands.)
  - Its non-terminal check never fires (`workbench.c:3238`).
  - A SIGTERM or SIGHUP leaves the terminal raw.
  - Alt+Shift bindings never match (`keymap.c:189-200,407-412`).
- **tam-edit:**
  - A hang-up or EOF busy-loops (`main.c:511-516`).
  - The OSC 52 escape is written in a single unlooped `write`
    (`clipboard.c:82`).
- **tam-pane:** `tam-pane msg subscribe | head -1` never exits, because
  SIGPIPE is ignored and the `fwrite` result is unchecked
  (`ipc_proto.c:867,927`). The IPC server also unlinks any existing socket
  before binding (`ipc.c:742`).
- **tam-last:** It writes the clipboard to the requesting client in a
  blocking loop inside its event loop (`window.c:305-318`).
- **tam-data:** It counts widths in code points, not cells (`utf8.h:94`),
  so wide characters misalign columns.
- **tam-clam:** A history over 8 MiB loads its oldest entries
  (`history/history.c:288-291`). Its pipes and temporary file lack
  close-on-exec (`exec.c:421,2007`, `edit/edit.c:310`).
- **tamlinux:**
  - tam-monitor's three JSON escapers write each byte above 126 as
    `\u00XX`, which corrupts UTF-8.
  - Its `run_tool` silently cuts output at 4 KiB (`native-hw.c:160`).
  - tam-meter and tam-boot print unescaped strings into JSON
    (`report.c`).
  - tam-meter still reads the version fallbacks libtam retired in `725dd1f`
    (`sensors.c:484-506`).
- **tam:** `print_json_string_array` prints its tokens unescaped
  (`src/install.c:753-788`).

## Limits

- **Method:** read-only. Three parallel source sweeps covered the code.
  The reviewer then checked the libtam findings 1–4, the tam-hide daemon,
  the tam-knox feed call, tam-edit's save mode and the renderer copy in
  the source. The other consumer findings rest on the sweeps' file:line
  evidence and were not reproduced.
- **Not run:** no consumer was built or run for this review, and no
  reproducer was written.
- **Not covered:**
  - The display archives' correctness. Finding 10 covers only their fit
    for a compositor.
  - tam's terminfo pager stack, beyond its overlap with libtam.
  - The Go and Python code in tam-edit and tam.
