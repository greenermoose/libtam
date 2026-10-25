#ifndef TAM_DISPLAY_SESSION_H
#define TAM_DISPLAY_SESSION_H
#include "tam_vt.h"

/* Optional libtam-display-session archive; libsystemd stays outside display/core.
 * Observe logind, verify seat/ACLs, and open only the selected primary card.
 * Caller first owns VT_PROCESS handoff. No TakeControl/TakeDevice, chmod or
 * KD_GRAPHICS: logind TakeControl would mute/reset the ordinary text VT. */
struct tam_display_session;
enum tam_display_session_event {
    TAM_DISPLAY_SESSION_RUNNING, TAM_DISPLAY_SESSION_LOST
};
enum tam_display_result tam_display_session_open(const struct tam_vt *,
    unsigned card, struct tam_display_session **, struct tam_display_error *);
int tam_display_session_device(const struct tam_display_session *);
/* Process pending messages, verify session and foreground VT before selection.
 * Any handoff ends this selection. Resume descriptors are never adopted. */
enum tam_display_result tam_display_session_check(struct tam_display_session *,
    struct tam_display_error *);
/* Same active local login/VT, allowing the shell to regain foreground after UI death. */
enum tam_display_result tam_display_session_check_restore(struct tam_display_session *,
    struct tam_display_error *);
enum tam_display_session_event tam_display_session_dispatch(struct tam_display_session *);
int tam_display_session_bus_fd(struct tam_display_session *);
int tam_display_session_bus_events(struct tam_display_session *);
/* Restore first, then drop owned master/close; finally release the owned VT handoff. */
void tam_display_session_close(struct tam_display_session *);
#endif
