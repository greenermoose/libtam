#define _GNU_SOURCE
#include "tam_vt.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static unsigned number = 2, active = 2, owner_bad, graphic, background, closes;
static int failure, acknowledgements, blanks, wakes;
static struct vt_mode vt_mode = {.mode = VT_AUTO};
int __wrap_open(const char *path, int flags, ...)
{
    assert((flags & (O_CLOEXEC | O_NOCTTY | O_NOFOLLOW)) == (O_CLOEXEC | O_NOCTTY | O_NOFOLLOW));
    if (failure) { errno = EACCES; return -1; }
    if (!strcmp(path, "/dev/tty")) return 40;
    assert(!strcmp(path, "/dev/tty2")); return 50;
}
int __wrap_close(int fd) { assert(fd == 40 || fd == 50); ++closes; return 0; }
int __wrap_fstat(int fd, struct stat *s)
{
    assert(fd == 50);
    memset(s, 0, sizeof(*s)); s->st_mode = S_IFCHR; s->st_rdev = makedev(4, number);
    s->st_uid = getuid() + owner_bad; return 0;
}
pid_t __wrap_tcgetpgrp(int fd) { assert(fd == 50); return getpgrp() + background; }
int __wrap_ioctl(int fd, unsigned long op, ...)
{
    va_list a; va_start(a, op);
    if (op == VT_RELDISP) { assert(va_arg(a, int) == 1); va_end(a); ++acknowledgements; return 0; }
    void *arg = va_arg(a, void *); va_end(a);
    if (op == VT_RELDISP) assert(!"unreachable");
    else if (op == VT_GETMODE) *(struct vt_mode *)arg = vt_mode;
    else if (op == VT_SETMODE) vt_mode = *(struct vt_mode *)arg;
    else if (op == TIOCGDEV) { assert(fd == 40); *(unsigned *)arg = makedev(number == 2 ? 4 : 136, number); }
    else if (op == VT_GETSTATE) { assert(fd == 50); ((struct vt_stat *)arg)->v_active = active; }
    else if (op == KDGETMODE) { assert(fd == 50); *(int *)arg = graphic ? KD_GRAPHICS : KD_TEXT; }
    else if (op == TIOCLINUX) {
        assert(fd == 50);
        unsigned char sub = *(unsigned char *)arg;
        if (sub == 14) ++blanks;
        else if (sub == 4) ++wakes;
        else assert(!"Unexpected TIOCLINUX subcode");
    }
    else assert(!"VT mutation is forbidden");
    return 0;
}
int main(void)
{
    struct tam_vt v;
    struct tam_display_error e;
    assert(tam_vt_open(&v, &e) == TAM_DISPLAY_OK);
    assert(v.fd == 50 && v.number == 2 && v.foreground_group == getpgrp());
    graphic = 1; assert(tam_vt_check(&v, &e) == TAM_DISPLAY_INACTIVE); graphic = 0;
    active = 3; assert(tam_vt_check(&v, &e) == TAM_DISPLAY_INACTIVE); active = 2;
    background = 1; assert(tam_vt_check(&v, &e) == TAM_DISPLAY_INACTIVE);
    assert(tam_vt_check_restore(&v, &e) == TAM_DISPLAY_OK); background = 0;
    owner_bad = 1; assert(tam_vt_check(&v, &e) == TAM_DISPLAY_PERMISSION); owner_bad = 0;
    assert(tam_vt_claim(&v, &e) == TAM_DISPLAY_OK && v.claimed && vt_mode.mode == VT_PROCESS);
    assert(vt_mode.relsig == SIGUSR1 && vt_mode.acqsig == SIGUSR2);
    assert(tam_vt_check(&v, &e) == TAM_DISPLAY_OK);
    assert(tam_vt_blank(&v, &e) == TAM_DISPLAY_OK && blanks == 1);
    assert(tam_vt_wake(&v, &e) == TAM_DISPLAY_OK && wakes == 1);
    background = 1; assert(tam_vt_check_restore(&v, &e) == TAM_DISPLAY_OK);
    assert(tam_vt_blank(&v, &e) == TAM_DISPLAY_INACTIVE && blanks == 1);
    assert(tam_vt_wake(&v, &e) == TAM_DISPLAY_OK && wakes == 2);
    background = 0;
    assert(tam_vt_release(&v, true, &e) == TAM_DISPLAY_OK && !v.claimed && acknowledgements == 1 && vt_mode.mode == VT_AUTO);
    vt_mode.mode = VT_PROCESS;
    assert(tam_vt_claim(&v, &e) == TAM_DISPLAY_BUSY && !v.claimed);
    vt_mode.mode = VT_AUTO;
    tam_vt_close(&v); assert(v.fd == -1 && closes == 2);
    number = 7; assert(tam_vt_open(&v, &e) == TAM_DISPLAY_INACTIVE && v.fd == -1);
    number = 2; owner_bad = 1;
    assert(tam_vt_open(&v, &e) == TAM_DISPLAY_PERMISSION && v.fd == -1); owner_bad = 0;
    failure = 1; assert(tam_vt_open(&v, &e) == TAM_DISPLAY_INACTIVE && v.fd == -1);
    assert(tam_vt_open(NULL, &e) == TAM_DISPLAY_INVALID);
    puts("Foreground VT ownership, PTY/graphics/background rejection and descriptor cleanup passed");
}
