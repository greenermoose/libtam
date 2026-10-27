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
    if (!test && !s->restoring) s->uncertain_change = *r;
    int result = tam_display_call(s, DRM_IOCTL_MODE_ATOMIC, &a), saved = errno;
    free(p);
    if (result < 0 && !test) s->recovery_required = true;
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

static bool same_object(const struct tam_display_request *change, const struct tam_display_object *old,
    const struct tam_display_object *now, bool recovery, bool disconnected, bool orphan)
{
    if (old->id != now->id || old->property_count != now->property_count) return false;
    for (size_t i = 0; i < old->property_count; ++i) {
        const struct tam_display_property *a = &old->properties[i], *b = &now->properties[i];
        if (a->id != b->id || a->flags != b->flags || strcmp(a->name, b->name)) return false;
        if ((a->flags & DRM_MODE_PROP_IMMUTABLE) || !strcmp(a->name, "EDID")) {
            /* EDID may disappear on disconnect, but a new nonzero identity is
             * never evidence that this is the monitor we captured. */
            if (a->value != b->value && !(disconnected &&
                !strcmp(a->name, "EDID") && !b->value)) return false;
            continue;
        }
        if (!(a->flags & DRM_MODE_PROP_ATOMIC) || transient(a)) continue;
        if (a->value == b->value) continue;
        /* The kernel may detach a lost connector and disable its unused
         * pipeline. Accept only those narrow zero transitions during recovery. */
        if (disconnected && !strcmp(a->name, "CRTC_ID") && !b->value) continue;
        if (orphan && !b->value && (!strcmp(a->name, "ACTIVE") ||
            !strcmp(a->name, "MODE_ID") || !strcmp(a->name, "CRTC_ID") ||
            !strcmp(a->name, "FB_ID"))) continue;
        bool ours = false;
        if (recovery) for (size_t j = 0; j < change->count; ++j) {
            const struct tam_display_write *w = &change->writes[j];
            if (w->object == old->id && w->property == a->id && w->value == b->value) ours = true;
        }
        if (!ours) return false;
    }
    return true;
}

static const struct tam_display_connector *find_connector(
    const struct tam_display_topology *t, uint32_t id)
{
    for (size_t i = 0; i < t->connector_count; ++i)
        if (t->connectors[i].object.id == id) return &t->connectors[i];
    return NULL;
}

/* Baseline routes are the only routes for which we own modes and buffers.
 * Restore every still-connected captured output, preserving shared CRTCs. */
static bool routed(const struct tam_display_topology *old,
    const struct tam_display_topology *now, uint32_t crtc)
{
    for (size_t i = 0; i < old->connector_count; ++i) {
        const struct tam_display_connector *c = find_connector(now, old->connectors[i].object.id);
        if (c && c->connection == 1 && old->connectors[i].crtc_id == crtc) return true;
    }
    return false;
}

/* A shared CRTC can lose its sole selected route even though another,
 * deliberately detached connector on that CRTC is still plugged in. Permit
 * kernel disablement there, then restore the surviving baseline route. */
static bool lost_pipeline(const struct tam_display_request *change,
    const struct tam_display_topology *old, const struct tam_display_topology *now,
    uint32_t crtc)
{
    bool lost = false;
    for (size_t i = 0; i < old->connector_count; ++i) {
        const struct tam_display_connector *a = &old->connectors[i];
        uint64_t route = a->crtc_id;
        for (size_t p = 0; p < a->object.property_count; ++p) {
            if (strcmp(a->object.properties[p].name, "CRTC_ID")) continue;
            for (size_t j = 0; j < change->count; ++j)
                if (change->writes[j].object == a->object.id &&
                    change->writes[j].property == a->object.properties[p].id)
                    route = change->writes[j].value;
        }
        if (route != crtc || a->connection != 1) continue;
        const struct tam_display_connector *b = find_connector(now, a->object.id);
        if (b && b->connection == 1) return false;
        lost = true;
    }
    return lost;
}

static bool same_topology(const struct tam_display_topology *old, const struct tam_display_topology *now,
    bool recovery, const struct tam_display_request *change)
{
    if (strcmp(now->driver, old->driver) || now->connector_count > old->connector_count ||
        (!recovery && now->connector_count != old->connector_count) ||
        now->crtc_count != old->crtc_count || now->plane_count != old->plane_count) return false;
    /* Enumeration order is not identity. Allow removal of captured connectors,
     * but reject every new or duplicate object, including same-count swaps. */
    for (size_t i = 0; i < now->connector_count; ++i) {
        if (!find_connector(old, now->connectors[i].object.id)) return false;
        for (size_t j = 0; j < i; ++j)
            if (now->connectors[j].object.id == now->connectors[i].object.id) return false;
    }
    bool unplug = false;
    for (size_t i = 0; i < old->connector_count; ++i) {
        const struct tam_display_connector *c = find_connector(now, old->connectors[i].object.id);
        if (!c) { if (!recovery) return false; unplug = true; continue; }
        uint32_t a = old->connectors[i].connection, b = c->connection;
        if (a != b) {
            if (!recovery || a != 1 || b != 2) return false;
            unplug = true;
        }
    }
    for (size_t i = 0; i < old->connector_count; ++i) {
        const struct tam_display_connector *c = find_connector(now, old->connectors[i].object.id);
        if (!c) continue;
        bool disconnected = recovery && old->connectors[i].connection == 1 && c->connection == 2;
        if (strcmp(old->connectors[i].name, c->name) ||
            !same_object(change, &old->connectors[i].object, &c->object,
                recovery, disconnected, false)) return false;
    }
    for (size_t i = 0; i < old->crtc_count; ++i)
        if (!same_object(change, &old->crtcs[i].object, &now->crtcs[i].object, recovery, false,
            unplug && lost_pipeline(change, old, now, old->crtcs[i].object.id))) return false;
    for (size_t i = 0; i < old->plane_count; ++i)
        if (!same_object(change, &old->planes[i].object, &now->planes[i].object, recovery, false,
            unplug && old->planes[i].crtc_id && lost_pipeline(change, old, now, old->planes[i].crtc_id))) return false;
    return true;
}

static enum tam_display_result observe(struct tam_display_state *s, bool recovery,
    struct tam_display_topology **out, struct tam_display_error *e)
{
    /* Same retained open description: never reopen/replay into a replacement.
     * Only disconnect/removal loss is allowed; additions, unknown state,
     * changed identities, objects or foreign routing still fail closed. */
    enum tam_display_result rc = tam_display_inspect_with(s->fd, out, e, s->call, s->context);
    if (rc) return rc;
    if (same_topology(s->baseline, *out, recovery, &s->change)) return TAM_DISPLAY_OK;
    if (recovery && s->recovery_required &&
        same_topology(s->baseline, *out, true, &s->uncertain_change)) {
        /* An interrupted commit may have installed the complete attempted
         * request. Bind recovery to that whole observed state, never a mix. */
        s->change = s->uncertain_change;
        return TAM_DISPLAY_OK;
    }
    free(*out); *out = NULL;
    return tam_display_fail(e, TAM_DISPLAY_STALE, 0,
        "Display identity, routing or properties changed outside this transaction");
}

enum tam_display_result tam_display_validate(struct tam_display_state *s, bool recovery,
    struct tam_display_error *e)
{
    struct tam_display_topology *now = NULL;
    enum tam_display_result rc = observe(s, recovery, &now, e);
    free(now);
    return rc;
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

static enum tam_display_result apply_plan_from_restore(struct tam_display_state *s,
    const struct tam_display_plan *plan, uint32_t must_connect_id,
    struct tam_display_error *e);

enum tam_display_result tam_display_state_keep(struct tam_display_state *s,
    const char *connector, struct tam_display_error *e)
{
    if (e) *e = (struct tam_display_error){0};
    if (!s || !s->baseline) return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "A valid owned display state is required");
    if (s->recovery_required) return tam_display_fail(e, TAM_DISPLAY_RESTORE_FAILED, 0, "Restore is required before further mutations");
    struct tam_display_plan plan;
    enum tam_display_result rc = tam_display_plan_keep(s->baseline, connector, &plan, e);
    if (rc) return rc;
    if (!plan.detach_count) return tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, 0, "No other active output to blank");
    if (s->changed) return apply_plan_from_restore(s, &plan, plan.keep_connector, e);
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
    s->detached_count = plan.detach_count;
    memcpy(s->detached, plan.detach, plan.detach_count * sizeof(uint32_t));
    return tam_display_atomic(s, &s->change, false, e);
}

enum tam_display_result tam_display_state_verify_console_fb(
    struct tam_display_state *s, struct tam_console_fb *fb,
    struct tam_display_error *e)
{
    if (e) *e = (struct tam_display_error){0};
    if (!s || !s->baseline || !fb)
        return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Invalid state or console framebuffer mapping");
    enum tam_display_result rc = tam_display_verify_console_fb(s->baseline, fb, e);
    if (rc) return rc;
    const struct tam_display_ref *matched = NULL;
    for (size_t i = 0; i < s->fb_count; ++i)
        if (s->fbs[i].original == fb->drm_fb_id) { matched = &s->fbs[i]; break; }
    if (!matched || !matched->width || !matched->height || !matched->pitch)
        return tam_display_fail(e, TAM_DISPLAY_STALE, 0, "Console framebuffer reference was not captured");
    if (matched->width != fb->width || matched->height != fb->height || matched->pitch != fb->stride)
        return tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, 0,
            "Captured DRM framebuffer geometry does not match the bound fbcon device");
    s->console_fb_id = fb->drm_fb_id;
    return TAM_DISPLAY_OK;
}

bool tam_display_state_is_detached(const struct tam_display_state *s, const char *connector)
{
    if (!s || !s->baseline || !s->changed || !connector || !*connector) return false;
    for (size_t i = 0; i < s->baseline->connector_count; ++i) {
        const struct tam_display_connector *c = &s->baseline->connectors[i];
        if (!tam_display_connector_matches(c->name, connector)) continue;
        for (size_t j = 0; j < s->detached_count; ++j)
            if (s->detached[j] == c->object.id) return true;
    }
    return false;
}

/* Patch a captured assignment without introducing a second assignment to the
 * same object/property. Preserve request grouping and all other owned values. */
static void replace(struct tam_display_request *r, const struct tam_display_object *o,
    const char *name, uint64_t value)
{
    for (size_t i = 0; i < o->property_count; ++i) {
        if (strcmp(o->properties[i].name, name)) continue;
        for (size_t j = 0; j < r->count; ++j)
            if (r->writes[j].object == o->id && r->writes[j].property == o->properties[i].id)
                r->writes[j].value = value;
    }
}

static enum tam_display_result recovery_request(struct tam_display_state *s,
    const struct tam_display_topology *now, struct tam_display_request *r,
    struct tam_display_error *e)
{
    const struct tam_display_topology *old = s->baseline;
    bool usable = false;
    for (size_t i = 0; i < old->connector_count; ++i) {
        const struct tam_display_connector *c = find_connector(now, old->connectors[i].object.id);
        if (!c || c->connection != 1) continue;
        struct tam_display_plan plan;
        struct tam_display_error ignored;
        if (!tam_display_plan_keep(old, old->connectors[i].name, &plan, &ignored)) usable = true;
    }
    if (!usable) return tam_display_fail(e, TAM_DISPLAY_NOT_FOUND, 0,
        "No captured connected console output remains; releasing display control");
    *r = s->restore;
    for (size_t i = 0; i < old->connector_count; ++i) {
        const struct tam_display_connector *c = find_connector(now, old->connectors[i].object.id);
        if (!c) {
            size_t kept = 0;
            for (size_t j = 0; j < r->count; ++j)
                if (r->writes[j].object != old->connectors[i].object.id)
                    r->writes[kept++] = r->writes[j];
            r->count = kept; /* Never submit assignments for a vanished object. */
        } else if (c->connection != 1) replace(r, &old->connectors[i].object, "CRTC_ID", 0);
    }
    for (size_t i = 0; i < old->crtc_count; ++i)
        if (!routed(old, now, old->crtcs[i].object.id)) {
            replace(r, &old->crtcs[i].object, "ACTIVE", 0);
            replace(r, &old->crtcs[i].object, "MODE_ID", 0);
        }
    for (size_t i = 0; i < old->plane_count; ++i)
        if (old->planes[i].crtc_id && !routed(old, now, old->planes[i].crtc_id)) {
            replace(r, &old->planes[i].object, "CRTC_ID", 0);
            replace(r, &old->planes[i].object, "FB_ID", 0);
        }
    return TAM_DISPLAY_OK;
}

enum tam_display_result tam_display_state_restore(struct tam_display_state *s,
    struct tam_display_error *e)
{
    if (e) *e = (struct tam_display_error){0};
    if (!s) return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Missing owned display state");
    if (!s->changed) return TAM_DISPLAY_OK;
    s->restoring = true;
    struct tam_display_topology *tested = NULL, *now = NULL;
    struct tam_display_request *request = malloc(sizeof(*request));
    enum tam_display_result rc = request ? observe(s, true, &tested, e) :
        tam_display_fail(e, TAM_DISPLAY_IO, ENOMEM, "Cannot allocate recovery request");
    if (!rc) rc = recovery_request(s, tested, request, e);
    if (!rc) rc = tam_display_atomic(s, request, true, e);
    /* Bind the actual write to precisely the observation used by TEST_ONLY.
     * A second unplug/replug, even another otherwise permissible loss, cancels
     * this request. No retry can postpone release indefinitely. */
    if (!rc) rc = tam_display_inspect_with(s->fd, &now, e, s->call, s->context);
    if (!rc && !same_topology(tested, now, false, &s->change))
        rc = tam_display_fail(e, TAM_DISPLAY_STALE, 0, "Display changed during recovery validation");
    if (!rc) rc = tam_display_atomic(s, request, false, e);
    free(now); free(tested); free(request);
    if (rc) {
        if (e) e->code = TAM_DISPLAY_RESTORE_FAILED;
        return TAM_DISPLAY_RESTORE_FAILED;
    }
    s->changed = false;
    s->recovery_required = false;
    s->detached_count = 0;
    /* A restored state uses new owned IDs. Do not reuse its old observation. */
    s->change.count = 0;
    return TAM_DISPLAY_OK;
}

static enum tam_display_result apply_plan_from_restore(struct tam_display_state *s,
    const struct tam_display_plan *plan, uint32_t must_connect_id,
    struct tam_display_error *e)
{
    struct tam_display_topology *tested = NULL, *now = NULL;
    struct tam_display_request *request = malloc(sizeof(*request));
    enum tam_display_result rc = request ? observe(s, true, &tested, e) :
        tam_display_fail(e, TAM_DISPLAY_IO, ENOMEM, "Cannot allocate display request");
    if (!rc && must_connect_id) {
        const struct tam_display_connector *c = find_connector(tested, must_connect_id);
        if (!c || c->connection != 1)
            rc = tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, 0, "Selected connector is disconnected");
    }
    if (!rc) rc = recovery_request(s, tested, request, e);
    if (!rc) {
        const struct tam_display_topology *old = s->baseline;
        for (size_t i = 0; i < old->connector_count; ++i)
            for (size_t j = 0; j < plan->detach_count; ++j)
                if (old->connectors[i].object.id == plan->detach[j])
                    replace(request, &old->connectors[i].object, "CRTC_ID", 0);
        for (size_t i = 0; i < old->crtc_count; ++i)
            for (size_t j = 0; j < plan->disable_crtc_count; ++j)
                if (old->crtcs[i].object.id == plan->disable_crtcs[j]) {
                    replace(request, &old->crtcs[i].object, "ACTIVE", 0);
                    replace(request, &old->crtcs[i].object, "MODE_ID", 0);
                }
        for (size_t i = 0; i < old->plane_count; ++i)
            for (size_t j = 0; j < plan->disable_plane_count; ++j)
                if (old->planes[i].object.id == plan->disable_planes[j]) {
                    replace(request, &old->planes[i].object, "CRTC_ID", 0);
                    replace(request, &old->planes[i].object, "FB_ID", 0);
                }
        rc = tam_display_atomic(s, request, true, e);
    }
    if (!rc) rc = tam_display_inspect_with(s->fd, &now, e, s->call, s->context);
    if (!rc && !same_topology(tested, now, false, &s->change))
        rc = tam_display_fail(e, TAM_DISPLAY_STALE, 0, "Display changed during transaction validation");
    if (!rc) {
        s->changed = true;
        s->restoring = false;
        rc = tam_display_atomic(s, request, false, e);
        if (!rc) {
            s->change = *request;
            s->detached_count = plan->detach_count;
            memcpy(s->detached, plan->detach, plan->detach_count * sizeof(uint32_t));
        }
    }
    free(now); free(tested); free(request);
    return rc;
}

enum tam_display_result tam_display_state_blank(struct tam_display_state *s,
    const char *const *connectors, size_t connector_count,
    struct tam_display_error *e)
{
    if (e) *e = (struct tam_display_error){0};
    if (!s || !s->baseline || !connectors || !connector_count || connector_count > TAM_DISPLAY_MAX_OBJECTS)
        return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Invalid state or blank connector selection");
    if (s->recovery_required) return tam_display_fail(e, TAM_DISPLAY_RESTORE_FAILED, 0, "Restore is required before further mutations");
    struct tam_display_plan plan;
    enum tam_display_result rc;
    if (!s->changed) {
        rc = tam_display_plan_blank(s->baseline, connectors, connector_count, &plan, e);
        if (rc) return rc;
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
        rc = tam_display_validate(s, false, e); if (rc) return rc;
        s->changed = true;
        s->restoring = false;
        s->detached_count = plan.detach_count;
        memcpy(s->detached, plan.detach, plan.detach_count * sizeof(uint32_t));
        return tam_display_atomic(s, &s->change, false, e);
    }
    struct tam_display_plan requested;
    rc = tam_display_plan_blank(s->baseline, connectors, connector_count, &requested, e);
    if (rc) return rc;
    const char *combined[TAM_DISPLAY_MAX_OBJECTS];
    size_t combined_count = 0;
    for (size_t i = 0; i < s->baseline->connector_count; ++i) {
        uint32_t id = s->baseline->connectors[i].object.id;
        bool want = false;
        for (size_t j = 0; j < s->detached_count; ++j)
            if (s->detached[j] == id) want = true;
        for (size_t j = 0; j < requested.detach_count; ++j)
            if (requested.detach[j] == id) want = true;
        if (want) combined[combined_count++] = s->baseline->connectors[i].name;
    }
    rc = tam_display_plan_blank(s->baseline, combined, combined_count, &plan, e);
    if (rc) return rc;
    return apply_plan_from_restore(s, &plan, 0, e);
}

enum tam_display_result tam_display_state_wake(struct tam_display_state *s,
    const char *connector, struct tam_display_error *e)
{
    if (e) *e = (struct tam_display_error){0};
    if (!s || !s->baseline || !connector || !*connector)
        return tam_display_fail(e, TAM_DISPLAY_INVALID, 0, "Invalid state or connector to wake");
    if (s->recovery_required) return tam_display_fail(e, TAM_DISPLAY_RESTORE_FAILED, 0, "Restore is required before further mutations");
    const struct tam_display_connector *target = NULL;
    for (size_t i = 0; i < s->baseline->connector_count; ++i) {
        const struct tam_display_connector *c = &s->baseline->connectors[i];
        if (tam_display_connector_matches(c->name, connector)) {
            if (target) return tam_display_fail(e, TAM_DISPLAY_AMBIGUOUS, 0, "Connector name is ambiguous");
            target = c;
        }
    }
    if (!target) return tam_display_fail(e, TAM_DISPLAY_NOT_FOUND, 0, "Selected connector not found");
    if (target->connection != 1 || !target->crtc_id)
        return tam_display_fail(e, TAM_DISPLAY_UNSUPPORTED, 0, "Selected connector has no active console route");
    if (!s->changed) return TAM_DISPLAY_OK;
    bool was_detached = false;
    const char *remaining[TAM_DISPLAY_MAX_OBJECTS];
    size_t remaining_count = 0;
    for (size_t i = 0; i < s->baseline->connector_count; ++i) {
        const struct tam_display_connector *c = &s->baseline->connectors[i];
        bool in_detached = false;
        for (size_t j = 0; j < s->detached_count; ++j)
            if (s->detached[j] == c->object.id) in_detached = true;
        if (!in_detached) continue;
        if (c->object.id == target->object.id) was_detached = true;
        else remaining[remaining_count++] = c->name;
    }
    if (!was_detached) return TAM_DISPLAY_OK;
    if (!remaining_count) return tam_display_state_restore(s, e);
    struct tam_display_plan plan;
    enum tam_display_result rc = tam_display_plan_blank(s->baseline, remaining, remaining_count, &plan, e);
    if (rc) return rc;
    return apply_plan_from_restore(s, &plan, target->object.id, e);
}

bool tam_display_state_recovery_required(const struct tam_display_state *s)
{
    return s && s->recovery_required;
}
