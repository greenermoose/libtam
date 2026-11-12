#include "tam.h"
#include "tam_key.h"
#include "tam_tty.h"
#include "tam_uni.h"

#include <assert.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void test_utf8(void)
{
    uint32_t cp = 0;
    char out[4];

    assert(tam_utf8_decode("A", 1, &cp) == 1 && cp == 'A');
    assert(tam_utf8_decode("\xc3\xa9", 2, &cp) == 2 && cp == 0x00E9);
    assert(tam_utf8_decode("\xe2\x82\xac", 3, &cp) == 3 && cp == 0x20AC);
    assert(tam_utf8_decode("\xf0\x9f\x98\x80", 4, &cp) == 4 && cp == 0x1F600);

    /* Truncated and invalid sequences consume one byte and yield U+FFFD. */
    assert(tam_utf8_decode("\xe2\x82", 2, &cp) == 1 && cp == 0xFFFD);
    assert(tam_utf8_decode("\xc3\x28", 2, &cp) == 1 && cp == 0xFFFD);
    assert(tam_utf8_decode("\x80", 1, &cp) == 1 && cp == 0xFFFD);

    assert(tam_utf8_encode('A', out) == 1 && out[0] == 'A');
    assert(tam_utf8_encode(0x00E9, out) == 2 &&
           memcmp(out, "\xc3\xa9", 2) == 0);
    assert(tam_utf8_encode(0x20AC, out) == 3 &&
           memcmp(out, "\xe2\x82\xac", 3) == 0);
    assert(tam_utf8_encode(0x1F600, out) == 4 &&
           memcmp(out, "\xf0\x9f\x98\x80", 4) == 0);

    /* Surrogates and out-of-range values become U+FFFD. */
    assert(tam_utf8_encode(0xD800, out) == 3 &&
           memcmp(out, "\xef\xbf\xbd", 3) == 0);
    assert(tam_utf8_encode(0x110000, out) == 3 &&
           memcmp(out, "\xef\xbf\xbd", 3) == 0);

    printf("  [PASS] test_utf8\n");
}

static void test_width(void)
{
    assert(tam_uni_width('A') == 1);
    assert(tam_uni_width(0x0301) == 0); /* combining acute */
    assert(tam_uni_width(0x4E2D) == 2); /* CJK ideograph */
    assert(tam_uni_width(0x09) == 0);   /* tab is control */

    assert(tam_uni_str_width("abc", 3) == 3);
    assert(tam_uni_str_width("a\xcc\x81", 3) == 1);
    assert(tam_uni_str_width("\xe4\xb8\xad\xe6\x96\x87", 6) == 4);

    char buf[16];
    int used = tam_uni_fit("abcdef", 6, 4, buf, sizeof buf);
    assert(used == 4 && strcmp(buf, "abcd") == 0);

    used = tam_uni_fit("\xe4\xb8\xad\xe6\x96\x87", 6, 3, buf, sizeof buf);
    assert(used == 2 && strcmp(buf, "\xe4\xb8\xad") == 0);

    used = tam_uni_fit("\xe4\xb8\xad\xe6\x96\x87", 6, 0, buf, sizeof buf);
    assert(used == 0 && buf[0] == '\0');

    printf("  [PASS] test_width\n");
}

static void check_key(tam_key_parser_t *p, tam_key_type_t type,
                      uint32_t cp, unsigned mods, const char *what)
{
    tam_key_event_t ev;
    int rc = tam_key_next(p, &ev);
    if (rc != 1 || ev.type != type || ev.codepoint != cp || ev.mods != mods) {
        fprintf(stderr, "  [FAIL] key %s: rc=%d type=%d cp=U+%04X mods=%u\n",
                what, rc, rc == 1 ? (int)ev.type : -1,
                rc == 1 ? ev.codepoint : 0, rc == 1 ? ev.mods : 0);
        abort();
    }
}

static void test_keys(void)
{
    tam_key_parser_t p;

    tam_key_init(&p);
    assert(tam_key_feed(&p, "a", 1) == 0);
    check_key(&p, TAM_KEY_CHAR, 'a', 0, "plain a");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\r\n\t\x7f", 4) == 0);
    check_key(&p, TAM_KEY_ENTER, 0, 0, "CR");
    check_key(&p, TAM_KEY_ENTER, 0, 0, "LF");
    check_key(&p, TAM_KEY_TAB, 0, 0, "tab");
    check_key(&p, TAM_KEY_BACKSPACE, 0, 0, "DEL");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x01\x1a", 2) == 0);
    check_key(&p, TAM_KEY_CHAR, 'a', TAM_MOD_CTRL, "Ctrl-A");
    check_key(&p, TAM_KEY_CHAR, 'z', TAM_MOD_CTRL, "Ctrl-Z");

    /* Arrows may arrive split across reads. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b", 1) == 0);
    assert(tam_key_next(&p, &(tam_key_event_t){0}) == 0);
    assert(tam_key_feed(&p, "[", 1) == 0);
    assert(tam_key_next(&p, &(tam_key_event_t){0}) == 0);
    assert(tam_key_feed(&p, "A", 1) == 0);
    check_key(&p, TAM_KEY_UP, 0, 0, "up");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[B\x1b[C\x1b[D", 9) == 0);
    check_key(&p, TAM_KEY_DOWN, 0, 0, "down");
    check_key(&p, TAM_KEY_RIGHT, 0, 0, "right");
    check_key(&p, TAM_KEY_LEFT, 0, 0, "left");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[H\x1b[F", 6) == 0);
    check_key(&p, TAM_KEY_HOME, 0, 0, "home");
    check_key(&p, TAM_KEY_END, 0, 0, "end");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[5~\x1b[6~\x1b[3~\x1b[2~", 16) == 0);
    check_key(&p, TAM_KEY_PAGE_UP, 0, 0, "page up");
    check_key(&p, TAM_KEY_PAGE_DOWN, 0, 0, "page down");
    check_key(&p, TAM_KEY_DELETE, 0, 0, "delete");
    check_key(&p, TAM_KEY_INSERT, 0, 0, "insert");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[1;5A", 6) == 0);
    check_key(&p, TAM_KEY_UP, 0, TAM_MOD_CTRL, "ctrl-up");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[Z", 3) == 0);
    check_key(&p, TAM_KEY_TAB, 0, TAM_MOD_SHIFT, "shift-tab");

    /* SS3 form, used by xterm in application cursor mode. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1bOA\x1bOP", 6) == 0);
    check_key(&p, TAM_KEY_UP, 0, 0, "SS3 up");
    check_key(&p, TAM_KEY_F1, 0, 0, "SS3 F1");

    /* Alt-prefixed text. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b" "b", 2) == 0);
    check_key(&p, TAM_KEY_CHAR, 'b', TAM_MOD_ALT, "alt-b");

    /* UTF-8 text passes through as one character. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\xc3\xa9", 2) == 0);
    check_key(&p, TAM_KEY_CHAR, 0x00E9, 0, "e acute");

    /* A lone ESC becomes ESC only after the timeout. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b", 1) == 0);
    tam_key_event_t ev;
    assert(tam_key_next(&p, &ev) == 0);
    assert(tam_key_timeout(&p, &ev) == 1);
    assert(ev.type == TAM_KEY_ESC);

    /* Unknown sequences are consumed without wedging the stream. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[99~z", 6) == 0);
    check_key(&p, TAM_KEY_CHAR, 'z', 0, "after unknown");

    printf("  [PASS] test_keys\n");
}

/* Enter raw mode on a pseudo-terminal and confirm the saved settings are
 * restored exactly. */
static void test_tty_raw(void)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0);
    assert(grantpt(master) == 0);
    assert(unlockpt(master) == 0);

    const char *name = ptsname(master);
    assert(name != NULL);

    int slave = open(name, O_RDWR | O_NOCTTY);
    assert(slave >= 0);

    tam_tty_t t;
    assert(tam_tty_open(&t, slave, slave) == 0);
    assert(t.interactive);

    struct termios before;
    assert(tcgetattr(slave, &before) == 0);

    assert(tam_tty_raw(&t, true) == 0);
    struct termios raw;
    assert(tcgetattr(slave, &raw) == 0);
    assert((raw.c_lflag & (ICANON | ECHO)) == 0);
    assert((raw.c_iflag & IXON) == 0);

    /* Bytes written to the master appear on the slave. */
    assert(write(master, "x", 1) == 1);
    char byte = 0;
    assert(tam_tty_read(&t, &byte, 1, 1000) == 1);
    assert(byte == 'x');

    assert(tam_tty_raw(&t, false) == 0);
    struct termios after;
    assert(tcgetattr(slave, &after) == 0);
    assert(after.c_lflag == before.c_lflag);
    assert(after.c_iflag == before.c_iflag);
    assert(after.c_oflag == before.c_oflag);

    tam_tty_close(&t);
    close(slave);
    close(master);
    printf("  [PASS] test_tty_raw\n");
}

static void test_tty_capabilities(void)
{
    /* Non-interactive descriptors still report a usable size and never emit
     * control sequences. */
    tam_tty_t t;
    assert(tam_tty_open(&t, -1, -1) == -1);

    int devnull = open("/dev/null", O_RDWR);
    assert(devnull >= 0);
    assert(tam_tty_open(&t, devnull, devnull) == 0);
    assert(!t.interactive);

    int rows = 0, cols = 0;
    assert(tam_tty_size(&t, &rows, &cols) == 0);
    assert(rows == 24 && cols == 80);

    assert(tam_tty_frame_begin(&t) == 0);
    assert(tam_tty_alt_screen(&t, true) == 0);
    assert(tam_tty_alt_screen(&t, true) == 0); /* idempotent */
    assert(tam_tty_alt_screen(&t, false) == 0);

    tam_tty_close(&t);
    close(devnull);
    printf("  [PASS] test_tty_capabilities\n");
}

int main(void)
{
    /* tam_uni_width depends on the locale's character tables. */
    setlocale(LC_CTYPE, "");

    printf("libtam terminal tests (%s)\n", tam_version());
    test_utf8();
    test_width();
    test_keys();
    test_tty_raw();
    test_tty_capabilities();
    printf("terminal tests passed\n");
    return 0;
}
