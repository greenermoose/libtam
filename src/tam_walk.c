/* Sorted, descriptor-based directory walk that never follows links. */
#include "tam.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WALK_MAX_DEPTH 256

typedef struct {
    int         flags;
    dev_t       dev;
    tam_walk_fn fn;
    void       *user;
    char        path[PATH_MAX];
} walk_t;

static int cmp_names(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void free_names(char **names, size_t n) {
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);
}

/* The entry names of an open directory, sorted bytewise. Leaves dfd open. */
static char **read_names(int dfd, size_t *count) {
    int dup_fd = fcntl(dfd, F_DUPFD_CLOEXEC, 0);
    if (dup_fd < 0) return NULL;
    DIR *dir = fdopendir(dup_fd);
    if (!dir) {
        close(dup_fd);
        return NULL;
    }
    rewinddir(dir);
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *de;
    errno = 0;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (n == cap) {
            size_t ncap = cap ? cap * 2 : 32;
            char **grown = realloc(names, ncap * sizeof *names);
            if (!grown) goto fail;
            names = grown;
            cap = ncap;
        }
        if (!(names[n] = strdup(de->d_name))) goto fail;
        n++;
        errno = 0;
    }
    if (errno != 0) goto fail;
    closedir(dir);
    if (n > 1) qsort(names, n, sizeof *names, cmp_names);
    *count = n;
    if (!names) names = malloc(sizeof *names); /* an empty, non-NULL list */
    return names;
fail: {
        int saved = errno ? errno : ENOMEM;
        free_names(names, n);
        closedir(dir);
        errno = saved;
        return NULL;
    }
}

static int walk_dir(walk_t *w, int dfd, size_t plen, int depth) {
    if (depth > WALK_MAX_DEPTH) {
        errno = ELOOP;
        return -1;
    }
    size_t count = 0;
    char **names = read_names(dfd, &count);
    if (!names) return -1;

    int result = 0;
    for (size_t i = 0; i < count && result == 0; i++) {
        const char *name = names[i];
        size_t nlen = strlen(name);
        size_t need = plen + (plen ? 1 : 0) + nlen;
        if (need >= sizeof w->path) {
            errno = ENAMETOOLONG;
            result = -1;
            break;
        }
        size_t pos = plen;
        if (plen) w->path[pos++] = '/';
        memcpy(w->path + pos, name, nlen + 1);

        struct stat st;
        if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            result = -1;
            break;
        }
        tam_walk_entry_t entry = {
            .path = w->path, .name = name, .dirfd = dfd, .depth = depth, .st = &st,
        };
        int r = w->fn(&entry, w->user);
        if (r < 0) {
            result = r;
            break;
        }
        if (r == TAM_WALK_SKIP || !S_ISDIR(st.st_mode)) continue;

        if ((w->flags & TAM_WALK_ONE_DEVICE) && st.st_dev != w->dev) {
            errno = EXDEV;
            result = -1;
            break;
        }
        int child = openat(dfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (child < 0) {
            result = -1;
            break;
        }
        struct stat cst;
        if (fstat(child, &cst) != 0 || cst.st_dev != st.st_dev || cst.st_ino != st.st_ino) {
            close(child);
            errno = ESTALE; /* replaced between the stat and the open */
            result = -1;
            break;
        }
        result = walk_dir(w, child, need, depth + 1);
        int saved = errno;
        close(child);
        errno = saved;
        w->path[plen] = '\0';
    }
    int saved = errno;
    free_names(names, count);
    errno = saved;
    return result;
}

int tam_fs_walk(const char *root, int flags, tam_walk_fn fn, void *user) {
    if (!root || !fn) {
        errno = EINVAL;
        return -1;
    }
    walk_t *w = malloc(sizeof *w);
    if (!w) return -1;
    w->flags = flags;
    w->fn = fn;
    w->user = user;
    w->path[0] = '\0';

    int result = -1;
    int dfd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    struct stat st;
    if (dfd >= 0 && fstat(dfd, &st) == 0) {
        w->dev = st.st_dev;
        result = walk_dir(w, dfd, 0, 1);
    }
    int saved = errno;
    if (dfd >= 0) close(dfd);
    free(w);
    errno = saved;
    return result;
}
