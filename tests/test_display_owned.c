#include "tam_display_internal.h"
#include <drm.h>
#include <drm_mode.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake {
    struct tam_display_topology *t;
    unsigned reads, calls, tests, commits, gem_closes, fb_closes, blob_closes;
    unsigned live_fbs, live_blobs;
    unsigned fail_at, fail_test, fail_commit, stale_at, unplug_at, replug_at, gate_at;
    bool no_handles, no_close, bad_blob, huge_blob, lost;
    bool interrupted_commit, gate_deny;
};

static void prop(struct tam_display_object *o, const char *name, uint32_t id, uint64_t value, uint32_t extra)
{
    struct tam_display_property *p = &o->properties[o->property_count++];
    p->id = id; p->value = value; p->flags = DRM_MODE_PROP_ATOMIC | extra;
    strcpy(p->name, name);
}
static struct fake fixture(void)
{
    struct fake f = {.t = calloc(1, sizeof(*f.t))};
    assert(f.t);
    strcpy(f.t->driver, "testgpu");
    f.t->connector_count = 3; f.t->crtc_count = 2; f.t->plane_count = 2;
    for (int i = 0; i < 3; ++i) {
        struct tam_display_connector *c = &f.t->connectors[i];
        c->object.id = 100 + i; c->connection = 1; c->crtc_id = i < 2 ? 10 : 11;
        strcpy(c->name, i == 0 ? "DP-1" : i == 1 ? "DP-2" : "HDMI-A-1");
        prop(&c->object, "CRTC_ID", 1, c->crtc_id, DRM_MODE_PROP_OBJECT);
        prop(&c->object, "EDID", 8, 900 + i, DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE);
    }
    for (int i = 0; i < 2; ++i) {
        struct tam_display_crtc *c = &f.t->crtcs[i];
        c->object.id = 10+i; c->mode_valid = true; c->framebuffer_id = 77;
        prop(&c->object, "ACTIVE", 2, 1, DRM_MODE_PROP_RANGE);
        prop(&c->object, "MODE_ID", 3, 50, DRM_MODE_PROP_BLOB);
        prop(&c->object, "OUT_FENCE_PTR", 4, 9999, DRM_MODE_PROP_RANGE);
        struct tam_display_plane *p = &f.t->planes[i];
        p->object.id = 200+i; p->crtc_id = 10+i; p->framebuffer_id = 77;
        prop(&p->object, "CRTC_ID", 1, 10+i, DRM_MODE_PROP_OBJECT);
        prop(&p->object, "FB_ID", 5, 77, DRM_MODE_PROP_OBJECT);
        prop(&p->object, "SRC_W", 6, 1920U << 16, DRM_MODE_PROP_RANGE);
        prop(&p->object, "IN_FENCE_FD", 7, UINT64_MAX, DRM_MODE_PROP_SIGNED_RANGE);
    }
    return f;
}

static struct tam_display_object *object(struct fake *f, uint32_t id)
{
    for (size_t i = 0; i < f->t->connector_count; ++i) if (f->t->connectors[i].object.id == id) return &f->t->connectors[i].object;
    for (size_t i = 0; i < f->t->crtc_count; ++i) if (f->t->crtcs[i].object.id == id) return &f->t->crtcs[i].object;
    for (size_t i = 0; i < f->t->plane_count; ++i) if (f->t->planes[i].object.id == id) return &f->t->planes[i].object;
    assert(!"Unknown object"); return NULL;
}

enum tam_display_result __wrap_tam_display_inspect_with(int fd,
    struct tam_display_topology **out, struct tam_display_error *e,
    tam_display_ioctl_fn call, void *context)
{
    (void)call;
    assert(fd == 42);
    struct fake *f = context;
    ++f->reads;
    if (f->reads == f->unplug_at) f->t->connectors[1].connection = 2;
    if (f->reads == f->replug_at) f->t->connectors[0].connection = 1;
    if (f->reads == f->gate_at) f->gate_deny = true;
    *out = NULL;
    if (f->lost) return tam_display_fail(e, TAM_DISPLAY_STALE, ENODEV, "Device gone");
    *out = malloc(sizeof(**out)); assert(*out);
    memcpy(*out, f->t, sizeof(**out));
    if (f->reads == f->stale_at) (*out)->connectors[0].object.id = 999;
    return TAM_DISPLAY_OK;
}

static int fake_ioctl(void *context, int fd, unsigned long request, void *arg)
{
    struct fake *f = context;
    assert(fd == 42);
    if (++f->calls == f->fail_at) { errno = EIO; return -1; }
    if (request == DRM_IOCTL_MODE_GETFB2) {
        struct drm_mode_fb_cmd2 *b = arg;
        assert(b->fb_id == 77);
        b->width = 1920; b->height = 1080; b->pitches[0] = 7680;
        if (!f->no_handles) b->handles[0] = b->handles[1] = 11;
    } else if (request == DRM_IOCTL_MODE_ADDFB2) {
        struct drm_mode_fb_cmd2 *b = arg;
        assert(b->handles[0] == 11 && b->handles[1] == 11);
        b->fb_id = 700; ++f->live_fbs;
    } else if (request == DRM_IOCTL_GEM_CLOSE) {
        struct drm_gem_close *g = arg;
        assert(g->handle == 11); ++f->gem_closes;
    } else if (request == DRM_IOCTL_MODE_CLOSEFB) {
        struct drm_mode_closefb *b = arg;
        assert(b->fb_id == 700);
        if (f->no_close) { errno = ENOTTY; return -1; }
        assert(f->live_fbs); --f->live_fbs; ++f->fb_closes;
    } else if (request == DRM_IOCTL_MODE_RMFB) {
        /* Legal ONLY for never-attached CLOSEFB compatibility probe. */
        assert((f->no_close || f->fail_at) && f->commits == 0 && *(uint32_t *)arg == 700);
        assert(f->live_fbs); --f->live_fbs;
    } else if (request == DRM_IOCTL_MODE_GETPROPBLOB) {
        struct drm_mode_get_blob *b = arg;
        assert(b->blob_id == 50);
        if (b->data) { memset((void *)(uintptr_t)b->data, 0x17, b->length); if (f->bad_blob) ++b->length; }
        else b->length = f->huge_blob ? UINT32_MAX : sizeof(struct drm_mode_modeinfo);
    } else if (request == DRM_IOCTL_MODE_CREATEPROPBLOB) {
        struct drm_mode_create_blob *b = arg;
        assert(b->length == sizeof(struct drm_mode_modeinfo));
        assert(*(unsigned char *)(uintptr_t)b->data == 0x17);
        b->blob_id = 500; ++f->live_blobs;
    } else if (request == DRM_IOCTL_MODE_DESTROYPROPBLOB) {
        assert(((struct drm_mode_destroy_blob *)arg)->blob_id == 500);
        assert(f->live_blobs); --f->live_blobs; ++f->blob_closes;
    } else if (request == DRM_IOCTL_MODE_ATOMIC) {
        struct drm_mode_atomic *a = arg;
        assert(a->flags & DRM_MODE_ATOMIC_ALLOW_MODESET);
        assert(!(a->flags & (DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT)));
        bool test = a->flags & DRM_MODE_ATOMIC_TEST_ONLY;
        if (test) { if (++f->tests == f->fail_test) { errno = EINVAL; return -1; } }
        else if (++f->commits == f->fail_commit) { errno = EIO; return -1; }
        uint32_t *ids = (void *)(uintptr_t)a->objs_ptr, *counts = (void *)(uintptr_t)a->count_props_ptr;
        uint32_t *props = (void *)(uintptr_t)a->props_ptr;
        uint64_t *values = (void *)(uintptr_t)a->prop_values_ptr;
        size_t pos = 0;
        for (unsigned i = 0; i < a->count_objs; ++i) {
            struct tam_display_object *o = object(f, ids[i]);
            for (unsigned j = 0; j < counts[i]; ++j, ++pos) {
                bool found = false;
                for (size_t k = 0; k < o->property_count; ++k) if (o->properties[k].id == props[pos]) {
                    struct tam_display_property *p = &o->properties[k];
                    assert(strcmp(p->name, "OUT_FENCE_PTR") && strcmp(p->name, "IN_FENCE_FD"));
                    if (!strcmp(p->name, "CRTC_ID"))
                        for (size_t c = 0; c < f->t->connector_count; ++c)
                            if (f->t->connectors[c].object.id == ids[i] &&
                                f->t->connectors[c].connection != 1) assert(!values[pos]);
                    if (!strcmp(p->name, "FB_ID") && values[pos]) assert(values[pos] == 700 && f->live_fbs);
                    if (!strcmp(p->name, "MODE_ID") && values[pos]) assert(values[pos] == 500 && f->live_blobs);
                    if (!test) p->value = values[pos];
                    found = true;
                }
                assert(found);
            }
        }
        if (!test && f->interrupted_commit) { f->interrupted_commit = false; errno = EINTR; return -1; }
    } else assert(!"Unexpected ioctl");
    return 0;
}

static enum tam_display_result gate(void *context, bool restoring, struct tam_display_error *e)
{
    (void)restoring;
    struct fake *f = context;
    return f->gate_deny ? tam_display_fail(e, TAM_DISPLAY_INACTIVE, 0, "Foreground session lost") : TAM_DISPLAY_OK;
}
static struct tam_display_state *capture(struct fake *f)
{
    struct tam_display_state *s = NULL;
    struct tam_display_error e = {0};
    enum tam_display_result rc = tam_display_capture_with(42, &s, &e, fake_ioctl, f);
    if (rc) fprintf(stderr, "capture: %s\n", e.message);
    assert(rc == TAM_DISPLAY_OK && s);
    assert(f->live_fbs == 1 && f->live_blobs == 1 && f->gem_closes == 1 && f->tests == 1);
    tam_display_state_set_access_check(s, gate, f);
    return s;
}
static void cleanup(struct fake *f, struct tam_display_state *s)
{
    tam_display_state_free(s);
    assert(!f->live_blobs && !f->live_fbs);
    free(f->t);
}

static void unplug(struct fake *f, unsigned which)
{
    f->t->connectors[which].connection = 2;
    f->t->connectors[which].object.properties[1].value = 0; /* EDID loss */
    f->t->connectors[which].object.properties[0].value = 0; /* kernel detach */
}

static void hotplug_checks(void)
{
    struct tam_display_error e;
    struct fake f;
    struct tam_display_state *s;
    /* All retained choices, every loss subset, shared CRTCs and shared FBs.
     * Surviving captured outputs return to normal; none needs a new mode. */
    for (unsigned keep = 0; keep < 3; ++keep) for (unsigned lost = 1; lost < 8; ++lost) {
        f = fixture(); s = capture(&f);
        assert(tam_display_state_keep(s, f.t->connectors[keep].name, &e) == TAM_DISPLAY_OK);
        for (unsigned i = 0; i < 3; ++i) if (lost & (1U << i)) unplug(&f, i);
        enum tam_display_result rc = tam_display_state_restore(s, &e);
        if (lost == 7) {
            assert(rc == TAM_DISPLAY_RESTORE_FAILED && f.commits == 1 && f.tests == 2);
            assert(strstr(e.message, "No captured connected console output"));
        } else {
            assert(rc == TAM_DISPLAY_OK && f.commits == 2 && f.tests == 3);
            for (unsigned i = 0; i < 3; ++i)
                assert(f.t->connectors[i].object.properties[0].value ==
                    ((lost & (1U << i)) ? 0U : i < 2 ? 10U : 11U));
            for (unsigned i = 0; i < 2; ++i) {
                bool active = i == 0 ? (lost & 3) != 3 : !(lost & 4);
                assert(f.t->crtcs[i].object.properties[0].value == (unsigned)active);
                assert(f.t->crtcs[i].object.properties[1].value == (active ? 500U : 0U));
                assert(f.t->planes[i].object.properties[0].value == (active ? 10U+i : 0U));
                assert(f.t->planes[i].object.properties[1].value == (active ? 700U : 0U));
            }
            assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_OK && f.commits == 2);
        }
        cleanup(&f, s);
    }
    /* MST-style removal: no writes to vanished connector IDs. Leave modes
     * on any surviving shared pipeline and disable only unused pipelines. */
    for (unsigned keep = 0; keep < 3; ++keep) for (unsigned lost = 1; lost < 8; ++lost) {
        f = fixture(); s = capture(&f);
        assert(tam_display_state_keep(s, f.t->connectors[keep].name, &e) == TAM_DISPLAY_OK);
        for (unsigned i = 3; i-- > 0;) if (lost & (1U << i)) {
            memmove(&f.t->connectors[i], &f.t->connectors[i+1],
                (f.t->connector_count - i - 1) * sizeof(f.t->connectors[0]));
            --f.t->connector_count;
        }
        assert(tam_display_state_restore(s, &e) ==
            (lost == 7 ? TAM_DISPLAY_RESTORE_FAILED : TAM_DISPLAY_OK));
        assert(f.commits == (lost == 7 ? 1U : 2U));
        if (lost != 7) {
            for (size_t i = 0; i < f.t->connector_count; ++i)
                assert(f.t->connectors[i].object.properties[0].value ==
                    (f.t->connectors[i].object.id < 102 ? 10U : 11U));
            assert(f.t->crtcs[0].object.properties[0].value == ((lost & 3) != 3));
            assert(f.t->crtcs[1].object.properties[0].value == !(lost & 4));
        }
        cleanup(&f, s);
    }
    /* No EDID property is required for a fixed connector; where present it
     * must match, except disappearance on a disconnected output. */
    for (unsigned mutation = 0; mutation < 11; ++mutation) {
        f = fixture(); s = capture(&f);
        assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_OK);
        unplug(&f, 0);
        switch (mutation) {
        case 0: f.t->connectors[0].object.properties[1].value = 999; break;
        case 1: f.t->connectors[1].object.properties[1].value = 999; break;
        case 2: f.t->connectors[0].object.id = 999; break;
        case 3: --f.t->crtc_count; break;
        case 4: f.t->connectors[0].connection = 3; break;
        case 5: f.t->connectors[1].object.properties[0].value = 11; break;
        case 6: ++f.t->connectors[0].object.properties[0].id; break;
        case 7: f.t->planes[0].object.id = 999; break;
        case 8: strcpy(f.t->driver, "replacement"); break;
        case 9: f.lost = true; break;
        case 10: f.t->planes[0].object.properties[2].value = 42; break;
        }
        assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_RESTORE_FAILED && f.commits == 1);
        cleanup(&f, s);
    }
    /* Additions never borrow an old output's mode, even with the same name. */
    f = fixture(); f.t->connectors[0].connection = 2;
    f.t->connectors[0].crtc_id = 0;
    f.t->connectors[0].object.properties[0].value = 0;
    f.t->connectors[0].object.properties[1].value = 0;
    s = capture(&f);
    assert(tam_display_state_keep(s, "DP-2", &e) == TAM_DISPLAY_OK);
    f.t->connectors[0].connection = 1;
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_RESTORE_FAILED && f.commits == 1);
    cleanup(&f, s);

    /* The sole retained route may disappear and its CRTC be disabled even
     * when a baseline-sharing but deliberately detached connector survives. */
    for (unsigned shared = 0; shared < 2; ++shared) {
        f = fixture(); s = capture(&f);
        assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_OK);
        unplug(&f, 0); if (!shared) unplug(&f, 1);
        f.t->crtcs[0].object.properties[0].value = 0;
        f.t->crtcs[0].object.properties[1].value = 0;
        f.t->planes[0].object.properties[0].value = 0;
        f.t->planes[0].object.properties[1].value = 0;
        assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_OK);
        assert(f.commits == 2);
        assert(f.t->crtcs[0].object.properties[0].value == shared);
        assert(f.t->planes[0].object.properties[1].value == (shared ? 700U : 0U));
        cleanup(&f, s);
    }
    /* Unplugging an excluded connector does not authorize unrelated
     * disablement of the still-retained output's pipeline. */
    f = fixture(); s = capture(&f);
    assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_OK);
    unplug(&f, 1);
    f.t->crtcs[0].object.properties[0].value = 0;
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_RESTORE_FAILED && f.commits == 1);
    cleanup(&f, s);

    /* Changes after TEST_ONLY must never commit the tested stale request. */
    for (unsigned race = 0; race < 4; ++race) {
        f = fixture(); s = capture(&f);
        assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_OK);
        unplug(&f, 0);
        switch (race) {
        case 0: f.unplug_at = f.reads + 2; break;
        case 1: f.replug_at = f.reads + 2; break;
        case 2: f.stale_at = f.reads + 2; break;
        case 3: f.gate_at = f.reads + 2; break;
        }
        assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_RESTORE_FAILED && f.commits == 1);
        assert(f.tests == 3);
        cleanup(&f, s);
    }
    /* Recovery TEST_ONLY failure never writes; uncertain writes are not retried. */
    for (unsigned failure = 0; failure < 3; ++failure) {
        f = fixture(); s = capture(&f);
        assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_OK);
        unplug(&f, 0);
        if (!failure) f.fail_test = 3;
        else if (failure == 1) f.fail_commit = 2;
        else f.interrupted_commit = true;
        assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_RESTORE_FAILED);
        assert(f.commits == (!failure ? 1U : 2U));
        cleanup(&f, s);
    }
}

int main(void)
{
    struct tam_display_error e;
    struct fake f = fixture();
    struct tam_display_state *s = capture(&f);
    assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_OK);
    assert(f.commits == 1 && f.tests == 2);
    /* Shared CRTC/plane survive; only HDMI's independent CRTC/plane turn off. */
    assert(f.t->connectors[0].object.properties[0].value == 10);
    assert(f.t->connectors[1].object.properties[0].value == 0);
    assert(f.t->crtcs[0].object.properties[0].value == 1);
    assert(f.t->crtcs[1].object.properties[0].value == 0);
    assert(f.t->planes[0].object.properties[1].value == 77);
    assert(f.t->planes[1].object.properties[1].value == 0);
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_OK);
    assert(f.commits == 2 && f.tests == 3);
    assert(f.t->connectors[1].object.properties[0].value == 10);
    assert(f.t->planes[1].object.properties[1].value == 700);
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_OK && f.commits == 2);
    cleanup(&f, s);

    /* Verify console framebuffer geometry and individual blank/wake transitions. */
    f = fixture(); s = capture(&f);
    struct tam_console_fb cfb = {.vt_number = 2, .fb_index = 0, .card = 1,
        .width = 1920, .height = 1080, .stride = 7680, .bits_per_pixel = 32};
    assert(tam_display_state_verify_console_fb(s, &cfb, &e) == TAM_DISPLAY_OK && cfb.drm_fb_id == 77);
    cfb.stride = 4096;
    assert(tam_display_state_verify_console_fb(s, &cfb, &e) == TAM_DISPLAY_UNSUPPORTED);
    assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_OK);
    assert(tam_display_state_is_detached(s, "DP-2") && tam_display_state_is_detached(s, "HDMI-A-1"));
    assert(!tam_display_state_is_detached(s, "DP-1"));
    /* Wake HDMI-A-1 individually while keeping DP-2 blanked. */
    assert(tam_display_state_wake(s, "HDMI-A-1", &e) == TAM_DISPLAY_OK);
    assert(tam_display_state_is_detached(s, "DP-2") && !tam_display_state_is_detached(s, "HDMI-A-1"));
    assert(f.t->connectors[1].object.properties[0].value == 0);
    assert(f.t->connectors[2].object.properties[0].value == 11);
    assert(f.t->crtcs[1].object.properties[0].value == 1);
    assert(f.t->planes[1].object.properties[1].value == 700);
    /* Blank HDMI-A-1 again incrementally, then wake DP-2 and HDMI-A-1. */
    const char *reblank[] = {"card1-HDMI-A-1"};
    assert(tam_display_state_blank(s, reblank, 1, &e) == TAM_DISPLAY_OK);
    assert(tam_display_state_is_detached(s, "HDMI-A-1") && f.t->crtcs[1].object.properties[0].value == 0);
    const char *all_remaining[] = {"DP-1"};
    assert(tam_display_state_blank(s, all_remaining, 1, &e) == TAM_DISPLAY_UNSUPPORTED);
    assert(tam_display_state_wake(s, "DP-2", &e) == TAM_DISPLAY_OK);
    assert(!tam_display_state_is_detached(s, "DP-2") && tam_display_state_is_detached(s, "HDMI-A-1"));
    assert(tam_display_state_wake(s, "HDMI-A-1", &e) == TAM_DISPLAY_OK);
    assert(!s->changed && !tam_display_state_is_detached(s, "HDMI-A-1"));
    cleanup(&f, s);

    /* Every capture ioctl failure is propagated; no real modeset is issued. */
    for (unsigned fail = 1; fail <= 9; ++fail) {
        f = fixture(); f.fail_at = fail; s = NULL;
        enum tam_display_result rc = tam_display_capture_with(42, &s, &e, fake_ioctl, &f);
        assert(rc != TAM_DISPLAY_OK || fail == 8); /* GEM_CLOSE cleanup is best effort. */
        assert(f.commits == 0);
        tam_display_state_free(s); free(f.t);
    }
    f = fixture(); f.no_handles = true; s = NULL;
    assert(tam_display_capture_with(42, &s, &e, fake_ioctl, &f) == TAM_DISPLAY_PERMISSION && !s);
    assert(!f.live_blobs && !f.live_fbs); free(f.t);
    f = fixture(); f.no_close = true;
    assert(tam_display_capture_with(42, &s, &e, fake_ioctl, &f) == TAM_DISPLAY_UNSUPPORTED && !s);
    assert(!f.live_blobs && !f.live_fbs); free(f.t);
    f = fixture(); f.bad_blob = true;
    assert(tam_display_capture_with(42, &s, &e, fake_ioctl, &f) == TAM_DISPLAY_STALE && !s); free(f.t);
    f = fixture(); f.huge_blob = true;
    assert(tam_display_capture_with(42, &s, &e, fake_ioctl, &f) == TAM_DISPLAY_LIMIT && !s); free(f.t);
    f = fixture(); f.fail_test = 1;
    assert(tam_display_capture_with(42, &s, &e, fake_ioctl, &f) != TAM_DISPLAY_OK && !s);
    assert(!f.live_blobs && !f.live_fbs); free(f.t);

    f = fixture(); s = capture(&f); f.fail_test = 2;
    assert(tam_display_state_keep(s, "DP-1", &e) != TAM_DISPLAY_OK && !f.commits);
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_OK); cleanup(&f, s);
    f = fixture(); s = capture(&f); f.stale_at = f.reads + 2;
    assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_STALE && !f.commits); cleanup(&f, s);
    f = fixture(); s = capture(&f); f.gate_deny = true;
    assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_INACTIVE && !f.commits);
    f.gate_deny = false; assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_OK); cleanup(&f, s);
    f = fixture(); s = capture(&f); tam_display_state_set_access_check(s, NULL, NULL);
    assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_PERMISSION && !f.commits);
    tam_display_state_set_access_check(s, gate, &f); assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_OK); cleanup(&f, s);

    /* EINTR after an applied commit is not retried. Owned restoration still works. */
    f = fixture(); s = capture(&f); f.interrupted_commit = true;
    assert(tam_display_state_keep(s, "DP-1", &e) != TAM_DISPLAY_OK && f.commits == 1);
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_OK && f.commits == 2); cleanup(&f, s);
    f = fixture(); s = capture(&f); f.fail_commit = 1;
    assert(tam_display_state_keep(s, "DP-1", &e) != TAM_DISPLAY_OK);
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_OK); cleanup(&f, s);
    f = fixture(); s = capture(&f);
    assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_OK);
    f.fail_test = 3;
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_RESTORE_FAILED && f.commits == 1);
    f.fail_test = 0; f.lost = true;
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_RESTORE_FAILED && f.commits == 1); cleanup(&f, s);
    f = fixture(); s = capture(&f);
    assert(tam_display_state_keep(s, "DP-1", &e) == TAM_DISPLAY_OK);
    f.t->planes[0].object.properties[2].value = 42;
    assert(tam_display_state_restore(s, &e) == TAM_DISPLAY_RESTORE_FAILED && f.commits == 1); cleanup(&f, s);
    hotplug_checks();
    puts("Owned restoration, unplug recovery and atomic failure checks passed");
}
