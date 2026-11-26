#ifndef TAM_INTERNAL_H
#define TAM_INTERNAL_H

/* Helpers shared by libtam's own sources. Not installed. */

#include <time.h>

/* Milliseconds on the monotonic clock. */
static inline long long tam__now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The value of one hexadecimal digit, or -1. */
static inline int tam__hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

#endif
