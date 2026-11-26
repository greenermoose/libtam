#include "tam_render.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

int tam_source_open_file(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return -1;
    struct stat st;
    int error = 0;
    if (fstat(fd, &st) < 0)
        error = errno;
    else if (!S_ISREG(st.st_mode))
        error = S_ISDIR(st.st_mode) ? EISDIR : EINVAL;
    if (!error)
        return fd;
    close(fd);
    errno = error;
    return -1;
}

static int open_spool(struct tam_source *s)
{
    s->spool = tmpfile();
    if (!s->spool)
        return -1;
    s->fd = fileno(s->spool);
    if (fcntl(s->fd, F_SETFD, FD_CLOEXEC) < 0) {
        int err = errno;
        fclose(s->spool);
        s->spool = NULL;
        errno = err;
        return -1;
    }
    return 0;
}

int tam_source_open(struct tam_source *s, int input)
{
    *s = (struct tam_source){.input = input, .fd = input, .block = -1};
    struct stat st;
    if (fstat(input, &st) < 0)
        return -1;
    if (S_ISREG(st.st_mode)) {
        s->eof = true;
        off_t start = lseek(input, 0, SEEK_CUR);
        if (start < 0)
            return -1;
        // An already-positioned stdin must start at its current byte.
        if (start == 0) {
            s->size = st.st_size;
            return 0;
        }
        s->eof = false;
    }

    return open_spool(s);
}

static int write_at(int fd, const unsigned char *data, size_t count, off_t offset)
{
    while (count) {
        ssize_t n = pwrite(fd, data, count, offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if (n == 0)
                errno = EIO;
            return -1;
        }
        data += n;
        offset += n;
        count -= (size_t)n;
    }
    return 0;
}

int tam_source_snapshot(struct tam_source *s, int input)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        struct stat before, after;
        if (fstat(input, &before) < 0)
            return -1;
        *s = (struct tam_source){.input = -1, .fd = -1, .block = -1};
        if (open_spool(s) < 0)
            return -1;
        // Read at most the initial size so a busy log cannot extend the copy.
        // Some readable pseudo-files report zero bytes even when nonempty.
        off_t limit = before.st_size > 0 ? before.st_size : INT64_MAX;
        while (s->size < limit) {
            off_t remaining = limit - s->size;
            size_t length = remaining < TAM_SOURCE_BLOCK ? (size_t)remaining : TAM_SOURCE_BLOCK;
            ssize_t n = pread(input, s->data, length, s->size);
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                goto failed;
            if (n == 0)
                break;
            // Leave holes in the temporary file for all-zero blocks.
            size_t first = 0;
            while (first < (size_t)n && s->data[first] == 0)
                first++;
            if (write_at(s->fd, s->data + first, (size_t)n - first,
                         s->size + (off_t)first) < 0)
                goto failed;
            s->size += n;
        }
        if (fstat(input, &after) < 0)
            goto failed;
        if (before.st_size == after.st_size &&
            before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
            before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
            before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
            before.st_ctim.tv_nsec == after.st_ctim.tv_nsec) {
            if (ftruncate(s->fd, s->size) < 0)
                goto failed;
            s->eof = true;
            return 0;
        }
        tam_source_close(s);
    }
    errno = EAGAIN;
    return -1;

failed:;
    int error = errno;
    tam_source_close(s);
    errno = error;
    return -1;
}

void tam_source_close(struct tam_source *s)
{
    if (s->spool)
        fclose(s->spool);
}

int tam_source_pump(struct tam_source *s)
{
    unsigned char buf[TAM_SOURCE_BLOCK];
    ssize_t count = read(s->input, buf, sizeof(buf));
    if (count < 0)
        return errno == EINTR || errno == EAGAIN ? 0 : -1;
    if (count == 0) {
        s->eof = true;
        return 0;
    }

    if (s->size > INT64_MAX - count) {
        errno = EFBIG;
        return -1;
    }
    if (write_at(s->fd, buf, (size_t)count, s->size) < 0)
        return -1;
    s->size += count;
    return 0;
}

int tam_source_byte(struct tam_source *s, off_t offset)
{
    if ((s->spool || s->bounded) && offset >= s->size)
        return s->eof ? TAM_RENDER_EOF : TAM_RENDER_WAIT;

    if (s->block >= 0 && offset >= s->block &&
        (uintmax_t)(offset - s->block) < s->used)
        return s->data[offset - s->block];

    // Align reads for backward scans. A partial block is retried from the
    // requested byte if its previous read ended before that byte.
    off_t start = offset - offset % TAM_SOURCE_BLOCK;
    if (start == s->block)
        start = offset;

    ssize_t count;
    do {
        count = pread(s->fd, s->data, sizeof(s->data), start);
    } while (count < 0 && errno == EINTR);
    s->block = start;
    s->used = count > 0 ? (size_t)count : 0;
    if (count < 0)
        return TAM_RENDER_ERROR;
    if ((offset - start) >= count)
        return TAM_RENDER_EOF;
    return s->data[offset - start];
}

off_t tam_source_size(struct tam_source *s)
{
    struct stat st;
    if (s->spool || s->bounded)
        return s->size;
    return fstat(s->fd, &st) == 0 ? st.st_size : -1;
}

int tam_source_refresh(struct tam_source *s, off_t *new_size)
{
    if (s->spool || s->bounded) {
        *new_size = s->size;
        return 0;
    }
    struct stat st;
    if (fstat(s->fd, &st) < 0)
        return -1;
    if (st.st_size != s->size) {
        s->block = -1;
        s->used = 0;
        s->size = st.st_size;
    }
    *new_size = st.st_size;
    return 0;
}
