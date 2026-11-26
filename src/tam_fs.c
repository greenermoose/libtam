#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>

bool tam_fs_file_exists(const char *path) {
    if (!path) return false;
    struct stat st;
    return (stat(path, &st) == 0);
}

/* Grow buf to hold at least need bytes plus a terminator. */
static int grow(char **buf, size_t *cap, size_t need) {
    if (need < *cap) return 0;
    size_t next = *cap ? *cap : 4096;
    while (next <= need) {
        if (next > SIZE_MAX / 2) { errno = ENOMEM; return -1; }
        next *= 2;
    }
    char *p = realloc(*buf, next);
    if (!p) { errno = ENOMEM; return -1; }
    *buf = p;
    *cap = next;
    return 0;
}

char *tam_fd_read_all(int fd, size_t max, size_t *out_len) {
    if (fd < 0) { errno = EBADF; return NULL; }
    char *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (grow(&buf, &cap, len + 4096) < 0) goto fail;
        size_t room = cap - len - 1;
        ssize_t n = read(fd, buf + len, room);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { fd, POLLIN, 0 };
                if (poll(&pfd, 1, -1) < 0 && errno != EINTR) goto fail;
                continue;
            }
            goto fail;
        }
        if (n == 0) break;
        len += (size_t)n;
        if (max > 0 && len > max) { errno = EFBIG; goto fail; }
    }
    if (grow(&buf, &cap, len) < 0) goto fail;
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
fail:;
    int saved = errno;
    free(buf);
    errno = saved;
    return NULL;
}

char *tam_fs_read(const char *path, size_t max, size_t *out_len) {
    if (!path) { errno = EINVAL; return NULL; }
    /* Non-blocking open, so a FIFO cannot stall us before fstat refuses it. */
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) < 0) goto fail;
    if (S_ISDIR(st.st_mode)) { errno = EISDIR; goto fail; }
    if (!S_ISREG(st.st_mode)) { errno = EINVAL; goto fail; }
    if (max > 0 && st.st_size > 0 && (uintmax_t)st.st_size > max) {
        errno = EFBIG;
        goto fail;
    }
    int flags = fcntl(fd, F_GETFL);
    if (flags >= 0) (void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    char *buf = tam_fd_read_all(fd, max, out_len);
    int saved = errno;
    close(fd);
    errno = saved;
    return buf;
fail:;
    int err = errno;
    close(fd);
    errno = err;
    return NULL;
}

/* The directory part of `path` ("." when it has none). Caller frees. */
static char *parent_of(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return strdup(".");
    if (slash == path) return strdup("/");
    return strndup(path, (size_t)(slash - path));
}

bool tam_fs_sync_dirat(int dirfd, const char *name) {
    int fd = openat(dirfd, name ? name : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

bool tam_fs_sync_dir(const char *path) {
    if (!path) return false;
    return tam_fs_sync_dirat(AT_FDCWD, path);
}

/* Follow a final symbolic link chain to the path the data belongs at. A
 * dangling link resolves to its missing target, so saving creates it.
 * Caller frees. */
static char *resolve_links(const char *path) {
    char *cur = strdup(path);
    if (!cur) return NULL;
    for (int hops = 0; hops < 40; hops++) {
        struct stat st;
        if (lstat(cur, &st) < 0) {
            if (errno == ENOENT) return cur;
            goto fail;
        }
        if (!S_ISLNK(st.st_mode)) return cur;
        size_t size = st.st_size > 0 ? (size_t)st.st_size + 1 : 4096;
        char *target = malloc(size);
        if (!target) goto fail;
        ssize_t n = readlink(cur, target, size);
        if (n < 0 || (size_t)n >= size) {
            if (n >= 0) errno = ENAMETOOLONG;
            free(target);
            goto fail;
        }
        target[n] = '\0';
        char *next;
        if (target[0] == '/') {
            next = target;
        } else {
            char *dir = parent_of(cur);
            next = dir ? malloc(strlen(dir) + strlen(target) + 2) : NULL;
            if (next) sprintf(next, "%s/%s", dir, target);
            free(dir);
            free(target);
            if (!next) { errno = ENOMEM; goto fail; }
        }
        free(cur);
        cur = next;
    }
    errno = ELOOP;
fail:;
    int saved = errno;
    free(cur);
    errno = saved;
    return NULL;
}

int tam_fs_write_atomic(const char *path, const void *data, size_t len,
                        int flags, mode_t mode) {
    if (!path || !path[0] || (!data && len > 0)) { errno = EINVAL; return -1; }

    char *target = (flags & TAM_FS_REPLACE_LINK) ? strdup(path) : resolve_links(path);
    if (!target) return -1;
    if (flags & TAM_FS_KEEP_MODE) {
        struct stat st;
        if (stat(target, &st) == 0 && S_ISREG(st.st_mode)) mode = st.st_mode & 07777;
    }

    size_t tlen = strlen(target);
    char *tmp_path = malloc(tlen + sizeof(".tmp.XXXXXX"));
    if (!tmp_path) { free(target); errno = ENOMEM; return -1; }
    memcpy(tmp_path, target, tlen);
    memcpy(tmp_path + tlen, ".tmp.XXXXXX", sizeof(".tmp.XXXXXX"));

    int fd = mkostemp(tmp_path, O_CLOEXEC);
    if (fd < 0) {
        int saved = errno;
        free(tmp_path);
        free(target);
        errno = saved;
        return -1;
    }

    bool ok = !mode || fchmod(fd, mode) == 0;
    const char *p = (const char *)data;
    size_t rem = len;
    while (ok && rem > 0) {
        ssize_t written = write(fd, p, rem);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            if (written == 0) errno = EIO;
            ok = false;
        } else {
            rem -= (size_t)written;
            p += written;
        }
    }
    ok = ok && fsync(fd) == 0;
    int close_rc = close(fd);
    ok = ok && close_rc == 0;
    ok = ok && rename(tmp_path, target) == 0;
    if (!ok) {
        int saved = errno;
        unlink(tmp_path);
        free(tmp_path);
        free(target);
        errno = saved;
        return -1;
    }
    free(tmp_path);

    char *dir = parent_of(target);
    free(target);
    bool synced = dir && tam_fs_sync_dir(dir);
    int saved = errno;
    free(dir);
    if (!synced) {
        errno = saved ? saved : ENOMEM;
        return 1;
    }
    return 0;
}

bool tam_fs_mkdir_p(const char *path, mode_t mode) {
    if (!path || path[0] == '\0') return false;
    size_t len = strlen(path);
    char *tmp = malloc(len + 1);
    if (!tmp) return false;
    memcpy(tmp, path, len + 1);
    bool ok = true;
    for (char *p = tmp + 1; ok; p++) {
        if (*p != '/' && *p != '\0') continue;
        char saved = *p;
        *p = '\0';
        if (mkdir(tmp, mode) != 0 && errno != EEXIST) ok = false;
        *p = saved;
        if (saved == '\0') break;
    }
    struct stat st;
    ok = ok && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    free(tmp);
    return ok;
}
