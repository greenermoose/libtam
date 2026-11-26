#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

struct tam_str_hdr {
    size_t len;
    size_t alloc;
    char buf[];
};

static inline struct tam_str_hdr *tam_str_get_hdr(const tam_str_t s) {
    return (struct tam_str_hdr *)((char *)s - sizeof(struct tam_str_hdr));
}

tam_str_t tam_str_new_len(const void *init, size_t len) {
    if (len > SIZE_MAX - sizeof(struct tam_str_hdr) - 1) {
        errno = ENOMEM;
        return NULL;
    }
    struct tam_str_hdr *hdr = malloc(sizeof(struct tam_str_hdr) + len + 1);
    if (!hdr) {
        return NULL;
    }
    hdr->len = len;
    hdr->alloc = len;
    if (len && init) {
        memcpy(hdr->buf, init, len);
    }
    hdr->buf[len] = '\0';
    return (tam_str_t)hdr->buf;
}

tam_str_t tam_str_new(const char *init) {
    size_t len = init ? strlen(init) : 0;
    return tam_str_new_len(init, len);
}

tam_str_t tam_str_dup(const tam_str_t s) {
    if (!s) return NULL;
    return tam_str_new_len(s, tam_str_len(s));
}

size_t tam_str_len(const tam_str_t s) {
    if (!s) return 0;
    return tam_str_get_hdr(s)->len;
}

size_t tam_str_avail(const tam_str_t s) {
    if (!s) return 0;
    struct tam_str_hdr *hdr = tam_str_get_hdr(s);
    return hdr->alloc - hdr->len;
}

void tam_str_free(tam_str_t s) {
    if (!s) return;
    free(tam_str_get_hdr(s));
}

/* Make room for addlen more bytes. On failure, NULL with errno, and s is
 * untouched and still allocated. */
static tam_str_t tam_str_make_room(tam_str_t s, size_t addlen) {
    if (!s) return NULL;
    struct tam_str_hdr *hdr = tam_str_get_hdr(s);
    size_t avail = hdr->alloc - hdr->len;
    if (avail >= addlen) {
        return s;
    }
    const size_t limit = SIZE_MAX - sizeof(struct tam_str_hdr) - 1;
    if (addlen > limit - hdr->len) {
        errno = ENOMEM;
        return NULL;
    }
    size_t newlen = hdr->len + addlen;
    size_t newalloc;
    if (newlen < 1024 * 1024) {
        newalloc = newlen * 2;
    } else {
        newalloc = newlen <= limit - 1024 * 1024 ? newlen + 1024 * 1024 : limit;
    }
    struct tam_str_hdr *newhdr = realloc(hdr, sizeof(struct tam_str_hdr) + newalloc + 1);
    if (!newhdr) {
        errno = ENOMEM;
        return NULL;
    }
    newhdr->alloc = newalloc;
    return (tam_str_t)newhdr->buf;
}

tam_str_t tam_str_cat_len(tam_str_t s, const void *t, size_t len) {
    if (!s || !t || len == 0) return s;
    s = tam_str_make_room(s, len);
    if (!s) return NULL;
    struct tam_str_hdr *hdr = tam_str_get_hdr(s);
    memcpy(s + hdr->len, t, len);
    hdr->len += len;
    s[hdr->len] = '\0';
    return s;
}

tam_str_t tam_str_cat(tam_str_t s, const char *t) {
    if (!s || !t) return s;
    return tam_str_cat_len(s, t, strlen(t));
}

tam_str_t tam_str_cat_printf(tam_str_t s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap_copy);
    va_end(ap_copy);
    if (needed < 0) {
        va_end(ap);
        errno = EOVERFLOW;
        return NULL;
    }
    s = tam_str_make_room(s, (size_t)needed);
    if (!s) {
        va_end(ap);
        return NULL;
    }
    struct tam_str_hdr *hdr = tam_str_get_hdr(s);
    vsnprintf(s + hdr->len, (size_t)needed + 1, fmt, ap);
    hdr->len += (size_t)needed;
    s[hdr->len] = '\0';
    va_end(ap);
    return s;
}

void tam_str_clear(tam_str_t s) {
    if (!s) return;
    struct tam_str_hdr *hdr = tam_str_get_hdr(s);
    hdr->len = 0;
    s[0] = '\0';
}

tam_str_t tam_str_trim(tam_str_t s, const char *cset) {
    if (!s || !cset) return s;
    struct tam_str_hdr *hdr = tam_str_get_hdr(s);
    size_t start = 0, end = hdr->len;
    while (start < end && strchr(cset, s[start])) start++;
    while (end > start && strchr(cset, s[end - 1])) end--;
    size_t len = end - start;
    if (start > 0) {
        memmove(s, s + start, len);
    }
    s[len] = '\0';
    hdr->len = len;
    return s;
}

/* The escape for byte c, or NULL when it is copied as it is. */
static const char *json_escape(unsigned char c, char tmp[7]) {
    switch (c) {
    case '"':  return "\\\"";
    case '\\': return "\\\\";
    case '\b': return "\\b";
    case '\f': return "\\f";
    case '\n': return "\\n";
    case '\r': return "\\r";
    case '\t': return "\\t";
    default:
        if (c < 0x20) {
            snprintf(tmp, 7, "\\u%04x", c);
            return tmp;
        }
        return NULL;
    }
}

tam_str_t tam_str_cat_json(tam_str_t s, const char *text) {
    if (!s || !text) return s;
    /* Size the escaped text first and grow once, so a failure leaves s
     * untouched, as for every other append. */
    char tmp[7];
    size_t need = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        const char *esc = json_escape(*p, tmp);
        size_t add = esc ? strlen(esc) : 1;
        if (need > SIZE_MAX - add) {
            errno = ENOMEM;
            return NULL;
        }
        need += add;
    }
    s = tam_str_make_room(s, need);
    if (!s) return NULL;
    struct tam_str_hdr *hdr = tam_str_get_hdr(s);
    char *out = s + hdr->len;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        const char *esc = json_escape(*p, tmp);
        if (esc) {
            size_t n = strlen(esc);
            memcpy(out, esc, n);
            out += n;
        } else {
            *out++ = (char)*p;
        }
    }
    hdr->len += need;
    s[hdr->len] = '\0';
    return s;
}
