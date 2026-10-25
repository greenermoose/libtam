#include "tam_display_internal.h"
#include <string.h>

static const struct tam_display_crtc *find_crtc(const struct tam_display_topology *t, uint32_t id)
{
    for (size_t i = 0; i < t->crtc_count; ++i)
        if (t->crtcs[i].object.id == id) return &t->crtcs[i];
    return NULL;
}

enum tam_display_result tam_display_plan_keep(const struct tam_display_topology *t,
    const char *connector, struct tam_display_plan *out, struct tam_display_error *error)
{
    if (out) memset(out, 0, sizeof(*out));
    if (error) memset(error, 0, sizeof(*error));
    if (!t || !connector || !*connector || !out ||
        t->connector_count > TAM_DISPLAY_MAX_OBJECTS ||
        t->crtc_count > TAM_DISPLAY_MAX_OBJECTS || t->plane_count > TAM_DISPLAY_MAX_OBJECTS)
        return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid routing observation or selection");
    const struct tam_display_connector *keep = NULL;
    for (size_t i = 0; i < t->connector_count; ++i) {
        const struct tam_display_connector *c = &t->connectors[i];
        if (!memchr(c->name, 0, sizeof(c->name)))
            return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid connector name");
        if (!strcmp(c->name, connector)) {
            if (keep) return tam_display_fail(error, TAM_DISPLAY_AMBIGUOUS, 0, "Connector name is ambiguous");
            keep = c;
        }
        if (!c->object.id || (c->crtc_id && !find_crtc(t, c->crtc_id)))
            return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Connector routing is incomplete");
        for (size_t j = 0; j < i; ++j)
            if (t->connectors[j].object.id == c->object.id)
                return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Duplicate connector ID");
    }
    if (!keep) return tam_display_fail(error, TAM_DISPLAY_NOT_FOUND, 0, "Selected connector not found");
    const struct tam_display_crtc *crtc = find_crtc(t, keep->crtc_id);
    if (keep->connection != 1 || !keep->crtc_id || !crtc || !crtc->mode_valid || !crtc->framebuffer_id)
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Selected connector has no active framebuffer");
    struct tam_display_plan p = {.keep_connector = keep->object.id};
    for (size_t i = 0; i < t->connector_count; ++i) {
        const struct tam_display_connector *c = &t->connectors[i];
        if (c != keep && c->crtc_id) p.detach[p.detach_count++] = c->object.id;
    }
    for (size_t i = 0; i < t->crtc_count; ++i) {
        uint32_t id = t->crtcs[i].object.id;
        if (!id) return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Invalid CRTC ID");
        for (size_t j = 0; j < i; ++j)
            if (t->crtcs[j].object.id == id)
                return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Duplicate CRTC ID");
        if (id != keep->crtc_id && t->crtcs[i].mode_valid)
            p.disable_crtcs[p.disable_crtc_count++] = id;
    }
    for (size_t i = 0; i < t->plane_count; ++i) {
        const struct tam_display_plane *plane = &t->planes[i];
        if (!plane->object.id || (plane->crtc_id && !find_crtc(t, plane->crtc_id)))
            return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Plane routing is incomplete");
        for (size_t j = 0; j < i; ++j)
            if (t->planes[j].object.id == plane->object.id)
                return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Duplicate plane ID");
        if (plane->crtc_id && plane->crtc_id != keep->crtc_id)
            p.disable_planes[p.disable_plane_count++] = plane->object.id;
    }
    *out = p;
    return TAM_DISPLAY_OK;
}
