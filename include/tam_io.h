#ifndef TAM_IO_H
#define TAM_IO_H

/* Small I/O and time helpers (tam_io). Part of libtam.a; included by tam.h. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write all `len` bytes to `fd`. Retries on EINTR, waits with poll when a
 * non-blocking descriptor would block, and treats a write of zero bytes as
 * EIO. 0, or -1 with errno; some bytes may have been written. */
int tam_write_all(int fd, const void *buf, size_t len);

/* Read the first line of a small regular file, such as a sysfs or /proc
 * attribute, into buf without the newline, trimmed of spaces, tabs and
 * carriage returns at both ends. An empty line gives "". 0, or -1 with
 * errno: ERANGE when the line does not fit in `size` (nothing is cut
 * short), EINVAL for a FIFO, device or directory, or open's errno. */
int tam_read_line(const char *path, char *buf, size_t size);

/* Time on the monotonic clock, which never jumps when the wall clock is
 * set: milliseconds and nanoseconds since an arbitrary start. */
int64_t tam_monotonic_ms(void);
int64_t tam_monotonic_ns(void);

#ifdef __cplusplus
}
#endif

#endif
