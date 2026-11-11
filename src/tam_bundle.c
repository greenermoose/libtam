#define _GNU_SOURCE
#include "tam.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    bool found;
    bool blocked;
} bundle_state;

static int state_line(void *context, size_t line, int format, char **fields, size_t count) {
    (void)line;
    (void)format;
    bundle_state *state = context;
    if (count != 2) return 1;
    if (strcmp(fields[0], "state")) return 0;
    if (state->found) return 1;
    state->found = true;
    if (!strcmp(fields[1], "applying") || !strcmp(fields[1], "activating")) {
        state->blocked = true;
        return 0;
    }
    return strcmp(fields[1], "staging") && strcmp(fields[1], "ready") &&
           strcmp(fields[1], "complete");
}

int tam_bundle_guard(const char *transaction_path) {
    const char *path = transaction_path ? transaction_path : "/var/lib/tamlinux/transaction";
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) return 0;
        if (!transaction_path && errno == EACCES) {
            struct stat legacy;
            /* Format 1 has a real current directory and no format-2
             * transaction. Its root-private state remains supported. */
            if (!lstat("/usr/lib/tamlinux/current", &legacy) && S_ISDIR(legacy.st_mode))
                return 0;
        }
        return -1;
    }
    struct stat before, after;
    char record[4097];
    int result = -1;
    size_t used = 0;
    if (fstat(fd, &before) || !S_ISREG(before.st_mode) ||
        (before.st_uid != 0 && (!transaction_path || before.st_uid != geteuid())) ||
        (before.st_mode & 0022) || before.st_size <= 0 || before.st_size > 4096) {
        errno = EINVAL;
        goto done;
    }
    while (used < (size_t)before.st_size) {
        ssize_t count = read(fd, record + used, (size_t)before.st_size - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) goto done;
        used += (size_t)count;
    }
    if (fstat(fd, &after) || before.st_size != after.st_size ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
        errno = EAGAIN;
        goto done;
    }
    record[used] = '\0';
    bundle_state state = {0};
    if (tam_rec_parse(record, used, 2, 1, state_line, &state, NULL) || !state.found) {
        errno = EINVAL;
        goto done;
    }
    if (state.blocked) {
        errno = EBUSY;
        goto done;
    }
    result = 0;
done:
    {
        int saved = errno;
        close(fd);
        errno = saved;
    }
    return result;
}
