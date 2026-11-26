#define _GNU_SOURCE
#include "tam.h"
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int path_exists(const char *path) {
    struct stat st;
    if (lstat(path, &st) == 0) return 1;
    if (errno == ENOENT) return 0;
    return -1;
}

int tam_install_pending(const char *pending_path) {
    if (pending_path && *pending_path) {
        return path_exists(pending_path);
    }
    return path_exists("/var/lib/tamlinux/pending.json");
}

/* A version token: letters, digits and . _ + - only. */
static bool version_token_ok(const char *s) {
    if (!*s) return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '+' ||
              c == '-'))
            return false;
    }
    return true;
}

/* Read the first line of a small file, trimmed. Fails on a line that does
 * not fit, rather than returning part of it. */
static int read_trimmed_line(const char *path, char *buf, size_t max_len) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }
    char line[512];
    size_t n = 0;
    while (n < sizeof(line) - 1) {
        ssize_t r = read(fd, line + n, sizeof(line) - 1 - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        n += (size_t)r;
    }
    close(fd);
    line[n] = '\0';
    char *nl = strchr(line, '\n');
    if (nl) {
        *nl = '\0';
    } else if (n == sizeof(line) - 1) {
        return -1;
    }
    n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t'))
        line[--n] = '\0';
    char *start = line;
    while (*start == ' ' || *start == '\t') start++;
    n = strlen(start);
    if (n == 0 || n >= max_len || !version_token_ok(start)) return -1;
    memcpy(buf, start, n + 1);
    return 0;
}

int tam_cultivar_read_version(const char *root, char *buf, size_t max_len) {
    if (!buf || max_len < 2) return -1;
    const char *prefix = (root && root[0] && strcmp(root, "/") != 0) ? root : "";
    char path[1024];

    if (snprintf(path, sizeof(path), "%s/usr/lib/tamlinux/current/VERSION", prefix) < (int)sizeof(path) &&
        read_trimmed_line(path, buf, max_len) == 0) {
        return 0;
    }
    return -1;
}
