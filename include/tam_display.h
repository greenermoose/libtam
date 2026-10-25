#ifndef TAM_DISPLAY_H
#define TAM_DISPLAY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental display API, independent of core and portal archives.
 * Inspection IDs are transient. Only an owned state can restore displays. */
#define TAM_DISPLAY_API_VERSION "0.2.0"
#define TAM_DISPLAY_MAX_OBJECTS 128
#define TAM_DISPLAY_MAX_PROPERTIES 128

enum tam_display_result {
    TAM_DISPLAY_OK, TAM_DISPLAY_INVALID, TAM_DISPLAY_IO,
    TAM_DISPLAY_LIMIT, TAM_DISPLAY_STALE, TAM_DISPLAY_UNSUPPORTED,
    TAM_DISPLAY_NOT_FOUND, TAM_DISPLAY_AMBIGUOUS,
    TAM_DISPLAY_PERMISSION, TAM_DISPLAY_INACTIVE, TAM_DISPLAY_BUSY,
    TAM_DISPLAY_RESTORE_FAILED
};

struct tam_display_error {
    enum tam_display_result code;
    int system_errno;
    char message[160];
};

struct tam_display_property {
    uint32_t id, flags;
    uint64_t value;
    char name[32];
};

struct tam_display_object {
    uint32_t id;
    size_t property_count;
    struct tam_display_property properties[TAM_DISPLAY_MAX_PROPERTIES];
};

struct tam_display_connector {
    struct tam_display_object object;
    char name[40];
    uint32_t connection; /* DRM: 1 connected, 2 disconnected, 3 unknown */
    uint32_t encoder_id, crtc_id;
};

struct tam_display_crtc {
    struct tam_display_object object;
    uint32_t framebuffer_id, x, y, width, height;
    bool mode_valid;
};

struct tam_display_plane {
    struct tam_display_object object;
    uint32_t crtc_id, framebuffer_id, possible_crtcs;
};

struct tam_display_topology {
    char driver[64];
    size_t connector_count, crtc_count, plane_count;
    struct tam_display_connector connectors[TAM_DISPLAY_MAX_OBJECTS];
    struct tam_display_crtc crtcs[TAM_DISPLAY_MAX_OBJECTS];
    struct tam_display_plane planes[TAM_DISPLAY_MAX_OBJECTS];
};

/* Caller owns fd and must already have legitimate session/device access.
 * Enables atomic/universal-plane visibility on that file description.
 * On failure *out is NULL; successful allocations need free().
 * Enumeration is bounded and retried, but is not an atomic hardware snapshot. */
enum tam_display_result tam_display_inspect_fd(int fd,
    struct tam_display_topology **out, struct tam_display_error *error);

/* Pure routing analysis, NOT an atomic request and never permission to apply.
 * Rejects incomplete routing and selections without a currently active output.
 * Disable CRTC/planes only when no retained output shares that CRTC. */
struct tam_display_plan {
    uint32_t keep_connector;
    size_t detach_count, disable_crtc_count, disable_plane_count;
    uint32_t detach[TAM_DISPLAY_MAX_OBJECTS];
    uint32_t disable_crtcs[TAM_DISPLAY_MAX_OBJECTS];
    uint32_t disable_planes[TAM_DISPLAY_MAX_OBJECTS];
};
enum tam_display_result tam_display_plan_keep(
    const struct tam_display_topology *topology, const char *connector,
    struct tam_display_plan *out, struct tam_display_error *error);

/* Capture uses a duplicate of an authorized primary DRM descriptor. It never
 * opens a card or takes master. Retains FB backing via GETFB2/ADDFB2 and copies
 * blob contents into owned blobs. Requires CLOSEFB support (never RMFB active
 * planes). No display change until apply/restore; atomic TEST_ONLY proves the
 * complete restore request before capture succeeds. Call from one owner only.
 * Release after restore; release uses CLOSEFB, never disables a scanout. */
struct tam_display_state;
enum tam_display_result tam_display_capture_fd(int fd,
    struct tam_display_state **out, struct tam_display_error *error);
/* Install a foreground/session authorization callback before any real commit.
 * Caller must arm an independent recovery owner before enabling writes. */
typedef enum tam_display_result (*tam_display_access_check)(void *, bool restoring,
    struct tam_display_error *);
void tam_display_state_set_access_check(struct tam_display_state *, tam_display_access_check, void *);
enum tam_display_result tam_display_state_keep(struct tam_display_state *,
    const char *connector, struct tam_display_error *error);
enum tam_display_result tam_display_state_restore(struct tam_display_state *,
    struct tam_display_error *error);
void tam_display_state_free(struct tam_display_state *);

/* Connector type names use the kernel's sysfs spelling. */
void tam_display_connector_name(uint32_t type, uint32_t number,
    char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
