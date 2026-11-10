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
#define TAM_VERSION_MINOR 5
#define TAM_VERSION_PATCH 0
#define TAM_VERSION_STRING "0.5.1"

const char *tam_version(void);

/* ========================================================================= */
/* Logging & Terminal Colors (tam_term)                                      */
/* ========================================================================= */

typedef enum {
    TAM_LOG_DEBUG = 0,
    TAM_LOG_INFO  = 1,
    TAM_LOG_WARN  = 2,
    TAM_LOG_ERROR = 3
} tam_log_level_t;

void tam_log_set_level(tam_log_level_t level);
void tam_log_info(const char *fmt, ...);
void tam_log_warn(const char *fmt, ...);
void tam_log_error(const char *fmt, ...);
void tam_log_debug(const char *fmt, ...);

/* ========================================================================= */
/* Dynamic Strings (tam_str)                                                 */
/* ========================================================================= */

typedef char *tam_str_t;

tam_str_t tam_str_new(const char *init);
tam_str_t tam_str_new_len(const void *init, size_t len);
tam_str_t tam_str_dup(const tam_str_t s);
size_t    tam_str_len(const tam_str_t s);
size_t    tam_str_avail(const tam_str_t s);
void      tam_str_free(tam_str_t s);
tam_str_t tam_str_cat(tam_str_t s, const char *t);
tam_str_t tam_str_cat_len(tam_str_t s, const void *t, size_t len);
tam_str_t tam_str_cat_printf(tam_str_t s, const char *fmt, ...);
void      tam_str_clear(tam_str_t s);
tam_str_t tam_str_trim(tam_str_t s, const char *cset);

/* Append text as the body of a JSON string (no surrounding quotes): quotes,
 * backslashes and control characters are escaped; other bytes, including
 * UTF-8, pass through. */
tam_str_t tam_str_cat_json(tam_str_t s, const char *text);

/* ========================================================================= */
/* Subprocess Execution with Mandatory Timeout (tam_exec)                    */
/* ========================================================================= */

/* argv runs directly (no shell) in its own process group, with standard
 * input from /dev/null unless `input` is given. The deadline is wall-clock
 * time on the monotonic clock. When it passes, or a stream reaches
 * `max_output`, the whole process group is killed, `timed_out` or
 * `truncated` is set, exit_code is -1 and the output so far is kept. */
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
    bool  truncated;
} tam_exec_result_t;

tam_exec_result_t tam_exec(const char *const argv[], int timeout_ms);
tam_exec_result_t tam_exec_opts(const char *const argv[], const tam_exec_opts_t *opts);
void tam_exec_result_free(tam_exec_result_t *res);

/* ========================================================================= */
/* File System Primitives (tam_fs)                                           */
/* ========================================================================= */

bool   tam_fs_file_exists(const char *path);
char  *tam_fs_read_file(const char *path, size_t *out_len);
/* Write to a temporary name in the same directory, sync it, rename it over
 * `path` and sync the directory, so after a crash `path` holds either the old
 * or the new contents. `mode` 0 keeps mkstemp's 0600. False on any failure,
 * with the temporary file removed. */
bool   tam_fs_write_atomic(const char *path, const void *data, size_t len, mode_t mode);
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
enum { TAM_WALK_CONTINUE = 0, TAM_WALK_SKIP = 1 };
enum { TAM_WALK_ONE_DEVICE = 1 }; /* flag: refuse (EXDEV) to enter another filesystem */

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
/* ========================================================================= */
/* Lightweight INI Parser (tam_ini)                                          */
/* ========================================================================= */

typedef int (*tam_ini_handler_t)(void *user, const char *section, const char *name, const char *value);
int tam_ini_parse_file(const char *filename, tam_ini_handler_t handler, void *user);
int tam_ini_parse_string(const char *string, tam_ini_handler_t handler, void *user);

#ifdef __cplusplus
}
#endif

#endif /* TAM_H */

