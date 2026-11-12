#ifndef TAM_UNI_H
#define TAM_UNI_H

#include "tam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 and terminal display width. Programs that use tam_uni_width should
 * call setlocale(LC_CTYPE, "") during startup so the C library supplies
 * Unicode character data. The decode and encode functions do not need a
 * locale. */

/* Expected length of a UTF-8 sequence from its first byte, or 0 if the byte
 * cannot begin a sequence. */
size_t tam_utf8_seq_len(unsigned char first);

/* Decode one UTF-8 sequence. Returns the bytes consumed, 1 to 4. Invalid or
 * truncated input consumes one byte and yields U+FFFD. */
size_t tam_utf8_decode(const char *s, size_t len, uint32_t *cp);

/* Encode one code point and return the bytes written, 1 to 4. Surrogates and
 * values above U+10FFFF are encoded as U+FFFD. out must have room for 4. */
size_t tam_utf8_encode(uint32_t cp, char out[4]);

/* Display width in terminal cells: 0 for combining and zero-width code
 * points, 2 for wide East Asian characters, 1 otherwise. C0, C1 and DEL
 * return 0; the caller decides how to draw them. */
int tam_uni_width(uint32_t cp);

/* Width of a UTF-8 string. Control bytes count as zero cells and invalid
 * bytes count as one. */
int tam_uni_str_width(const char *s, size_t len);

/* Copy at most cells columns of s into out, never splitting a UTF-8
 * sequence, and NUL-terminate. A zero-width code point after the last
 * fitting character is included. Returns the width used. */
int tam_uni_fit(const char *s, size_t len, int cells, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
