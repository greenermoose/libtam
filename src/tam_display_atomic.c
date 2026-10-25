#include "tam_display_internal.h"
#include <drm.h>
#include <drm_mode.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum tam_display_result tam_display_append(struct tam_display_request *r,
    uint32_t object, uint32_t property, uint64_t value, struct tam_display_error *e)
{
    if (!object || !property) return tam_display_fail(e, TAM_DISPLAY_STALE, 0, "Invalid atomic object or property");
    if (r->count == TAM_DISPLAY_MAX_WRITES)
        return tam_display_fail(e, TAM_DISPLAY_LIMIT, 0, "Atomic request exceeds bounded storage");
    r->writes[r->count++] = (struct tam_display_write){object, property, value};
    return TAM_DISPLAY_OK;
}

enum tam_display_result tam_display_atomic(struct tam_display_state *s,
    const struct tam_display_request *r, bool test, struct tam_display_error *e)
{
    struct packed {
        uint32_t objects[3 * TAM_DISPLAY_MAX_OBJECTS], counts[3 * TAM_DISPLAY_MAX_OBJECTS];
        uint32_t properties[TAM_DISPLAY_MAX_WRITES];
        uint64_t values[TAM_DISPLAY_MAX_WRITES];
    } *p = calloc(1, sizeof(*p));
    if (!p) return tam_display_fail(e, TAM_DISPLAY_IO, ENOMEM, "Cannot allocate atomic transaction");
    uint32_t count = 0;
    for (size_t i = 0; i < r->count; ++i) {
        if (!i || r->writes[i].object != r->writes[i-1].object) {
            if (count == 3 * TAM_DISPLAY_MAX_OBJECTS) {
                free(p); return tam_display_fail(e, TAM_DISPLAY_LIMIT, 0, "Too many atomic objects");
            }
            p->objects[count++] = r->writes[i].object;
        }
        ++p->counts[count-1];
        p->properties[i] = r->writes[i].property;
        p->values[i] = r->writes[i].value;
    }
    struct drm_mode_atomic a = {.flags = DRM_MODE_ATOMIC_ALLOW_MODESET |
        (test ? DRM_MODE_ATOMIC_TEST_ONLY : 0), .count_objs = count,
        .objs_ptr = (uintptr_t)p->objects, .count_props_ptr = (uintptr_t)p->counts,
        .props_ptr = (uintptr_t)p->properties, .prop_values_ptr = (uintptr_t)p->values};
    if (!test) {
        if (!s->access_check) { free(p); return tam_display_fail(e, TAM_DISPLAY_PERMISSION, 0, "Foreground/session authorization check is required for writes"); }
        enum tam_display_result rc = s->access_check(s->access_context, s->restoring, e);
        if (rc) { free(p); return rc; }
    }
    int result = tam_display_call(s, DRM_IOCTL_MODE_ATOMIC, &a), saved = errno;
    free(p);
    if (result < 0) return tam_display_fail(e,
        saved == EPERM || saved == EACCES ? TAM_DISPLAY_PERMISSION :
        saved == ENOENT || saved == ENODEV ? TAM_DISPLAY_STALE : TAM_DISPLAY_IO,
        saved, test ? "Kernel rejected atomic TEST_ONLY" : "Atomic display transaction failed; restoration required");
    return TAM_DISPLAY_OK;
}

static bool transient(const struct tam_display_property *p)
{
    return !strcmp(p->name, "IN_FENCE_FD") || !strcmp(p->name, "OUT_FENCE_PTR");
}

static bool same_object(struct tam_display_state *s, const struct tam_display_object *old,
    const struct tam_display_object *now, bool recovery)
{
    if (old->id != now->id || old->property_count != now->property_count) return false;
    for (size_t i = 0; i < old->property_count; ++i) {
        const struct tam_display_property *a = &old->properties[i], *b = &now->properties[i];
        if (a->id != b->id || a->flags != b->flags || strcmp(a->name, b->name)) return false;
        if ((a->flags & DRM_MODE_PROP_IMMUTABLE) || !strcmp(a->name, "EDID")) {
            if (a->value != b->value) return false;
            continue;
        }
        if (!(a->flags & DRM_MODE_PROP_ATOMIC) || transient(a)) continue;
        if (a->value == b->value) continue;
        bool ours = false;
        if (recovery) for (size_t j = 0; j < s->change.count; ++j) {
            const struct tam_display_write *w = &s->change.writes[j];
            if (w->object == old->id && w->property == a->id && w->value == b->value) ours = true;
        }
        if (!ours) return false;
    }
    return true;
}

enum tam_display_result tam_display_validate(struct tam_display_state *s, bool recovery,
    struct tam_display_error *e)
{
    /* Same retained open description: never reopen/replay into a replacement.
     * Re-enumeration must agree with the objects and atomic property identities.
     * Unexpected routing/property changes fail closed for design review. */
    struct tam_display_topology *now = NULL, *old = s->baseline;
    enum tam_display_result rc = tam_display_inspect_with(s->fd, &now, e, s->call, s->context);
    if (rc) return rc;
    bool same = !strcmp(now->driver, old->driver) && now->connector_count == old->connector_count &&
        now->crtc_count == old->crtc_count && now->plane_count == old->plane_count;
    for (size_t i = 0; same && i < old->connector_count; ++i)
        same = old->connectors[i].connection == now->connectors[i].connection &&
            !strcmp(old->connectors[i].name, now->connectors[i].name) &&
            same_object(s, &old->connectors[i].object, &now->connectors[i].object, recovery);
    for (size_t i = 0; same && i < old->crtc_count; ++i)
        same = same_object(s, &old->crtcs[i].object, &now->crtcs[i].object, recovery);
    for (size_t i = 0; same && i < old->plane_count; ++i)
        same = same_object(s, &old->planes[i].object, &now->planes[i].object, recovery);
    free(now);
    return same ? TAM_DISPLAY_OK : tam_display_fail(e, TAM_DISPLAY_STALE, 0,
        "Display identity, routing or properties changed outside this transaction");
}

static enum tam_display_result set(struct tam_display_request *r,
    const struct tam_display_object *o, const char *name, uint64_t value, struct tam_display_error *e)
{
    const struct tam_display_property *found = NULL;
    for (size_t i = 0; i < o->property_count; ++i)
        if (!strcmp(o->properties[i].name, name)) {
            if (found) return tam_display_fail(e, TAM_DISPLAY_STALE, 0, "Duplicate atomic property");
            found = &o->properties[i];
        }
    if (!found || !(found->flags & DRM_MODE_PROP_ATOMIC) || (found->flags & DRM_MODE_PROP_IMMUTABLE))
        return tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, 0, "Required mutable atomic property missing");
    return tam_display_append(r, o->id, found->id, value, e);
}

void tam_display_state_set_access_check(struct tam_display_state *s, tam_display_access_check check, void *context)
{
    if (s) { s->access_check = check; s->access_context = context; }
}

enum tam_display_result tam_display_state_keep(struct tam_display_state *s,
    const char *connector, struct tam_display_error *e)
{
    if (e) *e = (struct tam_display_error){0};
    if (!s || s->changed) return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "A fresh owned display state is required");
    struct tam_display_plan plan;
    enum tam_display_result rc = tam_display_plan_keep(s->baseline, connector, &plan, e);
    if (rc) return rc;
    if (!plan.detach_count) return tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, 0, "No other active output to blank");
    s->change.count = 0;
    for (size_t i = 0; i < s->baseline->connector_count; ++i) {
        const struct tam_display_connector *c = &s->baseline->connectors[i];
        for (size_t j = 0; j < plan.detach_count; ++j) if (c->object.id == plan.detach[j]) {
            rc = set(&s->change, &c->object, "CRTC_ID", 0, e); if (rc) return rc;
        }
    }
    for (size_t i = 0; i < s->baseline->crtc_count; ++i) {
        const struct tam_display_crtc *c = &s->baseline->crtcs[i];
        for (size_t j = 0; j < plan.disable_crtc_count; ++j) if (c->object.id == plan.disable_crtcs[j]) {
            rc = set(&s->change, &c->object, "ACTIVE", 0, e); if (rc) return rc;
            rc = set(&s->change, &c->object, "MODE_ID", 0, e); if (rc) return rc;
        }
    }
    for (size_t i = 0; i < s->baseline->plane_count; ++i) {
        const struct tam_display_plane *p = &s->baseline->planes[i];
        for (size_t j = 0; j < plan.disable_plane_count; ++j) if (p->object.id == plan.disable_planes[j]) {
            rc = set(&s->change, &p->object, "CRTC_ID", 0, e); if (rc) return rc;
            rc = set(&s->change, &p->object, "FB_ID", 0, e); if (rc) return rc;
        }
    }
    rc = tam_display_validate(s, false, e); if (rc) return rc;
    rc = tam_display_atomic(s, &s->change, true, e); if (rc) return rc;
    /* Recheck immediately before the sole write; no periodic reapplication. */
    rc = tam_display_validate(s, false, e); if (rc) return rc;
    s->changed = true; /* Even EINTR is an uncertain write requiring recovery. */
    s->restoring = false;
    return tam_display_atomic(s, &s->change, false, e);
}

enum tam_display_result tam_display_state_restore(struct tam_display_state *s,
    struct tam_display_error *e)
{
    if (e) *e = (struct tam_display_error){0};
    if (!s) return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Missing owned display state");
    if (!s->changed) return TAM_DISPLAY_OK;
    s->restoring = true;
    enum tam_display_result rc = tam_display_validate(s, true, e);
    if (!rc) rc = tam_display_atomic(s, &s->restore, true, e);
    if (!rc) rc = tam_display_validate(s, true, e);
    if (!rc) rc = tam_display_atomic(s, &s->restore, false, e);
    if (rc) {
        if (e) e->code = TAM_DISPLAY_RESTORE_FAILED;
        return TAM_DISPLAY_RESTORE_FAILED;
    }
    s->changed = false;
    /* A restored state uses new owned IDs. Do not reuse its old observation. */
    s->change.count = 0;
    return TAM_DISPLAY_OK;
}
