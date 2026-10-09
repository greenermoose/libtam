#ifndef TAM_H
#define TAM_H

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

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
#define TAM_VERSION_MINOR 2
#define TAM_VERSION_PATCH 0
#define TAM_VERSION_STRING "0.2.0"

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

/* ========================================================================= */
/* Subprocess Execution with Mandatory Timeout (tam_exec)                    */
/* ========================================================================= */

typedef struct {
    int   exit_code;
    char *stdout_buf;
    size_t stdout_len;
    char *stderr_buf;
    size_t stderr_len;
    bool  timed_out;
} tam_exec_result_t;

tam_exec_result_t tam_exec(const char *const argv[], int timeout_ms);
void tam_exec_result_free(tam_exec_result_t *res);

/* ========================================================================= */
/* File System Primitives (tam_fs)                                           */
/* ========================================================================= */

bool   tam_fs_file_exists(const char *path);
char  *tam_fs_read_file(const char *path, size_t *out_len);
bool   tam_fs_write_atomic(const char *path, const void *data, size_t len, mode_t mode);

/* ========================================================================= */
/* Lightweight INI Parser (tam_ini)                                          */
/* ========================================================================= */

typedef int (*tam_ini_handler_t)(void *user, const char *section, const char *name, const char *value);
int tam_ini_parse_file(const char *filename, tam_ini_handler_t handler, void *user);
int tam_ini_parse_string(const char *string, tam_ini_handler_t handler, void *user);

/* ========================================================================= */
/* Desktop Portals (tam_portal)                                              */
/* ========================================================================= */

typedef struct {
    const char *title;         /* Dialog title (defaults to "Select file" if NULL) */
    bool multiple;             /* Allow selecting multiple files */
    bool directory;            /* Directory chooser mode */
    const char *extensions;    /* Space-separated file extensions, e.g. "png jpg" */
    int timeout_ms;            /* Timeout in ms; <= 0 for default 600,000 ms (10 min) */
} tam_portal_file_options_t;

typedef struct {
    char **paths;              /* Null-terminated array of file paths */
    size_t count;              /* Number of paths in array */
    int status;                /* 0 = success, 1 = nothing picked / cancelled, 2 = failed */
    char *error_message;       /* Error message if status == 2, else NULL */
} tam_portal_file_result_t;

tam_portal_file_result_t tam_portal_file_choose(const tam_portal_file_options_t *options);
void tam_portal_file_result_free(tam_portal_file_result_t *res);
char *tam_portal_uri_to_path(const char *uri);

#ifdef __cplusplus
}
#endif

#endif /* TAM_H */

