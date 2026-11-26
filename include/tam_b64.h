#ifndef TAM_B64_H
#define TAM_B64_H

/* Base64 (RFC 4648, standard alphabet with '=' padding), as terminals use
 * for OSC 52 clipboard transfers (tam_b64). Part of libtam.a; included by
 * tam.h. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Encoded length of `len` bytes, without a terminator. */
size_t tam_b64_encoded_len(size_t len);

/* Encode `len` bytes into `out`, which has room for tam_b64_encoded_len(len)
 * plus a terminating NUL. Returns the encoded length. */
size_t tam_b64_encode(const void *data, size_t len, char *out);

/* Decode `len` characters into a buffer the caller frees, NUL-terminated
 * for convenience; `*out_len` gets the byte count. Padding may be left off.
 * NULL with errno EINVAL for any other character or a length that no
 * encoding has, or ENOMEM. */
unsigned char *tam_b64_decode(const char *s, size_t len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
