#define _GNU_SOURCE
#include "tam_display_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int real_ioctl(void *context, int fd, unsigned long request, void *arg)
{
    (void)context;
    return ioctl(fd, request, arg);
}

enum tam_display_result tam_display_capture_fd(int fd, struct tam_display_state **out,
    struct tam_display_error *e)
{
    if (out) *out = NULL;
    struct stat st;
    if (!out || fd < 0) return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Invalid capture descriptor");
    if (fstat(fd, &st) < 0) return tam_display_fail(e, TAM_DISPLAY_IO, errno, "Cannot inspect DRM descriptor");
    if (!S_ISCHR(st.st_mode) || major(st.st_rdev) != 226 || minor(st.st_rdev) >= 64)
        return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Authorized primary DRM descriptor required");
    int owned = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (owned < 0) return tam_display_fail(e, TAM_DISPLAY_IO, errno, "Cannot retain DRM descriptor");
    enum tam_display_result rc = tam_display_capture_with(owned, out, e, real_ioctl, NULL);
    if (rc) close(owned);
    else (*out)->close_fd = true;
    return rc;
}
