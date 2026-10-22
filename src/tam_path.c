#include "tam.h"
#include <stdlib.h>
#include <string.h>

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

char *tam_xdg_path(tam_xdg_kind_t kind, const char *command, const char *leaf,
                   bool create) {
    static const struct { const char *var, *fallback; } bases[] = {
        [TAM_XDG_STATE]   = { "XDG_STATE_HOME", ".local/state" },
        [TAM_XDG_CACHE]   = { "XDG_CACHE_HOME", ".cache" },
        [TAM_XDG_DATA]    = { "XDG_DATA_HOME", ".local/share" },
        [TAM_XDG_CONFIG]  = { "XDG_CONFIG_HOME", ".config" },
        [TAM_XDG_RUNTIME] = { "XDG_RUNTIME_DIR", NULL },
    };
    if ((unsigned)kind > TAM_XDG_RUNTIME || !command || command[0] == '\0' ||
        strchr(command, '/') != NULL) {
        return NULL;
    }
    char *base = NULL;
    const char *env = xdg_base(bases[kind].var);
    const char *home = xdg_base("HOME");
    if (env) {
        base = strdup(env);
    } else if (bases[kind].fallback && home) {
        base = tam_path_join(home, bases[kind].fallback);
    }
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
