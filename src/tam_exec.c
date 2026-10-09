#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

void tam_exec_result_free(tam_exec_result_t *res) {
    if (!res) return;
    free(res->stdout_buf);
    free(res->stderr_buf);
    res->stdout_buf = NULL;
    res->stderr_buf = NULL;
    res->stdout_len = 0;
    res->stderr_len = 0;
}

tam_exec_result_t tam_exec(const char *const argv[], int timeout_ms) {
    tam_exec_result_t res;
    memset(&res, 0, sizeof(res));
    res.exit_code = -1;

    if (!argv || !argv[0]) {
        return res;
    }

    int out_pipe[2];
    int err_pipe[2];

    if (pipe(out_pipe) < 0) return res;
    if (pipe(err_pipe) < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        return res;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return res;
    }

    if (pid == 0) {
        /* Child */
        close(out_pipe[0]);
        close(err_pipe[0]);

        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);

        close(out_pipe[1]);
        close(err_pipe[1]);

        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    /* Parent */
    close(out_pipe[1]);
    close(err_pipe[1]);

    /* Make read ends non-blocking */
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

    size_t out_cap = 4096, err_cap = 4096;
    res.stdout_buf = malloc(out_cap);
    res.stderr_buf = malloc(err_cap);
    if (res.stdout_buf) res.stdout_buf[0] = '\0';
    if (res.stderr_buf) res.stderr_buf[0] = '\0';

    struct pollfd fds[2];
    fds[0].fd = out_pipe[0];
    fds[0].events = POLLIN;
    fds[1].fd = err_pipe[0];
    fds[1].events = POLLIN;

    int remaining_ms = timeout_ms > 0 ? timeout_ms : 30000;
    int open_pipes = 2;

    while (open_pipes > 0 && remaining_ms > 0) {
        int poll_timeout = (remaining_ms < 100) ? remaining_ms : 100;
        int p = poll(fds, 2, poll_timeout);
        remaining_ms -= poll_timeout;

        if (p < 0 && errno == EINTR) continue;

        if (p > 0) {
            char buf[1024];
            if (fds[0].fd >= 0 && (fds[0].revents & (POLLIN | POLLHUP))) {
                ssize_t n = read(fds[0].fd, buf, sizeof(buf));
                if (n > 0) {
                    if (res.stdout_len + (size_t)n + 1 > out_cap) {
                        out_cap = (res.stdout_len + (size_t)n + 1) * 2;
                        char *new_buf = realloc(res.stdout_buf, out_cap);
                        if (new_buf) res.stdout_buf = new_buf;
                    }
                    if (res.stdout_buf) {
                        memcpy(res.stdout_buf + res.stdout_len, buf, (size_t)n);
                        res.stdout_len += (size_t)n;
                        res.stdout_buf[res.stdout_len] = '\0';
                    }
                } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    close(fds[0].fd);
                    fds[0].fd = -1;
                    open_pipes--;
                }
            }

            if (fds[1].fd >= 0 && (fds[1].revents & (POLLIN | POLLHUP))) {
                ssize_t n = read(fds[1].fd, buf, sizeof(buf));
                if (n > 0) {
                    if (res.stderr_len + (size_t)n + 1 > err_cap) {
                        err_cap = (res.stderr_len + (size_t)n + 1) * 2;
                        char *new_buf = realloc(res.stderr_buf, err_cap);
                        if (new_buf) res.stderr_buf = new_buf;
                    }
                    if (res.stderr_buf) {
                        memcpy(res.stderr_buf + res.stderr_len, buf, (size_t)n);
                        res.stderr_len += (size_t)n;
                        res.stderr_buf[res.stderr_len] = '\0';
                    }
                } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    close(fds[1].fd);
                    fds[1].fd = -1;
                    open_pipes--;
                }
            }
        }
    }

    if (fds[0].fd >= 0) close(fds[0].fd);
    if (fds[1].fd >= 0) close(fds[1].fd);

    if (remaining_ms <= 0) {
        res.timed_out = true;
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        res.exit_code = -1;
        return res;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        res.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res.exit_code = 128 + WTERMSIG(status);
    }

    return res;
}
