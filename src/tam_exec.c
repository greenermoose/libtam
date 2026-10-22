#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>

#define TAM_EXEC_DEFAULT_TIMEOUT_MS 30000
#define TAM_EXEC_DEFAULT_MAX_OUTPUT ((size_t)16 * 1024 * 1024)

void tam_exec_result_free(tam_exec_result_t *res) {
    if (!res) return;
    free(res->stdout_buf);
    free(res->stderr_buf);
    res->stdout_buf = NULL;
    res->stderr_buf = NULL;
    res->stdout_len = 0;
    res->stderr_len = 0;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void close_fd(int *fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} capture_t;

/* Read what is available from fd into c. Returns 0 at end of file, 1 when
 * more may follow, and -1 when the cap is reached (what fits is kept). */
static int capture_read(int fd, capture_t *c, size_t max) {
    char chunk[16384];
    ssize_t n = read(fd, chunk, sizeof(chunk));
    if (n == 0) return 0;
    if (n < 0) return (errno == EAGAIN || errno == EINTR) ? 1 : 0;
    size_t take = (size_t)n;
    int capped = 0;
    if (c->len + take > max) {
        take = max - c->len;
        capped = 1;
    }
    if (c->len + take + 1 > c->cap) {
        size_t cap = c->cap ? c->cap : 4096;
        while (c->len + take + 1 > cap) cap *= 2;
        char *next = realloc(c->buf, cap);
        if (!next) return -1;
        c->buf = next;
        c->cap = cap;
    }
    memcpy(c->buf + c->len, chunk, take);
    c->len += take;
    c->buf[c->len] = '\0';
    return capped ? -1 : 1;
}

static void child_exec(const char *const argv[], int in_fd, int out_fd, int err_fd) {
    setpgid(0, 0);
    if (in_fd < 0) in_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (in_fd >= 0) dup2(in_fd, STDIN_FILENO);
    dup2(out_fd, STDOUT_FILENO);
    dup2(err_fd, STDERR_FILENO);
    execvp(argv[0], (char *const *)argv);
    _exit(127);
}

/* Kill the child's process group, so helpers it started go too, and reap it. */
static void kill_and_reap(pid_t pid, int *status) {
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    while (waitpid(pid, status, 0) < 0 && errno == EINTR) {
    }
}

tam_exec_result_t tam_exec(const char *const argv[], int timeout_ms) {
    tam_exec_opts_t opts = { .timeout_ms = timeout_ms };
    return tam_exec_opts(argv, &opts);
}

tam_exec_result_t tam_exec_opts(const char *const argv[], const tam_exec_opts_t *opts) {
    tam_exec_result_t res;
    memset(&res, 0, sizeof(res));
    res.exit_code = -1;
    if (!argv || !argv[0]) return res;

    int timeout = opts && opts->timeout_ms > 0 ? opts->timeout_ms : TAM_EXEC_DEFAULT_TIMEOUT_MS;
    size_t max = opts && opts->max_output > 0 ? opts->max_output : TAM_EXEC_DEFAULT_MAX_OUTPUT;
    const char *input = opts ? (const char *)opts->input : NULL;
    size_t input_len = input ? opts->input_len : 0;

    int out_pipe[2] = {-1, -1}, err_pipe[2] = {-1, -1}, in_pipe[2] = {-1, -1};
    if (pipe2(out_pipe, O_CLOEXEC) < 0 || pipe2(err_pipe, O_CLOEXEC) < 0 ||
        (input && pipe2(in_pipe, O_CLOEXEC) < 0)) {
        close_fd(&out_pipe[0]); close_fd(&out_pipe[1]);
        close_fd(&err_pipe[0]); close_fd(&err_pipe[1]);
        return res;
    }

    /* A child that exits before reading all its input must not kill the
     * caller with SIGPIPE; block it for the call and discard any pending. */
    sigset_t pipe_set, old_mask;
    sigemptyset(&pipe_set);
    sigaddset(&pipe_set, SIGPIPE);
    sigprocmask(SIG_BLOCK, &pipe_set, &old_mask);

    pid_t pid = fork();
    if (pid == 0) {
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
        child_exec(argv, in_pipe[0], out_pipe[1], err_pipe[1]);
    }
    close_fd(&in_pipe[0]);
    close_fd(&out_pipe[1]);
    close_fd(&err_pipe[1]);
    if (pid < 0) {
        close_fd(&in_pipe[1]);
        close_fd(&out_pipe[0]);
        close_fd(&err_pipe[0]);
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
        return res;
    }
    setpgid(pid, pid);

    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);
    if (in_pipe[1] >= 0) {
        fcntl(in_pipe[1], F_SETFL, O_NONBLOCK);
        if (input_len == 0) close_fd(&in_pipe[1]);
    }

    capture_t out = {0}, err = {0};
    size_t written = 0;
    long long deadline = now_ms() + timeout;
    int status = 0;
    bool reaped = false;

    while (out_pipe[0] >= 0 || err_pipe[0] >= 0) {
        long long remaining = deadline - now_ms();
        if (remaining <= 0) {
            res.timed_out = true;
            break;
        }
        struct pollfd fds[3];
        int nfds = 0, out_i = -1, err_i = -1, in_i = -1;
        if (out_pipe[0] >= 0) { out_i = nfds; fds[nfds++] = (struct pollfd){ out_pipe[0], POLLIN, 0 }; }
        if (err_pipe[0] >= 0) { err_i = nfds; fds[nfds++] = (struct pollfd){ err_pipe[0], POLLIN, 0 }; }
        if (in_pipe[1] >= 0) { in_i = nfds; fds[nfds++] = (struct pollfd){ in_pipe[1], POLLOUT, 0 }; }
        int p = poll(fds, (nfds_t)nfds, (int)remaining);
        if (p < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (in_i >= 0 && fds[in_i].revents) {
            ssize_t n = write(in_pipe[1], input + written, input_len - written);
            if (n > 0) written += (size_t)n;
            if ((n < 0 && errno != EAGAIN && errno != EINTR) || written == input_len)
                close_fd(&in_pipe[1]);
        }
        int capped = 0;
        if (out_i >= 0 && fds[out_i].revents) {
            int r = capture_read(out_pipe[0], &out, max);
            if (r == 0) close_fd(&out_pipe[0]);
            if (r < 0) capped = 1;
        }
        if (err_i >= 0 && fds[err_i].revents) {
            int r = capture_read(err_pipe[0], &err, max);
            if (r == 0) close_fd(&err_pipe[0]);
            if (r < 0) capped = 1;
        }
        if (capped) {
            res.truncated = true;
            break;
        }
    }
    close_fd(&in_pipe[1]);
    close_fd(&out_pipe[0]);
    close_fd(&err_pipe[0]);

    /* The output is closed; the child still has until the deadline to exit. */
    while (!res.timed_out && !res.truncated) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            reaped = true;
            break;
        }
        if (w < 0 && errno != EINTR) break;
        if (now_ms() >= deadline) {
            res.timed_out = true;
            break;
        }
        struct timespec pause = { 0, 5 * 1000000L };
        nanosleep(&pause, NULL);
    }
    if (!reaped) kill_and_reap(pid, &status);

    if (!sigismember(&old_mask, SIGPIPE)) {
        struct timespec zero = { 0, 0 };
        while (sigtimedwait(&pipe_set, NULL, &zero) == SIGPIPE) {
        }
    }
    sigprocmask(SIG_SETMASK, &old_mask, NULL);

    res.stdout_buf = out.buf ? out.buf : calloc(1, 1);
    res.stdout_len = out.len;
    res.stderr_buf = err.buf ? err.buf : calloc(1, 1);
    res.stderr_len = err.len;
    if (res.timed_out || res.truncated) {
        res.exit_code = -1;
    } else if (WIFEXITED(status)) {
        res.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res.exit_code = 128 + WTERMSIG(status);
    }
    return res;
}
