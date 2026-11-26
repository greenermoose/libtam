#include "tam_key.h"
#include "tam_uni.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
    size_t room = sizeof(p->buf) - p->len;
    if (len > room) len = room;
    if (len > 0) memcpy(p->buf + p->len, data, len);
    p->len += len;
    return (int)len;
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

/* SGR mouse report parameters "b;col;row" (after "ESC [ <"). */
static int mouse_event(const unsigned char *s, size_t n, bool release,
                       tam_key_event_t *ev)
{
    int params[3];
    if (parse_params(s, n, params, 3) != 3) return -1;
    int b = params[0];
    ev->type = TAM_KEY_MOUSE;
    ev->mods = ((b & 4) ? TAM_MOD_SHIFT : 0) | ((b & 8) ? TAM_MOD_ALT : 0) |
               ((b & 16) ? TAM_MOD_CTRL : 0);
    ev->motion = (b & 32) != 0;
    int base = b & ~(4 | 8 | 16 | 32);
    if (base >= 64 && base < 128) ev->button = TAM_MOUSE_WHEEL_UP + (base & 3);
    else if (base < 64) ev->button = base & 3;
    else ev->button = TAM_MOUSE_NONE; /* extra buttons are not reported */
    ev->col = params[1];
    ev->row = params[2];
    ev->release = release;
    return 1;
}

static const char paste_end[] = "\x1b[201~";
#define PASTE_END_LEN (sizeof(paste_end) - 1)

/* In a bracketed paste: hand out the bytes up to the end marker, holding
 * back a tail that may be the start of the marker. */
static int paste_next(tam_key_parser_t *p, tam_key_event_t *ev)
{
    size_t emit = p->len;
    for (size_t k = 0; k < p->len; k++) {
        if (p->buf[k] != 0x1B) continue;
        size_t left = p->len - k;
        size_t cmp = left < PASTE_END_LEN ? left : PASTE_END_LEN;
        if (memcmp(p->buf + k, paste_end, cmp) == 0) {
            emit = k;
            break;
        }
    }
    if (emit == 0) {
        if (p->len < PASTE_END_LEN) return 0; /* the marker is still arriving */
        consume(p, PASTE_END_LEN);
        p->in_paste = false;
        ev->type = TAM_KEY_PASTE_END;
        return 1;
    }
    memcpy(p->paste, p->buf, emit);
    consume(p, emit);
    ev->type = TAM_KEY_PASTE;
    ev->text = p->paste;
    ev->len = emit;
    return 1;
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

        if (i > 2 && p->buf[2] == '<' && (p->buf[i] == 'M' || p->buf[i] == 'm')) {
            int result = mouse_event(p->buf + 3, i - 3, p->buf[i] == 'm', ev);
            consume(p, i + 1);
            return result > 0 ? 1 : -1;
        }
        if (p->buf[i] == '~' && i == 5 && memcmp(p->buf + 2, "200", 3) == 0) {
            consume(p, i + 1);
            p->in_paste = true;
            ev->type = TAM_KEY_PASTE_BEGIN;
            return 1;
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
    memset(ev, 0, sizeof(*ev));

    for (;;) {
        if (p->len == 0) return 0;
        if (p->in_paste) return paste_next(p, ev);

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
    memset(ev, 0, sizeof(*ev));
    /* In a paste a pending ESC may start the end marker; keep waiting. */
    if (p->in_paste) return 0;
    if (p->len == 1 && p->buf[0] == 0x1B) {
        ev->type = TAM_KEY_ESC;
        ev->codepoint = 0;
        ev->mods = 0;
        consume(p, 1);
        return 1;
    }
    return 0;
}

/* ---- Key names ---------------------------------------------------------- */

static const struct {
    const char    *name;
    tam_key_type_t type;
    uint32_t       cp;
    unsigned       mods;
} key_names[] = {
    /* The first name of a key is the one tam_key_name_format writes. */
    {"Up", TAM_KEY_UP, 0, 0},           {"Down", TAM_KEY_DOWN, 0, 0},
    {"Left", TAM_KEY_LEFT, 0, 0},       {"Right", TAM_KEY_RIGHT, 0, 0},
    {"Home", TAM_KEY_HOME, 0, 0},       {"End", TAM_KEY_END, 0, 0},
    {"PageUp", TAM_KEY_PAGE_UP, 0, 0},  {"PgUp", TAM_KEY_PAGE_UP, 0, 0},
    {"PageDown", TAM_KEY_PAGE_DOWN, 0, 0}, {"PgDn", TAM_KEY_PAGE_DOWN, 0, 0},
    {"Enter", TAM_KEY_ENTER, 0, 0},     {"Return", TAM_KEY_ENTER, 0, 0},
    {"Tab", TAM_KEY_TAB, 0, 0},         {"Backtab", TAM_KEY_TAB, 0, TAM_MOD_SHIFT},
    {"Backspace", TAM_KEY_BACKSPACE, 0, 0},
    {"Insert", TAM_KEY_INSERT, 0, 0},   {"Ins", TAM_KEY_INSERT, 0, 0},
    {"Delete", TAM_KEY_DELETE, 0, 0},   {"Del", TAM_KEY_DELETE, 0, 0},
    {"Escape", TAM_KEY_ESC, 0, 0},      {"Esc", TAM_KEY_ESC, 0, 0},
    {"Space", TAM_KEY_CHAR, ' ', 0},
};

static const char us_plain[] = "`1234567890-=[]\\;',./";
static const char us_shifted[] = "~!@#$%^&*()_+{}|:\"<>?";

bool tam_key_name_parse(const char *name, tam_key_event_t *ev)
{
    if (name == NULL || ev == NULL) return false;
    while (*name == ' ' || *name == '\t') name++;
    size_t len = strlen(name);
    while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\t')) len--;
    if (len == 0 || len >= 64) return false;
    char buf[64];
    memcpy(buf, name, len);
    buf[len] = '\0';

    unsigned mods = 0;
    const char *p = buf;
    for (;;) {
        static const struct { const char *word; size_t n; unsigned mod; } mod_words[] = {
            {"ctrl", 4, TAM_MOD_CTRL}, {"alt", 3, TAM_MOD_ALT},
            {"shift", 5, TAM_MOD_SHIFT}, {"meta", 4, 0},
        };
        bool matched = false;
        for (size_t i = 0; i < sizeof(mod_words) / sizeof(mod_words[0]); i++) {
            size_t n = mod_words[i].n;
            if (strncasecmp(p, mod_words[i].word, n) == 0 &&
                (p[n] == '+' || p[n] == '-') && p[n + 1] != '\0') {
                if (mod_words[i].mod == 0) return false; /* Meta */
                mods |= mod_words[i].mod;
                p += n + 1;
                matched = true;
                break;
            }
        }
        if (!matched) break;
    }

    memset(ev, 0, sizeof(*ev));
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
        if (strcasecmp(p, key_names[i].name) == 0) {
            ev->type = key_names[i].type;
            ev->codepoint = key_names[i].cp;
            ev->mods = mods | key_names[i].mods;
            return true;
        }
    }
    if ((p[0] == 'F' || p[0] == 'f') && isdigit((unsigned char)p[1])) {
        char *end = NULL;
        long n = strtol(p + 1, &end, 10);
        if (*end != '\0' || n < 1 || n > 12) return false;
        ev->type = (tam_key_type_t)(TAM_KEY_F1 + (n - 1));
        ev->mods = mods;
        return true;
    }

    uint32_t cp;
    if ((p[0] == 'U' || p[0] == 'u') && p[1] == '+' && isxdigit((unsigned char)p[2])) {
        char *end = NULL;
        unsigned long v = strtoul(p + 2, &end, 16);
        if (*end != '\0' || v > 0x10FFFF) return false;
        cp = (uint32_t)v;
    } else {
        size_t plen = strlen(p);
        size_t used = tam_utf8_decode(p, plen, &cp);
        if (used != plen || (cp == 0xFFFD && used == 1 && (unsigned char)p[0] >= 0x80))
            return false;
    }
    if (cp < 0x20 || cp == 0x7F) return false;

    ev->type = TAM_KEY_CHAR;
    if (cp < 0x80) {
        const char *at;
        if (mods & (TAM_MOD_CTRL | TAM_MOD_ALT)) {
            /* With Ctrl or Alt a letter's case is not significant; Shift is
             * spelled out. Alt+Shift+letter arrives as the capital. */
            if (cp >= 'A' && cp <= 'Z') cp = cp - 'A' + 'a';
            if ((mods & TAM_MOD_SHIFT) && !(mods & TAM_MOD_CTRL)) {
                at = strchr(us_plain, (int)cp);
                if (cp >= 'a' && cp <= 'z') cp = cp - 'a' + 'A';
                else if (at != NULL && cp != 0) cp = (unsigned char)us_shifted[at - us_plain];
                mods &= ~(unsigned)TAM_MOD_SHIFT;
            }
            if ((mods & TAM_MOD_CTRL) && cp == '\\') cp = '|'; /* 0x1C */
        } else if (mods & TAM_MOD_SHIFT) {
            at = strchr(us_plain, (int)cp);
            if (cp >= 'a' && cp <= 'z') cp = cp - 'a' + 'A';
            else if (at != NULL && cp != 0) cp = (unsigned char)us_shifted[at - us_plain];
            mods &= ~(unsigned)TAM_MOD_SHIFT;
        }
    }
    ev->codepoint = cp;
    ev->mods = mods;
    return true;
}

size_t tam_key_name_format(const tam_key_event_t *ev, char *buf, size_t size)
{
    char name[24] = "";
    unsigned mods = ev ? ev->mods : 0;
    const char *kname = NULL;
    if (ev == NULL) {
        kname = "Unknown";
    } else if (ev->type == TAM_KEY_CHAR) {
        uint32_t cp = ev->codepoint;
        if (cp == ' ') {
            kname = "Space";
        } else if (cp >= 'A' && cp <= 'Z') {
            /* A capital is Shift+letter. */
            mods |= TAM_MOD_SHIFT;
            snprintf(name, sizeof name, "%c", (char)cp);
        } else if (cp >= 'a' && cp <= 'z') {
            snprintf(name, sizeof name, "%c", (char)(mods ? cp - 'a' + 'A' : cp));
        } else if ((mods & TAM_MOD_CTRL) && cp == '|') {
            snprintf(name, sizeof name, "\\");
        } else if (cp > 0x20 && cp < 0x7F) {
            snprintf(name, sizeof name, "%c", (char)cp);
        } else {
            snprintf(name, sizeof name, "U+%04X", (unsigned)cp);
        }
        if (kname == NULL) kname = name;
    } else if (ev->type >= TAM_KEY_F1 && ev->type <= TAM_KEY_F12) {
        snprintf(name, sizeof name, "F%d", (int)(ev->type - TAM_KEY_F1 + 1));
        kname = name;
    } else {
        for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
            if (key_names[i].type == ev->type && key_names[i].cp == 0 &&
                key_names[i].mods == 0) {
                kname = key_names[i].name;
                break;
            }
        }
        if (kname == NULL) kname = "Unknown";
    }
    int n = snprintf(buf, size, "%s%s%s%s",
                     (mods & TAM_MOD_CTRL) ? "Ctrl+" : "",
                     (mods & TAM_MOD_ALT) ? "Alt+" : "",
                     (mods & TAM_MOD_SHIFT) ? "Shift+" : "", kname);
    return n < 0 ? 0 : (size_t)n;
}
