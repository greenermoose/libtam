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

static int read_trimmed_line(const char *path, char *buf, size_t max_len) {
    if (tam_read_line(path, buf, max_len) < 0) return -1;
    return version_token_ok(buf) ? 0 : -1;
}

int tam_install_guard(const char *program) {
    const char *name = (program && program[0]) ? program : "tamlinux";
    int pending = tam_install_pending(NULL);
    if (pending == 0) return 0;
    if (pending > 0) {
        fprintf(stderr, "%s: Tamlinux has an interrupted update pending; finish "
                        "recovery from the console.\n", name);
        return 1;
    }
    fprintf(stderr, "%s: cannot check for an interrupted Tamlinux update: %s\n",
            name, strerror(errno));
    return -1;
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
