#include "tam_key.h"
#include "tam_uni.h"

#include <string.h>

static void consume(tam_key_parser_t *p, size_t n)
{
    if (n >= p->len) {
        p->len = 0;
        return;
    }
    memmove(p->buf, p->buf + n, p->len - n);
    p->len -= n;
}

void tam_key_init(tam_key_parser_t *p)
{
    if (p != NULL) memset(p, 0, sizeof(*p));
}

size_t tam_key_pending(const tam_key_parser_t *p)
{
    return p == NULL ? 0 : p->len;
}

int tam_key_feed(tam_key_parser_t *p, const void *data, size_t len)
{
    if (p == NULL || (data == NULL && len > 0)) return -1;
    if (len == 0) return 0;
    if (len > sizeof(p->buf) - p->len) return -1;
    memcpy(p->buf + p->len, data, len);
    p->len += len;
    return 0;
}

/* Map one control byte. Returns 0 when the byte is ordinary text. */
static int byte_event(unsigned char b, unsigned mods, tam_key_event_t *ev)
{
    ev->codepoint = 0;
    ev->mods = mods;

    switch (b) {
    case '\r':
    case '\n':
        ev->type = TAM_KEY_ENTER;
        return 1;
    case '\t':
        ev->type = TAM_KEY_TAB;
        return 1;
    case 0x08:
    case 0x7F:
        ev->type = TAM_KEY_BACKSPACE;
        return 1;
    default:
        break;
    }

    if (b < 0x20) {
        ev->type = TAM_KEY_CHAR;
        ev->mods = mods | TAM_MOD_CTRL;
        ev->codepoint = b == 0 ? (uint32_t)' ' : (uint32_t)('a' + (b - 1));
        return 1;
    }

    return 0;
}

/* Decode one UTF-8 character. Returns 0 when the sequence is incomplete. */
static int utf8_event(const unsigned char *s, size_t len, size_t *used,
                      tam_key_event_t *ev)
{
    if (len == 0) return 0;

    if (s[0] < 0x80) {
        *used = 1;
        ev->type = TAM_KEY_CHAR;
        ev->codepoint = s[0];
        ev->mods = 0;
        return 1;
    }

    size_t need = tam_utf8_seq_len(s[0]);
    if (need == 0) {
        *used = 1;
        ev->type = TAM_KEY_CHAR;
        ev->codepoint = 0xFFFD;
        ev->mods = 0;
        return 1;
    }
    if (len < need) return 0;

    uint32_t cp = 0xFFFD;
    size_t n = tam_utf8_decode((const char *)s, len, &cp);
    *used = n == 0 ? 1 : n;
    ev->type = TAM_KEY_CHAR;
    ev->codepoint = cp;
    ev->mods = 0;
    return 1;
}

static unsigned mods_from_param(int value)
{
    if (value <= 1) return 0;
    unsigned bits = (unsigned)(value - 1);
    unsigned mods = 0;
    if (bits & 1u) mods |= TAM_MOD_SHIFT;
    if (bits & 2u) mods |= TAM_MOD_ALT;
    if (bits & 4u) mods |= TAM_MOD_CTRL;
    return mods;
}

static int parse_params(const unsigned char *s, size_t n, int *params, int max)
{
    int count = 0;
    int value = 0;
    bool have = false;

    for (size_t i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (c >= '0' && c <= '9') {
            if (value < 1000000) value = value * 10 + (c - '0');
            have = true;
        } else if (c == ';') {
            if (count < max) params[count] = have ? value : 0;
            count++;
            value = 0;
            have = false;
        } else {
            return -1;
        }
    }

    if (have || count > 0) {
        if (count < max) params[count] = have ? value : 0;
        count++;
    }

    return count > max ? max : count;
}

static int csi_event(const unsigned char *params_bytes, size_t params_len,
                     unsigned char final, tam_key_event_t *ev)
{
    int params[8];
    int np = parse_params(params_bytes, params_len, params, 8);
    if (np < 0) return -1;

    unsigned mods = np >= 2 ? mods_from_param(params[1]) : 0;
    ev->codepoint = 0;
    ev->mods = mods;

    switch (final) {
    case 'A': ev->type = TAM_KEY_UP; return 1;
    case 'B': ev->type = TAM_KEY_DOWN; return 1;
    case 'C': ev->type = TAM_KEY_RIGHT; return 1;
    case 'D': ev->type = TAM_KEY_LEFT; return 1;
    case 'H': ev->type = TAM_KEY_HOME; return 1;
    case 'F': ev->type = TAM_KEY_END; return 1;
    case 'Z':
        ev->type = TAM_KEY_TAB;
        ev->mods = mods | TAM_MOD_SHIFT;
        return 1;
    case '~':
        if (np < 1) return -1;
        switch (params[0]) {
        case 1: ev->type = TAM_KEY_HOME; return 1;
        case 2: ev->type = TAM_KEY_INSERT; return 1;
        case 3: ev->type = TAM_KEY_DELETE; return 1;
        case 4: ev->type = TAM_KEY_END; return 1;
        case 5: ev->type = TAM_KEY_PAGE_UP; return 1;
        case 6: ev->type = TAM_KEY_PAGE_DOWN; return 1;
        case 7: ev->type = TAM_KEY_HOME; return 1;
        case 8: ev->type = TAM_KEY_END; return 1;
        case 11: ev->type = TAM_KEY_F1; return 1;
        case 12: ev->type = TAM_KEY_F2; return 1;
        case 13: ev->type = TAM_KEY_F3; return 1;
        case 14: ev->type = TAM_KEY_F4; return 1;
        case 15: ev->type = TAM_KEY_F5; return 1;
        case 17: ev->type = TAM_KEY_F6; return 1;
        case 18: ev->type = TAM_KEY_F7; return 1;
        case 19: ev->type = TAM_KEY_F8; return 1;
        case 20: ev->type = TAM_KEY_F9; return 1;
        case 21: ev->type = TAM_KEY_F10; return 1;
        case 23: ev->type = TAM_KEY_F11; return 1;
        case 24: ev->type = TAM_KEY_F12; return 1;
        default: return -1;
        }
    default:
        return -1;
    }
}

static int ss3_event(unsigned char final, tam_key_event_t *ev)
{
    ev->codepoint = 0;
    ev->mods = 0;
    switch (final) {
    case 'A': ev->type = TAM_KEY_UP; return 1;
    case 'B': ev->type = TAM_KEY_DOWN; return 1;
    case 'C': ev->type = TAM_KEY_RIGHT; return 1;
    case 'D': ev->type = TAM_KEY_LEFT; return 1;
    case 'H': ev->type = TAM_KEY_HOME; return 1;
    case 'F': ev->type = TAM_KEY_END; return 1;
    case 'P': ev->type = TAM_KEY_F1; return 1;
    case 'Q': ev->type = TAM_KEY_F2; return 1;
    case 'R': ev->type = TAM_KEY_F3; return 1;
    case 'S': ev->type = TAM_KEY_F4; return 1;
    default: return -1;
    }
}

/* Parse a sequence beginning with ESC. Returns 1 with an event, 0 when more
 * bytes are needed, and -1 when an unsupported sequence was consumed. */
static int escape_event(tam_key_parser_t *p, tam_key_event_t *ev)
{
    if (p->len < 2) return 0;

    unsigned char second = p->buf[1];

    if (second == '[') {
        size_t i = 2;
        while (i < p->len) {
            unsigned char c = p->buf[i];
            if (c >= 0x40 && c <= 0x7E) break;
            if (c < 0x20 || c > 0x3F) {
                ev->type = TAM_KEY_ESC;
                ev->codepoint = 0;
                ev->mods = 0;
                consume(p, 1);
                return 1;
            }
            i++;
        }

        if (i == p->len) {
            if (p->len >= TAM_KEY_BUF_SIZE) {
                ev->type = TAM_KEY_ESC;
                ev->codepoint = 0;
                ev->mods = 0;
                consume(p, 1);
                return 1;
            }
            return 0;
        }

        int result = csi_event(p->buf + 2, i - 2, p->buf[i], ev);
        consume(p, i + 1);
        return result > 0 ? 1 : -1;
    }

    if (second == 'O') {
        if (p->len < 3) return 0;
        int result = ss3_event(p->buf[2], ev);
        consume(p, 3);
        return result > 0 ? 1 : -1;
    }

    if (second == 0x1B) {
        ev->type = TAM_KEY_ESC;
        ev->codepoint = 0;
        ev->mods = 0;
        consume(p, 1);
        return 1;
    }

    if (byte_event(second, TAM_MOD_ALT, ev)) {
        consume(p, 2);
        return 1;
    }

    size_t used = 0;
    if (!utf8_event(p->buf + 1, p->len - 1, &used, ev)) return 0;
    ev->mods |= TAM_MOD_ALT;
    consume(p, used + 1);
    return 1;
}

int tam_key_next(tam_key_parser_t *p, tam_key_event_t *ev)
{
    if (p == NULL || ev == NULL) return -1;

    for (;;) {
        if (p->len == 0) return 0;

        if (p->buf[0] == 0x1B) {
            int result = escape_event(p, ev);
            if (result > 0) return 1;
            if (result == 0) return 0;
            continue;
        }

        if (p->buf[0] < 0x80) {
            if (byte_event(p->buf[0], 0, ev)) {
                consume(p, 1);
                return 1;
            }
            ev->type = TAM_KEY_CHAR;
            ev->codepoint = p->buf[0];
            ev->mods = 0;
            consume(p, 1);
            return 1;
        }

        size_t used = 0;
        if (!utf8_event(p->buf, p->len, &used, ev)) return 0;
        consume(p, used);
        return 1;
    }
}

int tam_key_timeout(tam_key_parser_t *p, tam_key_event_t *ev)
{
    if (p == NULL || ev == NULL) return 0;
    if (p->len == 1 && p->buf[0] == 0x1B) {
        ev->type = TAM_KEY_ESC;
        ev->codepoint = 0;
        ev->mods = 0;
        consume(p, 1);
        return 1;
    }
    return 0;
}
