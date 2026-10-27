#define _GNU_SOURCE
#include "tam_display_internal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void sanitize_line(char *s, size_t capacity)
{
    if (!capacity) return;
    s[capacity - 1] = 0;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = 0;
    for (size_t i = 0; i < len; ++i)
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) s[i] = '?';
}

static bool read_attr(const char *path, char *buf, size_t capacity)
{
    if (!buf || !capacity) return false;
    buf[0] = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, capacity - 1);
    close(fd);
    if (n < 0) return false;
    buf[n] = 0;
    sanitize_line(buf, capacity);
    return true;
}

static bool parse_u32(const char *s, uint32_t *out)
{
    if (!s || !*s || *s < '0' || *s > '9') return false;
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || !end || *end || v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_prefix_index(const char *name, const char *prefix, unsigned *out)
{
    size_t plen = strlen(prefix);
    if (strncmp(name, prefix, plen) != 0) return false;
    uint32_t idx = 0;
    if (!parse_u32(name + plen, &idx) || idx >= 64) return false;
    *out = (unsigned)idx;
    return true;
}

static bool check_vtcon_bound(const char *sysfs_root)
{
    char dirpath[512];
    int n = snprintf(dirpath, sizeof(dirpath), "%s/class/vtconsole", sysfs_root);
    if (n < 0 || (size_t)n >= sizeof(dirpath)) return false;
    DIR *dir = opendir(dirpath);
    if (!dir) return false;
    bool bound = false;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        unsigned idx = 0;
        if (!parse_prefix_index(de->d_name, "vtcon", &idx)) continue;
        char bpath[800], npath[800], bval[32], nval[128];
        if (snprintf(bpath, sizeof(bpath), "%s/%s/bind", dirpath, de->d_name) >= (int)sizeof(bpath) ||
            snprintf(npath, sizeof(npath), "%s/%s/name", dirpath, de->d_name) >= (int)sizeof(npath))
            continue;
        if (read_attr(bpath, bval, sizeof(bval)) && !strcmp(bval, "1") &&
            read_attr(npath, nval, sizeof(nval)) && strstr(nval, "frame buffer device")) {
            bound = true;
            break;
        }
    }
    closedir(dir);
    return bound;
}

static int find_drm_cards(const char *sysfs_root, unsigned fb_index, unsigned *card_out)
{
    char path[512];
    int count = 0;
    unsigned found = 0;
    int n = snprintf(path, sizeof(path), "%s/class/graphics/fb%u/device/drm", sysfs_root, fb_index);
    if (n > 0 && (size_t)n < sizeof(path)) {
        DIR *dir = opendir(path);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) != NULL) {
                unsigned c = 0;
                if (parse_prefix_index(de->d_name, "card", &c)) {
                    if (count == 0 || found != c) { found = c; ++count; }
                }
            }
            closedir(dir);
        }
    }
    if (count > 0) {
        *card_out = found;
        return count;
    }
    n = snprintf(path, sizeof(path), "%s/class/drm", sysfs_root);
    if (n <= 0 || (size_t)n >= sizeof(path)) return 0;
    DIR *dir = opendir(path);
    if (!dir) return 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        unsigned c = 0;
        if (!parse_prefix_index(de->d_name, "card", &c)) continue;
        char fb_link[640];
        snprintf(fb_link, sizeof(fb_link), "%s/card%u/device/graphics/fb%u", path, c, fb_index);
        if (access(fb_link, F_OK) == 0) {
            if (count == 0 || found != c) { found = c; ++count; }
        }
    }
    closedir(dir);
    *card_out = found;
    return count;
}

enum tam_display_result tam_display_map_console_fb_with(unsigned vt_number,
    const char *sysfs_root, const char *dev_root,
    struct tam_console_fb *out, struct tam_display_error *error,
    tam_display_ioctl_fn call, void *context)
{
    if (out) memset(out, 0, sizeof(*out));
    if (error) memset(error, 0, sizeof(*error));
    if (!out || vt_number < 1 || vt_number > 63 || !sysfs_root || !*sysfs_root || !dev_root || !*dev_root)
        return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid VT or sysfs/dev root for console framebuffer mapping");
    if (!check_vtcon_bound(sysfs_root))
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Framebuffer console (fbcon) is not bound");

    char gpath[512];
    int n = snprintf(gpath, sizeof(gpath), "%s/class/graphics", sysfs_root);
    if (n <= 0 || (size_t)n >= sizeof(gpath))
        return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Sysfs path exceeds bounds");
    DIR *dir = opendir(gpath);
    if (!dir)
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, errno, "Cannot inspect Linux graphics framebuffer class");
    unsigned fbs[64];
    size_t fb_count = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        unsigned idx = 0;
        if (parse_prefix_index(de->d_name, "fb", &idx) && fb_count < 64)
            fbs[fb_count++] = idx;
    }
    closedir(dir);
    if (!fb_count)
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "No Linux framebuffer device registered");
    for (size_t i = 0; i < fb_count; ++i)
        for (size_t j = i + 1; j < fb_count; ++j)
            if (fbs[j] < fbs[i]) { unsigned tmp = fbs[i]; fbs[i] = fbs[j]; fbs[j] = tmp; }

    unsigned selected_fb = fbs[0];
    bool con2fb_verified = false;
    struct fb_con2fbmap map = {.console = vt_number, .framebuffer = UINT32_MAX};
    int ioctl_rc = -1, saved_errno = ENOENT;
    if (call) {
        errno = 0;
        ioctl_rc = call(context, -1, FBIOGET_CON2FBMAP, &map);
        saved_errno = errno;
    } else {
        for (size_t i = 0; i < fb_count; ++i) {
            char devpath[512];
            snprintf(devpath, sizeof(devpath), "%s/fb%u", dev_root, fbs[i]);
            int fd = open(devpath, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0) { saved_errno = errno; continue; }
            errno = 0;
            ioctl_rc = ioctl(fd, FBIOGET_CON2FBMAP, &map);
            saved_errno = errno;
            close(fd);
            if (ioctl_rc == 0) break;
        }
    }
    if (ioctl_rc == 0) {
        bool present = false;
        for (size_t i = 0; i < fb_count; ++i)
            if (fbs[i] == map.framebuffer) present = true;
        if (map.framebuffer >= 64 || !present)
            return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Kernel CON2FBMAP references a missing framebuffer device");
        selected_fb = map.framebuffer;
        con2fb_verified = true;
    } else {
        if (saved_errno != EACCES && saved_errno != EPERM && saved_errno != ENOENT &&
            saved_errno != ENOTTY && saved_errno != ENODEV)
            return tam_display_fail(error, TAM_DISPLAY_IO, saved_errno, "Cannot query console-to-framebuffer mapping");
        if (fb_count > 1)
            return tam_display_fail(error, TAM_DISPLAY_AMBIGUOUS, saved_errno,
                "Multiple framebuffer devices exist and /dev/fb CON2FBMAP could not be queried");
    }

    char attr_path[600], val[128];
    snprintf(attr_path, sizeof(attr_path), "%s/fb%u/state", gpath, selected_fb);
    if (!read_attr(attr_path, val, sizeof(val)) || strcmp(val, "0") != 0)
        return tam_display_fail(error, TAM_DISPLAY_INACTIVE, 0, "Console framebuffer device is not active");

    char fb_name[64];
    snprintf(attr_path, sizeof(attr_path), "%s/fb%u/name", gpath, selected_fb);
    if (!read_attr(attr_path, fb_name, sizeof(fb_name)) || !*fb_name)
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Console framebuffer device name is unavailable");

    snprintf(attr_path, sizeof(attr_path), "%s/fb%u/virtual_size", gpath, selected_fb);
    if (!read_attr(attr_path, val, sizeof(val)))
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Console framebuffer virtual_size is unavailable");
    char *comma = strchr(val, ',');
    uint32_t width = 0, height = 0, stride = 0, bpp = 0;
    if (!comma)
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Malformed console framebuffer virtual_size");
    *comma = 0;
    if (!parse_u32(val, &width) || !parse_u32(comma + 1, &height) ||
        !width || width > 65536 || !height || height > 65536)
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Invalid console framebuffer dimensions");

    snprintf(attr_path, sizeof(attr_path), "%s/fb%u/stride", gpath, selected_fb);
    if (!read_attr(attr_path, val, sizeof(val)) || !parse_u32(val, &stride) || !stride || stride > (1U << 28))
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Invalid console framebuffer stride");

    snprintf(attr_path, sizeof(attr_path), "%s/fb%u/bits_per_pixel", gpath, selected_fb);
    if (!read_attr(attr_path, val, sizeof(val)) || !parse_u32(val, &bpp) ||
        (bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32))
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Unsupported console framebuffer bits_per_pixel");

    unsigned card = 0;
    int cards = find_drm_cards(sysfs_root, selected_fb, &card);
    if (cards <= 0)
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0,
            "Console framebuffer is not backed by a DRM KMS device");
    if (cards > 1)
        return tam_display_fail(error, TAM_DISPLAY_AMBIGUOUS, 0,
            "Console framebuffer maps to multiple DRM devices");

    out->vt_number = vt_number;
    out->fb_index = selected_fb;
    out->card = card;
    out->width = width;
    out->height = height;
    out->stride = stride;
    out->bits_per_pixel = bpp;
    out->con2fb_verified = con2fb_verified;
    snprintf(out->fb_name, sizeof(out->fb_name), "%s", fb_name);
    return TAM_DISPLAY_OK;
}

enum tam_display_result tam_display_map_console_fb_root(unsigned vt_number,
    const char *sysfs_root, const char *dev_root,
    struct tam_console_fb *out, struct tam_display_error *error)
{
    return tam_display_map_console_fb_with(vt_number, sysfs_root, dev_root, out, error, NULL, NULL);
}

enum tam_display_result tam_display_map_console_fb(unsigned vt_number,
    struct tam_console_fb *out, struct tam_display_error *error)
{
    return tam_display_map_console_fb_with(vt_number, "/sys", "/dev", out, error, NULL, NULL);
}

enum tam_display_result tam_display_verify_console_fb(
    const struct tam_display_topology *t, struct tam_console_fb *fb,
    struct tam_display_error *error)
{
    if (error) memset(error, 0, sizeof(*error));
    if (!t || !fb || !fb->width || !fb->height || !fb->stride)
        return tam_display_fail(error, TAM_DISPLAY_INVALID, 0, "Invalid topology or console framebuffer metadata");
    uint32_t console_fb_id = 0;
    size_t active_outputs = 0;
    for (size_t i = 0; i < t->connector_count; ++i) {
        const struct tam_display_connector *c = &t->connectors[i];
        if (c->connection != 1 || !c->crtc_id) continue;
        const struct tam_display_crtc *crtc = NULL;
        for (size_t j = 0; j < t->crtc_count; ++j)
            if (t->crtcs[j].object.id == c->crtc_id) { crtc = &t->crtcs[j]; break; }
        if (!crtc || !crtc->mode_valid || !crtc->framebuffer_id)
            return tam_display_fail(error, TAM_DISPLAY_STALE, 0, "Connected console output lacks a valid CRTC mode or framebuffer");
        if ((crtc->width && crtc->width > fb->width) || (crtc->height && crtc->height > fb->height))
            return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "CRTC scanout exceeds console framebuffer dimensions");
        bool plane_ok = t->plane_count == 0;
        for (size_t j = 0; j < t->plane_count; ++j) {
            if (t->planes[j].crtc_id == crtc->object.id &&
                t->planes[j].framebuffer_id == crtc->framebuffer_id) {
                plane_ok = true;
                break;
            }
        }
        if (!plane_ok)
            return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Active CRTC has no plane scanning out the console framebuffer");
        if (!console_fb_id) console_fb_id = crtc->framebuffer_id;
        else if (console_fb_id != crtc->framebuffer_id)
            return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "Active outputs do not share a single console framebuffer");
        ++active_outputs;
    }
    if (!active_outputs || !console_fb_id)
        return tam_display_fail(error, TAM_DISPLAY_UNSUPPORTED, 0, "No active DRM console scanout matches the bound framebuffer console");
    fb->drm_fb_id = console_fb_id;
    return TAM_DISPLAY_OK;
}
