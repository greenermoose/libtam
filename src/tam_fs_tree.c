/* Whole-tree file system operations: exchange, clone or copy, removal, lock. */
#include "tam.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1 << 1)
#endif

int tam_fs_exchange(int olddirfd, const char *oldname, int newdirfd, const char *newname) {
    if (!oldname || !newname) {
        errno = EINVAL;
        return -1;
    }
    /* The raw system call, so the same code builds against C libraries
     * without a renameat2 wrapper. */
    return (int)syscall(SYS_renameat2, olddirfd, oldname, newdirfd, newname,
                        (unsigned)RENAME_EXCHANGE);
}

static int copy_read_write(int srcfd, int dstfd) {
    char buf[65536];
    off_t off = 0;
    for (;;) {
        ssize_t n = pread(srcfd, buf, sizeof buf, off);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return -1;
        if (n == 0) return 0;
        for (ssize_t done = 0; done < n;) {
            ssize_t w = pwrite(dstfd, buf + done, (size_t)(n - done), off + done);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) return -1;
            done += w;
        }
        off += n;
    }
}

int tam_fs_clone_or_copy(int srcfd, int dstfd, bool *cloned) {
    if (cloned) *cloned = false;
    struct stat st;
    if (fstat(srcfd, &st) != 0) return -1;
    if (!S_ISREG(st.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    if (ioctl(dstfd, FICLONE, srcfd) == 0) {
        if (cloned) *cloned = true;
        return 0;
    }
    if (ftruncate(dstfd, 0) != 0) return -1;

    loff_t in = 0, out = 0;
    off_t left = st.st_size;
    while (left > 0) {
        ssize_t n = copy_file_range(srcfd, &in, dstfd, &out, (size_t)left, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == ENOSYS || errno == EXDEV || errno == EINVAL ||
                      errno == EOPNOTSUPP || errno == EPERM)) {
            if (ftruncate(dstfd, 0) != 0) return -1;
            return copy_read_write(srcfd, dstfd);
        }
        if (n < 0) return -1;
        if (n == 0) break; /* the source shrank while copying */
        left -= n;
    }
    return ftruncate(dstfd, out);
}

static int remove_contents(int dfd, dev_t dev, int depth) {
    if (depth > 256) {
        errno = ELOOP;
        return -1;
    }
    int dup_fd = fcntl(dfd, F_DUPFD_CLOEXEC, 0);
    if (dup_fd < 0) return -1;
    DIR *dir = fdopendir(dup_fd);
    if (!dir) {
        close(dup_fd);
        return -1;
    }
    int result = 0;
    struct dirent *de;
    /* Removing while reading is safe for unlinkat; rewind after each pass
     * until a pass removes nothing, so no entry is skipped. */
    for (bool removed = true; removed && result == 0;) {
        removed = false;
        rewinddir(dir);
        errno = 0;
        while (result == 0 && (de = readdir(dir)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            struct stat st;
            if (fstatat(dfd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
                if (errno == ENOENT) continue;
                result = -1;
                break;
            }
            if (S_ISDIR(st.st_mode)) {
                if (st.st_dev != dev) {
                    errno = EXDEV;
                    result = -1;
                    break;
                }
                int child = openat(dfd, de->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (child < 0) {
                    result = -1;
                    break;
                }
                struct stat cst;
                int why = 0;
                if (fstat(child, &cst) != 0) why = errno;
                else if (cst.st_ino != st.st_ino) why = ESTALE; /* replaced meanwhile */
                else if (cst.st_dev != dev) why = EXDEV;
                if (why) {
                    close(child);
                    errno = why;
                    result = -1;
                    break;
                }
                result = remove_contents(child, dev, depth + 1);
                int saved = errno;
                close(child);
                errno = saved;
                if (result == 0 && unlinkat(dfd, de->d_name, AT_REMOVEDIR) != 0) result = -1;
            } else if (unlinkat(dfd, de->d_name, 0) != 0 && errno != ENOENT) {
                result = -1;
            }
            removed = true;
            errno = 0;
        }
        if (result == 0 && errno != 0) result = -1;
    }
    int saved = errno;
    closedir(dir);
    errno = saved;
    return result;
}

int tam_fs_remove_tree(int dirfd, const char *name) {
    if (!name || !name[0] || strchr(name, '/') || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        errno = EINVAL;
        return -1;
    }
    struct stat parent, st;
    if (fstatat(dirfd, ".", &parent, 0) != 0) return -1;
    if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return unlinkat(dirfd, name, 0);
    if (st.st_dev != parent.st_dev) {
        errno = EXDEV;
        return -1;
    }
    int fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    int r = remove_contents(fd, st.st_dev, 1);
    int saved = errno;
    close(fd);
    if (r != 0) {
        errno = saved;
        return -1;
    }
    return unlinkat(dirfd, name, AT_REMOVEDIR);
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int tam_fs_lock(const char *path, int timeout_ms) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    long long deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) return fd;
        if (errno == EINTR) continue;
        if (errno != EWOULDBLOCK || now_ms() >= deadline) break;
        struct timespec pause = {0, 50 * 1000000L};
        nanosleep(&pause, NULL);
    }
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
}

void tam_fs_unlock(int fd) {
    if (fd < 0) return;
    flock(fd, LOCK_UN);
    close(fd);
}
