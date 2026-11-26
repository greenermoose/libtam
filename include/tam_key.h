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
    TAM_KEY_F12,
    /* Bracketed paste (tam_tty_paste): BEGIN, then PASTE events carrying the
     * pasted bytes as they arrive (`text`, `len`; a UTF-8 sequence may span
     * two events), then END. Pasted bytes are never decoded as keys. */
    TAM_KEY_PASTE_BEGIN,
    TAM_KEY_PASTE,
    TAM_KEY_PASTE_END,
    /* SGR mouse report (tam_tty_mouse): `button`, `col`, `row`, `release`,
     * `motion`, and the modifiers held. */
    TAM_KEY_MOUSE
} tam_key_type_t;

enum tam_key_mod {
    TAM_MOD_SHIFT = 1u << 0,
    TAM_MOD_ALT   = 1u << 1,
    TAM_MOD_CTRL  = 1u << 2
};

/* Mouse buttons in TAM_KEY_MOUSE events. */
enum tam_mouse_button {
    TAM_MOUSE_LEFT = 0,
    TAM_MOUSE_MIDDLE = 1,
    TAM_MOUSE_RIGHT = 2,
    TAM_MOUSE_NONE = 3,        /* motion with no button held */
    TAM_MOUSE_WHEEL_UP = 4,
    TAM_MOUSE_WHEEL_DOWN = 5,
    TAM_MOUSE_WHEEL_LEFT = 6,
    TAM_MOUSE_WHEEL_RIGHT = 7
};

typedef struct {
    tam_key_type_t type;
    uint32_t       codepoint; /* TAM_KEY_CHAR only */
    unsigned       mods;      /* TAM_MOD_* */
    const char    *text;      /* TAM_KEY_PASTE: bytes, valid until the next */
    size_t         len;       /* call on this parser */
    int            button;    /* TAM_KEY_MOUSE: TAM_MOUSE_* */
    int            col, row;  /* TAM_KEY_MOUSE: cell, counted from 1 */
    bool           release;   /* TAM_KEY_MOUSE: button released */
    bool           motion;    /* TAM_KEY_MOUSE: the pointer moved */
} tam_key_event_t;

#define TAM_KEY_BUF_SIZE 64

typedef struct {
    unsigned char buf[TAM_KEY_BUF_SIZE];
    size_t        len;
    bool          in_paste;
    char          paste[TAM_KEY_BUF_SIZE]; /* the bytes of the last PASTE */
} tam_key_parser_t;

void tam_key_init(tam_key_parser_t *p);

/* Append bytes read from the terminal. The parser holds TAM_KEY_BUF_SIZE
 * bytes, so it may take only part of `len`: it returns how many bytes it
 * took (0 when full), or -1 for a NULL parser or data. Take events with
 * tam_key_next, then feed the rest:
 *     while (len > 0) {
 *         int n = tam_key_feed(&p, data, len);
 *         if (n < 0) break;
 *         data += n; len -= (size_t)n;
 *         while (tam_key_next(&p, &ev) == 1) handle(&ev);
 *     } */
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

/* Key names for configuration files and help text, such as "Ctrl+S",
 * "Alt+Shift+A", "F5", "PageUp", "Space", "Shift+Tab" or "U+00E9".
 * Modifier names (Ctrl, Alt, Shift, case-insensitive) join with '+' or '-'.
 * With Ctrl or Alt a letter's case does not matter and Shift is written out;
 * a lone character is taken literally, so "A" is Shift+a. Shifted US
 * punctuation folds as the terminal sends it: "Shift+1" is "!". "Meta" is
 * refused: Meta belongs to the compositor.
 * tam_key_name_parse fills type, codepoint and mods with exactly what
 * tam_key_next reports for that key, so bindings compare field by field.
 * False for a name it does not know. */
bool tam_key_name_parse(const char *name, tam_key_event_t *ev);

/* The canonical name of a key event, as tam_key_name_parse reads it back
 * ("Ctrl+Alt+Shift+X" order). Returns the length it needed; the output is
 * cut, but always terminated, when that is size or more. */
size_t tam_key_name_format(const tam_key_event_t *ev, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif
