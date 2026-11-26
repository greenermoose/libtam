#include "tam_b64.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t tam_b64_encoded_len(size_t len)
{
    return (len / 3 + (len % 3 != 0)) * 4;
}

size_t tam_b64_encode(const void *data, size_t len, char *out)
{
    const unsigned char *in = data;
    size_t o = 0, i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        out[o++] = alphabet[(v >> 18) & 63];
        out[o++] = alphabet[(v >> 12) & 63];
        out[o++] = alphabet[(v >> 6) & 63];
        out[o++] = alphabet[v & 63];
    }
    if (i < len) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        out[o++] = alphabet[(v >> 18) & 63];
        out[o++] = alphabet[(v >> 12) & 63];
        out[o++] = i + 1 < len ? alphabet[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

static int value_of(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

unsigned char *tam_b64_decode(const char *s, size_t len, size_t *out_len)
{
    if (s == NULL && len > 0) {
        errno = EINVAL;
        return NULL;
    }
    /* Strip up to two '=' of padding, then the rest must be whole groups
     * or a group of 2 or 3 characters. */
    size_t pad = 0;
    while (pad < 2 && len > 0 && s[len - 1] == '=') {
        len--;
        pad++;
    }
    if (len % 4 == 1 || (pad > 0 && (len + pad) % 4 != 0)) {
        errno = EINVAL;
        return NULL;
    }
    unsigned char *out = malloc(len / 4 * 3 + 3 + 1);
    if (out == NULL) return NULL;
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        int v = value_of((unsigned char)s[i]);
        if (v < 0) {
            free(out);
            errno = EINVAL;
            return NULL;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)(acc >> bits);
            acc &= (1u << bits) - 1;
        }
    }
    out[o] = '\0';
    if (out_len != NULL) *out_len = o;
    return out;
}
