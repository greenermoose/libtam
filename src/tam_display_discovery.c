#include "tam_display_internal.h"
#include <stdio.h>

enum tam_display_result tam_display_fail(struct tam_display_error *error,
    enum tam_display_result code, int system_errno, const char *message)
{
    if (error) {
        error->code = code;
        error->system_errno = system_errno;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return code;
}

void tam_display_connector_name(uint32_t type, uint32_t number,
    char *buffer, size_t capacity)
{
    /* Order is DRM_MODE_CONNECTOR_* in the installed Linux UAPI. */
    static const char *const names[] = {
        "Unknown", "VGA", "DVI-I", "DVI-D", "DVI-A", "Composite",
        "SVIDEO", "LVDS", "Component", "DIN", "DP", "HDMI-A", "HDMI-B",
        "TV", "eDP", "Virtual", "DSI", "DPI", "Writeback", "SPI", "USB"
    };
    if (!buffer || !capacity) return;
    if (type < sizeof(names) / sizeof(*names))
        snprintf(buffer, capacity, "%s-%u", names[type], number);
    else
        snprintf(buffer, capacity, "Unknown%u-%u", type, number);
}
