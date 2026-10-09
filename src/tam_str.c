#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

struct tam_str_hdr {
    size_t len;
    size_t alloc;
    char buf[];
};

static inline struct tam_str_hdr *tam_str_get_hdr(const tam_str_t s) {
    return (struct tam_str_hdr *)((char *)s - sizeof(struct tam_str_hdr));
}

tam_str_t tam_str_new_len(const void *init, size_t len) {
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

static tam_str_t tam_str_make_room(tam_str_t s, size_t addlen) {
    if (!s) return NULL;
    struct tam_str_hdr *hdr = tam_str_get_hdr(s);
    size_t avail = hdr->alloc - hdr->len;
    if (avail >= addlen) {
        return s;
    }
    size_t newlen = hdr->len + addlen;
    size_t newalloc = (newlen < 1024 * 1024) ? newlen * 2 : newlen + (1024 * 1024);
    struct tam_str_hdr *newhdr = realloc(hdr, sizeof(struct tam_str_hdr) + newalloc + 1);
    if (!newhdr) {
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
        return s;
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
    char *start = s;
    char *end = s + hdr->len - 1;

    while (start <= end && strchr(cset, *start)) start++;
    while (end > start && strchr(cset, *end)) end--;

    size_t len = (start > end) ? 0 : (size_t)(end - start + 1);
    if (start != s) {
        memmove(s, start, len);
    }
    s[len] = '\0';
    hdr->len = len;
    return s;
}
