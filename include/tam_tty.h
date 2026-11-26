#ifndef TAM_TTY_H
#define TAM_TTY_H

#include "tam.h"

#include <signal.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ANSI/xterm terminal control. There is no terminfo dependency: output uses
 * portable escape sequences and input is decoded by tam_key. */

typedef struct {
    int  in_fd;
    int  out_fd;
    int  rows;
    int  cols;
    int  colors;       /* 0, 16, 256 or 16777216 */
    bool truecolor;
    bool sync_output;  /* DEC private mode 2026 is assumed supported */
    bool interactive;  /* both descriptors are terminals */
    bool raw;
    bool alt_screen;
    bool cursor_hidden;
    bool mouse_on;
    bool paste_on;
    bool saved_termios;
    bool closed;
    struct termios saved;
    bool cbreak;               /* tam_tty_cbreak is on */
    bool suspended;            /* between tam_tty_suspend and resume */
    unsigned resume_modes;     /* what tam_tty_resume turns back on */
    int  winch_fd;             /* tam_tty_watch_resize's descriptor, or -1 */
    struct sigaction old_winch;
} tam_tty_t;

/* tam_tty_read results besides a byte count, 0 (timeout) and -1 (error). */
enum tam_tty_read_result {
    TAM_TTY_EOF = -2,          /* end of input: the terminal hung up */
    TAM_TTY_RESIZED = -3       /* the window changed size (tam_tty_watch_resize) */
};

/* Record the descriptors and capabilities. Does not change terminal modes.
 * Returns 0, or -1 if both descriptors are not usable. */
int tam_tty_open(tam_tty_t *t, int in_fd, int out_fd);

/* Raw input with echo, canonical mode and signal generation disabled. The
 * saved settings are restored by tam_tty_raw(t, false) and tam_tty_close. */
int tam_tty_raw(tam_tty_t *t, bool enable);

/* Like raw input, but Ctrl+C, Ctrl+\ and Ctrl+Z still send their signals
 * and output processing stays on, for a program that reads single keys but
 * leaves job control to the shell. Raw and cbreak replace each other. */
int tam_tty_cbreak(tam_tty_t *t, bool enable);

int tam_tty_alt_screen(tam_tty_t *t, bool enable);
int tam_tty_cursor_visible(tam_tty_t *t, bool visible);
int tam_tty_mouse(tam_tty_t *t, bool enable); /* SGR mouse reporting */
int tam_tty_paste(tam_tty_t *t, bool enable); /* bracketed paste */

/* Refresh rows and columns from the terminal, falling back to 24x80. */
int tam_tty_size(tam_tty_t *t, int *rows, int *cols);

/* Wait up to timeout_ms for input (negative blocks), then read. Returns the
 * byte count, 0 on timeout or interruption, TAM_TTY_EOF at end of input,
 * TAM_TTY_RESIZED when watching for resizes and the window changed (rows
 * and cols are already updated), or -1 on error. */
int tam_tty_read(tam_tty_t *t, void *buf, size_t cap, int timeout_ms);

/* Watch for window resizes: SIGWINCH writes to a pipe, so tam_tty_read
 * wakes with TAM_TTY_RESIZED instead of waiting out its timeout. Returns the
 * pipe's read end for a program that runs its own poll loop (then call
 * tam_tty_resized when it is readable), or -1 with errno. One terminal per
 * process; tam_tty_close puts the old SIGWINCH action back. */
int tam_tty_watch_resize(tam_tty_t *t);

/* Drain the resize pipe. True, with rows and cols refreshed, when the window
 * changed since the last call. */
bool tam_tty_resized(tam_tty_t *t);

/* Leave the terminal as the shell expects it, around running another
 * program or stopping: mouse and paste reporting off, cursor shown, main
 * screen, normal input. tam_tty_resume turns back on exactly what was on,
 * and refreshes the size. Both are no-ops when repeated. */
int tam_tty_suspend(tam_tty_t *t);
int tam_tty_resume(tam_tty_t *t);

/* Stop the program as Ctrl+Z would in a shell (raw input turns Ctrl+Z into
 * a key): suspend, stop with SIGTSTP, and resume when continued. */
int tam_tty_job_stop(tam_tty_t *t);

/* Put the terminal back if the program dies or exits without
 * tam_tty_close: on SIGTERM, SIGHUP, SIGINT, SIGQUIT, SIGSEGV, SIGBUS,
 * SIGFPE, SIGILL and SIGABRT, wherever the program left the default action,
 * and at exit. After restoring, a signal goes on to its default action. A
 * program with its own handlers calls tam_tty_restore_now from them. One
 * terminal per process; tam_tty_close stops it. 0, or -1 with errno. */
int tam_tty_install_restore(tam_tty_t *t);

/* Undo the modes of the terminal registered with tam_tty_install_restore,
 * using only async-signal-safe calls, so it may run in a signal handler. */
void tam_tty_restore_now(void);

/* Write all bytes, retrying short writes and EINTR. Returns 0 or -1. */
int tam_tty_write(tam_tty_t *t, const void *buf, size_t len);

/* Ask the terminal to put `len` bytes on the clipboard with OSC 52
 * ("\033]52;c;<base64>\033\\"). Many terminals limit the size, so more
 * than TAM_TTY_OSC52_MAX bytes is refused with EMSGSIZE; nothing is sent to
 * a descriptor that is not a terminal (ENOTTY). Whether the terminal allows
 * clipboard writes is up to it. 0, or -1 with errno. */
#define TAM_TTY_OSC52_MAX 74994
int tam_tty_osc52_copy(tam_tty_t *t, const void *data, size_t len);

/* Wrap a frame in DEC synchronized output when the terminal supports it. */
int tam_tty_frame_begin(tam_tty_t *t);
int tam_tty_frame_end(tam_tty_t *t);

/* Restore every mode this structure enabled. Safe to call twice. */
void tam_tty_close(tam_tty_t *t);

#ifdef __cplusplus
}
#endif

#endif
