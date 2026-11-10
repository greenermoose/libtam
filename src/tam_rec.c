/* Line record files: one record per line, whitespace-separated fields, the
 * last field the rest of the line, and a leading "format N" record. */
#include "tam.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool is_space(char c) { return c == ' ' || c == '\t'; }

static int fail(tam_rec_error_t *err, size_t line, const char *reason, int format) {
    if (err) {
        err->line = line;
        err->reason = reason;
        err->format = format;
    }
    errno = EINVAL;
    return -1;
}

/* Split `buf` in place into at most `max` fields; the last is the rest of the
 * line with trailing space removed. Returns the field count. */
static size_t split(char *buf, char **fields, size_t max) {
    size_t n = 0;
    char *p = buf;
    while (n < max) {
        while (is_space(*p)) p++;
        if (*p == '\0') break;
        if (n == max - 1) {
            char *end = p + strlen(p);
            while (end > p && is_space(end[-1])) end--;
            *end = '\0';
            fields[n++] = p;
            break;
        }
        fields[n++] = p;
        while (*p && !is_space(*p)) p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

static int parse_format(char **fields, size_t n) {
    if (n != 2 || strcmp(fields[0], "format") != 0) return 0;
    const char *d = fields[1];
    if (*d < '1' || *d > '9' || strlen(d) > 6) return 0;
    long v = 0;
    for (; *d; d++) {
        if (*d < '0' || *d > '9') return 0;
        v = v * 10 + (*d - '0');
    }
    return (int)v;
}

int tam_rec_parse(const char *text, size_t len, size_t max_fields, int max_format,
                  tam_rec_fn fn, void *user, tam_rec_error_t *err) {
    if (err) memset(err, 0, sizeof *err);
    if (!text || max_fields < 1 || max_format < 1 || !fn) return fail(err, 0, "invalid arguments", 0);
    if (memchr(text, '\0', len)) return fail(err, 0, "contains a NUL byte", 0);
    if (len == 0) return fail(err, 0, "empty file; no format line", 0);
    if (text[len - 1] != '\n') return fail(err, 0, "no final newline; the file may be truncated", 0);

    size_t cap = max_fields > 2 ? max_fields : 2;
    char **fields = malloc(cap * sizeof *fields);
    char *buf = NULL;
    size_t buf_cap = 0;
    if (!fields) return -1;

    int format = 0;
    int result = 0;
    size_t line = 0;
    for (const char *p = text; p < text + len && result == 0;) {
        const char *nl = memchr(p, '\n', (size_t)(text + len - p));
        size_t n = (size_t)(nl - p);
        line++;
        for (size_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)p[i];
            if ((c < 0x20 && c != '\t') || c == 0x7f) {
                result = fail(err, line, "control character in line", format);
                break;
            }
        }
        if (result) break;
        if (n + 1 > buf_cap) {
            char *grown = realloc(buf, n + 1);
            if (!grown) {
                result = -1;
                break;
            }
            buf = grown;
            buf_cap = n + 1;
        }
        memcpy(buf, p, n);
        buf[n] = '\0';
        p = nl + 1;

        const char *q = buf;
        while (is_space(*q)) q++;
        if (*q == '\0' || *q == '#') continue;

        if (format == 0) {
            size_t nf = split(buf, fields, 2);
            format = parse_format(fields, nf);
            if (format == 0) {
                result = fail(err, line, "the first record must be \"format N\"", 0);
            } else if (format > max_format) {
                result = fail(err, line, "format is newer than this program reads", format);
            }
            continue;
        }
        size_t nf = split(buf, fields, max_fields);
        result = fn(user, line, format, fields, nf);
    }
    if (result == 0 && format == 0) result = fail(err, 0, "no format line", 0);
    if (err && result != -1) err->format = format;
    free(buf);
    free(fields);
    return result;
}

tam_str_t tam_rec_cat(tam_str_t s, const char *const fields[], size_t n) {
    if (!s || !fields || n == 0) return NULL;
    for (size_t i = 0; i < n; i++) {
        const char *f = fields[i];
        if (!f || !f[0]) return NULL;
        size_t flen = strlen(f);
        for (size_t j = 0; j < flen; j++) {
            unsigned char c = (unsigned char)f[j];
            if (c < 0x20 || c == 0x7f) return NULL;
            if (c == ' ' && i + 1 < n) return NULL;
        }
        if (f[0] == ' ' || f[flen - 1] == ' ') return NULL;
        if (i == 0 && f[0] == '#') return NULL; /* would read back as a comment */
    }
    size_t total = n; /* separators and the newline */
    for (size_t i = 0; i < n; i++) total += strlen(fields[i]);
    char *rec = malloc(total);
    if (!rec) return NULL;
    size_t pos = 0;
    for (size_t i = 0; i < n; i++) {
        size_t flen = strlen(fields[i]);
        memcpy(rec + pos, fields[i], flen);
        pos += flen;
        rec[pos++] = i + 1 < n ? ' ' : '\n';
    }
    tam_str_t out = tam_str_cat_len(s, rec, total);
    free(rec);
    return out;
}
