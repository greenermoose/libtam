#include "tam_display_internal.h"
#include <drm.h>
#include <drm_mode.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* No retries of writes: EINTR can mean that a transaction completed. */
int tam_display_call(struct tam_display_state *s, unsigned long request, void *arg)
{
    return s->call(s->context, s->fd, request, arg);
}

static enum tam_display_result ioerror(struct tam_display_error *e, const char *what)
{
    int saved = errno;
    enum tam_display_result rc = saved == EACCES || saved == EPERM ? TAM_DISPLAY_PERMISSION :
        saved == ENODEV || saved == ENOENT ? TAM_DISPLAY_STALE : TAM_DISPLAY_IO;
    return tam_display_fail(e, rc, saved, what);
}

static void close_handles(struct tam_display_state *s, struct drm_mode_fb_cmd2 *fb)
{
    for (size_t i = 0; i < 4; ++i) {
        if (!fb->handles[i]) continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) if (fb->handles[j] == fb->handles[i]) duplicate = true;
        if (!duplicate) {
            struct drm_gem_close c = {.handle = fb->handles[i]};
            tam_display_call(s, DRM_IOCTL_GEM_CLOSE, &c);
        }
    }
}

static enum tam_display_result retain_fb(struct tam_display_state *s, uint32_t id,
    uint64_t *owned, struct tam_display_error *e)
{
    for (size_t i = 0; i < s->fb_count; ++i)
        if (s->fbs[i].original == id) { *owned = s->fbs[i].owned; return TAM_DISPLAY_OK; }
    if (s->fb_count == TAM_DISPLAY_MAX_REFS)
        return tam_display_fail(e, TAM_DISPLAY_LIMIT, 0, "Too many framebuffer references");
    struct drm_mode_fb_cmd2 fb = {.fb_id = id};
    if (tam_display_call(s, DRM_IOCTL_MODE_GETFB2, &fb) < 0) return ioerror(e, "Cannot retain console framebuffer");
    enum tam_display_result rc = TAM_DISPLAY_OK;
    if (!fb.handles[0]) {
        rc = tam_display_fail(e, TAM_DISPLAY_PERMISSION, 0, "DRM master must provide restorable framebuffer handles");
        goto done;
    }
    fb.fb_id = 0;
    if (tam_display_call(s, DRM_IOCTL_MODE_ADDFB2, &fb) < 0) { rc = ioerror(e, "Cannot register owned framebuffer"); goto done; }
    /* Prove non-destructive close support BEFORE anything can scan out a clone.
     * RMFB is safe only for this never-attached probe, not during restoration. */
    struct drm_mode_closefb probe = {.fb_id = fb.fb_id};
    if (tam_display_call(s, DRM_IOCTL_MODE_CLOSEFB, &probe) < 0) {
        int saved = errno;
        uint32_t unused = fb.fb_id;
        tam_display_call(s, DRM_IOCTL_MODE_RMFB, &unused);
        rc = tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, saved, "Non-destructive DRM CLOSEFB is required");
        goto done;
    }
    fb.fb_id = 0;
    if (tam_display_call(s, DRM_IOCTL_MODE_ADDFB2, &fb) < 0) { rc = ioerror(e, "Cannot retain owned framebuffer"); goto done; }
    s->fbs[s->fb_count++] = (struct tam_display_ref){
        .original = id,
        .owned = fb.fb_id,
        .width = fb.width,
        .height = fb.height,
        .pitch = fb.pitches[0],
        .handle = fb.handles[0]
    };
    *owned = fb.fb_id;
done:
    close_handles(s, &fb);
    return rc;
}

static enum tam_display_result retain_blob(struct tam_display_state *s, uint32_t id,
    uint64_t *owned, struct tam_display_error *e)
{
    for (size_t i = 0; i < s->blob_count; ++i)
        if (s->blobs[i].original == id) { *owned = s->blobs[i].owned; return TAM_DISPLAY_OK; }
    if (s->blob_count == TAM_DISPLAY_MAX_REFS)
        return tam_display_fail(e, TAM_DISPLAY_LIMIT, 0, "Too many property blobs");
    struct drm_mode_get_blob b = {.blob_id = id};
    if (tam_display_call(s, DRM_IOCTL_MODE_GETPROPBLOB, &b) < 0) return ioerror(e, "Cannot query restoration blob");
    if (!b.length || b.length > 1024 * 1024 || b.length > 4 * 1024 * 1024 - s->blob_bytes)
        return tam_display_fail(e, TAM_DISPLAY_LIMIT, 0, "Restoration blob exceeds bounded storage");
    uint32_t length = b.length;
    void *bytes = malloc(length);
    if (!bytes) return tam_display_fail(e, TAM_DISPLAY_IO, ENOMEM, "Cannot allocate restoration blob");
    b.data = (uintptr_t)bytes;
    enum tam_display_result rc = TAM_DISPLAY_OK;
    if (tam_display_call(s, DRM_IOCTL_MODE_GETPROPBLOB, &b) < 0) rc = ioerror(e, "Cannot read restoration blob");
    else if (b.length != length) rc = tam_display_fail(e, TAM_DISPLAY_STALE, 0, "Restoration blob changed size");
    else {
        struct drm_mode_create_blob c = {.data = (uintptr_t)bytes, .length = length};
        if (tam_display_call(s, DRM_IOCTL_MODE_CREATEPROPBLOB, &c) < 0) rc = ioerror(e, "Cannot own restoration blob");
        else {
            s->blobs[s->blob_count++] = (struct tam_display_ref){.original = id, .owned = c.blob_id};
            s->blob_bytes += length;
            *owned = c.blob_id;
        }
    }
    free(bytes);
    return rc;
}

static enum tam_display_result capture_object(struct tam_display_state *s,
    const struct tam_display_object *o, struct tam_display_error *e)
{
    for (size_t i = 0; i < o->property_count; ++i) {
        const struct tam_display_property *p = &o->properties[i];
        if (!(p->flags & DRM_MODE_PROP_ATOMIC) || (p->flags & DRM_MODE_PROP_IMMUTABLE)) continue;
        /* Fences are process-local handles/pointers, never snapshot values. */
        if (!strcmp(p->name, "OUT_FENCE_PTR")) continue;
        if (!strcmp(p->name, "IN_FENCE_FD")) continue;
        if (strstr(p->name, "WRITEBACK") || strstr(p->name, "_PTR"))
            return tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, 0, "Unsupported transient atomic property");
        if (p->value && (p->flags & DRM_MODE_PROP_EXTENDED_TYPE) == DRM_MODE_PROP_OBJECT &&
            strcmp(p->name, "CRTC_ID") && strcmp(p->name, "FB_ID"))
            return tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, 0, "Unsupported referenced atomic object");
        uint64_t value = p->value;
        enum tam_display_result rc = TAM_DISPLAY_OK;
        if (value && (p->flags & DRM_MODE_PROP_BLOB)) {
            if (value > UINT32_MAX) return tam_display_fail(e, TAM_DISPLAY_STALE, 0, "Invalid property blob ID");
            rc = retain_blob(s, (uint32_t)value, &value, e);
        } else if (value && !strcmp(p->name, "FB_ID")) {
            if (value > UINT32_MAX) return tam_display_fail(e, TAM_DISPLAY_STALE, 0, "Invalid framebuffer ID");
            rc = retain_fb(s, (uint32_t)value, &value, e);
        }
        if (rc) return rc;
        rc = tam_display_append(&s->restore, o->id, p->id, value, e);
        if (rc) return rc;
    }
    return TAM_DISPLAY_OK;
}

enum tam_display_result tam_display_capture_with(int fd, struct tam_display_state **out,
    struct tam_display_error *e, tam_display_ioctl_fn call, void *context)
{
    if (out) *out = NULL;
    if (!out || !call || fd < 0) return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Invalid owned display capture");
    struct tam_display_state *s = calloc(1, sizeof(*s));
    if (!s) return tam_display_fail(e, TAM_DISPLAY_IO, ENOMEM, "Cannot allocate owned display state");
    s->fd = fd; s->call = call; s->context = context;
    enum tam_display_result rc = tam_display_inspect_with(fd, &s->baseline, e, call, context);
    if (rc) goto fail;
    for (size_t i = 0; i < s->baseline->connector_count; ++i) {
        rc = capture_object(s, &s->baseline->connectors[i].object, e); if (rc) goto fail;
    }
    for (size_t i = 0; i < s->baseline->crtc_count; ++i) {
        rc = capture_object(s, &s->baseline->crtcs[i].object, e); if (rc) goto fail;
    }
    for (size_t i = 0; i < s->baseline->plane_count; ++i) {
        rc = capture_object(s, &s->baseline->planes[i].object, e); if (rc) goto fail;
    }
    if (!s->restore.count || !s->fb_count || !s->blob_count) {
        rc = tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, 0, "No complete atomic console state to restore"); goto fail;
    }
    rc = tam_display_validate(s, false, e); if (rc) goto fail;
    rc = tam_display_atomic(s, &s->restore, true, e); if (rc) goto fail;
    *out = s;
    return TAM_DISPLAY_OK;
fail:
    /* Internal capture borrows fd on failure; public wrapper closes its dup. */
    tam_display_state_free(s);
    return rc;
}

void tam_display_state_free(struct tam_display_state *s)
{
    if (!s) return;
    /* Never RMFB: restored clones may be active scanout. Even on failure this
     * release only drops our references; it does not disable CRTCs/planes. */
    for (size_t i = 0; i < s->fb_count; ++i) {
        struct drm_mode_closefb c = {.fb_id = s->fbs[i].owned};
        tam_display_call(s, DRM_IOCTL_MODE_CLOSEFB, &c);
    }
    for (size_t i = 0; i < s->blob_count; ++i) {
        struct drm_mode_destroy_blob b = {.blob_id = s->blobs[i].owned};
        tam_display_call(s, DRM_IOCTL_MODE_DESTROYPROPBLOB, &b);
    }
    if (s->close_fd && s->fd >= 0) close(s->fd);
    free(s->baseline); free(s);
}
