#ifndef TAM_INTERNAL_H
#define TAM_INTERNAL_H

/* Helpers shared by libtam's own sources. Not installed. */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* Milliseconds on the monotonic clock. */
static inline long long tam__now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The value of one hexadecimal digit, or -1. */
static inline int tam__hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* In a forked child: give the program to be exec'd no blocked signals and
 * the default action for the signals a parent commonly ignores or catches
 * (ignored signals otherwise stay ignored across exec). */
static inline void tam__child_default_signals(void) {
    static const int sigs[] = {SIGPIPE, SIGINT, SIGQUIT, SIGTERM, SIGHUP,
                               SIGCHLD, SIGTSTP, SIGTTIN, SIGTTOU, SIGALRM,
                               SIGUSR1, SIGUSR2, SIGWINCH};
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
        (void)sigaction(sigs[i], &dfl, NULL);
    sigset_t none;
    sigemptyset(&none);
    (void)sigprocmask(SIG_SETMASK, &none, NULL);
}

/* In a forked child: close every descriptor from `first` up except `keep`
 * and `keep2` (-1 for none), so the program holds none of the parent's
 * files. */
static inline void tam__child_close_from(int first, int keep, int keep2) {
    int a = keep < first ? -1 : keep, b = keep2 < first ? -1 : keep2;
    if (a > b) { int t = a; a = b; b = t; }
    if (a == b) a = -1;
#ifdef SYS_close_range
    int ok = 1, from = first;
    int stops[2] = { a, b };
    for (int i = 0; i < 2 && ok; i++) {
        if (stops[i] < 0) continue;
        if (stops[i] > from && syscall(SYS_close_range, from, stops[i] - 1, 0) != 0) ok = 0;
        from = stops[i] + 1;
    }
    if (ok && syscall(SYS_close_range, from, ~0U, 0) != 0) ok = 0;
    if (ok) return;
#endif
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 65536) max = 65536;
    for (int fd = first; fd < max; fd++)
        if (fd != a && fd != b) (void)close(fd);
}

/* In a forked child: send errno through the close-on-exec report pipe and
 * exit 127, as a failed exec does. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
static inline void tam__child_fail(int report, int err) {
    ssize_t n;
    do {
        n = write(report, &err, sizeof(err));
    } while (n < 0 && errno == EINTR);
    _exit(127);
}

#endif
