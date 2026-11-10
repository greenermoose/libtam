#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

bool tam_fs_file_exists(const char *path) {
    if (!path) return false;
    struct stat st;
    return (stat(path, &st) == 0);
}

char *tam_fs_read_file(const char *path, size_t *out_len) {
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
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

bool tam_fs_write_atomic(const char *path, const void *data, size_t len, mode_t mode) {
    if (!path || (!data && len > 0)) return false;

    char tmp_path[4096];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) return false;

    int fd = mkstemp(tmp_path);
    if (fd < 0) return false;

    bool ok = !mode || fchmod(fd, mode) == 0;
    const char *p = (const char *)data;
    size_t rem = len;
    while (ok && rem > 0) {
        ssize_t written = write(fd, p, rem);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) ok = false;
        else {
            rem -= (size_t)written;
            p += written;
        }
    }
    ok = ok && fsync(fd) == 0;
    ok = close(fd) == 0 && ok;
    ok = ok && rename(tmp_path, path) == 0;
    if (!ok) {
        int saved = errno;
        unlink(tmp_path);
        errno = saved;
        return false;
    }

    char *dir = parent_of(path);
    ok = dir && tam_fs_sync_dir(dir);
    free(dir);
    return ok;
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
