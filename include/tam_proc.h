#ifndef TAM_PROC_H
#define TAM_PROC_H

/* Starting programs (tam_proc). Part of libtam.a; included by tam.h. To run a
 * program and capture its output with a deadline, use tam_exec. */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Options for starting a program. A zero-initialised struct changes
 * nothing, so set only what you need. */
typedef struct {
    int  stdin_fd;    /* > 0: this descriptor becomes the program's stdin */
    bool keep_stdio;  /* detached: keep stdout and stderr (not /dev/null) */
    int  keep_fd;     /* > 2: detached: leave this descriptor open, with
                       * close-on-exec cleared (for WAYLAND_SOCKET) */
    const char *const *env; /* NULL-ended "NAME=VALUE" entries added to (or
                             * replacing in) the program's environment */
} tam_proc_opts_t;

/* The program execvp would run for `name`: a name containing '/' is checked
 * as it is, otherwise each $PATH entry is tried in order (an empty entry
 * means the current directory, as for execvp; an unset or empty PATH means
 * /usr/local/bin:/usr/bin:/bin). A match is a regular file the caller may
 * execute. A path the caller frees, or NULL with errno ENOENT. */
char *tam_which(const char *name);

/* Start argv[0] (looked up in $PATH) detached: in a new session, with
 * standard streams on /dev/null, no blocked signals, default signal
 * actions, and none of the caller's other descriptors. It is not the
 * caller's child, so it never needs reaping. Waits only until the program
 * has started. 0, or -1 with errno: the exec failure (ENOENT when it was not
 * found), or a fork or pipe failure. The _opts form takes tam_proc_opts_t:
 * stdin_fd, keep_stdio (a compositor keeps its children's output in its
 * log), keep_fd and env (a Wayland client's WAYLAND_SOCKET). With env, the
 * program is looked up before forking, so a missing one fails with ENOENT
 * without starting anything. */
int tam_spawn_detached(const char *const argv[]);
int tam_spawn_detached_opts(const char *const argv[], const tam_proc_opts_t *opts);

/* Run argv[0] (looked up in $PATH) in the foreground and wait for it, as
 * system() would but without a shell: it shares the caller's terminal,
 * standard streams and process group; it starts with no blocked signals and
 * default signal actions; and the caller ignores SIGINT and SIGQUIT until it
 * ends, so those keys reach only the program. The caller restores any
 * terminal mode it changed before calling. On return 0, `*exit_code` (may be
 * NULL) is the exit status, or 128 plus the signal that ended it. -1 with
 * errno when it could not start (ENOENT when it was not found). The _opts
 * form uses stdin_fd (for example the terminal, when the caller's stdin is a
 * pipe) and env; keep_stdio and keep_fd do not apply. */
int tam_run_foreground(const char *const argv[], int *exit_code);
int tam_run_foreground_opts(const char *const argv[], const tam_proc_opts_t *opts,
                            int *exit_code);

#ifdef __cplusplus
}
#endif

#endif
