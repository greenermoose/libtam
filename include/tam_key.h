#ifndef TAM_KEY_H
#define TAM_KEY_H

#include "tam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Byte-stream key decoder for ANSI/xterm terminals. Feed terminal bytes with
 * tam_key_feed and take events with tam_key_next. No terminfo is involved. */

typedef enum {
    TAM_KEY_NONE = 0,
    TAM_KEY_CHAR,
    TAM_KEY_ENTER,
    TAM_KEY_TAB,
    TAM_KEY_BACKSPACE,
    TAM_KEY_ESC,
    TAM_KEY_UP,
    TAM_KEY_DOWN,
    TAM_KEY_LEFT,
    TAM_KEY_RIGHT,
    TAM_KEY_HOME,
    TAM_KEY_END,
    TAM_KEY_PAGE_UP,
    TAM_KEY_PAGE_DOWN,
    TAM_KEY_INSERT,
    TAM_KEY_DELETE,
    TAM_KEY_F1,
    TAM_KEY_F2,
    TAM_KEY_F3,
    TAM_KEY_F4,
    TAM_KEY_F5,
    TAM_KEY_F6,
    TAM_KEY_F7,
    TAM_KEY_F8,
    TAM_KEY_F9,
    TAM_KEY_F10,
    TAM_KEY_F11,
    TAM_KEY_F12
} tam_key_type_t;

enum {
    TAM_MOD_SHIFT = 1u << 0,
    TAM_MOD_ALT   = 1u << 1,
    TAM_MOD_CTRL  = 1u << 2
};

typedef struct {
    tam_key_type_t type;
    uint32_t       codepoint; /* TAM_KEY_CHAR only */
    unsigned       mods;      /* TAM_MOD_* */
} tam_key_event_t;

#define TAM_KEY_BUF_SIZE 64

typedef struct {
    unsigned char buf[TAM_KEY_BUF_SIZE];
    size_t        len;
} tam_key_parser_t;

void tam_key_init(tam_key_parser_t *p);

/* Append bytes read from the terminal. Returns 0, or -1 if the pending input
 * would exceed the internal buffer (the caller should decode first). */
int tam_key_feed(tam_key_parser_t *p, const void *data, size_t len);

/* Decode the next event. Returns 1 when an event is available, 0 when more
 * bytes are needed, and -1 on an internal error. Unsupported sequences are
 * consumed and decoding continues. */
int tam_key_next(tam_key_parser_t *p, tam_key_event_t *ev);

/* After an escape timeout, turn a pending lone ESC into TAM_KEY_ESC. Returns
 * 1 when it produced an event, 0 otherwise. */
int tam_key_timeout(tam_key_parser_t *p, tam_key_event_t *ev);

/* Bytes still buffered, normally zero after decoding. */
size_t tam_key_pending(const tam_key_parser_t *p);

#ifdef __cplusplus
}
#endif

#endif
