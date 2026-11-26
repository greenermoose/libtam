#define _POSIX_C_SOURCE 200809L

#include "tam_pager.h"
#include "tam_uni.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "tam_internal.h"

const char *tam_pager_attention_sgr(bool linux_vt)
{
    return linux_vt ? "\033[7m" : "\033[38;5;15;48;5;236m";
}

const char *tam_pager_link_focus_sgr(bool linux_vt)
{
    (void)linux_vt;
    return "\033[1;30;46m";
}

bool tam_osc8_url_safe(const char *url, size_t max_len)
{
    if (!url || url[0] == '\0')
        return false;
    size_t len = 0;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        if (*p < 0x20 || *p >= 0x7f)
            return false;
        len++;
        if (max_len > 0 && len > max_len)
            return false;
    }
    return true;
}

size_t tam_osc8_open(char *out, size_t cap, const char *url)
{
    if (!out || !tam_osc8_url_safe(url, 1024))
        return 0;
    size_t ulen = strlen(url);
    size_t need = 5 + ulen + 2;
    if (cap < need + 1)
        return 0;
    memcpy(out, "\033]8;;", 5);
    memcpy(out + 5, url, ulen);
    memcpy(out + 5 + ulen, "\033\\", 2);
    out[need] = '\0';
    return need;
}

size_t tam_osc8_close(char *out, size_t cap)
{
    if (!out || cap < 8)
        return 0;
    memcpy(out, "\033]8;;\033\\", 7);
    out[7] = '\0';
    return 7;
}

static size_t sanitize_text(const char *src, char *out, size_t cap)
{
    if (!out || cap == 0)
        return 0;
    if (!src) {
        out[0] = '\0';
        return 0;
    }
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && used + 1 < cap; p++) {
        if (*p < 0x20 || *p == 0x7f)
            out[used++] = '?';
        else
            out[used++] = (char)*p;
    }
    out[used] = '\0';
    return used;
}

static void fit_ellipsis(const char *src, int cells, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    out[0] = '\0';
    if (!src || cells <= 0)
        return;
    size_t len = strlen(src);
    if (tam_uni_str_width(src, len) <= cells) {
        tam_uni_fit(src, len, cells, out, cap);
        return;
    }
    if (cells <= 3) {
        tam_uni_fit(src, len, cells, out, cap);
        return;
    }
    char tmp[512];
    size_t max_prefix = cap > 4 ? cap - 3 : 1;
    size_t tcap = max_prefix < sizeof(tmp) ? max_prefix : sizeof(tmp);
    tam_uni_fit(src, len, cells - 3, tmp, tcap);
    size_t tlen = strlen(tmp);
    if (tlen + 3 >= cap)
        tlen = cap > 4 ? cap - 4 : 0;
    memcpy(out, tmp, tlen);
    memcpy(out + tlen, "...", 3);
    out[tlen + 3] = '\0';
}

size_t tam_pager_format_footer(char *out, size_t cap, int width,
                               const tam_pager_footer_t *f)
{
    if (!out || cap == 0)
        return 0;
    out[0] = '\0';
    if (!f || width <= 0)
        return 0;

    char range[96];
    if (f->has_attn) {
        if (f->total_known) {
            snprintf(range, sizeof(range), "L%zu %zu-%zu/%zu",
                     f->attn_line, f->top_line, f->bot_line, f->total_lines);
        } else {
            snprintf(range, sizeof(range), "L%zu %zu-%zu/?",
                     f->attn_line, f->top_line, f->bot_line);
        }
    } else {
        if (f->total_known) {
            snprintf(range, sizeof(range), "%zu-%zu/%zu",
                     f->top_line, f->bot_line, f->total_lines);
        } else {
            snprintf(range, sizeof(range), "%zu-%zu/?",
                     f->top_line, f->bot_line);
        }
    }

    char ver[64] = {0};
    if (f->version && f->version[0] != '\0')
        snprintf(ver, sizeof(ver), "v%s", f->version);

    char label[512];
    sanitize_text(f->label, label, sizeof(label));
    char hint[512];
    sanitize_text(f->link_hint, hint, sizeof(hint));

    int range_w = tam_uni_str_width(range, strlen(range));
    int ver_w = ver[0] ? tam_uni_str_width(ver, strlen(ver)) : 0;
    int label_w = label[0] ? tam_uni_str_width(label, strlen(label)) : 0;

    int left_budget = width;
    if (ver_w > 0 && range_w + 2 + ver_w <= width)
        left_budget = width - ver_w - 2;
    else
        ver_w = 0;

    char left[1024] = {0};
    if (label[0] && range_w + 3 < left_budget) {
        int avail_label = left_budget - range_w - 3;
        char fitted_label[512];
        if (label_w <= avail_label) {
            snprintf(fitted_label, sizeof(fitted_label), "%s", label);
        } else {
            fit_ellipsis(label, avail_label, fitted_label, sizeof(fitted_label));
        }
        snprintf(left, sizeof(left), "%s | %s", fitted_label, range);
    } else {
        tam_uni_fit(range, strlen(range), left_budget, left, sizeof(left));
    }

    if (hint[0]) {
        int used_w = tam_uni_str_width(left, strlen(left));
        int rem = left_budget - used_w;
        if (rem >= 8) {
            char fitted_hint[512];
            fit_ellipsis(hint, rem - 4, fitted_hint, sizeof(fitted_hint));
            if (fitted_hint[0]) {
                size_t llen = strlen(left);
                snprintf(left + llen, sizeof(left) - llen, " -> %s", fitted_hint);
            }
        }
    }

    size_t used = 0;
    size_t llen = strlen(left);
    if (llen >= cap)
        llen = cap - 1;
    memcpy(out, left, llen);
    used = llen;

    if (ver_w > 0) {
        int left_w = tam_uni_str_width(left, strlen(left));
        int pad = width - left_w - ver_w;
        if (pad < 2)
            pad = 2;
        while (pad-- > 0 && used + 1 < cap)
            out[used++] = ' ';
        size_t vlen = strlen(ver);
        if (vlen > cap - 1 - used)
            vlen = cap - 1 - used;
        memcpy(out + used, ver, vlen);
        used += vlen;
    }

    out[used] = '\0';
    return used;
}

static bool has_prefix_ci(const char *s, const char *pfx)
{
    size_t n = strlen(pfx);
    return strncasecmp(s, pfx, n) == 0;
}

tam_uri_kind_t tam_uri_classify(const char *url)
{
    if (!url || url[0] == '\0')
        return TAM_URI_INVALID;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        if (*p < 0x20 || *p == 0x7f)
            return TAM_URI_INVALID;
    }
    if (url[0] == '#')
        return TAM_URI_ANCHOR;
    if (has_prefix_ci(url, "http://") ||
        has_prefix_ci(url, "https://") ||
        has_prefix_ci(url, "mailto:"))
        return TAM_URI_EXTERNAL;
    if (has_prefix_ci(url, "file://"))
        return TAM_URI_LOCAL;

    /* Reject other explicit URI schemes (e.g. javascript:, data:). */
    if (isalpha((unsigned char)url[0])) {
        const char *p = url + 1;
        while (*p && (isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.'))
            p++;
        if (*p == ':')
            return TAM_URI_INVALID;
    }
    return TAM_URI_LOCAL;
}

static bool percent_decode_checked(const char *src, size_t len,
                                   char *out, size_t cap, size_t *out_len)
{
    if (!out || cap == 0)
        return false;
    size_t used = 0;
    for (size_t i = 0; i < len; i++) {
        if (used + 1 >= cap) {
            out[used] = '\0';
            if (out_len)
                *out_len = used;
            return false;
        }
        if (src[i] == '%' && i + 2 < len) {
            int hi = tam__hex_val(src[i + 1]);
            int lo = tam__hex_val(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                unsigned char b = (unsigned char)((hi << 4) | lo);
                if (b != 0) {
                    out[used++] = (char)b;
                    i += 2;
                    continue;
                }
            }
        }
        out[used++] = src[i];
    }
    out[used] = '\0';
    if (out_len)
        *out_len = used;
    return true;
}

size_t tam_uri_percent_decode(const char *src, size_t len,
                              char *out, size_t cap)
{
    size_t used = 0;
    (void)percent_decode_checked(src, len, out, cap, &used);
    return used;
}

int tam_uri_resolve_local(const char *base_path, const char *href,
                          char *path_out, size_t path_cap,
                          char *frag_out, size_t frag_cap)
{
    if (!href || !path_out || path_cap == 0)
        return -1;
    path_out[0] = '\0';
    if (frag_out && frag_cap > 0)
        frag_out[0] = '\0';

    const char *hash = strchr(href, '#');
    size_t before_hash = hash ? (size_t)(hash - href) : strlen(href);
    if (hash && frag_out && frag_cap > 0) {
        if (!percent_decode_checked(hash + 1, strlen(hash + 1),
                                    frag_out, frag_cap, NULL)) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }

    /* Strip ?query from path component. */
    const char *q = memchr(href, '?', before_hash);
    size_t path_len = q ? (size_t)(q - href) : before_hash;

    const char *raw = href;
    if (path_len >= 7 && strncasecmp(raw, "file://", 7) == 0) {
        raw += 7;
        path_len -= 7;
        if (path_len >= 9 && strncasecmp(raw, "localhost", 9) == 0 &&
            (path_len == 9 || raw[9] == '/')) {
            raw += 9;
            path_len -= 9;
        } else if (path_len > 0 && raw[0] != '/') {
            errno = EINVAL;
            return -1;
        }
    }

    if (path_len == 0)
        return 0;

    char decoded[4096];
    if (!percent_decode_checked(raw, path_len, decoded, sizeof(decoded), NULL)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (decoded[0] == '\0')
        return 0;

    if (decoded[0] == '/') {
        if (strlen(decoded) >= path_cap) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(path_out, decoded);
        return 0;
    }

    if (base_path && base_path[0] != '\0' &&
        strcmp(base_path, "-") != 0 &&
        strcmp(base_path, "standard input") != 0) {
        const char *slash = strrchr(base_path, '/');
        if (slash) {
            size_t dir_len = (size_t)(slash - base_path + 1);
            size_t dec_len = strlen(decoded);
            if (dir_len + dec_len >= path_cap) {
                errno = ENAMETOOLONG;
                return -1;
            }
            memcpy(path_out, base_path, dir_len);
            memcpy(path_out + dir_len, decoded, dec_len + 1);
            return 0;
        }
    }

    if (strlen(decoded) >= path_cap) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(path_out, decoded);
    return 0;
}

size_t tam_uri_slugify(const char *text, size_t len, char *out, size_t cap)
{
    if (!out || cap == 0)
        return 0;
    size_t used = 0;
    for (size_t i = 0; i < len && used + 1 < cap; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 'A' && c <= 'Z') {
            out[used++] = (char)(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '_' || c >= 0x80) {
            out[used++] = (char)c;
        } else if (c == ' ' || c == '\t') {
            out[used++] = '-';
        }
    }
    out[used] = '\0';
    return used;
}

/* Close every descriptor above stderr except `keep`, so the opener does not
 * hold the caller's files, sockets or terminal open. */
static void close_inherited(int keep)
{
#ifdef SYS_close_range
    if (keep > STDERR_FILENO + 1 &&
        syscall(SYS_close_range, STDERR_FILENO + 1, keep - 1, 0) != 0)
        goto fallback;
    if (syscall(SYS_close_range, keep + 1, ~0U, 0) == 0)
        return;
fallback:
#endif
    {
        long max = sysconf(_SC_OPEN_MAX);
        if (max < 0 || max > 65536) max = 65536;
        for (int fd = STDERR_FILENO + 1; fd < max; fd++)
            if (fd != keep) (void)close(fd);
    }
}

int tam_uri_open_external(const char *url)
{
    if (tam_uri_classify(url) != TAM_URI_EXTERNAL) {
        errno = EINVAL;
        return -1;
    }

    int report[2];
    if (pipe(report) < 0)
        return -1;
    if (fcntl(report[0], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(report[1], F_SETFD, FD_CLOEXEC) < 0) {
        int err = errno;
        close(report[0]);
        close(report[1]);
        errno = err;
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        int err = errno;
        close(report[0]);
        close(report[1]);
        errno = err;
        return -1;
    }
    if (pid == 0) {
        close(report[0]);
        pid_t grand = fork();
        if (grand < 0) {
            int err = errno;
            (void)write(report[1], &err, sizeof(err));
            _exit(127);
        }
        if (grand > 0)
            _exit(0);
        if (setsid() < 0) {
            int err = errno;
            (void)write(report[1], &err, sizeof(err));
            _exit(127);
        }
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDIN_FILENO);
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                close(null_fd);
        }
        close_inherited(report[1]);
        static const int sigs[] = {SIGINT, SIGQUIT, SIGTERM, SIGWINCH, SIGPIPE, SIGTSTP};
        for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
            (void)signal(sigs[i], SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        (void)sigprocmask(SIG_SETMASK, &none, NULL);
        char *argv[] = {"xdg-open", (char *)url, NULL};
        execvp("xdg-open", argv);
        int err = errno;
        (void)write(report[1], &err, sizeof(err));
        _exit(127);
    }
    close(report[1]);
    int child_error = 0;
    ssize_t count;
    do {
        count = read(report[0], &child_error, sizeof(child_error));
    } while (count < 0 && errno == EINTR);
    int read_error = errno;
    close(report[0]);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (count < 0) { errno = read_error; return -1; }
    if (count != 0) { errno = count == sizeof(child_error) ? child_error : EIO; return -1; }
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (waited >= 0) errno = EIO;
        return -1;
    }
    return 0;
}
