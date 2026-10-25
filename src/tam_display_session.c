#define _GNU_SOURCE
#include "tam_display_session.h"
#include "tam_display_internal.h"
#include <systemd/sd-bus.h>
#include <systemd/sd-device.h>
#include <systemd/sd-login.h>
#include <drm.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define DEST "org.freedesktop.login1"
struct tam_display_session {
    sd_bus *bus;
    sd_bus_slot *properties, *owner, *removed;
    char *id, *path;
    struct tam_vt vt;
    int fd;
    bool master;
    enum tam_display_session_event event;
};
static enum tam_display_result failed(struct tam_display_error *e, int n, const char *message)
{
    return tam_display_fail(e, n == -EACCES || n == -EPERM ? TAM_DISPLAY_PERMISSION :
        n == -EBUSY ? TAM_DISPLAY_BUSY : TAM_DISPLAY_IO, n < 0 ? -n : 0, message);
}
static enum tam_display_result local(struct tam_display_session *s, bool restoring, struct tam_display_error *e)
{
    enum tam_display_result rc = restoring ? tam_vt_check_restore(&s->vt, e) : tam_vt_check(&s->vt, e);
    if (rc) return rc;
    uid_t uid;
    unsigned number;
    char *type = NULL;
    int n = sd_session_get_uid(s->id, &uid);
    if (n >= 0) n = sd_session_get_vt(s->id, &number);
    if (n >= 0) n = sd_session_get_type(s->id, &type);
    bool valid = n >= 0 && uid == s->vt.uid && number == s->vt.number && type && !strcmp(type, "tty") &&
        sd_session_is_active(s->id) > 0 && sd_session_is_remote(s->id) == 0;
    free(type);
    return valid ? TAM_DISPLAY_OK : tam_display_fail(e, TAM_DISPLAY_INACTIVE, n < 0 ? -n : 0,
        "Active local text login session must own this VT");
}
static int changed(sd_bus_message *m, void *context, sd_bus_error *error)
{
    (void)m; (void)error;
    struct tam_display_session *s = context;
    if (sd_session_is_active(s->id) <= 0) s->event = TAM_DISPLAY_SESSION_LOST;
    return 0;
}
static int removed(sd_bus_message *m, void *context, sd_bus_error *error)
{
    (void)error;
    struct tam_display_session *s = context;
    const char *id, *path;
    if (sd_bus_message_read(m, "so", &id, &path) < 0 || !strcmp(id, s->id)) s->event = TAM_DISPLAY_SESSION_LOST;
    return 0;
}
static int owner_changed(sd_bus_message *m, void *context, sd_bus_error *error)
{
    (void)error;
    struct tam_display_session *s = context;
    const char *name, *old, *now;
    if (sd_bus_message_read(m, "sss", &name, &old, &now) < 0 ||
        (!strcmp(name, DEST) && strcmp(old, now))) s->event = TAM_DISPLAY_SESSION_LOST;
    return 0;
}

enum tam_display_result tam_display_session_open(const struct tam_vt *v, unsigned card,
    struct tam_display_session **out, struct tam_display_error *e)
{
    if (out) *out = NULL;
    if (!v || !v->claimed || !out || card >= 64)
        return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Owned foreground VT handoff and primary card are required");
    struct tam_display_session *s = calloc(1, sizeof(*s));
    if (!s) return tam_display_fail(e, TAM_DISPLAY_IO, ENOMEM, "Cannot allocate display session");
    s->fd = -1; s->vt = *v;
    enum tam_display_result rc;
    int n = sd_pid_get_session(0, &s->id);
    if (n < 0) { rc = failed(e, n, "Cannot identify the calling login session"); goto fail; }
    rc = local(s, false, e); if (rc) goto fail;
    char path[40];
    snprintf(path, sizeof(path), "/dev/dri/card%u", card);
    struct stat node;
    if (lstat(path, &node) < 0) { rc = failed(e, -errno, "Cannot identify requested DRM card"); goto fail; }
    if (!S_ISCHR(node.st_mode) || major(node.st_rdev) != 226 || minor(node.st_rdev) >= 64) {
        rc = tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Expected a primary DRM character device"); goto fail;
    }
    /* Permissions alone may come from a persistent group; also verify seat. */
    sd_device *device = NULL;
    char *seat = NULL;
    const char *device_seat = NULL;
    n = sd_session_get_seat(s->id, &seat);
    if (n >= 0) n = sd_device_new_from_devnum(&device, 'c', node.st_rdev);
    if (n >= 0) n = sd_device_get_property_value(device, "ID_SEAT", &device_seat);
    if (n == -ENOENT && device) { device_seat = "seat0"; n = 0; }
    bool same = n >= 0 && seat && *seat && device_seat && !strcmp(seat, device_seat) && sd_device_has_tag(device, "seat") > 0;
    sd_device_unref(device); free(seat);
    if (!same) { rc = tam_display_fail(e, TAM_DISPLAY_PERMISSION, n < 0 ? -n : 0, "DRM card must belong to this login seat"); goto fail; }
    n = sd_bus_open_system(&s->bus);
    if (n < 0) { rc = failed(e, n, "Cannot observe login-session lifetime"); goto fail; }
    n = sd_bus_set_method_call_timeout(s->bus, 2000000);
    if (n < 0) { rc = failed(e, n, "Cannot bound session queries"); goto fail; }
    sd_bus_message *reply = NULL;
    n = sd_bus_call_method(s->bus, DEST, "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
        "GetSession", NULL, &reply, "s", s->id);
    const char *object = NULL;
    if (n >= 0) n = sd_bus_message_read(reply, "o", &object);
    if (n >= 0) { s->path = strdup(object); if (!s->path) n = -ENOMEM; }
    sd_bus_message_unref(reply);
    if (n < 0) { rc = failed(e, n, "Cannot resolve session observation object"); goto fail; }
    n = sd_bus_match_signal(s->bus, &s->properties, DEST, s->path, "org.freedesktop.DBus.Properties", "PropertiesChanged", changed, s);
    if (n >= 0) n = sd_bus_match_signal(s->bus, &s->removed, DEST, "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "SessionRemoved", removed, s);
    if (n >= 0) n = sd_bus_match_signal(s->bus, &s->owner, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged", owner_changed, s);
    if (n < 0) { rc = failed(e, n, "Cannot observe session loss"); goto fail; }
    rc = tam_display_session_check(s, e); if (rc) goto fail;
    /* No TakeControl: logind would mute keyboard, enter KD_GRAPHICS and later
     * reset tty ownership. Direct access uses existing ACLs, never privilege. */
    s->fd = open(path, O_RDWR | O_NOFOLLOW | O_NOCTTY | O_CLOEXEC);
    if (s->fd < 0) { rc = failed(e, -errno, "Seat permissions do not authorize direct DRM access"); goto fail; }
    struct stat actual;
    if (fstat(s->fd, &actual) < 0 || !S_ISCHR(actual.st_mode) || actual.st_rdev != node.st_rdev ||
        actual.st_dev != node.st_dev || actual.st_ino != node.st_ino) {
        rc = tam_display_fail(e, TAM_DISPLAY_STALE, 0, "DRM node changed during authorized acquisition"); goto fail;
    }
    /* SET_MASTER cannot displace an existing master. Opening an unowned card
     * can already grant master; this also verifies that our description owns it. */
    if (ioctl(s->fd, DRM_IOCTL_SET_MASTER, NULL) < 0) {
        rc = failed(e, -errno, "DRM master unavailable; existing display ownership is never displaced"); goto fail;
    }
    s->master = true;
    rc = tam_display_session_check(s, e); if (rc) goto fail;
    *out = s; return TAM_DISPLAY_OK;
fail:
    tam_display_session_close(s); return rc;
}
int tam_display_session_device(const struct tam_display_session *s) { return s ? s->fd : -1; }
int tam_display_session_bus_fd(struct tam_display_session *s) { return s ? sd_bus_get_fd(s->bus) : -1; }
int tam_display_session_bus_events(struct tam_display_session *s) { return s ? sd_bus_get_events(s->bus) : 0; }
enum tam_display_session_event tam_display_session_dispatch(struct tam_display_session *s)
{
    if (!s) return TAM_DISPLAY_SESSION_LOST;
    for (int i = 0; i < 64; ++i) {
        int n = sd_bus_process(s->bus, NULL);
        if (n < 0) s->event = TAM_DISPLAY_SESSION_LOST;
        if (n <= 0) break;
    }
    return s->event;
}
enum tam_display_result tam_display_session_check(struct tam_display_session *s, struct tam_display_error *e)
{
    if (!s) return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Missing authorized display session");
    if (tam_display_session_dispatch(s) != TAM_DISPLAY_SESSION_RUNNING)
        return tam_display_fail(e, TAM_DISPLAY_INACTIVE, 0, "Session/device loss ends this display selection");
    return local(s, false, e);
}
enum tam_display_result tam_display_session_check_restore(struct tam_display_session *s, struct tam_display_error *e)
{
    if (!s || tam_display_session_dispatch(s) != TAM_DISPLAY_SESSION_RUNNING)
        return tam_display_fail(e, TAM_DISPLAY_RESTORE_FAILED, 0, "Session/device ownership was lost before restoration");
    return local(s, true, e);
}
void tam_display_session_close(struct tam_display_session *s)
{
    if (!s) return;
    if (s->master) ioctl(s->fd, DRM_IOCTL_DROP_MASTER, NULL);
    if (s->fd >= 0) close(s->fd);
    sd_bus_slot_unref(s->properties); sd_bus_slot_unref(s->owner); sd_bus_slot_unref(s->removed);
    sd_bus_flush_close_unref(s->bus);
    free(s->path); free(s->id); free(s);
}
