#include "tam_tty.h"
#include "tam_b64.h"
#include "tam_io.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
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

/* ---- Restore after a crash -------------------------------------------
 * The signal handler must not touch the caller's tam_tty_t, which may be
 * gone (a local in main after main returns). It reads this snapshot, which
 * every mode change on the registered terminal keeps current. */

static struct {
    const tam_tty_t *owner;     /* compared, never dereferenced, here */
    volatile sig_atomic_t active;
    int in_fd, out_fd;
    volatile sig_atomic_t have_termios, mouse, paste, cursor_hidden, alt;
    struct termios saved;
} g_restore;

static const int restore_signals[] = {SIGTERM, SIGHUP, SIGINT, SIGQUIT,
                                      SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
static bool g_restore_handlers[sizeof(restore_signals) / sizeof(restore_signals[0])];
static bool g_atexit_done;

static void snapshot(const tam_tty_t *t)
{
    if (t == NULL || g_restore.owner != t || !g_restore.active) return;
    g_restore.have_termios = 0;
    if (t->saved_termios) g_restore.saved = t->saved;
    g_restore.have_termios = t->saved_termios;
    g_restore.mouse = t->mouse_on;
    g_restore.paste = t->paste_on;
    g_restore.cursor_hidden = t->cursor_hidden;
    g_restore.alt = t->alt_screen;
}

static void put(char *buf, size_t *n, const char *s)
{
    while (*s) buf[(*n)++] = *s++;
}

void tam_tty_restore_now(void)
{
    if (!g_restore.active) return;
    int saved_errno = errno;
    char seq[64];
    size_t n = 0;
    if (g_restore.mouse) put(seq, &n, "\x1b[?1006l\x1b[?1000l");
    if (g_restore.paste) put(seq, &n, "\x1b[?2004l");
    if (g_restore.cursor_hidden) put(seq, &n, "\x1b[?25h");
    if (g_restore.alt) put(seq, &n, "\x1b[?1049l");
    const char *p = seq;
    while (n > 0) {
        ssize_t w = write(g_restore.out_fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        p += w;
        n -= (size_t)w;
    }
    if (g_restore.have_termios) (void)tcsetattr(g_restore.in_fd, TCSANOW, &g_restore.saved);
    g_restore.mouse = g_restore.paste = g_restore.cursor_hidden = g_restore.alt = 0;
    g_restore.have_termios = 0;
    errno = saved_errno;
}

static void restore_handler(int sig)
{
    tam_tty_restore_now();
    /* Carry on with the default action: for a fault the instruction runs
     * again, for the rest the raised signal arrives when we return. */
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    (void)sigaction(sig, &dfl, NULL);
    (void)raise(sig);
}

static void restore_at_exit(void)
{
    tam_tty_restore_now();
}

int tam_tty_install_restore(tam_tty_t *t)
{
    if (t == NULL) {
        errno = EINVAL;
        return -1;
    }
    g_restore.active = 0;
    g_restore.owner = t;
    g_restore.in_fd = t->in_fd;
    g_restore.out_fd = t->out_fd;
    g_restore.active = t->interactive;
    snapshot(t);
    for (size_t i = 0; i < sizeof(restore_signals) / sizeof(restore_signals[0]); i++) {
        struct sigaction old;
        if (sigaction(restore_signals[i], NULL, &old) < 0) continue;
        bool ours = !(old.sa_flags & SA_SIGINFO) && old.sa_handler == restore_handler;
        if (ours || (!(old.sa_flags & SA_SIGINFO) && old.sa_handler == SIG_DFL)) {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = restore_handler;
            sigemptyset(&sa.sa_mask);
            if (sigaction(restore_signals[i], &sa, NULL) == 0) g_restore_handlers[i] = true;
        }
    }
    if (!g_atexit_done && atexit(restore_at_exit) == 0) g_atexit_done = true;
    return 0;
}

/* Stop restoring for t, and put back the default actions we installed. */
static void uninstall_restore(const tam_tty_t *t)
{
    if (g_restore.owner != t) return;
    g_restore.active = 0;
    g_restore.owner = NULL;
    for (size_t i = 0; i < sizeof(restore_signals) / sizeof(restore_signals[0]); i++) {
        if (!g_restore_handlers[i]) continue;
        struct sigaction cur;
        if (sigaction(restore_signals[i], NULL, &cur) == 0 &&
            !(cur.sa_flags & SA_SIGINFO) && cur.sa_handler == restore_handler) {
            struct sigaction dfl;
            memset(&dfl, 0, sizeof(dfl));
            dfl.sa_handler = SIG_DFL;
            sigemptyset(&dfl.sa_mask);
            (void)sigaction(restore_signals[i], &dfl, NULL);
        }
        g_restore_handlers[i] = false;
    }
}

int tam_tty_open(tam_tty_t *t, int in_fd, int out_fd)
{
    if (t == NULL) return -1;
    memset(t, 0, sizeof(*t));
    t->winch_fd = -1;

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
    snapshot(t);
    return 0;
}

enum { MODE_COOKED, MODE_RAW, MODE_CBREAK };

static int set_mode(tam_tty_t *t, int mode)
{
    if (t == NULL) return -1;
    int now = t->raw ? MODE_RAW : t->cbreak ? MODE_CBREAK : MODE_COOKED;
    if (!t->interactive) {
        t->raw = mode == MODE_RAW;
        t->cbreak = mode == MODE_CBREAK;
        return 0;
    }
    if (mode == now) return 0;

    if (mode == MODE_COOKED) {
        if (t->saved_termios) {
            if (tcsetattr(t->in_fd, TCSADRAIN, &t->saved) < 0) return -1;
            t->saved_termios = false;
        }
    } else {
        if (!t->saved_termios) {
            if (tcgetattr(t->in_fd, &t->saved) < 0) return -1;
            t->saved_termios = true;
        }
        struct termios tio = t->saved;
        if (mode == MODE_RAW) {
            cfmakeraw(&tio);
            /* Raw mode drops output post-processing, which we want: the
             * renderer emits full lines with explicit positions. */
            tio.c_oflag &= (tcflag_t)~OPOST;
        } else {
            tio.c_lflag &= (tcflag_t)~(ICANON | ECHO | ECHONL);
        }
        tio.c_cc[VMIN] = 1;
        tio.c_cc[VTIME] = 0;
        if (tcsetattr(t->in_fd, TCSADRAIN, &tio) < 0) return -1;
    }

    t->raw = mode == MODE_RAW;
    t->cbreak = mode == MODE_CBREAK;
    snapshot(t);
    return 0;
}

int tam_tty_raw(tam_tty_t *t, bool enable)
{
    if (t == NULL) return -1;
    if (!enable && !t->raw) return 0;
    return set_mode(t, enable ? MODE_RAW : MODE_COOKED);
}

int tam_tty_cbreak(tam_tty_t *t, bool enable)
{
    if (t == NULL) return -1;
    if (!enable && !t->cbreak) return 0;
    return set_mode(t, enable ? MODE_CBREAK : MODE_COOKED);
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
    return set_flag(t, &t->mouse_on, enable, "\x1b[?1000h\x1b[?1006h",
                    "\x1b[?1006l\x1b[?1000l");
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
        return n == 0 ? TAM_TTY_EOF : (int)n;
    }

    struct pollfd pfd[2] = {
        { .fd = t->in_fd, .events = POLLIN, .revents = 0 },
        { .fd = t->winch_fd, .events = POLLIN, .revents = 0 },
    };
    nfds_t nfds = t->winch_fd >= 0 ? 2 : 1;
    int ready = poll(pfd, nfds, timeout_ms);

    if (ready < 0) return errno == EINTR ? 0 : -1;
    if (ready == 0) return 0;
    if (nfds == 2 && pfd[1].revents != 0 && tam_tty_resized(t)) return TAM_TTY_RESIZED;
    if (pfd[0].revents == 0) return 0;

    ssize_t n;
    do {
        n = read(t->in_fd, buf, cap);
    } while (n < 0 && errno == EINTR);

    if (n < 0) return errno == EAGAIN ? 0 : -1;
    if (n == 0) return TAM_TTY_EOF;
    return (int)n;
}

/* ---- Resize notification ---------------------------------------------- */

static int g_winch_pipe[2] = {-1, -1};

static void winch_handler(int sig)
{
    (void)sig;
    int saved = errno;
    ssize_t w = write(g_winch_pipe[1], "w", 1);
    (void)w;
    errno = saved;
}

int tam_tty_watch_resize(tam_tty_t *t)
{
    if (t == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (t->winch_fd >= 0) return t->winch_fd;
    if (g_winch_pipe[0] < 0 && pipe2(g_winch_pipe, O_CLOEXEC | O_NONBLOCK) < 0) return -1;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = winch_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGWINCH, &sa, &t->old_winch) < 0) return -1;
    t->winch_fd = g_winch_pipe[0];
    return t->winch_fd;
}

bool tam_tty_resized(tam_tty_t *t)
{
    if (t == NULL || t->winch_fd < 0) return false;
    bool any = false;
    char drain[64];
    while (read(t->winch_fd, drain, sizeof(drain)) > 0) any = true;
    if (any) tam_tty_size(t, NULL, NULL);
    return any;
}

/* ---- Suspend and resume ------------------------------------------------ */

enum {
    RESUME_RAW = 1, RESUME_CBREAK = 2, RESUME_ALT = 4, RESUME_HIDDEN = 8,
    RESUME_MOUSE = 16, RESUME_PASTE = 32
};

int tam_tty_suspend(tam_tty_t *t)
{
    if (t == NULL) return -1;
    if (t->suspended) return 0;
    unsigned m = 0;
    if (t->raw) m |= RESUME_RAW;
    if (t->cbreak) m |= RESUME_CBREAK;
    if (t->alt_screen) m |= RESUME_ALT;
    if (t->cursor_hidden) m |= RESUME_HIDDEN;
    if (t->mouse_on) m |= RESUME_MOUSE;
    if (t->paste_on) m |= RESUME_PASTE;
    int rc = 0;
    rc |= tam_tty_mouse(t, false);
    rc |= tam_tty_paste(t, false);
    rc |= tam_tty_cursor_visible(t, true);
    rc |= tam_tty_alt_screen(t, false);
    rc |= set_mode(t, MODE_COOKED);
    t->resume_modes = m;
    t->suspended = true;
    return rc < 0 ? -1 : 0;
}

int tam_tty_resume(tam_tty_t *t)
{
    if (t == NULL) return -1;
    if (!t->suspended) return 0;
    unsigned m = t->resume_modes;
    int rc = 0;
    if (m & RESUME_RAW) rc |= set_mode(t, MODE_RAW);
    else if (m & RESUME_CBREAK) rc |= set_mode(t, MODE_CBREAK);
    if (m & RESUME_ALT) rc |= tam_tty_alt_screen(t, true);
    if (m & RESUME_HIDDEN) rc |= tam_tty_cursor_visible(t, false);
    if (m & RESUME_MOUSE) rc |= tam_tty_mouse(t, true);
    if (m & RESUME_PASTE) rc |= tam_tty_paste(t, true);
    t->suspended = false;
    t->resume_modes = 0;
    tam_tty_size(t, NULL, NULL);
    return rc < 0 ? -1 : 0;
}

int tam_tty_job_stop(tam_tty_t *t)
{
    if (t == NULL) return -1;
    if (tam_tty_suspend(t) < 0) return -1;
    struct sigaction dfl, old;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigset_t tstp, oldmask;
    sigemptyset(&tstp);
    sigaddset(&tstp, SIGTSTP);
    int have_old = sigaction(SIGTSTP, &dfl, &old) == 0;
    (void)sigprocmask(SIG_UNBLOCK, &tstp, &oldmask);
    /* The kernel discards SIGTSTP for an orphaned process group, which is
     * what a program has when its parent is outside its session (started
     * directly as a session leader, as a terminal starts its first
     * program). There SIGSTOP, which cannot be discarded, does the job;
     * under a shell SIGTSTP keeps job control's usual report. */
    pid_t parent = getppid();
    bool orphaned = parent <= 1 || getsid(parent) != getsid(0);
    (void)raise(orphaned ? SIGSTOP : SIGTSTP); /* stops here until SIGCONT */
    (void)sigprocmask(SIG_SETMASK, &oldmask, NULL);
    if (have_old) (void)sigaction(SIGTSTP, &old, NULL);
    return tam_tty_resume(t);
}

int tam_tty_write(tam_tty_t *t, const void *buf, size_t len)
{
    if (t == NULL || (buf == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    return tam_write_all(t->out_fd, buf, len);
}

int tam_tty_osc52_copy(tam_tty_t *t, const void *data, size_t len)
{
    if (t == NULL || (data == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (len > TAM_TTY_OSC52_MAX) {
        errno = EMSGSIZE;
        return -1;
    }
    if (!isatty(t->out_fd)) {
        errno = ENOTTY;
        return -1;
    }
    static const char head[] = "\033]52;c;";
    static const char tail[] = "\033\\";
    size_t enc = tam_b64_encoded_len(len);
    size_t total = sizeof(head) - 1 + enc + sizeof(tail) - 1;
    char *seq = malloc(total + 1);
    if (seq == NULL) return -1;
    memcpy(seq, head, sizeof(head) - 1);
    tam_b64_encode(data, len, seq + sizeof(head) - 1);
    memcpy(seq + sizeof(head) - 1 + enc, tail, sizeof(tail));
    int rc = tam_write_all(t->out_fd, seq, total);
    int err = errno;
    free(seq);
    errno = err;
    return rc;
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
    set_mode(t, MODE_COOKED);
    uninstall_restore(t);
    if (t->winch_fd >= 0) {
        (void)sigaction(SIGWINCH, &t->old_winch, NULL);
        t->winch_fd = -1;
    }

    t->closed = true;
}
