#ifndef TAM_PAGER_H
#define TAM_PAGER_H

#include "tam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared terminal pager primitives: attention-line styling, OSC 8 hyperlinks,
 * standard footer formatting, and URI/anchor/external-open helpers. Pure libc. */

/* SGR sequence for the highlighted attention row (reverse on Linux VT,
 * white-on-dark-gray on 256-color ANSI terminals). */
const char *tam_pager_attention_sgr(bool linux_vt);

/* SGR sequence for the currently focused hyperlink on the attention row. */
const char *tam_pager_link_focus_sgr(bool linux_vt);

/* Return true if url is non-empty, contains no control/DEL bytes (< 0x20 or
 * 0x7f), and is at most max_len bytes (when max_len > 0). */
bool tam_osc8_url_safe(const char *url, size_t max_len);

/* Write an OSC 8 opening sequence ("\033]8;;<url>\033\\") into out when url is
 * safe and fits with a trailing NUL. Returns bytes written (0 if skipped). */
size_t tam_osc8_open(char *out, size_t cap, const char *url);

/* Write an OSC 8 closing sequence ("\033]8;;\033\\") into out when it fits
 * with a trailing NUL. Returns bytes written (0 if cap is too small). */
size_t tam_osc8_close(char *out, size_t cap);

typedef struct {
    const char *label;       /* file path or "(file X of Y) path" */
    bool        has_attn;    /* true: "L<attn> <top>-<bot>/<total>", false: "<top>-<bot>/<total>" */
    size_t      attn_line;   /* 1-based attention line (0 if empty) */
    size_t      top_line;    /* 1-based first visible line (0 if empty) */
    size_t      bot_line;    /* 1-based last visible line (0 if empty) */
    size_t      total_lines; /* total lines when total_known is true */
    bool        total_known; /* false prints '?' for total */
    const char *link_hint;   /* optional focused link target */
    const char *version;     /* e.g. "0.4.0" -> right-aligned as "v0.4.0" */
} tam_pager_footer_t;

/* Format the standard Tamlinux pager footer:
 *   <label> | L<attn> <top>-<bot>/<total>    v<version>
 * Fits within width terminal cells, truncating label/link_hint on narrow
 * terminals while keeping line numbers and version visible. Returns byte
 * length written (excluding NUL). */
size_t tam_pager_format_footer(char *out, size_t cap, int width,
                               const tam_pager_footer_t *f);

typedef enum {
    TAM_URI_INVALID = 0,
    TAM_URI_ANCHOR,
    TAM_URI_EXTERNAL,
    TAM_URI_LOCAL
} tam_uri_kind_t;

/* Classify a hyperlink target:
 *   #fragment                   -> TAM_URI_ANCHOR
 *   http://, https://, mailto:  -> TAM_URI_EXTERNAL
 *   file:// or filesystem path  -> TAM_URI_LOCAL
 *   control chars / other schemes (e.g. javascript:, data:) -> TAM_URI_INVALID */
tam_uri_kind_t tam_uri_classify(const char *url);

/* Percent-decode src[0..len) into out (skipping NUL %00). Returns byte length
 * written (excluding NUL). */
size_t tam_uri_percent_decode(const char *src, size_t len,
                              char *out, size_t cap);

/* Resolve a local href (relative path, absolute path, or file:// URL, with
 * optional ?query and #fragment) against base_path. Writes the resolved path
 * to path_out (empty if href is a pure #anchor) and the decoded fragment to
 * frag_out (empty if none). Non-local file:// hosts and truncated outputs fail.
 * Returns 0 on success or -1 on error. */
int tam_uri_resolve_local(const char *base_path, const char *href,
                          char *path_out, size_t path_cap,
                          char *frag_out, size_t frag_cap);

/* Convert heading text into a lowercase GitHub-style anchor slug. Returns byte
 * length written (excluding NUL). */
size_t tam_uri_slugify(const char *text, size_t len, char *out, size_t cap);

/* Open an external URL (http://, https://, mailto:) via xdg-open without a
 * shell, redirecting stdio to /dev/null and avoiding zombies. Callers should
 * confirm with the user before invoking. Returns 0 on successful exec or -1 on
 * invalid URL, fork or exec failure. */
int tam_uri_open_external(const char *url);

#ifdef __cplusplus
}
#endif

#endif
