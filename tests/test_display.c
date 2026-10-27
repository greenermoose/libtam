#include "tam_display_internal.h"
#include <drm.h>
#include <drm_mode.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct fixture {
    unsigned calls, fail_at, resource_reads, property_reads, plane_reads;
    bool huge_resources, huge_properties, huge_planes, churn, once, no_atomic, missing_route;
    bool interrupt;
    bool property_churn, plane_churn, device_lost;
};

static int fake_ioctl(void *context, int fd, unsigned long request, void *arg)
{
    struct fixture *f = context;
    assert(fd == 42);
    ++f->calls;
    if (f->interrupt) { errno = EINTR; return -1; }
    if (f->device_lost) { errno = ENODEV; return -1; }
    if (f->calls == f->fail_at) { errno = EACCES; return -1; }
    if (request == DRM_IOCTL_SET_CLIENT_CAP) {
        struct drm_set_client_cap *cap = arg;
        assert(cap->value == 1);
        assert(cap->capability == DRM_CLIENT_CAP_ATOMIC ||
               cap->capability == DRM_CLIENT_CAP_UNIVERSAL_PLANES);
        if (f->no_atomic) { errno = EINVAL; return -1; }
    } else if (request == DRM_IOCTL_VERSION) {
        struct drm_version *v = arg;
        assert(v->name_len >= 8);
        memcpy(v->name, "fake\033gpu", 8);
    } else if (request == DRM_IOCTL_MODE_GETRESOURCES) {
        struct drm_mode_card_res *r = arg;
        ++f->resource_reads;
        if (r->connector_id_ptr) {
            assert(r->count_fbs == 0 && r->count_encoders == 0);
            uint32_t ids[] = {100, 101, 102}, crtcs[] = {10, 11};
            memcpy((void *)(uintptr_t)r->connector_id_ptr, ids, sizeof(ids));
            memcpy((void *)(uintptr_t)r->crtc_id_ptr, crtcs, sizeof(crtcs));
        }
        r->count_connectors = f->huge_resources ? UINT32_MAX : 3;
        if (f->churn && !(f->resource_reads % 2)) r->count_connectors = 4;
        if (f->once && f->resource_reads == 2) r->count_connectors = 4;
        r->count_crtcs = 2; r->count_fbs = 3; r->count_encoders = 3;
    } else if (request == DRM_IOCTL_MODE_GETCRTC) {
        struct drm_mode_crtc *c = arg;
        c->fb_id = 77; c->mode_valid = 1;
        c->mode.hdisplay = 1920; c->mode.vdisplay = 1080;
    } else if (request == DRM_IOCTL_MODE_GETCONNECTOR) {
        struct drm_mode_get_connector *c = arg;
        assert(c->count_modes == 1 && c->modes_ptr);
        assert(c->count_encoders == 0 && c->count_props == 0);
        c->connection = 1; c->encoder_id = c->connector_id + 100;
        c->connector_type = DRM_MODE_CONNECTOR_DisplayPort;
        c->connector_type_id = c->connector_id - 99;
    } else if (request == DRM_IOCTL_MODE_OBJ_GETPROPERTIES) {
        struct drm_mode_obj_get_properties *p = arg;
        ++f->property_reads;
        if (p->props_ptr && p->count_props) {
            ((uint32_t *)(uintptr_t)p->props_ptr)[0] =
                p->obj_type == DRM_MODE_OBJECT_CONNECTOR ? 1 : 2;
            ((uint64_t *)(uintptr_t)p->prop_values_ptr)[0] = p->obj_id == 102 ? 11 : 10;
        }
        p->count_props = f->huge_properties ? UINT32_MAX : 1;
        if (f->property_churn && !(f->property_reads % 2)) p->count_props = 2;
    } else if (request == DRM_IOCTL_MODE_GETPROPERTY) {
        struct drm_mode_get_property *p = arg;
        strcpy(p->name, p->prop_id == 1 && !f->missing_route ? "CRTC_ID" : "other");
    } else if (request == DRM_IOCTL_MODE_GETPLANERESOURCES) {
        struct drm_mode_get_plane_res *p = arg;
        ++f->plane_reads;
        if (p->plane_id_ptr) {
            uint32_t ids[] = {200, 201};
            memcpy((void *)(uintptr_t)p->plane_id_ptr, ids, sizeof(ids));
        }
        p->count_planes = f->huge_planes ? UINT32_MAX : 2;
        if (f->plane_churn && !(f->plane_reads % 2)) p->count_planes = 3;
    } else if (request == DRM_IOCTL_MODE_GETPLANE) {
        struct drm_mode_get_plane *p = arg;
        p->crtc_id = p->plane_id == 200 ? 10 : 11;
        p->fb_id = 77; p->possible_crtcs = 3;
    } else {
        /* Any unexpected ioctl, especially mutation/master, fails the test. */
        assert(!"Unexpected DRM ioctl");
    }
    return 0;
}

static struct tam_display_topology *observe(struct fixture *f)
{
    struct tam_display_topology *t = NULL;
    struct tam_display_error error;
    assert(tam_display_inspect_with(42, &t, &error, fake_ioctl, f) == TAM_DISPLAY_OK);
    assert(t && error.code == TAM_DISPLAY_OK);
    assert(!strcmp(t->driver, "fake?gpu"));
    assert(t->connector_count == 3 && t->crtc_count == 2 && t->plane_count == 2);
    assert(t->connectors[0].crtc_id == 10 && t->connectors[2].crtc_id == 11);
    return t;
}

static void failed(struct fixture f, enum tam_display_result result)
{
    struct tam_display_topology *t = (void *)1;
    struct tam_display_error e;
    assert(tam_display_inspect_with(42, &t, &e, fake_ioctl, &f) == result);
    assert(!t && e.code == result && *e.message);
    assert(f.calls < 100);
}

static void planning(void)
{
    struct fixture f = {0};
    struct tam_display_topology *t = observe(&f);
    struct tam_display_plan p;
    struct tam_display_error e;
    /* DP-1 and DP-2 share a CRTC; DP-3 shares only their framebuffer.
     * Detach DP-2 without disabling the retained CRTC or its plane. */
    assert(tam_display_plan_keep(t, "DP-1", &p, &e) == TAM_DISPLAY_OK);
    assert(p.keep_connector == 100 && p.detach_count == 2);
    assert(p.detach[0] == 101 && p.detach[1] == 102);
    assert(p.disable_crtc_count == 1 && p.disable_crtcs[0] == 11);
    assert(p.disable_plane_count == 1 && p.disable_planes[0] == 201);
    assert(tam_display_plan_keep(t, "DP-3", &p, &e) == TAM_DISPLAY_OK);
    assert(p.disable_crtc_count == 1 && p.disable_crtcs[0] == 10);
    assert(tam_display_plan_keep(t, "DP-8", &p, &e) == TAM_DISPLAY_NOT_FOUND);
    assert(p.keep_connector == 0 && p.detach_count == 0);
    t->connectors[0].connection = 2;
    assert(tam_display_plan_keep(t, "DP-1", &p, &e) == TAM_DISPLAY_UNSUPPORTED);
    t->connectors[0].connection = 1;
    t->connectors[0].crtc_id = 999;
    assert(tam_display_plan_keep(t, "DP-1", &p, &e) == TAM_DISPLAY_STALE);
    t->connectors[0].crtc_id = 10;
    strcpy(t->connectors[1].name, "DP-1");
    assert(tam_display_plan_keep(t, "DP-1", &p, &e) == TAM_DISPLAY_AMBIGUOUS);
    strcpy(t->connectors[1].name, "DP-2");
    t->planes[0].crtc_id = 999;
    assert(tam_display_plan_keep(t, "DP-1", &p, &e) == TAM_DISPLAY_STALE);
    t->planes[0].crtc_id = 10;
    t->crtcs[1].object.id = 10;
    assert(tam_display_plan_keep(t, "DP-1", &p, &e) == TAM_DISPLAY_STALE);
    t->connector_count = TAM_DISPLAY_MAX_OBJECTS + 1;
    assert(tam_display_plan_keep(t, "DP-1", &p, &e) == TAM_DISPLAY_INVALID);
    assert(tam_display_plan_keep(NULL, "DP-1", &p, &e) == TAM_DISPLAY_INVALID);
    /* Single-output systems produce an empty analysis, never blank the last. */
    t->connector_count = t->crtc_count = t->plane_count = 1;
    assert(tam_display_plan_keep(t, "DP-1", &p, &e) == TAM_DISPLAY_OK);
    assert(!p.detach_count && !p.disable_crtc_count && !p.disable_plane_count);
    free(t);

    /* Targeted blank planning: blank 1 or 2 connectors, refuse blanking all 3. */
    t = observe(&f);
    assert(tam_display_plan_keep(t, "card1-DP-1", &p, &e) == TAM_DISPLAY_OK && p.keep_connector == 100);
    const char *one[] = {"DP-2"};
    assert(tam_display_plan_blank(t, one, 1, &p, &e) == TAM_DISPLAY_OK);
    assert(p.keep_connector == 100 && p.detach_count == 1 && p.detach[0] == 101);
    assert(p.disable_crtc_count == 0 && p.disable_plane_count == 0);
    const char *two[] = {"card1-DP-2", "DP-3"};
    assert(tam_display_plan_blank(t, two, 2, &p, &e) == TAM_DISPLAY_OK);
    assert(p.keep_connector == 100 && p.detach_count == 2);
    assert(p.disable_crtc_count == 1 && p.disable_crtcs[0] == 11);
    assert(p.disable_plane_count == 1 && p.disable_planes[0] == 201);
    const char *all3[] = {"DP-1", "DP-2", "DP-3"};
    assert(tam_display_plan_blank(t, all3, 3, &p, &e) == TAM_DISPLAY_UNSUPPORTED);
    const char *dup[] = {"DP-2", "card1-DP-2"};
    assert(tam_display_plan_blank(t, dup, 2, &p, &e) == TAM_DISPLAY_INVALID);
    struct tam_console_fb cfb = {.vt_number = 2, .fb_index = 0, .card = 1,
        .width = 1920, .height = 1080, .stride = 7680, .bits_per_pixel = 32};
    assert(tam_display_verify_console_fb(t, &cfb, &e) == TAM_DISPLAY_OK && cfb.drm_fb_id == 77);
    cfb.width = 1280;
    assert(tam_display_verify_console_fb(t, &cfb, &e) == TAM_DISPLAY_UNSUPPORTED);
    free(t);
}

struct fbcon_fixture {
    unsigned mapped_fb;
    int ioctl_errno;
};
static int fake_fbcon_ioctl(void *context, int fd, unsigned long request, void *arg)
{
    (void)fd;
    struct fbcon_fixture *fx = context;
    assert(request == FBIOGET_CON2FBMAP);
    if (fx->ioctl_errno) { errno = fx->ioctl_errno; return -1; }
    struct fb_con2fbmap *m = arg;
    assert(m->console == 2);
    m->framebuffer = fx->mapped_fb;
    return 0;
}
static void write_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    assert(fp);
    fputs(content, fp);
    fclose(fp);
}
static void fbcon_mapping_checks(void)
{
    char root[] = "/tmp/tam-fbcon-test-XXXXXX";
    assert(mkdtemp(root));
    char path[512];
    snprintf(path, sizeof(path), "%s/class/vtconsole/vtcon0", root);
    assert(mkdir(root, 0700) == 0 || errno == EEXIST);
    char cmd[640];
    snprintf(cmd, sizeof(cmd),
        "mkdir -p '%s/class/vtconsole/vtcon0' '%s/class/vtconsole/vtcon1' "
        "'%s/class/graphics/fb0/device/drm/card1' '%s/dev'",
        root, root, root, root);
    assert(system(cmd) == 0);
    snprintf(path, sizeof(path), "%s/class/vtconsole/vtcon0/bind", root); write_file(path, "0\n");
    snprintf(path, sizeof(path), "%s/class/vtconsole/vtcon0/name", root); write_file(path, "(S) dummy device\n");
    snprintf(path, sizeof(path), "%s/class/vtconsole/vtcon1/bind", root); write_file(path, "1\n");
    snprintf(path, sizeof(path), "%s/class/vtconsole/vtcon1/name", root); write_file(path, "(M) frame buffer device\n");
    snprintf(path, sizeof(path), "%s/class/graphics/fb0/state", root); write_file(path, "0\n");
    snprintf(path, sizeof(path), "%s/class/graphics/fb0/name", root); write_file(path, "amdgpudrmfb\n");
    snprintf(path, sizeof(path), "%s/class/graphics/fb0/virtual_size", root); write_file(path, "1920,1080\n");
    snprintf(path, sizeof(path), "%s/class/graphics/fb0/stride", root); write_file(path, "7680\n");
    snprintf(path, sizeof(path), "%s/class/graphics/fb0/bits_per_pixel", root); write_file(path, "32\n");

    struct tam_console_fb out;
    struct tam_display_error e;
    struct fbcon_fixture fx = {.mapped_fb = 0, .ioctl_errno = 0};
    assert(tam_display_map_console_fb_with(2, root, "/dev", &out, &e, fake_fbcon_ioctl, &fx) == TAM_DISPLAY_OK);
    assert(out.vt_number == 2 && out.fb_index == 0 && out.card == 1 && out.con2fb_verified);
    assert(out.width == 1920 && out.height == 1080 && out.stride == 7680 && out.bits_per_pixel == 32);
    assert(!strcmp(out.fb_name, "amdgpudrmfb"));

    /* EACCES on /dev/fb0 falls back cleanly when only one fb device exists. */
    fx.ioctl_errno = EACCES;
    assert(tam_display_map_console_fb_with(2, root, "/dev", &out, &e, fake_fbcon_ioctl, &fx) == TAM_DISPLAY_OK);
    assert(!out.con2fb_verified && out.fb_index == 0 && out.card == 1);

    /* With a second fb device, EACCES refuses ambiguous fallback, while CON2FBMAP resolves fb1 -> card2. */
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s/class/graphics/fb1/device/drm/card2'", root);
    assert(system(cmd) == 0);
    snprintf(path, sizeof(path), "%s/class/graphics/fb1/state", root); write_file(path, "0\n");
    snprintf(path, sizeof(path), "%s/class/graphics/fb1/name", root); write_file(path, "inteldrmfb\n");
    snprintf(path, sizeof(path), "%s/class/graphics/fb1/virtual_size", root); write_file(path, "2560,1440\n");
    snprintf(path, sizeof(path), "%s/class/graphics/fb1/stride", root); write_file(path, "10240\n");
    snprintf(path, sizeof(path), "%s/class/graphics/fb1/bits_per_pixel", root); write_file(path, "32\n");
    assert(tam_display_map_console_fb_with(2, root, "/dev", &out, &e, fake_fbcon_ioctl, &fx) == TAM_DISPLAY_AMBIGUOUS);
    fx.ioctl_errno = 0; fx.mapped_fb = 1;
    assert(tam_display_map_console_fb_with(2, root, "/dev", &out, &e, fake_fbcon_ioctl, &fx) == TAM_DISPLAY_OK);
    assert(out.con2fb_verified && out.fb_index == 1 && out.card == 2 && out.width == 2560);

    /* Non-DRM framebuffer (efifb without device/drm) is rejected as unsupported. */
    snprintf(cmd, sizeof(cmd), "rm -rf '%s/class/graphics/fb1/device/drm'", root);
    assert(system(cmd) == 0);
    assert(tam_display_map_console_fb_with(2, root, "/dev", &out, &e, fake_fbcon_ioctl, &fx) == TAM_DISPLAY_UNSUPPORTED);

    /* Unbound fbcon is rejected. */
    snprintf(path, sizeof(path), "%s/class/vtconsole/vtcon1/bind", root); write_file(path, "0\n");
    fx.mapped_fb = 0;
    assert(tam_display_map_console_fb_with(2, root, "/dev", &out, &e, fake_fbcon_ioctl, &fx) == TAM_DISPLAY_UNSUPPORTED);

    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    assert(system(cmd) == 0);
}

int main(void)
{
    struct fixture f = {0};
    struct tam_display_topology *t = observe(&f);
    unsigned total = f.calls;
    free(t);
    /* Inject permission/device failures at every query and check all cleanup. */
    for (unsigned i = 1; i <= total; ++i)
        failed((struct fixture){.fail_at = i}, TAM_DISPLAY_IO);
    failed((struct fixture){.huge_resources = true}, TAM_DISPLAY_LIMIT);
    failed((struct fixture){.huge_properties = true}, TAM_DISPLAY_LIMIT);
    failed((struct fixture){.huge_planes = true}, TAM_DISPLAY_LIMIT);
    failed((struct fixture){.churn = true}, TAM_DISPLAY_STALE);
    failed((struct fixture){.no_atomic = true}, TAM_DISPLAY_UNSUPPORTED);
    failed((struct fixture){.missing_route = true}, TAM_DISPLAY_UNSUPPORTED);
    failed((struct fixture){.interrupt = true}, TAM_DISPLAY_IO);
    failed((struct fixture){.device_lost = true}, TAM_DISPLAY_STALE);
    failed((struct fixture){.property_churn = true}, TAM_DISPLAY_STALE);
    failed((struct fixture){.plane_churn = true}, TAM_DISPLAY_STALE);
    f = (struct fixture){.no_atomic = true};
    assert(tam_display_inspect_with(42, &t, NULL, fake_ioctl, &f) == TAM_DISPLAY_UNSUPPORTED && !t);
    f = (struct fixture){.once = true};
    free(observe(&f));
    assert(f.resource_reads == 4);
    assert(tam_display_inspect_fd(-1, &t, NULL) == TAM_DISPLAY_INVALID && !t);
    assert(tam_display_inspect_fd(0, NULL, NULL) == TAM_DISPLAY_INVALID);
    planning();
    fbcon_mapping_checks();
    char name[40];
    tam_display_connector_name(11, 2, name, sizeof(name));
    assert(!strcmp(name, "HDMI-A-2"));
    tam_display_connector_name(UINT32_MAX, UINT32_MAX, name, sizeof(name));
    assert(!strcmp(name, "Unknown4294967295-4294967295"));
    puts("display: query failure injection, bounds, hotplug retry and routing passed");
    return 0;
}
