#ifndef TAM_DISPLAY_INTERNAL_H
#define TAM_DISPLAY_INTERNAL_H
#include "tam_display.h"

/* Injection seam for deterministic tests. Production uses ioctl(2). */
typedef int (*tam_display_ioctl_fn)(void *, int, unsigned long, void *);
enum tam_display_result tam_display_inspect_with(int fd,
    struct tam_display_topology **out, struct tam_display_error *error,
    tam_display_ioctl_fn call, void *context);
enum tam_display_result tam_display_fail(struct tam_display_error *,
    enum tam_display_result, int, const char *);

#define TAM_DISPLAY_MAX_WRITES 4096
#define TAM_DISPLAY_MAX_REFS 256
struct tam_display_write { uint32_t object, property; uint64_t value; };
struct tam_display_request { size_t count; struct tam_display_write writes[TAM_DISPLAY_MAX_WRITES]; };
struct tam_display_ref { uint32_t original, owned; };
struct tam_display_state {
    int fd;
    tam_display_ioctl_fn call;
    void *context;
    struct tam_display_topology *baseline;
    struct tam_display_request restore, change;
    struct tam_display_ref fbs[TAM_DISPLAY_MAX_REFS], blobs[TAM_DISPLAY_MAX_REFS];
    size_t fb_count, blob_count, blob_bytes;
    bool changed, close_fd, restoring;
    tam_display_access_check access_check;
    void *access_context;
};
int tam_display_call(struct tam_display_state *, unsigned long, void *);
enum tam_display_result tam_display_capture_with(int fd, struct tam_display_state **,
    struct tam_display_error *, tam_display_ioctl_fn, void *);
enum tam_display_result tam_display_atomic(struct tam_display_state *,
    const struct tam_display_request *, bool test, struct tam_display_error *);
enum tam_display_result tam_display_validate(struct tam_display_state *,
    bool recovery, struct tam_display_error *);
enum tam_display_result tam_display_append(struct tam_display_request *, uint32_t,
    uint32_t, uint64_t, struct tam_display_error *);
#endif
