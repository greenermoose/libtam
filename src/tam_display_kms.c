#include "tam_display_internal.h"
#include <drm.h>
#include <drm_mode.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#define ATTEMPTS 4
struct reader {
    int fd;
    tam_display_ioctl_fn call;
    void *context;
    struct tam_display_error *error;
};

static enum tam_display_result query(struct reader *r, unsigned long request, void *arg)
{
    /* Bound EINTR too; never spin forever in a hardware enquiry. */
    for (int i = 0; i < ATTEMPTS; ++i) {
        if (r->call(r->context, r->fd, request, arg) == 0) return TAM_DISPLAY_OK;
        if (errno != EINTR) break;
    }
    int saved = errno;
    enum tam_display_result code = saved == ENOENT || saved == ENODEV ?
        TAM_DISPLAY_STALE : TAM_DISPLAY_IO;
    return tam_display_fail(r->error, code, saved, "DRM query failed; device access or topology changed");
}

static void sanitize(char *s, size_t capacity)
{
    s[capacity - 1] = 0;
    for (size_t i = 0; s[i]; ++i)
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) s[i] = '?';
}

static enum tam_display_result properties(struct reader *r, uint32_t type,
    struct tam_display_object *object)
{
    uint32_t ids[TAM_DISPLAY_MAX_PROPERTIES];
    uint64_t values[TAM_DISPLAY_MAX_PROPERTIES];
    for (int attempt = 0; attempt < ATTEMPTS; ++attempt) {
        struct drm_mode_obj_get_properties p = {.obj_id = object->id, .obj_type = type};
        enum tam_display_result rc = query(r, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &p);
        if (rc) return rc;
        if (p.count_props > TAM_DISPLAY_MAX_PROPERTIES)
            return tam_display_fail(r->error, TAM_DISPLAY_LIMIT, 0, "Too many DRM properties");
        uint32_t count = p.count_props;
        p.props_ptr = (uintptr_t)ids;
        p.prop_values_ptr = (uintptr_t)values;
        rc = query(r, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &p);
        if (rc) return rc;
        if (p.count_props != count) continue;
        object->property_count = count;
        for (size_t i = 0; i < count; ++i) {
            struct drm_mode_get_property metadata = {.prop_id = ids[i]};
            rc = query(r, DRM_IOCTL_MODE_GETPROPERTY, &metadata);
            if (rc) return rc;
            struct tam_display_property *dest = &object->properties[i];
            dest->id = ids[i];
            dest->value = values[i];
            dest->flags = metadata.flags;
            memcpy(dest->name, metadata.name, sizeof(dest->name));
            sanitize(dest->name, sizeof(dest->name));
        }
        return TAM_DISPLAY_OK;
    }
    return tam_display_fail(r->error, TAM_DISPLAY_STALE, 0, "DRM properties did not stabilize");
}

static enum tam_display_result resources(struct reader *r, struct drm_mode_card_res *res,
    uint32_t *connectors, uint32_t *crtcs)
{
    for (int attempt = 0; attempt < ATTEMPTS; ++attempt) {
        memset(res, 0, sizeof(*res));
        enum tam_display_result rc = query(r, DRM_IOCTL_MODE_GETRESOURCES, res);
        if (rc) return rc;
        if (res->count_connectors > TAM_DISPLAY_MAX_OBJECTS ||
            res->count_crtcs > TAM_DISPLAY_MAX_OBJECTS)
            return tam_display_fail(r->error, TAM_DISPLAY_LIMIT, 0, "Too many DRM connectors or CRTCs");
        uint32_t nc = res->count_connectors, nr = res->count_crtcs;
        res->connector_id_ptr = (uintptr_t)connectors;
        res->crtc_id_ptr = (uintptr_t)crtcs;
        /* Unused encoder/FB arrays are deliberately not requested. */
        res->count_fbs = 0;
        res->count_encoders = 0;
        rc = query(r, DRM_IOCTL_MODE_GETRESOURCES, res);
        if (rc) return rc;
        if (res->count_connectors == nc && res->count_crtcs == nr) return TAM_DISPLAY_OK;
    }
    return tam_display_fail(r->error, TAM_DISPLAY_STALE, 0, "DRM resources did not stabilize");
}

static enum tam_display_result planes(struct reader *r, struct tam_display_topology *t)
{
    uint32_t ids[TAM_DISPLAY_MAX_OBJECTS];
    for (int attempt = 0; attempt < ATTEMPTS; ++attempt) {
        struct drm_mode_get_plane_res res = {0};
        enum tam_display_result rc = query(r, DRM_IOCTL_MODE_GETPLANERESOURCES, &res);
        if (rc) return rc;
        if (res.count_planes > TAM_DISPLAY_MAX_OBJECTS)
            return tam_display_fail(r->error, TAM_DISPLAY_LIMIT, 0, "Too many DRM planes");
        uint32_t count = res.count_planes;
        res.plane_id_ptr = (uintptr_t)ids;
        rc = query(r, DRM_IOCTL_MODE_GETPLANERESOURCES, &res);
        if (rc) return rc;
        if (res.count_planes != count) continue;
        t->plane_count = count;
        for (size_t i = 0; i < count; ++i) {
            struct drm_mode_get_plane p = {.plane_id = ids[i]};
            rc = query(r, DRM_IOCTL_MODE_GETPLANE, &p);
            if (rc) return rc;
            struct tam_display_plane *dest = &t->planes[i];
            dest->object.id = ids[i];
            dest->crtc_id = p.crtc_id;
            dest->framebuffer_id = p.fb_id;
            dest->possible_crtcs = p.possible_crtcs;
            rc = properties(r, DRM_MODE_OBJECT_PLANE, &dest->object);
            if (rc) return rc;
        }
        return TAM_DISPLAY_OK;
    }
    return tam_display_fail(r->error, TAM_DISPLAY_STALE, 0, "DRM planes did not stabilize");
}

enum tam_display_result tam_display_inspect_with(int fd,
    struct tam_display_topology **out, struct tam_display_error *error,
    tam_display_ioctl_fn call, void *context)
{
    struct tam_display_error local_error;
    if (!error) error = &local_error;
    if (out) *out = NULL;
    memset(error, 0, sizeof(*error));
    if (!out || fd < 0 || !call)
        return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid DRM inspection arguments");
    struct reader r = {fd, call, context, error};
    struct tam_display_topology *t = calloc(1, sizeof(*t));
    if (!t) return tam_display_fail(error, TAM_DISPLAY_IO, ENOMEM, "Cannot allocate DRM observation");
    enum tam_display_result rc;
    struct drm_set_client_cap cap = {.capability = DRM_CLIENT_CAP_ATOMIC, .value = 1};
    rc = query(&r, DRM_IOCTL_SET_CLIENT_CAP, &cap);
    if (rc) {
        if (error->system_errno == EINVAL)
            rc = tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, EINVAL, "Atomic DRM client support unavailable");
        goto fail;
    }
    cap.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES;
    rc = query(&r, DRM_IOCTL_SET_CLIENT_CAP, &cap);
    if (rc) goto fail;
    struct drm_version version = {.name = t->driver, .name_len = sizeof(t->driver) - 1};
    rc = query(&r, DRM_IOCTL_VERSION, &version);
    if (rc) goto fail;
    sanitize(t->driver, sizeof(t->driver));
    uint32_t connector_ids[TAM_DISPLAY_MAX_OBJECTS], crtc_ids[TAM_DISPLAY_MAX_OBJECTS];
    struct drm_mode_card_res res;
    rc = resources(&r, &res, connector_ids, crtc_ids);
    if (rc) goto fail;
    t->connector_count = res.count_connectors;
    t->crtc_count = res.count_crtcs;
    for (size_t i = 0; i < t->crtc_count; ++i) {
        struct drm_mode_crtc crtc = {.crtc_id = crtc_ids[i]};
        rc = query(&r, DRM_IOCTL_MODE_GETCRTC, &crtc);
        if (rc) goto fail;
        struct tam_display_crtc *dest = &t->crtcs[i];
        dest->object.id = crtc_ids[i];
        dest->framebuffer_id = crtc.fb_id;
        dest->mode_valid = crtc.mode_valid != 0;
        dest->x = crtc.x; dest->y = crtc.y;
        dest->width = crtc.mode.hdisplay; dest->height = crtc.mode.vdisplay;
        rc = properties(&r, DRM_MODE_OBJECT_CRTC, &dest->object);
        if (rc) goto fail;
    }
    for (size_t i = 0; i < t->connector_count; ++i) {
        /* count_modes=1 avoids GETCONNECTOR's implicit forced mode probe. */
        struct drm_mode_modeinfo mode;
        struct drm_mode_get_connector conn = {.connector_id = connector_ids[i],
            .count_modes = 1, .modes_ptr = (uintptr_t)&mode};
        rc = query(&r, DRM_IOCTL_MODE_GETCONNECTOR, &conn);
        if (rc) goto fail;
        struct tam_display_connector *dest = &t->connectors[i];
        dest->object.id = connector_ids[i];
        dest->connection = conn.connection;
        dest->encoder_id = conn.encoder_id;
        tam_display_connector_name(conn.connector_type, conn.connector_type_id,
            dest->name, sizeof(dest->name));
        rc = properties(&r, DRM_MODE_OBJECT_CONNECTOR, &dest->object);
        if (rc) goto fail;
        bool routed = false;
        for (size_t j = 0; j < dest->object.property_count; ++j)
            if (!strcmp(dest->object.properties[j].name, "CRTC_ID")) {
                uint64_t id = dest->object.properties[j].value;
                if (id > UINT32_MAX) {
                    rc = tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Invalid connector CRTC property");
                    goto fail;
                }
                dest->crtc_id = (uint32_t)id;
                routed = true;
            }
        if (!routed) {
            rc = tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Connector lacks atomic CRTC_ID property");
            goto fail;
        }
    }
    rc = planes(&r, t);
    if (rc) goto fail;
    *out = t;
    return TAM_DISPLAY_OK;
fail:
    free(t);
    return rc;
}

static int real_ioctl(void *context, int fd, unsigned long request, void *arg)
{
    (void)context;
    return ioctl(fd, request, arg);
}

enum tam_display_result tam_display_inspect_fd(int fd,
    struct tam_display_topology **out, struct tam_display_error *error)
{
    return tam_display_inspect_with(fd, out, error, real_ioctl, NULL);
}
