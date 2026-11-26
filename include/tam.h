#ifndef TAM_H
#define TAM_H

/* Feature macros take effect only before the first system header; a program
 * that includes one earlier keeps its own choice. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Version Information                                                       */
/* ========================================================================= */

#define TAM_VERSION_MAJOR 0
#define TAM_VERSION_MINOR 11
#define TAM_VERSION_PATCH 0
#define TAM_VERSION_STRING "0.11.0"

const char *tam_version(void);

/* Read-only startup barrier. NULL checks /var/lib/tamlinux/pending.json, the
 * file the installer writes. Returns 0 when absent, 1 when an installation or
 * upgrade transaction is pending, -1 on unexpected error. */
int tam_install_pending(const char *pending_path);

/* The startup guard every Tamlinux command runs first. 0 when no update is
 * pending. Otherwise it prints why on stderr, prefixed with `program`, and
 * returns 1 when an update is pending or -1 when the check itself failed;
 * either way the caller exits without doing its work. */
int tam_install_guard(const char *program);

/* Read the active cultivar version string (e.g. "0.6.35") from
 * usr/lib/tamlinux/current/VERSION under root (or "/" when root is NULL).
 * Returns 0 on success; -1 if it is unavailable, its first line does not fit
 * in max_len, or it holds anything but letters, digits and . _ + -. */
int tam_cultivar_read_version(const char *root, char *buf, size_t max_len);

/* ========================================================================= */
/* Logging & Terminal Colors (tam_term)                                      */
/* ========================================================================= */

typedef enum {
    TAM_LOG_DEBUG = 0,
    TAM_LOG_INFO  = 1,
    TAM_LOG_WARN  = 2,
    TAM_LOG_ERROR = 3
} tam_log_level_t;

/* printf-style functions are checked by the compiler. */
#if defined(__GNUC__) || defined(__clang__)
#define TAM_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define TAM_PRINTF(fmt, args)
#endif

void tam_log_set_level(tam_log_level_t level);
void tam_log_info(const char *fmt, ...) TAM_PRINTF(1, 2);
void tam_log_warn(const char *fmt, ...) TAM_PRINTF(1, 2);
void tam_log_error(const char *fmt, ...) TAM_PRINTF(1, 2);
void tam_log_debug(const char *fmt, ...) TAM_PRINTF(1, 2);

/* ========================================================================= */
/* Dynamic Strings (tam_str)                                                 */
/* ========================================================================= */

/* An append returns the string, which may have moved. On failure (out of
 * memory, a size overflow, or a printf format error) it returns NULL with
 * errno set and leaves the old string as it was and still allocated, so keep
 * the old pointer until the result is checked:
 *     tam_str_t t = tam_str_cat(s, "x");
 *     if (!t) { tam_str_free(s); return -1; }
 *     s = t;
 * A NULL string or text argument returns `s` unchanged. */
typedef char *tam_str_t;

tam_str_t tam_str_new(const char *init);
tam_str_t tam_str_new_len(const void *init, size_t len);
tam_str_t tam_str_dup(const tam_str_t s);
size_t    tam_str_len(const tam_str_t s);
size_t    tam_str_avail(const tam_str_t s);
void      tam_str_free(tam_str_t s);
tam_str_t tam_str_cat(tam_str_t s, const char *t);
tam_str_t tam_str_cat_len(tam_str_t s, const void *t, size_t len);
tam_str_t tam_str_cat_printf(tam_str_t s, const char *fmt, ...) TAM_PRINTF(2, 3);
void      tam_str_clear(tam_str_t s);
tam_str_t tam_str_trim(tam_str_t s, const char *cset);

/* Append text as the body of a JSON string (no surrounding quotes): quotes,
 * backslashes and control characters are escaped; other bytes, including
 * UTF-8, pass through. Fails like any other append. */
tam_str_t tam_str_cat_json(tam_str_t s, const char *text);

/* Append a whole JSON string, with its quotes; NULL text appends null. */
tam_str_t tam_str_cat_json_str(tam_str_t s, const char *text);

/* Append "key":"value" (value NULL appends "key":null), with no separator
 * before it; the caller writes the commas and braces. */
tam_str_t tam_str_cat_json_kv(tam_str_t s, const char *key, const char *value);

/* ========================================================================= */
/* Subprocess Execution with Mandatory Timeout (tam_exec)                    */
/* ========================================================================= */

/* argv runs directly (no shell) in its own process group, with standard
 * input from /dev/null unless `input` is given, no blocked signals, and the
 * default action for the signals a caller commonly ignores or catches (so a
 * caller's SIG_IGN for SIGPIPE or SIGINT does not reach the child). The
 * deadline is wall-clock time on the monotonic clock. When it passes, or a
 * stream reaches `max_output`, the whole process group is killed,
 * `timed_out` or `truncated` is set, exit_code is -1 and the output so far
 * is kept. */
typedef struct {
    int         timeout_ms;  /* <= 0: 30,000 ms */
    const void *input;       /* bytes for standard input; NULL: /dev/null */
    size_t      input_len;
    size_t      max_output;  /* cap per stream in bytes; 0: 16 MiB */
} tam_exec_opts_t;

typedef struct {
    int   exit_code;   /* exit status, 128 + signal, 127 if not found, or -1 */
    char *stdout_buf;  /* NUL-terminated; NULL only if the child never ran */
    size_t stdout_len;
    char *stderr_buf;
    size_t stderr_len;
    bool  timed_out;
    bool  truncated;   /* a stream reached max_output */
    int   error;       /* 0, or errno: the program could not be started (for
                        * example ENOENT, with exit_code 127), a pipe or
                        * fork failed (exit_code -1), or ENOMEM capturing */
} tam_exec_result_t;

tam_exec_result_t tam_exec(const char *const argv[], int timeout_ms);
tam_exec_result_t tam_exec_opts(const char *const argv[], const tam_exec_opts_t *opts);
void tam_exec_result_free(tam_exec_result_t *res);

/* ========================================================================= */
/* File System Primitives (tam_fs)                                           */
/* ========================================================================= */

/* The tam_fs functions that return bool leave errno set when they return
 * false; the functions that return int give -1 with errno. */
bool   tam_fs_file_exists(const char *path);

/* Read a whole regular file into a NUL-terminated buffer the caller frees.
 * The file is opened close-on-exec and without blocking, so a FIFO, device or
 * directory is refused (EINVAL, EISDIR) instead of stalling. It reads until
 * end of file, so /proc and sysfs files work. `max` caps the size in bytes
 * (0: no cap; over it: EFBIG). NULL with errno on failure. */
char  *tam_fs_read(const char *path, size_t max, size_t *out_len);

/* Read an open descriptor (a pipe, stdin or a socket) until end of file, with
 * the same buffer, cap and errors as tam_fs_read. The descriptor stays open. */
char  *tam_fd_read_all(int fd, size_t max, size_t *out_len);

/* Flags for tam_fs_write_atomic. */
enum tam_fs_write_flags {
    TAM_FS_KEEP_MODE    = 1, /* an existing file keeps its permission bits */
    TAM_FS_REPLACE_LINK = 2  /* replace a symbolic link instead of its target */
};

/* Write to a temporary name in the same directory, sync it, rename it over
 * the destination and sync the directory, so after a crash the destination
 * holds either the old or the new contents. The destination is `path`, or,
 * when `path` is a symbolic link, the file it finally points to (unless
 * TAM_FS_REPLACE_LINK). New contents get `mode` (0: 0600), or the existing
 * file's mode with TAM_FS_KEEP_MODE.
 * Returns 0 when written and synced; 1 when the file was replaced but the
 * directory could not be synced (errno set; the new contents are in place but
 * may not survive a crash); -1 with errno when nothing was replaced (the
 * temporary file is removed). */
int    tam_fs_write_atomic(const char *path, const void *data, size_t len,
                           int flags, mode_t mode);

/* Create a directory and any missing parents; new directories get `mode`.
 * True when the directory exists afterwards. */
bool   tam_fs_mkdir_p(const char *path, mode_t mode);


/* Sync a directory, so names created, renamed or removed in it are on disk.
 * `tam_fs_sync_dirat` syncs `name` under `dirfd` (`name` "." for dirfd
 * itself). */
bool   tam_fs_sync_dir(const char *path);
bool   tam_fs_sync_dirat(int dirfd, const char *name);

/* ========================================================================= */
/* Directory Trees (tam_fs_tree)                                             */
/* ========================================================================= */

/* Swap two existing paths on one filesystem in a single step
 * (renameat2 RENAME_EXCHANGE); works for non-empty directories. 0, or -1 with
 * errno: ENOENT if either is missing, EXDEV across filesystems (or Btrfs
 * subvolumes), EINVAL or ENOSYS where the filesystem or kernel lacks it. The
 * caller syncs the parent directories. */
int    tam_fs_exchange(int olddirfd, const char *oldname,
                       int newdirfd, const char *newname);

/* Copy an open regular file's contents to another open file: a clone
 * (FICLONE) where the filesystem shares extents, else copy_file_range, else
 * read and write. Copies from offset 0 and leaves dst the size of src.
 * `*cloned` (may be NULL) says whether extents were shared. 0, or -1 with
 * errno. */
int    tam_fs_clone_or_copy(int srcfd, int dstfd, bool *cloned);

/* Remove `name` under `dirfd`, and everything below it if it is a directory.
 * Never follows symbolic links (a link is removed, not its target) and never
 * enters another filesystem: on finding one it stops with EXDEV, leaving it
 * and its parents in place. Missing `name` is success. 0, or -1 with errno. */
int    tam_fs_remove_tree(int dirfd, const char *name);

/* Take an exclusive flock on `path` (created 0600 if missing), waiting up to
 * `timeout_ms` (0: try once). The lock lasts until tam_fs_unlock or the
 * process exits. A descriptor, or -1 with errno (EWOULDBLOCK on timeout). */
int    tam_fs_lock(const char *path, int timeout_ms);
void   tam_fs_unlock(int fd);

/* Walk the tree under `root` in a fixed order: each directory's entries
 * sorted bytewise by name, a directory reported before its contents, "." and
 * ".." left out, and the root itself not reported. Symbolic links are
 * reported, never followed. Each entry's `path` is relative to the root, and
 * `dirfd`/`name` let the callback open it without resolving the path again.
 * The callback returns TAM_WALK_CONTINUE, TAM_WALK_SKIP (do not enter this
 * directory) or a negative value, which stops the walk and is returned. */
enum tam_walk_result { TAM_WALK_CONTINUE = 0, TAM_WALK_SKIP = 1 };
enum tam_walk_flags { TAM_WALK_ONE_DEVICE = 1 }; /* flag: refuse (EXDEV) to enter another filesystem */

typedef struct {
    const char        *path;   /* relative to root, no leading "./" */
    const char        *name;   /* last component */
    int                dirfd;  /* open directory that holds `name` */
    int                depth;  /* 1 for the root's own entries */
    const struct stat *st;     /* lstat-style: links are not followed */
} tam_walk_entry_t;

typedef int (*tam_walk_fn)(const tam_walk_entry_t *entry, void *user);

/* 0 when the walk finished, a callback's negative value, or -1 with errno
 * (ELOOP past 256 levels, ENAMETOOLONG past PATH_MAX). */
int    tam_fs_walk(const char *root, int flags, tam_walk_fn fn, void *user);

/* ========================================================================= */
/* SHA-256 (tam_sha256)                                                      */
/* ========================================================================= */

#define TAM_SHA256_LEN 32
#define TAM_SHA256_HEX_LEN 64

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t  block[64];
    size_t   used;
} tam_sha256_t;

void   tam_sha256_init(tam_sha256_t *ctx);
void   tam_sha256_update(tam_sha256_t *ctx, const void *data, size_t len);
void   tam_sha256_final(tam_sha256_t *ctx, uint8_t digest[TAM_SHA256_LEN]);
/* Lowercase hex, NUL-terminated. */
void   tam_sha256_hex(const uint8_t digest[TAM_SHA256_LEN],
                      char out[TAM_SHA256_HEX_LEN + 1]);
/* Hash an open file from its current offset to the end. 0, or -1 with errno. */
int    tam_sha256_fd(int fd, uint8_t digest[TAM_SHA256_LEN]);
/* Hash a file by path, without following a final symbolic link. */
int    tam_sha256_file(const char *path, uint8_t digest[TAM_SHA256_LEN]);

/* ========================================================================= */
/* Line Records (tam_rec)                                                    */
/* ========================================================================= */

/* A record file is UTF-8 text, one record per line, every line ending in a
 * newline (a missing final newline means a truncated file and is refused).
 * Fields are separated by runs of spaces or tabs; the last of up to
 * `max_fields` fields is the rest of the line, so it may contain spaces.
 * Blank lines and lines starting with '#' are skipped. The first record must
 * be "format N", N from 1 to `max_format`. Control characters other than tab,
 * a carriage return or a NUL byte anywhere refuse the file. */
typedef struct {
    size_t      line;    /* 1-based line of the problem; 0 for the whole file */
    const char *reason;  /* static text */
    int         format;  /* the format number read, when there was one */
} tam_rec_error_t;

/* Called for each record after the format line; `fields` point into a
 * private copy of the line, valid until the callback returns. Return 0 to
 * continue, or a positive value to stop the parse and have it returned
 * (-1 is the parser's own error). */
typedef int (*tam_rec_fn)(void *user, size_t line, int format,
                          char **fields, size_t nfields);

/* 0, a callback's nonzero value, or -1 with `err` (may be NULL) filled. */
int    tam_rec_parse(const char *text, size_t len, size_t max_fields,
                     int max_format, tam_rec_fn fn, void *user,
                     tam_rec_error_t *err);

/* Append one record: fields joined by one space, then a newline. Refuses
 * (returns NULL, leaving `s` unchanged and still owned by the caller) an empty
 * field, any control character, a field other than the last containing a
 * space, or a last field with leading or trailing space. */
tam_str_t tam_rec_cat(tam_str_t s, const char *const fields[], size_t n);

/* ========================================================================= */
/* Paths (tam_path)                                                          */
/* ========================================================================= */

/* dir + "/" + rest, with exactly one slash between them. Caller frees. */
char *tam_path_join(const char *dir, const char *rest);

/* Join a relative path under root, refusing anything that could leave it: an
 * empty or absolute `rel`, or any ".." component. The check is lexical; it
 * does not resolve symbolic links. NULL when refused. Caller frees. */
char *tam_path_join_under(const char *root, const char *rel);

/* True for a non-empty relative path with no empty, "." or ".." component,
 * no leading or trailing slash, and no control character: the paths a record
 * file may name inside a tree. */
bool tam_path_is_safe_rel(const char *rel);

typedef enum {
    TAM_XDG_STATE,    /* $XDG_STATE_HOME, else ~/.local/state */
    TAM_XDG_CACHE,    /* $XDG_CACHE_HOME, else ~/.cache */
    TAM_XDG_DATA,     /* $XDG_DATA_HOME, else ~/.local/share */
    TAM_XDG_CONFIG,   /* $XDG_CONFIG_HOME, else ~/.config */
    TAM_XDG_RUNTIME   /* $XDG_RUNTIME_DIR; no fallback */
} tam_xdg_kind_t;

/* <base>/tamlinux/<command>/<leaf> (leaf may be NULL for the directory
 * itself). A base variable that is unset or not absolute falls back as the
 * XDG specification says. With `create`, the directory (not the leaf) is
 * made with mode 0700 where missing. NULL when no base is known or the
 * directory cannot be made. Caller frees. */
char *tam_xdg_path(tam_xdg_kind_t kind, const char *command, const char *leaf,
                   bool create);

/* The base directory itself ($XDG_..._HOME or its fallback, or
 * $XDG_RUNTIME_DIR), for a program that keeps its own layout. Values that
 * are not absolute are ignored. A path the caller frees, or NULL with errno
 * ENOENT. */
char *tam_xdg_base(tam_xdg_kind_t kind);

/* Visit the directories searched for data or configuration files, most
 * important first: for TAM_XDG_DATA the data home, then each absolute entry
 * of $XDG_DATA_DIRS (default /usr/local/share:/usr/share); for
 * TAM_XDG_CONFIG the config home, then $XDG_CONFIG_DIRS (default /etc/xdg);
 * for the other kinds only the base. A non-zero return from `fn` stops the
 * walk and is returned; 0 when every directory was visited. */
typedef int (*tam_xdg_dir_fn)(const char *dir, void *user);
int tam_xdg_search(tam_xdg_kind_t kind, tam_xdg_dir_fn fn, void *user);

/* A private runtime directory, $XDG_RUNTIME_DIR/<name>, for sockets and
 * other per-session files. Creates it with mode 0700 where missing, and
 * checks that it and $XDG_RUNTIME_DIR are real directories (not links),
 * owned by the caller and closed to group and others. `name` is one path
 * component. A path the caller frees, or NULL with errno: ENOENT (no
 * runtime directory), EPERM (wrong owner or mode), ENOTDIR or EINVAL. */
char *tam_runtime_dir(const char *name);
/* ========================================================================= */
/* Lightweight INI Parser (tam_ini)                                          */
/* ========================================================================= */

/* Lines are `name = value` (or `name: value`) under `[section]` headers;
 * blank lines and lines starting with ; or # are skipped. The handler gets
 * each pair in order; a non-zero return stops parsing and its line number is
 * returned. A line that is not a header, pair or comment, an unclosed header
 * and a section name over 127 bytes are errors: parsing continues (the keys
 * of a bad section are not passed on) and the first error's line number is
 * returned. 0 when every line parsed, -1 when the text or file could not be
 * read. */
typedef int (*tam_ini_handler_t)(void *user, const char *section, const char *name, const char *value);
int tam_ini_parse_file(const char *filename, tam_ini_handler_t handler, void *user);
int tam_ini_parse_string(const char *string, tam_ini_handler_t handler, void *user);

#ifdef __cplusplus
}
#endif

/* The rest of the core. */
#include "tam_io.h"
#include "tam_proc.h"
#include "tam_sock.h"
#include "tam_b64.h"

#endif /* TAM_H */

