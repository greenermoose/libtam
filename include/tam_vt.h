#ifndef TAM_VT_H
#define TAM_VT_H
#include "tam_display.h"
#include <sys/types.h>
#include <linux/vt.h>

/* A borrowed, owned-by-uid real VT. Never changes KD mode or activates another VT.
 * foreground_group is captured in the UI before forking a recovery guard. */
struct tam_vt {
    int fd;
    unsigned number;
    uid_t uid;
    pid_t foreground_group;
    bool claimed;
    struct vt_mode saved_mode;
};
enum tam_display_result tam_vt_open(struct tam_vt *, struct tam_display_error *);
enum tam_display_result tam_vt_check(const struct tam_vt *, struct tam_display_error *);
/* Recovery may outlive the UI process group. Still requires the same owned,
 * active text VT, but allows the login shell to regain the foreground. */
enum tam_display_result tam_vt_check_restore(const struct tam_vt *, struct tam_display_error *);
/* Claim only VT_AUTO, preserving KD_TEXT/keyboard settings. The caller stays
 * in its controlling terminal session; SIGUSR1 requests release, SIGUSR2 acquire.
 * Restore KMS/drop master BEFORE acknowledging a pending VT handoff. */
enum tam_display_result tam_vt_claim(struct tam_vt *, struct tam_display_error *);
enum tam_display_result tam_vt_release(struct tam_vt *, bool handoff, struct tam_display_error *);
void tam_vt_close(struct tam_vt *);

/* Whole-VT TIOCLINUX blank/wake and runtime consoleblank idle timer (0..60 min).
 * Requires the caller's owned active foreground text VT. */
enum tam_display_result tam_vt_blank(const struct tam_vt *, struct tam_display_error *);
enum tam_display_result tam_vt_wake(const struct tam_vt *, struct tam_display_error *);
enum tam_display_result tam_vt_idle_seconds_path(const char *param_path, int *seconds_out);
enum tam_display_result tam_vt_idle_path(const struct tam_vt *, unsigned minutes,
    const char *param_path, bool *verified, struct tam_display_error *);
enum tam_display_result tam_vt_idle(const struct tam_vt *, unsigned minutes,
    bool *verified, struct tam_display_error *);
#endif
