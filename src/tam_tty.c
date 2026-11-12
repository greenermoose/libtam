#include "tam_tty.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Capability detection is deliberately small: this library speaks ANSI/xterm
 * and never reads terminfo. TERM is inspected only to decide how much color
 * to send and whether synchronized output is safe. */

static bool env_is(const char *name, const char *value)
{
    const char *v = getenv(name);
    return v != NULL && strcmp(v, value) == 0;
}

static bool term_has(const char *needle)
{
    const char *term = getenv("TERM");
    return term != NULL && strstr(term, needle) != NULL;
}

static void detect_capabilities(tam_tty_t *t)
{
    t->truecolor = false;
    t->colors = 0;

    const char *colorterm = getenv("COLORTERM");
    if (colorterm != NULL &&
        (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0)) {
        t->truecolor = true;
    }

    if (t->interactive && getenv("NO_COLOR") == NULL) {
        if (t->truecolor) {
            t->colors = 16777216;
        } else if (term_has("256color")) {
            t->colors = 256;
        } else if (env_is("TERM", "dumb")) {
            t->colors = 0;
        } else {
            t->colors = 16;
        }
    }

    /* The Linux console does not implement DEC private mode 2026. */
    t->sync_output = t->interactive && !env_is("TERM", "linux") &&
                     !env_is("TERM", "dumb");
}

int tam_tty_open(tam_tty_t *t, int in_fd, int out_fd)
{
    if (t == NULL) return -1;
    memset(t, 0, sizeof(*t));

    if (in_fd < 0 || out_fd < 0) return -1;

    t->in_fd = in_fd;
    t->out_fd = out_fd;
    t->interactive = isatty(in_fd) && isatty(out_fd);
    t->rows = 24;
    t->cols = 80;

    detect_capabilities(t);
    tam_tty_size(t, &t->rows, &t->cols);
    return 0;
}

static int set_flag(tam_tty_t *t, bool *field, bool enable,
                    const char *on, const char *off)
{
    if (t == NULL || field == NULL) return -1;
    if (*field == enable) return 0;
    const char *seq = enable ? on : off;
    if (seq != NULL && t->interactive) {
        if (tam_tty_write(t, seq, strlen(seq)) < 0) return -1;
    }
    *field = enable;
    return 0;
}

int tam_tty_raw(tam_tty_t *t, bool enable)
{
    if (t == NULL) return -1;
    if (!t->interactive) {
        t->raw = enable;
        return 0;
    }
    if (enable == t->raw) return 0;

    if (enable) {
        if (!t->saved_termios) {
            if (tcgetattr(t->in_fd, &t->saved) < 0) return -1;
            t->saved_termios = true;
        }
        struct termios raw = t->saved;
        cfmakeraw(&raw);
        /* Raw mode drops output post-processing, which we want, but keep
         * carriage-return translation off as well since the renderer emits
         * full lines with explicit positions. */
        raw.c_oflag &= (tcflag_t)~OPOST;
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(t->in_fd, TCSADRAIN, &raw) < 0) return -1;
    } else {
        if (t->saved_termios) {
            if (tcsetattr(t->in_fd, TCSADRAIN, &t->saved) < 0) return -1;
            t->saved_termios = false;
        }
    }

    t->raw = enable;
    return 0;
}

int tam_tty_alt_screen(tam_tty_t *t, bool enable)
{
    return set_flag(t, &t->alt_screen, enable, "\x1b[?1049h", "\x1b[?1049l");
}

int tam_tty_cursor_visible(tam_tty_t *t, bool visible)
{
    /* cursor_hidden is the inverse of the requested visibility. */
    return set_flag(t, &t->cursor_hidden, !visible, "\x1b[?25l", "\x1b[?25h");
}

int tam_tty_mouse(tam_tty_t *t, bool enable)
{
    if (t == NULL) return -1;
    if (enable == t->mouse_on) return 0;
    int rc;
    if (enable) {
        rc = tam_tty_write(t, "\x1b[?1000h\x1b[?1006h", 16);
    } else {
        rc = t->interactive ? tam_tty_write(t, "\x1b[?1006l\x1b[?1000l", 16) : 0;
    }
    if (rc < 0) return -1;
    t->mouse_on = enable;
    return 0;
}

int tam_tty_paste(tam_tty_t *t, bool enable)
{
    return set_flag(t, &t->paste_on, enable, "\x1b[?2004h", "\x1b[?2004l");
}

int tam_tty_size(tam_tty_t *t, int *rows, int *cols)
{
    if (t == NULL) return -1;

    int r = t->rows > 0 ? t->rows : 24;
    int c = t->cols > 0 ? t->cols : 80;

    struct winsize ws;
    if (t->interactive && ioctl(t->out_fd, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_row > 0 && ws.ws_col > 0) {
        r = ws.ws_row;
        c = ws.ws_col;
    }

    t->rows = r;
    t->cols = c;
    if (rows != NULL) *rows = r;
    if (cols != NULL) *cols = c;
    return 0;
}

int tam_tty_read(tam_tty_t *t, void *buf, size_t cap, int timeout_ms)
{
    if (t == NULL || buf == NULL || cap == 0) return -1;

    if (!t->interactive) {
        ssize_t n = read(t->in_fd, buf, cap);
        if (n < 0) return errno == EINTR ? 0 : -1;
        return n == 0 ? -1 : (int)n;
    }

    struct pollfd pfd = { .fd = t->in_fd, .events = POLLIN, .revents = 0 };
    int ready = poll(&pfd, 1, timeout_ms);

    if (ready < 0) return errno == EINTR ? 0 : -1;
    if (ready == 0) return 0;

    ssize_t n;
    do {
        n = read(t->in_fd, buf, cap);
    } while (n < 0 && errno == EINTR);

    if (n < 0) return -1;
    if (n == 0) return -1;
    return (int)n;
}

int tam_tty_write(tam_tty_t *t, const void *buf, size_t len)
{
    if (t == NULL || (buf == NULL && len > 0)) return -1;

    const char *p = buf;
    while (len > 0) {
        ssize_t n = write(t->out_fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

int tam_tty_frame_begin(tam_tty_t *t)
{
    if (t == NULL) return -1;
    if (!t->sync_output || !t->interactive) return 0;
    return tam_tty_write(t, "\x1b[?2026h", 8);
}

int tam_tty_frame_end(tam_tty_t *t)
{
    if (t == NULL) return -1;
    if (!t->sync_output || !t->interactive) return 0;
    return tam_tty_write(t, "\x1b[?2026l", 8);
}

void tam_tty_close(tam_tty_t *t)
{
    if (t == NULL || t->closed) return;

    /* Disable the optional modes first so the restore sequence is not
     * interpreted by a still-active mouse or paste mode. */
    tam_tty_mouse(t, false);
    tam_tty_paste(t, false);
    tam_tty_cursor_visible(t, true);
    tam_tty_alt_screen(t, false);
    tam_tty_raw(t, false);

    t->closed = true;
}
