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
#endif
