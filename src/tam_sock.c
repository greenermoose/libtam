#define _GNU_SOURCE
#include "tam_sock.h"
#include "tam_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define TAM_SOCK_DEFAULT_TIMEOUT_MS 5000

static int fill_addr(struct sockaddr_un *addr, const char *path)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    size_t len = strlen(path);
    memset(addr, 0, sizeof(*addr));
    if (len >= sizeof(addr->sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    addr->sun_family = AF_UNIX;
    memcpy(addr->sun_path, path, len + 1);
    return 0;
}

/* Is anyone listening at this socket path? 1 live, 0 stale, -1 unknown. */
static int socket_is_live(const struct sockaddr_un *addr)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int rc = connect(fd, (const struct sockaddr *)addr, sizeof(*addr));
    int err = errno;
    close(fd);
    if (rc == 0) return 1;
    return err == ECONNREFUSED ? 0 : -1;
}

int tam_sock_listen(const char *path, mode_t mode, int backlog)
{
    struct sockaddr_un addr;
    if (fill_addr(&addr, path) < 0) return -1;

    struct stat st;
    if (lstat(path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            errno = EEXIST;
            return -1;
        }
        int live = socket_is_live(&addr);
        if (live != 0) {
            errno = live > 0 ? EADDRINUSE : EEXIST;
            return -1;
        }
        if (unlink(path) < 0 && errno != ENOENT) return -1;
    } else if (errno != ENOENT) {
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    mode_t old = umask((mode_t)(~mode & 0777));
    int rc = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    int err = errno;
    umask(old);
    if (rc < 0 || listen(fd, backlog > 0 ? backlog : 16) < 0) {
        if (rc == 0) {
            err = errno;
            unlink(path);
        }
        close(fd);
        errno = err;
        return -1;
    }
    return fd;
}

int tam_sock_peer_uid(int fd, uid_t *uid)
{
    if (uid == NULL) {
        errno = EINVAL;
        return -1;
    }
    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) return -1;
    if (len != sizeof(cred)) {
        errno = EPROTO;
        return -1;
    }
    *uid = cred.uid;
    return 0;
}

int tam_sock_connect(const char *path, int timeout_ms)
{
    struct sockaddr_un addr;
    if (fill_addr(&addr, path) < 0) return -1;
    if (timeout_ms <= 0) timeout_ms = TAM_SOCK_DEFAULT_TIMEOUT_MS;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    int rc = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && (errno == EINPROGRESS || errno == EAGAIN)) {
        long long deadline = tam__now_ms() + timeout_ms;
        for (;;) {
            long long left = deadline - tam__now_ms();
            if (left <= 0) {
                close(fd);
                errno = ETIMEDOUT;
                return -1;
            }
            struct pollfd pfd = { fd, POLLOUT, 0 };
            int p = poll(&pfd, 1, (int)left);
            if (p < 0 && errno == EINTR) continue;
            if (p < 0) break;
            if (p == 0) continue;
            int soerr = 0;
            socklen_t len = sizeof(soerr);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) < 0) soerr = errno;
            if (soerr != 0) {
                close(fd);
                errno = soerr;
                return -1;
            }
            rc = 0;
            break;
        }
    }
    if (rc < 0) {
        int err = errno;
        close(fd);
        errno = err;
        return -1;
    }
    int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) (void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    return fd;
}

ssize_t tam_sock_read_line(int fd, char *buf, size_t size, int timeout_ms)
{
    if (buf == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (timeout_ms <= 0) timeout_ms = TAM_SOCK_DEFAULT_TIMEOUT_MS;
    long long deadline = tam__now_ms() + timeout_ms;
    size_t used = 0;
    ssize_t result = -1;
    int err = 0;
    /* Requests may carry secrets (tam-hide's passcodes): the peek copy is
     * wiped on every exit, and buf on failure. */
    char peek[512];
    buf[0] = '\0';
    for (;;) {
        long long left = deadline - tam__now_ms();
        if (left <= 0) {
            err = ETIMEDOUT;
            break;
        }
        struct pollfd pfd = { fd, POLLIN, 0 };
        int p = poll(&pfd, 1, (int)left);
        if (p < 0) {
            if (errno == EINTR) continue;
            err = errno;
            break;
        }
        if (p == 0) continue;

        /* Peek, so bytes after the newline stay for the next read. */
        size_t room = size - 1 - used;
        size_t want = room + 1 < sizeof(peek) ? room + 1 : sizeof(peek);
        ssize_t n = recv(fd, peek, want, MSG_PEEK | MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            err = errno;
            break;
        }
        if (n == 0) {
            if (used == 0) {
                err = ENODATA;
            } else {
                buf[used] = '\0';
                result = (ssize_t)used;
            }
            break;
        }
        char *nl = memchr(peek, '\n', (size_t)n);
        size_t take = nl != NULL ? (size_t)(nl - peek) + 1 : (size_t)n;
        size_t keep = nl != NULL ? take - 1 : take;
        if (keep > room) {
            err = EMSGSIZE;
            break;
        }
        ssize_t got;
        do {
            got = recv(fd, buf + used, take, MSG_DONTWAIT);
        } while (got < 0 && errno == EINTR);
        if (got != (ssize_t)take) {
            err = got >= 0 ? EIO : errno;
            break;
        }
        used += keep;
        buf[used] = '\0';
        if (nl != NULL) {
            result = (ssize_t)used;
            break;
        }
    }
    explicit_bzero(peek, sizeof(peek));
    if (result < 0) {
        explicit_bzero(buf, size);
        errno = err;
    }
    return result;
}
