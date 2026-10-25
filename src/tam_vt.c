#define _GNU_SOURCE
#include "tam_vt.h"
#include "tam_display_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <stdio.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static bool valid(dev_t d) { return major(d) == 4 && minor(d) >= 1 && minor(d) <= 63; }

static enum tam_display_result check(const struct tam_vt *v, bool foreground, struct tam_display_error *e)
{
    struct stat st;
    struct vt_stat active = {0};
    int mode = -1;
    if (e) *e = (struct tam_display_error){0};
    if (!v || v->fd < 0 || v->foreground_group <= 0)
        return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Invalid foreground VT lease");
    if (fstat(v->fd, &st) < 0 || ioctl(v->fd, VT_GETSTATE, &active) < 0 ||
        ioctl(v->fd, KDGETMODE, &mode) < 0)
        return tam_display_fail(e, TAM_DISPLAY_IO, errno, "Cannot verify Linux text VT");
    if (!S_ISCHR(st.st_mode) || !valid(st.st_rdev) || minor(st.st_rdev) != v->number ||
        st.st_uid != v->uid || getuid() != v->uid || geteuid() != v->uid)
        return tam_display_fail(e, TAM_DISPLAY_PERMISSION, 0, "Text VT must belong to the calling user");
    if (active.v_active != v->number || mode != KD_TEXT || (foreground && tcgetpgrp(v->fd) != v->foreground_group))
        return tam_display_fail(e, TAM_DISPLAY_INACTIVE, 0, "Owned text VT and command must be in the foreground");
    if (v->claimed) {
        struct vt_mode owned;
        if (ioctl(v->fd, VT_GETMODE, &owned) < 0 || owned.mode != VT_PROCESS ||
            owned.relsig != SIGUSR1 || owned.acqsig != SIGUSR2)
            return tam_display_fail(e, TAM_DISPLAY_BUSY, 0, "Owned VT handoff was replaced");
    }
    return TAM_DISPLAY_OK;
}

enum tam_display_result tam_vt_check(const struct tam_vt *v, struct tam_display_error *e)
{ return check(v, true, e); }

enum tam_display_result tam_vt_check_restore(const struct tam_vt *v, struct tam_display_error *e)
{ return check(v, false, e); }

enum tam_display_result tam_vt_open(struct tam_vt *v, struct tam_display_error *e)
{
    if (!v) return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Missing VT output");
    *v = (struct tam_vt){.fd = -1};
    int alias = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC | O_NOFOLLOW);
    unsigned int device = 0;
    if (alias < 0) return tam_display_fail(e, TAM_DISPLAY_INACTIVE, errno, "A controlling Linux text VT is required");
    int rc = ioctl(alias, TIOCGDEV, &device), saved = errno;
    close(alias);
    if (rc < 0 || !valid(device))
        return tam_display_fail(e, TAM_DISPLAY_INACTIVE, rc < 0 ? saved : 0, "A real Linux text VT is required");
    char path[32];
    snprintf(path, sizeof(path), "/dev/tty%u", minor(device));
    v->fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC | O_NOFOLLOW);
    v->number = minor(device); v->uid = getuid(); v->foreground_group = getpgrp();
    enum tam_display_result result = tam_vt_check(v, e);
    if (result) tam_vt_close(v);
    return result;
}

enum tam_display_result tam_vt_claim(struct tam_vt *v, struct tam_display_error *e)
{
    enum tam_display_result rc = tam_vt_check(v, e);
    if (rc) return rc;
    if (v->claimed) return tam_display_fail(e, TAM_DISPLAY_BUSY, 0, "VT handoff already owned");
    if (ioctl(v->fd, VT_GETMODE, &v->saved_mode) < 0)
        return tam_display_fail(e, TAM_DISPLAY_IO, errno, "Cannot inspect VT handoff mode");
    if (v->saved_mode.mode != VT_AUTO)
        return tam_display_fail(e, TAM_DISPLAY_BUSY, 0, "Existing VT controller is never displaced");
    struct vt_mode owned = {.mode = VT_PROCESS, .relsig = SIGUSR1, .acqsig = SIGUSR2};
    if (ioctl(v->fd, VT_SETMODE, &owned) < 0)
        return tam_display_fail(e, TAM_DISPLAY_PERMISSION, errno, "Cannot own text VT handoff");
    v->claimed = true;
    return tam_vt_check(v, e);
}

enum tam_display_result tam_vt_release(struct tam_vt *v, bool handoff, struct tam_display_error *e)
{
    if (!v || !v->claimed) return TAM_DISPLAY_OK;
    struct stat st;
    struct vt_mode mode;
    if (fstat(v->fd, &st) < 0 || !S_ISCHR(st.st_mode) || !valid(st.st_rdev) ||
        minor(st.st_rdev) != v->number || st.st_uid != v->uid ||
        ioctl(v->fd, VT_GETMODE, &mode) < 0 || mode.mode != VT_PROCESS ||
        mode.relsig != SIGUSR1 || mode.acqsig != SIGUSR2)
        return tam_display_fail(e, TAM_DISPLAY_RESTORE_FAILED, 0, "Owned VT handoff changed before release");
    if (handoff && ioctl(v->fd, VT_RELDISP, 1) < 0)
        return tam_display_fail(e, TAM_DISPLAY_RESTORE_FAILED, errno, "Cannot acknowledge VT handoff");
    if (ioctl(v->fd, VT_SETMODE, &v->saved_mode) < 0)
        return tam_display_fail(e, TAM_DISPLAY_RESTORE_FAILED, errno, "Cannot restore owned VT handoff mode");
    v->claimed = false;
    return TAM_DISPLAY_OK;
}

void tam_vt_close(struct tam_vt *v)
{
    if (v && v->fd >= 0) { close(v->fd); v->fd = -1; }
}
