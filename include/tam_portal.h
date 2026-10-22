#ifndef TAM_PORTAL_H
#define TAM_PORTAL_H

/* Desktop portals (libtam-portal). A separate library from libtam because it
 * needs D-Bus: link it with `pkg-config --libs libtam-portal`. */

#include <tam.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#endif /* TAM_PORTAL_H */
