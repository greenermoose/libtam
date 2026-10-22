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

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Version Information                                                       */
/* ========================================================================= */

#define TAM_VERSION_MAJOR 0
#define TAM_VERSION_MINOR 4
#define TAM_VERSION_PATCH 0
#define TAM_VERSION_STRING "0.4.0"

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
bool   tam_fs_write_atomic(const char *path, const void *data, size_t len, mode_t mode);
/* Create a directory and any missing parents; new directories get `mode`.
 * True when the directory exists afterwards. */
bool   tam_fs_mkdir_p(const char *path, mode_t mode);

/* ========================================================================= */
/* Paths (tam_path)                                                          */
/* ========================================================================= */

/* dir + "/" + rest, with exactly one slash between them. Caller frees. */
char *tam_path_join(const char *dir, const char *rest);

/* Join a relative path under root, refusing anything that could leave it: an
 * empty or absolute `rel`, or any ".." component. The check is lexical; it
 * does not resolve symbolic links. NULL when refused. Caller frees. */
char *tam_path_join_under(const char *root, const char *rel);

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

