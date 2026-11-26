#include "tam.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

char *tam_path_join(const char *dir, const char *rest) {
    if (!dir || !rest) return NULL;
    size_t dlen = strlen(dir);
    while (dlen > 1 && dir[dlen - 1] == '/') dlen--;
    while (rest[0] == '/') rest++;
    size_t rlen = strlen(rest);
    char *out = malloc(dlen + rlen + 2);
    if (!out) return NULL;
    memcpy(out, dir, dlen);
    size_t pos = dlen;
    if (rlen > 0 && pos > 0 && out[pos - 1] != '/') out[pos++] = '/';
    memcpy(out + pos, rest, rlen + 1);
    return out;
}

char *tam_path_join_under(const char *root, const char *rel) {
    if (!root || !rel || rel[0] == '\0' || rel[0] == '/') return NULL;
    for (const char *p = rel; *p != '\0';) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == 2 && p[0] == '.' && p[1] == '.') return NULL;
        p += n;
        while (*p == '/') p++;
    }
    return tam_path_join(root, rel);
}

/* An XDG base variable counts only when it is an absolute path. */
static const char *xdg_base(const char *var) {
    const char *v = getenv(var);
    return v && v[0] == '/' ? v : NULL;
}

static const struct { const char *var, *fallback; } xdg_bases[] = {
    [TAM_XDG_STATE]   = { "XDG_STATE_HOME", ".local/state" },
    [TAM_XDG_CACHE]   = { "XDG_CACHE_HOME", ".cache" },
    [TAM_XDG_DATA]    = { "XDG_DATA_HOME", ".local/share" },
    [TAM_XDG_CONFIG]  = { "XDG_CONFIG_HOME", ".config" },
    [TAM_XDG_RUNTIME] = { "XDG_RUNTIME_DIR", NULL },
};

char *tam_xdg_base(tam_xdg_kind_t kind) {
    if ((unsigned)kind > TAM_XDG_RUNTIME) {
        errno = EINVAL;
        return NULL;
    }
    const char *env = xdg_base(xdg_bases[kind].var);
    if (env) return strdup(env);
    const char *home = xdg_base("HOME");
    if (xdg_bases[kind].fallback && home) return tam_path_join(home, xdg_bases[kind].fallback);
    errno = ENOENT;
    return NULL;
}

char *tam_xdg_path(tam_xdg_kind_t kind, const char *command, const char *leaf,
                   bool create) {
    if ((unsigned)kind > TAM_XDG_RUNTIME || !command || command[0] == '\0' ||
        strchr(command, '/') != NULL) {
        return NULL;
    }
    char *base = tam_xdg_base(kind);
    if (!base) return NULL;
    char *top = tam_path_join(base, "tamlinux");
    free(base);
    char *dir = top ? tam_path_join(top, command) : NULL;
    free(top);
    if (!dir) return NULL;
    if (create && !tam_fs_mkdir_p(dir, 0700)) {
        free(dir);
        return NULL;
    }
    if (!leaf) return dir;
    char *path = tam_path_join_under(dir, leaf);
    free(dir);
    return path;
}

int tam_xdg_search(tam_xdg_kind_t kind, tam_xdg_dir_fn fn, void *user) {
    if (!fn) return -1;
    char *home = tam_xdg_base(kind);
    if (home) {
        int rc = fn(home, user);
        free(home);
        if (rc != 0) return rc;
    }
    const char *list = NULL, *fallback = NULL;
    if (kind == TAM_XDG_DATA) {
        list = getenv("XDG_DATA_DIRS");
        fallback = "/usr/local/share:/usr/share";
    } else if (kind == TAM_XDG_CONFIG) {
        list = getenv("XDG_CONFIG_DIRS");
        fallback = "/etc/xdg";
    } else {
        return 0;
    }
    if (!list || !list[0]) list = fallback;
    for (const char *p = list;;) {
        const char *end = strchr(p, ':');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0 && p[0] == '/') {
            char *dir = strndup(p, len);
            if (!dir) return -1;
            int rc = fn(dir, user);
            free(dir);
            if (rc != 0) return rc;
        }
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

/* A real directory (not a link) owned by us with no group or other bits. */
static int private_dir_ok(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) return -1;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    if (st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

char *tam_runtime_dir(const char *name) {
    if (!name || !name[0] || strchr(name, '/') || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        errno = EINVAL;
        return NULL;
    }
    char *base = tam_xdg_base(TAM_XDG_RUNTIME);
    if (!base) return NULL;
    if (private_dir_ok(base) < 0) {
        int err = errno;
        free(base);
        errno = err;
        return NULL;
    }
    char *dir = tam_path_join(base, name);
    free(base);
    if (!dir) return NULL;
    if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
        int err = errno;
        free(dir);
        errno = err;
        return NULL;
    }
    if (private_dir_ok(dir) < 0) {
        int err = errno;
        free(dir);
        errno = err;
        return NULL;
    }
    return dir;
}

bool tam_path_is_safe_rel(const char *rel) {
    if (!rel || rel[0] == '\0' || rel[0] == '/') return false;
    const char *p = rel;
    for (;;) {
        const char *start = p;
        while (*p != '/' && *p != '\0') {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c == 0x7f) return false;
            p++;
        }
        size_t n = (size_t)(p - start);
        if (n == 0) return false;                       /* "//" or trailing "/" */
        if (n == 1 && start[0] == '.') return false;
        if (n == 2 && start[0] == '.' && start[1] == '.') return false;
        if (*p == '\0') return true;
        p++;
    }
}
