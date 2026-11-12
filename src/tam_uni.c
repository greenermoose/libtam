#include "tam_uni.h"

#include <wchar.h>

size_t tam_utf8_seq_len(unsigned char first)
{
    if (first < 0x80) return 1;
    if ((first & 0xE0) == 0xC0) return 2;
    if ((first & 0xF0) == 0xE0) return 3;
    if ((first & 0xF8) == 0xF0) return 4;
    return 0;
}

size_t tam_utf8_decode(const char *s, size_t len, uint32_t *cp)
{
    if (cp == NULL) return 0;
    if (s == NULL || len == 0) {
        *cp = 0xFFFD;
        return 0;
    }

    const unsigned char *b = (const unsigned char *)s;
    if (b[0] < 0x80) {
        *cp = b[0];
        return 1;
    }

    size_t need = tam_utf8_seq_len(b[0]);
    if (need == 0 || len < need) {
        *cp = 0xFFFD;
        return 1;
    }

    uint32_t value;
    switch (need) {
    case 2: value = (uint32_t)(b[0] & 0x1F); break;
    case 3: value = (uint32_t)(b[0] & 0x0F); break;
    default: value = (uint32_t)(b[0] & 0x07); break;
    }

    for (size_t i = 1; i < need; i++) {
        if ((b[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return 1;
        }
        value = (value << 6) | (uint32_t)(b[i] & 0x3F);
    }

    if ((need == 2 && value < 0x80) ||
        (need == 3 && value < 0x800) ||
        (need == 4 && value < 0x10000) ||
        (value >= 0xD800 && value <= 0xDFFF) ||
        value > 0x10FFFF) {
        *cp = 0xFFFD;
        return 1;
    }

    *cp = value;
    return need;
}

size_t tam_utf8_encode(uint32_t cp, char out[4])
{
    if (out == NULL) return 0;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;

    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int tam_uni_width(uint32_t cp)
{
    if (cp == 0) return 0;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;

    int width = wcwidth((wchar_t)cp);
    if (width < 0) return 1;
    return width;
}

int tam_uni_str_width(const char *s, size_t len)
{
    if (s == NULL) return 0;

    int total = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        size_t used = tam_utf8_decode(s + i, len - i, &cp);
        if (used == 0) break;
        int width = tam_uni_width(cp);
        if (width > 0) {
            if (total > 0x3FFFFFFF - width) return 0x3FFFFFFF;
            total += width;
        }
        i += used;
    }
    return total;
}

int tam_uni_fit(const char *s, size_t len, int cells, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return 0;
    out[0] = '\0';
    if (s == NULL || cells <= 0) return 0;

    size_t used_bytes = 0;
    int used_cells = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        size_t n = tam_utf8_decode(s + i, len - i, &cp);
        if (n == 0) break;
        if (n > out_cap - 1 - used_bytes) break;

        int width = tam_uni_width(cp);
        if (width > 0 && used_cells + width > cells) break;

        for (size_t k = 0; k < n; k++) out[used_bytes + k] = s[i + k];
        used_bytes += n;
        if (width > 0) used_cells += width;
        i += n;
    }
    out[used_bytes] = '\0';
    return used_cells;
}
