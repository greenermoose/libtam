#define _GNU_SOURCE
#include "tam_display_session.h"
#include <systemd/sd-bus.h>
#include <systemd/sd-device.h>
#include <systemd/sd-login.h>
#include <drm.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static struct {
    bool graphical, remote, inactive, deny_open, busy_master, deny_match, wrong_fd, bad_vt, wrong_seat;
    unsigned opens, masters, drops, closed, matches;
    int event;
    sd_bus_message_handler_t handlers[3];
    void *contexts[3];
} f;
static sd_bus *bus = (sd_bus *)1;
static sd_bus_message *reply = (sd_bus_message *)2;
enum tam_display_result __wrap_tam_vt_check(const struct tam_vt *v, struct tam_display_error *e)
{ (void)v; (void)e; return f.bad_vt ? TAM_DISPLAY_INACTIVE : TAM_DISPLAY_OK; }
enum tam_display_result __wrap_tam_vt_check_restore(const struct tam_vt *v, struct tam_display_error *e)
{ (void)v; (void)e; return f.bad_vt ? TAM_DISPLAY_INACTIVE : TAM_DISPLAY_OK; }
int __wrap_sd_pid_get_session(pid_t p, char **s) { assert(p == 0); *s = strdup("test-session"); return 0; }
int __wrap_sd_session_get_uid(const char *s, uid_t *u) { assert(!strcmp(s, "test-session")); *u = getuid(); return 0; }
int __wrap_sd_session_get_vt(const char *s, unsigned *v) { (void)s; *v = 2; return 0; }
int __wrap_sd_session_get_type(const char *s, char **t) { (void)s; *t = strdup(f.graphical ? "wayland" : "tty"); return 0; }
int __wrap_sd_session_get_seat(const char *s, char **t) { (void)s; *t = strdup("seat0"); return 0; }
int __wrap_sd_session_is_active(const char *s) { (void)s; return !f.inactive; }
int __wrap_sd_session_is_remote(const char *s) { (void)s; return f.remote; }
int __wrap_sd_device_new_from_devnum(sd_device **d, char type, dev_t num)
{ assert(type == 'c' && num == makedev(226, 1)); *d = (sd_device *)3; return 0; }
int __wrap_sd_device_get_property_value(sd_device *d, const char *name, const char **v)
{ assert(d == (sd_device *)3 && !strcmp(name, "ID_SEAT")); *v = f.wrong_seat ? "seat1" : "seat0"; return 0; }
int __wrap_sd_device_has_tag(sd_device *d, const char *tag)
{ assert(d == (sd_device *)3 && !strcmp(tag, "seat")); return 1; }
sd_device *__wrap_sd_device_unref(sd_device *d) { (void)d; return NULL; }
int __wrap_lstat(const char *p, struct stat *s)
{ assert(!strcmp(p, "/dev/dri/card1")); memset(s, 0, sizeof(*s)); s->st_mode = S_IFCHR; s->st_rdev = makedev(226, 1); return 0; }
int __wrap_fstat(int fd, struct stat *s)
{ assert(fd == 42); memset(s, 0, sizeof(*s)); s->st_mode = S_IFCHR; s->st_rdev = makedev(226, f.wrong_fd ? 2 : 1); return 0; }
int __wrap_open(const char *path, int flags, ...)
{
    assert(!strcmp(path, "/dev/dri/card1"));
    assert((flags & (O_CLOEXEC | O_NOFOLLOW | O_NOCTTY)) == (O_CLOEXEC | O_NOFOLLOW | O_NOCTTY));
    if (f.deny_open) { errno = EACCES; return -1; }
    ++f.opens; return 42;
}
int __wrap_close(int fd) { assert(fd == 42); ++f.closed; return 0; }
int __wrap_ioctl(int fd, unsigned long op, ...)
{
    assert(fd == 42);
    if (op == DRM_IOCTL_SET_MASTER) {
        if (f.busy_master) { errno = EBUSY; return -1; }
        ++f.masters;
    } else if (op == DRM_IOCTL_DROP_MASTER) { assert(f.masters); ++f.drops; }
    else assert(!"Unexpected device mutation");
    return 0;
}
int __wrap_sd_bus_open_system(sd_bus **b) { *b = bus; return 0; }
int __wrap_sd_bus_set_method_call_timeout(sd_bus *b, uint64_t t) { assert(b == bus && t == 2000000); return 0; }
int __wrap_sd_bus_call_method(sd_bus *b, const char *dest, const char *path,
    const char *interface, const char *method, sd_bus_error *e, sd_bus_message **r,
    const char *types, ...)
{
    (void)path; (void)interface; (void)e;
    assert(b == bus && !strcmp(dest, "org.freedesktop.login1"));
    /* TakeControl/TakeDevice/ReleaseControl MUST NEVER be called: logind's
     * side effects would mute/reset this ordinary text VT. */
    assert(!strcmp(method, "GetSession") && !strcmp(types, "s"));
    va_list a; va_start(a, types); assert(!strcmp(va_arg(a, const char *), "test-session")); va_end(a);
    *r = reply; return 0;
}
int __wrap_sd_bus_message_read(sd_bus_message *m, const char *types, ...)
{
    assert(m == reply);
    va_list a; va_start(a, types);
    if (!strcmp(types, "o")) *va_arg(a, const char **) = "/org/freedesktop/login1/session/test";
    else if (!strcmp(types, "so")) { *va_arg(a, const char **) = "test-session"; *va_arg(a, const char **) = "/session/test"; }
    else if (!strcmp(types, "sss")) {
        *va_arg(a, const char **) = "org.freedesktop.login1";
        *va_arg(a, const char **) = ":1.1"; *va_arg(a, const char **) = ":1.2";
    } else assert(!"Unexpected message signature");
    va_end(a); return 1;
}
sd_bus_message *__wrap_sd_bus_message_unref(sd_bus_message *m) { (void)m; return NULL; }
int __wrap_sd_bus_match_signal(sd_bus *b, sd_bus_slot **slot, const char *sender,
    const char *path, const char *interface, const char *member,
    sd_bus_message_handler_t callback, void *context)
{
    (void)sender; (void)interface; (void)member;
    assert(b == bus && strcmp(path, "/org/freedesktop/login1/session/self"));
    if (f.deny_match) return -EACCES;
    assert(f.matches < 3);
    f.handlers[f.matches] = callback; f.contexts[f.matches++] = context;
    *slot = (sd_bus_slot *)4; return 0;
}
sd_bus_slot *__wrap_sd_bus_slot_unref(sd_bus_slot *s) { (void)s; return NULL; }
sd_bus *__wrap_sd_bus_flush_close_unref(sd_bus *b) { (void)b; return NULL; }
int __wrap_sd_bus_process(sd_bus *b, sd_bus_message **m)
{
    (void)m; assert(b == bus);
    if (f.event > 0 && f.event < 4) {
        int which = f.event - 1;
        f.event += 10;
        f.handlers[which](reply, f.contexts[which], NULL); return 1;
    }
    return 0;
}
int __wrap_sd_bus_get_fd(sd_bus *b) { assert(b == bus); return 70; }
int __wrap_sd_bus_get_events(sd_bus *b) { assert(b == bus); return 1; }
static struct tam_vt vt(void)
{ return (struct tam_vt){.fd = 50, .number = 2, .uid = getuid(), .foreground_group = getpgrp(), .claimed = true}; }
static struct tam_display_session *acquire(void)
{
    struct tam_vt v = vt();
    struct tam_display_session *s = NULL;
    struct tam_display_error e;
    assert(tam_display_session_open(&v, 1, &s, &e) == TAM_DISPLAY_OK && s);
    assert(tam_display_session_device(s) == 42 && f.opens == 1 && f.masters == 1 && f.matches == 3);
    assert(tam_display_session_bus_fd(s) == 70 && tam_display_session_bus_events(s) == 1);
    return s;
}
int main(void)
{
    struct tam_display_error e;
    struct tam_display_session *s = acquire();
    assert(tam_display_session_check(s, &e) == TAM_DISPLAY_OK);
    assert(tam_display_session_check_restore(s, &e) == TAM_DISPLAY_OK);
    tam_display_session_close(s); assert(f.drops == 1 && f.closed == 1);
    for (int event = 1; event <= 3; ++event) {
        memset(&f, 0, sizeof(f)); s = acquire(); f.event = event;
        if (event == 1) f.inactive = true;
        assert(tam_display_session_dispatch(s) == TAM_DISPLAY_SESSION_LOST);
        assert(tam_display_session_check_restore(s, &e) == TAM_DISPLAY_RESTORE_FAILED);
        tam_display_session_close(s); assert(f.drops == 1 && f.closed == 1);
    }
    for (unsigned i = 0; i < 9; ++i) {
        memset(&f, 0, sizeof(f));
        switch (i) {
        case 0: f.graphical = true; break;
        case 1: f.remote = true; break;
        case 2: f.inactive = true; break;
        case 3: f.bad_vt = true; break;
        case 4: f.deny_match = true; break;
        case 5: f.wrong_seat = true; break;
        case 6: f.deny_open = true; break;
        case 7: f.wrong_fd = true; break;
        case 8: f.busy_master = true; break;
        }
        struct tam_vt v = vt(); s = (void *)1;
        assert(tam_display_session_open(&v, 1, &s, &e) != TAM_DISPLAY_OK && !s);
        assert(f.drops == f.masters && f.closed == f.opens);
    }
    puts("Direct seat/ACL acquisition, occupied master, session loss and cleanup passed; no logind takeover");
}
