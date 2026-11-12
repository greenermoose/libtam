#ifndef TAM_TTY_H
#define TAM_TTY_H

#include "tam.h"

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
} tam_tty_t;

/* Record the descriptors and capabilities. Does not change terminal modes.
 * Returns 0, or -1 if both descriptors are not usable. */
int tam_tty_open(tam_tty_t *t, int in_fd, int out_fd);

/* Raw input with echo, canonical mode and signal generation disabled. The
 * saved settings are restored by tam_tty_raw(t, false) and tam_tty_close. */
int tam_tty_raw(tam_tty_t *t, bool enable);

int tam_tty_alt_screen(tam_tty_t *t, bool enable);
int tam_tty_cursor_visible(tam_tty_t *t, bool visible);
int tam_tty_mouse(tam_tty_t *t, bool enable); /* SGR mouse reporting */
int tam_tty_paste(tam_tty_t *t, bool enable); /* bracketed paste */

/* Refresh rows and columns from the terminal, falling back to 24x80. */
int tam_tty_size(tam_tty_t *t, int *rows, int *cols);

/* Wait up to timeout_ms for input (negative blocks), then read. Returns the
 * byte count, 0 on timeout or interruption, or -1 on error or end of file. */
int tam_tty_read(tam_tty_t *t, void *buf, size_t cap, int timeout_ms);

/* Write all bytes, retrying short writes and EINTR. Returns 0 or -1. */
int tam_tty_write(tam_tty_t *t, const void *buf, size_t len);

/* Wrap a frame in DEC synchronized output when the terminal supports it. */
int tam_tty_frame_begin(tam_tty_t *t);
int tam_tty_frame_end(tam_tty_t *t);

/* Restore every mode this structure enabled. Safe to call twice. */
void tam_tty_close(tam_tty_t *t);

#ifdef __cplusplus
}
#endif

#endif
