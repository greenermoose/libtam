#define _GNU_SOURCE
#include "tam.h"
#include <errno.h>
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
    int r = path_exists("/var/lib/tamlinux/pending.json");
    if (r != 0) return r;
    return path_exists("/var/lib/tamlinux/source/pending.json");
}

static int read_trimmed_line(const char *path, char *buf, size_t max_len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                     line[n - 1] == ' ' || line[n - 1] == '\t')) {
        line[--n] = '\0';
    }
    if (n == 0 || n >= max_len) return -1;
    memcpy(buf, line, n + 1);
    return 0;
}

static int read_bundle_version(const char *path, char *buf, size_t max_len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    int found = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "version ", 8) == 0) {
            char *val = line + 8;
            size_t n = strlen(val);
            while (n > 0 && (val[n - 1] == '\n' || val[n - 1] == '\r' ||
                             val[n - 1] == ' ' || val[n - 1] == '\t')) {
                val[--n] = '\0';
            }
            if (n > 0 && n < max_len) {
                memcpy(buf, val, n + 1);
                found = 0;
            }
            break;
        }
    }
    fclose(f);
    return found;
}

int tam_cultivar_read_version(const char *root, char *buf, size_t max_len) {
    if (!buf || max_len < 2) return -1;
    const char *prefix = (root && root[0] && strcmp(root, "/") != 0) ? root : "";
    char path[1024];

    if (snprintf(path, sizeof(path), "%s/usr/lib/tamlinux/current/VERSION", prefix) < (int)sizeof(path) &&
        read_trimmed_line(path, buf, max_len) == 0) {
        return 0;
    }
    if (snprintf(path, sizeof(path), "%s/usr/lib/tamlinux/source/current/VERSION", prefix) < (int)sizeof(path) &&
        read_trimmed_line(path, buf, max_len) == 0) {
        return 0;
    }
    if (snprintf(path, sizeof(path), "%s/usr/lib/tamlinux/source/current/BUNDLE", prefix) < (int)sizeof(path) &&
        read_bundle_version(path, buf, max_len) == 0) {
        return 0;
    }
    return -1;
}
