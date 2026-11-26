#include "tam_io.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int tam_write_all(int fd, const void *buf, size_t len)
{
    if (fd < 0 || (buf == NULL && len > 0)) {
        errno = fd < 0 ? EBADF : EINVAL;
        return -1;
    }
    const char *p = buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { fd, POLLOUT, 0 };
                if (poll(&pfd, 1, -1) < 0 && errno != EINTR) return -1;
                continue;
            }
            return -1;
        }
        if (n == 0) {
            errno = EIO;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

int tam_read_line(const char *path, char *buf, size_t size)
{
    if (path == NULL || buf == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) < 0) {
        int err = errno;
        close(fd);
        errno = err;
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        errno = S_ISDIR(st.st_mode) ? EISDIR : EINVAL;
        return -1;
    }

    /* Read until the first newline or end of file. A line longer than the
     * buffer, even after trimming, is refused rather than cut. */
    char chunk[512];
    size_t used = 0;
    int done = 0, too_long = 0;
    while (!done) {
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            int err = errno;
            close(fd);
            errno = err;
            return -1;
        }
        if (n == 0) break;
        for (ssize_t i = 0; i < n; i++) {
            if (chunk[i] == '\n') {
                done = 1;
                break;
            }
            if (used == 0 && is_blank(chunk[i])) continue;
            if (used + 1 < size) {
                buf[used++] = chunk[i];
            } else if (!is_blank(chunk[i])) {
                too_long = 1;
            }
        }
        if (too_long) break;
    }
    close(fd);
    if (too_long) {
        buf[0] = '\0';
        errno = ERANGE;
        return -1;
    }
    while (used > 0 && is_blank(buf[used - 1])) used--;
    buf[used] = '\0';
    return 0;
}

int64_t tam_monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int64_t tam_monotonic_ms(void)
{
    return tam_monotonic_ns() / 1000000;
}
