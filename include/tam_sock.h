#ifndef TAM_SOCK_H
#define TAM_SOCK_H

/* Local stream sockets for a program's control interface (tam_sock). Part of
 * libtam.a; included by tam.h. Put the socket in a private directory, such
 * as one from tam_runtime_dir, so only its owner can reach it. */

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bind and listen on a Unix stream socket at `path`, created with `mode`
 * (for example 0600) and close-on-exec. A leftover socket that nobody is
 * listening on is removed first; a live one gives EADDRINUSE, and anything
 * else at `path` gives EEXIST, so a second instance never steals the name.
 * Telling live from stale connects once: a live listener sees one connection
 * that closes without sending anything, which it should simply drop.
 * The process umask is changed for the moment of bind, so do not call it
 * while another thread creates files. A descriptor, or -1 with errno
 * (ENAMETOOLONG when `path` does not fit a socket address). */
int tam_sock_listen(const char *path, mode_t mode, int backlog);

/* The user id of the process at the other end of a connected Unix socket.
 * 0, or -1 with errno. */
int tam_sock_peer_uid(int fd, uid_t *uid);

/* Connect to the Unix stream socket at `path`, waiting at most `timeout_ms`
 * (<= 0: 5,000 ms). A blocking, close-on-exec descriptor, or -1 with errno
 * (ETIMEDOUT, ECONNREFUSED, ENOENT, ...). */
int tam_sock_connect(const char *path, int timeout_ms);

/* Read one line from a connected socket into buf, without its newline and
 * NUL-terminated, within one deadline for the whole line (<= 0: 5,000 ms),
 * so a slow or silent peer cannot hold the caller. Bytes after the newline
 * stay unread. Returns the line's length. At end of stream, a partial line
 * is returned as it is; with nothing read, -1 with errno ENODATA. Otherwise
 * -1 with errno: ETIMEDOUT, EMSGSIZE when the line does not fit in `size`
 * (the bytes read are discarded), or the read error. */
ssize_t tam_sock_read_line(int fd, char *buf, size_t size, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
