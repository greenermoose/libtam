#include "tam_display_internal.h"
#include <string.h>

static const struct tam_display_crtc *find_crtc(const struct tam_display_topology *t, uint32_t id)
{
    for (size_t i = 0; i < t->crtc_count; ++i)
        if (t->crtcs[i].object.id == id) return &t->crtcs[i];
    return NULL;
}

bool tam_display_connector_matches(const char *connector_name, const char *query)
{
    if (!connector_name || !query || !*query) return false;
    if (!strcmp(connector_name, query)) return true;
    if (!strncmp(query, "card", 4) && query[4] >= '0' && query[4] <= '9') {
        const char *p = query + 4;
        while (*p >= '0' && *p <= '9') ++p;
        if (*p == '-' && !strcmp(connector_name, p + 1)) return true;
    }
    return false;
}

static enum tam_display_result validate_topology(const struct tam_display_topology *t,
    struct tam_display_error *error)
{
    if (!t || t->connector_count > TAM_DISPLAY_MAX_OBJECTS ||
        t->crtc_count > TAM_DISPLAY_MAX_OBJECTS || t->plane_count > TAM_DISPLAY_MAX_OBJECTS)
        return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid routing observation or selection");
    for (size_t i = 0; i < t->connector_count; ++i) {
        const struct tam_display_connector *c = &t->connectors[i];
        if (!memchr(c->name, 0, sizeof(c->name)))
            return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid connector name");
        if (!c->object.id || (c->crtc_id && !find_crtc(t, c->crtc_id)))
            return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Connector routing is incomplete");
        for (size_t j = 0; j < i; ++j)
            if (t->connectors[j].object.id == c->object.id)
                return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Duplicate connector ID");
    }
    for (size_t i = 0; i < t->crtc_count; ++i) {
        uint32_t id = t->crtcs[i].object.id;
        if (!id) return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Invalid CRTC ID");
        for (size_t j = 0; j < i; ++j)
            if (t->crtcs[j].object.id == id)
                return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Duplicate CRTC ID");
    }
    for (size_t i = 0; i < t->plane_count; ++i) {
        const struct tam_display_plane *plane = &t->planes[i];
        if (!plane->object.id || (plane->crtc_id && !find_crtc(t, plane->crtc_id)))
            return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Plane routing is incomplete");
        for (size_t j = 0; j < i; ++j)
            if (t->planes[j].object.id == plane->object.id)
                return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Duplicate plane ID");
    }
    return TAM_DISPLAY_OK;
}

static bool active_output(const struct tam_display_topology *t, const struct tam_display_connector *c)
{
    if (!c || c->connection != 1 || !c->crtc_id) return false;
    const struct tam_display_crtc *crtc = find_crtc(t, c->crtc_id);
    if (!crtc || !crtc->mode_valid || !crtc->framebuffer_id) return false;
    if (!t->plane_count) return true;
    for (size_t i = 0; i < t->plane_count; ++i)
        if (t->planes[i].crtc_id == crtc->object.id && t->planes[i].framebuffer_id != 0)
            return true;
    return false;
}

enum tam_display_result tam_display_plan_keep(const struct tam_display_topology *t,
    const char *connector, struct tam_display_plan *out, struct tam_display_error *error)
{
    if (out) memset(out, 0, sizeof(*out));
    if (error) memset(error, 0, sizeof(*error));
    if (!t || !connector || !*connector || !out)
        return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid routing observation or selection");
    enum tam_display_result rc = validate_topology(t, error);
    if (rc) return rc;
    const struct tam_display_connector *keep = NULL;
    for (size_t i = 0; i < t->connector_count; ++i) {
        const struct tam_display_connector *c = &t->connectors[i];
        if (tam_display_connector_matches(c->name, connector)) {
            if (keep) return tam_display_fail(error, TAM_DISPLAY_AMBIGUOUS, 0, "Connector name is ambiguous");
            keep = c;
        }
    }
    if (!keep) return tam_display_fail(error, TAM_DISPLAY_NOT_FOUND, 0, "Selected connector not found");
    if (!active_output(t, keep))
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Selected connector has no active framebuffer");
    struct tam_display_plan p = {.keep_connector = keep->object.id};
    for (size_t i = 0; i < t->connector_count; ++i) {
        const struct tam_display_connector *c = &t->connectors[i];
        if (c != keep && c->crtc_id) p.detach[p.detach_count++] = c->object.id;
    }
    for (size_t i = 0; i < t->crtc_count; ++i) {
        uint32_t id = t->crtcs[i].object.id;
        if (id != keep->crtc_id && t->crtcs[i].mode_valid)
            p.disable_crtcs[p.disable_crtc_count++] = id;
    }
    for (size_t i = 0; i < t->plane_count; ++i) {
        const struct tam_display_plane *plane = &t->planes[i];
        if (plane->crtc_id && plane->crtc_id != keep->crtc_id)
            p.disable_planes[p.disable_plane_count++] = plane->object.id;
    }
    *out = p;
    return TAM_DISPLAY_OK;
}

enum tam_display_result tam_display_plan_blank(
    const struct tam_display_topology *t,
    const char *const *connectors, size_t connector_count,
    struct tam_display_plan *out, struct tam_display_error *error)
{
    if (out) memset(out, 0, sizeof(*out));
    if (error) memset(error, 0, sizeof(*error));
    if (!t || !connectors || !connector_count || connector_count > TAM_DISPLAY_MAX_OBJECTS || !out)
        return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid blank routing observation or selection");
    enum tam_display_result rc = validate_topology(t, error);
    if (rc) return rc;
    struct tam_display_plan p = {0};
    for (size_t k = 0; k < connector_count; ++k) {
        if (!connectors[k] || !*connectors[k])
            return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid connector name");
        const struct tam_display_connector *target = NULL;
        for (size_t i = 0; i < t->connector_count; ++i) {
            const struct tam_display_connector *c = &t->connectors[i];
            if (tam_display_connector_matches(c->name, connectors[k])) {
                if (target) return tam_display_fail(error, TAM_DISPLAY_AMBIGUOUS, 0, "Connector name is ambiguous");
                target = c;
            }
        }
        if (!target) return tam_display_fail(error, TAM_DISPLAY_NOT_FOUND, 0, "Selected connector not found");
        if (!active_output(t, target))
            return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Selected connector has no active framebuffer");
        for (size_t j = 0; j < p.detach_count; ++j)
            if (p.detach[j] == target->object.id)
                return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Duplicate connector in blank selection");
        p.detach[p.detach_count++] = target->object.id;
    }
    for (size_t i = 0; i < t->connector_count; ++i) {
        const struct tam_display_connector *c = &t->connectors[i];
        bool detached = false;
        for (size_t j = 0; j < p.detach_count; ++j)
            if (p.detach[j] == c->object.id) detached = true;
        if (!detached && active_output(t, c) && !p.keep_connector)
            p.keep_connector = c->object.id;
    }
    if (!p.keep_connector)
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0,
            "At least one connected console output must remain active");
    for (size_t i = 0; i < t->crtc_count; ++i) {
        uint32_t crtc_id = t->crtcs[i].object.id;
        if (!t->crtcs[i].mode_valid) continue;
        bool retained = false;
        for (size_t c = 0; c < t->connector_count; ++c) {
            if (t->connectors[c].crtc_id != crtc_id || !active_output(t, &t->connectors[c])) continue;
            bool detached = false;
            for (size_t j = 0; j < p.detach_count; ++j)
                if (p.detach[j] == t->connectors[c].object.id) detached = true;
            if (!detached) { retained = true; break; }
        }
        if (!retained) p.disable_crtcs[p.disable_crtc_count++] = crtc_id;
    }
    for (size_t i = 0; i < t->plane_count; ++i) {
        uint32_t crtc_id = t->planes[i].crtc_id;
        if (!crtc_id) continue;
        for (size_t j = 0; j < p.disable_crtc_count; ++j)
            if (p.disable_crtcs[j] == crtc_id) {
                p.disable_planes[p.disable_plane_count++] = t->planes[i].object.id;
                break;
            }
    }
    *out = p;
    return TAM_DISPLAY_OK;
}
