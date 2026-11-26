#define _GNU_SOURCE
#include "tam_proc.h"
#include "tam_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int is_program(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

char *tam_which(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        errno = ENOENT;
        return NULL;
    }
    if (strchr(name, '/') != NULL) {
        if (is_program(name)) return strdup(name);
        errno = ENOENT;
        return NULL;
    }
    const char *path = getenv("PATH");
    if (path == NULL || path[0] == '\0') path = "/usr/local/bin:/usr/bin:/bin";
    size_t nlen = strlen(name);
    for (const char *p = path;;) {
        const char *end = strchr(p, ':');
        size_t dlen = end != NULL ? (size_t)(end - p) : strlen(p);
        const char *dir = dlen > 0 ? p : ".";
        size_t use = dlen > 0 ? dlen : 1;
        char *cand = malloc(use + nlen + 2);
        if (cand == NULL) return NULL;
        memcpy(cand, dir, use);
        cand[use] = '/';
        memcpy(cand + use + 1, name, nlen + 1);
        if (is_program(cand)) return cand;
        free(cand);
        if (end == NULL) break;
        p = end + 1;
    }
    errno = ENOENT;
    return NULL;
}

int tam_spawn_detached(const char *const argv[])
{
    if (argv == NULL || argv[0] == NULL) {
        errno = EINVAL;
        return -1;
    }
    int report[2];
    if (pipe2(report, O_CLOEXEC) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        int err = errno;
        close(report[0]);
        close(report[1]);
        errno = err;
        return -1;
    }
    if (pid == 0) {
        close(report[0]);
        /* A second fork leaves the program to init, so it never becomes our
         * zombie, and setsid detaches it from our terminal. */
        pid_t grand = fork();
        if (grand < 0) tam__child_fail(report[1], errno);
        if (grand > 0) _exit(0);
        if (setsid() < 0) tam__child_fail(report[1], errno);
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDIN_FILENO);
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) close(null_fd);
        }
        tam__child_close_from(STDERR_FILENO + 1, report[1]);
        tam__child_default_signals();
        execvp(argv[0], (char *const *)argv);
        tam__child_fail(report[1], errno);
    }
    close(report[1]);
    int child_error = 0;
    ssize_t count;
    do {
        count = read(report[0], &child_error, sizeof(child_error));
    } while (count < 0 && errno == EINTR);
    int read_error = errno;
    close(report[0]);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (count < 0) {
        errno = read_error;
        return -1;
    }
    if (count != 0) {
        errno = count == (ssize_t)sizeof(child_error) ? child_error : EIO;
        return -1;
    }
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (waited >= 0) errno = EIO;
        return -1;
    }
    return 0;
}

int tam_run_foreground(const char *const argv[], int *exit_code)
{
    if (argv == NULL || argv[0] == NULL) {
        errno = EINVAL;
        return -1;
    }
    int report[2];
    if (pipe2(report, O_CLOEXEC) < 0) return -1;

    struct sigaction ign, old_int, old_quit;
    memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    int have_int = sigaction(SIGINT, &ign, &old_int) == 0;
    int have_quit = sigaction(SIGQUIT, &ign, &old_quit) == 0;

    pid_t pid = fork();
    if (pid == 0) {
        close(report[0]);
        tam__child_default_signals();
        execvp(argv[0], (char *const *)argv);
        tam__child_fail(report[1], errno);
    }
    int fork_error = errno;
    close(report[1]);
    int err = 0;
    if (pid < 0) {
        err = fork_error;
    } else {
        int child_error = 0;
        ssize_t count;
        do {
            count = read(report[0], &child_error, sizeof(child_error));
        } while (count < 0 && errno == EINTR);
        if (count == (ssize_t)sizeof(child_error)) err = child_error;
    }
    close(report[0]);

    int status = 0;
    pid_t waited = -1;
    if (pid > 0) {
        do {
            waited = waitpid(pid, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0 && err == 0) err = errno;
    }
    if (have_int) (void)sigaction(SIGINT, &old_int, NULL);
    if (have_quit) (void)sigaction(SIGQUIT, &old_quit, NULL);

    if (err != 0) {
        errno = err;
        return -1;
    }
    if (exit_code != NULL) {
        if (WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) *exit_code = 128 + WTERMSIG(status);
        else *exit_code = -1;
    }
    return 0;
}
