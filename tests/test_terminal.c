#include "tam.h"
#include "tam_key.h"
#include "tam_pager.h"
#include "tam_tty.h"
#include "tam_uni.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>

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
    assert(tam_key_feed(&p, "a", 1) == (int)(1));
    check_key(&p, TAM_KEY_CHAR, 'a', 0, "plain a");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\r\n\t\x7f", 4) == (int)(4));
    check_key(&p, TAM_KEY_ENTER, 0, 0, "CR");
    check_key(&p, TAM_KEY_ENTER, 0, 0, "LF");
    check_key(&p, TAM_KEY_TAB, 0, 0, "tab");
    check_key(&p, TAM_KEY_BACKSPACE, 0, 0, "DEL");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x01\x1a", 2) == (int)(2));
    check_key(&p, TAM_KEY_CHAR, 'a', TAM_MOD_CTRL, "Ctrl-A");
    check_key(&p, TAM_KEY_CHAR, 'z', TAM_MOD_CTRL, "Ctrl-Z");

    /* Arrows may arrive split across reads. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b", 1) == (int)(1));
    assert(tam_key_next(&p, &(tam_key_event_t){0}) == 0);
    assert(tam_key_feed(&p, "[", 1) == (int)(1));
    assert(tam_key_next(&p, &(tam_key_event_t){0}) == 0);
    assert(tam_key_feed(&p, "A", 1) == (int)(1));
    check_key(&p, TAM_KEY_UP, 0, 0, "up");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[B\x1b[C\x1b[D", 9) == (int)(9));
    check_key(&p, TAM_KEY_DOWN, 0, 0, "down");
    check_key(&p, TAM_KEY_RIGHT, 0, 0, "right");
    check_key(&p, TAM_KEY_LEFT, 0, 0, "left");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[H\x1b[F", 6) == (int)(6));
    check_key(&p, TAM_KEY_HOME, 0, 0, "home");
    check_key(&p, TAM_KEY_END, 0, 0, "end");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[5~\x1b[6~\x1b[3~\x1b[2~", 16) == (int)(16));
    check_key(&p, TAM_KEY_PAGE_UP, 0, 0, "page up");
    check_key(&p, TAM_KEY_PAGE_DOWN, 0, 0, "page down");
    check_key(&p, TAM_KEY_DELETE, 0, 0, "delete");
    check_key(&p, TAM_KEY_INSERT, 0, 0, "insert");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[1;5A", 6) == (int)(6));
    check_key(&p, TAM_KEY_UP, 0, TAM_MOD_CTRL, "ctrl-up");

    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[Z", 3) == (int)(3));
    check_key(&p, TAM_KEY_TAB, 0, TAM_MOD_SHIFT, "shift-tab");

    /* SS3 form, used by xterm in application cursor mode. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1bOA\x1bOP", 6) == (int)(6));
    check_key(&p, TAM_KEY_UP, 0, 0, "SS3 up");
    check_key(&p, TAM_KEY_F1, 0, 0, "SS3 F1");

    /* Alt-prefixed text. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b" "b", 2) == (int)(2));
    check_key(&p, TAM_KEY_CHAR, 'b', TAM_MOD_ALT, "alt-b");

    /* UTF-8 text passes through as one character. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\xc3\xa9", 2) == (int)(2));
    check_key(&p, TAM_KEY_CHAR, 0x00E9, 0, "e acute");

    /* A lone ESC becomes ESC only after the timeout. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b", 1) == (int)(1));
    tam_key_event_t ev;
    assert(tam_key_next(&p, &ev) == 0);
    assert(tam_key_timeout(&p, &ev) == 1);
    assert(ev.type == TAM_KEY_ESC);

    /* Unknown sequences are consumed without wedging the stream. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[99~z", 6) == (int)(6));
    check_key(&p, TAM_KEY_CHAR, 'z', 0, "after unknown");

    /* A large chunk is taken in part, and the rest after decoding. */
    tam_key_init(&p);
    char many[200];
    memset(many, 'x', sizeof many);
    const char *rest = many;
    size_t left = sizeof many;
    int seen = 0;
    while (left > 0) {
        int took = tam_key_feed(&p, rest, left);
        assert(took >= 0 && (size_t)took <= left);
        assert(took > 0 || tam_key_pending(&p) == TAM_KEY_BUF_SIZE);
        rest += took;
        left -= (size_t)took;
        tam_key_event_t kev;
        while (tam_key_next(&p, &kev) == 1) {
            assert(kev.type == TAM_KEY_CHAR && kev.codepoint == 'x');
            seen++;
        }
    }
    assert(seen == (int)sizeof many);
    assert(tam_key_feed(NULL, "x", 1) == -1);

    /* An unfinished escape sequence that fills the buffer still yields an
     * event, so a feed loop cannot wedge. */
    tam_key_init(&p);
    char csi[TAM_KEY_BUF_SIZE];
    csi[0] = '\x1b';
    csi[1] = '[';
    memset(csi + 2, '1', sizeof csi - 2);
    assert(tam_key_feed(&p, csi, sizeof csi) == (int)sizeof csi);
    assert(tam_key_feed(&p, "q", 1) == 0);
    tam_key_event_t first;
    assert(tam_key_next(&p, &first) == 1 && first.type == TAM_KEY_ESC);
    assert(tam_key_pending(&p) < TAM_KEY_BUF_SIZE);
    assert(tam_key_feed(&p, "q", 1) == 1);

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

static void test_pager_primitives(void)
{
    assert(strcmp(tam_pager_attention_sgr(true), "\033[7m") == 0);
    assert(strcmp(tam_pager_attention_sgr(false), "\033[38;5;15;48;5;236m") == 0);
    assert(strcmp(tam_pager_link_focus_sgr(false), "\033[1;30;46m") == 0);

    /* OSC 8 safety and formatting. */
    assert(tam_osc8_url_safe("https://tamlinux.org", 1024));
    assert(!tam_osc8_url_safe("", 1024));
    assert(!tam_osc8_url_safe("https://bad\033url", 1024));
    assert(!tam_osc8_url_safe("https://bad\nurl", 1024));
    assert(!tam_osc8_url_safe("http://a\xC2\x9B" "31mX", 1024));
    assert(!tam_osc8_url_safe("abcdef", 4));

    char buf[256];
    size_t n = tam_osc8_open(buf, sizeof buf, "https://tamlinux.org");
    assert(n > 0 && strcmp(buf, "\033]8;;https://tamlinux.org\033\\") == 0);
    assert(tam_osc8_open(buf, sizeof buf, "bad\x07url") == 0);
    n = tam_osc8_close(buf, sizeof buf);
    assert(n == 7 && strcmp(buf, "\033]8;;\033\\") == 0);

    /* Footer formatting: attention mode, plain mode, narrow, unknown total, hint. */
    tam_pager_footer_t f = {
        .label = "sandbox/demo.md",
        .has_attn = true,
        .attn_line = 5,
        .top_line = 1,
        .bot_line = 24,
        .total_lines = 85,
        .total_known = true,
        .version = "0.4.0"
    };
    tam_pager_format_footer(buf, sizeof buf, 60, &f);
    assert(strncmp(buf, "sandbox/demo.md | L5 1-24/85", 28) == 0);
    assert(strcmp(buf + strlen(buf) - 6, "v0.4.0") == 0);
    assert(tam_uni_str_width(buf, strlen(buf)) == 60);

    f.has_attn = false;
    f.total_known = false;
    tam_pager_format_footer(buf, sizeof buf, 48, &f);
    assert(strstr(buf, "sandbox/demo.md | 1-24/?") != NULL);
    assert(strcmp(buf + strlen(buf) - 6, "v0.4.0") == 0);

    f.has_attn = true;
    f.total_known = true;
    f.link_hint = "https://tamlinux.org";
    tam_pager_format_footer(buf, sizeof buf, 72, &f);
    assert(strstr(buf, "-> https://tamlinux.org") != NULL);
    assert(strcmp(buf + strlen(buf) - 6, "v0.4.0") == 0);

    /* Narrow terminal keeps line numbers and version visible. */
    f.link_hint = NULL;
    tam_pager_format_footer(buf, sizeof buf, 22, &f);
    assert(strstr(buf, "L5 1-24/85") != NULL);
    assert(strstr(buf, "v0.4.0") != NULL);

    /* URI classification. */
    assert(tam_uri_classify("#section") == TAM_URI_ANCHOR);
    assert(tam_uri_classify("https://tamlinux.org") == TAM_URI_EXTERNAL);
    assert(tam_uri_classify("HTTP://example.com") == TAM_URI_EXTERNAL);
    assert(tam_uri_classify("mailto:fred@example.com") == TAM_URI_EXTERNAL);
    assert(tam_uri_classify("page.html#top") == TAM_URI_LOCAL);
    assert(tam_uri_classify("file:///tmp/a.md") == TAM_URI_LOCAL);
    assert(tam_uri_classify("javascript:alert(1)") == TAM_URI_INVALID);
    assert(tam_uri_classify("data:text/html,hi") == TAM_URI_INVALID);
    assert(tam_uri_classify("bad\nurl") == TAM_URI_INVALID);

    /* Percent decode, local resolution, and slugify. */
    char path[256], frag[128];
    tam_uri_percent_decode("hello%20world%21", 16, path, sizeof path);
    assert(strcmp(path, "hello world!") == 0);

    assert(tam_uri_resolve_local("sandbox/demo.md", "page.html#Check%20List",
                                 path, sizeof path, frag, sizeof frag) == 0);
    assert(strcmp(path, "sandbox/page.html") == 0);
    assert(strcmp(frag, "Check List") == 0);

    assert(tam_uri_resolve_local("sandbox/demo.md", "#features",
                                 path, sizeof path, frag, sizeof frag) == 0);
    assert(path[0] == '\0' && strcmp(frag, "features") == 0);

    assert(tam_uri_resolve_local("sandbox/demo.md", "file:///etc/hosts",
                                 path, sizeof path, frag, sizeof frag) == 0);
    assert(strcmp(path, "/etc/hosts") == 0 && frag[0] == '\0');
    assert(tam_uri_resolve_local("sandbox/demo.md", "file://localhost/etc/hosts",
                                 path, sizeof path, frag, sizeof frag) == 0);
    assert(strcmp(path, "/etc/hosts") == 0);
    assert(tam_uri_resolve_local("sandbox/demo.md", "file://example.com/etc/hosts",
                                 path, sizeof path, frag, sizeof frag) == -1);
    char tiny[4];
    assert(tam_uri_resolve_local("sandbox/demo.md", "page.html#longfrag",
                                 path, sizeof path, tiny, sizeof tiny) == -1);
    assert(tam_uri_resolve_local("sandbox/demo.md", "page.html",
                                 tiny, sizeof tiny, frag, sizeof frag) == -1);

    tam_uri_slugify("Hello, World! (v2.0)", 20, frag, sizeof frag);
    assert(strcmp(frag, "hello-world-v20") == 0);

    char dir[] = "/tmp/libtam-opener-XXXXXX";
    assert(mkdtemp(dir));
    const char *old_path = getenv("PATH");
    char *saved_path = old_path ? strdup(old_path) : NULL;
    assert(!old_path || saved_path);
    assert(setenv("PATH", dir, 1) == 0);
    assert(tam_uri_open_external("https://example.invalid") == -1);
    assert(errno == ENOENT);
    char opener[256];
    snprintf(opener, sizeof opener, "%s/xdg-open", dir);
    assert(symlink("/bin/true", opener) == 0);
    assert(tam_uri_open_external("https://example.invalid") == 0);
    assert(unlink(opener) == 0);
    assert(rmdir(dir) == 0);
    if (saved_path) {
        assert(setenv("PATH", saved_path, 1) == 0);
        free(saved_path);
    } else {
        assert(unsetenv("PATH") == 0);
    }

    printf("  [PASS] test_pager_primitives\n");
}

static int open_pty(int *slave)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    *slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    assert(*slave >= 0);
    return master;
}

/* Read what the slave side wrote to the terminal. */
static size_t drain_master(int master, char *buf, size_t cap)
{
    size_t n = 0;
    struct timespec pause = { 0, 20 * 1000000L };
    nanosleep(&pause, NULL);
    int flags = fcntl(master, F_GETFL);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
    for (;;) {
        ssize_t r = read(master, buf + n, cap - 1 - n);
        if (r <= 0) break;
        n += (size_t)r;
    }
    fcntl(master, F_SETFL, flags);
    buf[n] = '\0';
    return n;
}

static void feed_all(tam_key_parser_t *p, const char *data, size_t len,
                     tam_key_event_t *evs, int *count, char *pasted, size_t *plen)
{
    while (len > 0) {
        int took = tam_key_feed(p, data, len);
        assert(took >= 0);
        data += took;
        len -= (size_t)took;
        tam_key_event_t ev;
        while (tam_key_next(p, &ev) == 1) {
            if (ev.type == TAM_KEY_PASTE) {
                memcpy(pasted + *plen, ev.text, ev.len);
                *plen += ev.len;
            } else {
                evs[(*count)++] = ev;
            }
        }
    }
}

static void test_paste_and_mouse(void)
{
    tam_key_parser_t p;
    tam_key_event_t evs[32];
    int count = 0;
    static char pasted[4096];
    size_t plen = 0;

    /* A paste with an escape sequence and a newline inside, between keys. */
    tam_key_init(&p);
    const char *in = "a\x1b[200~qs\x1b[Ahello\nx\x1b[201~b";
    feed_all(&p, in, strlen(in), evs, &count, pasted, &plen);
    assert(count == 4);
    assert(evs[0].type == TAM_KEY_CHAR && evs[0].codepoint == 'a');
    assert(evs[1].type == TAM_KEY_PASTE_BEGIN);
    assert(evs[2].type == TAM_KEY_PASTE_END);
    assert(evs[3].type == TAM_KEY_CHAR && evs[3].codepoint == 'b');
    assert(plen == 12 && memcmp(pasted, "qs\x1b[Ahello\nx", 12) == 0);

    /* A large paste, with the end marker split across feeds, byte by byte. */
    tam_key_init(&p);
    count = 0;
    plen = 0;
    static char big[3000];
    memcpy(big, "\x1b[200~", 6);
    for (int i = 6; i < 2994; i++) big[i] = (char)('a' + i % 26);
    memcpy(big + 2994, "\x1b[201~", 6);
    for (size_t i = 0; i < sizeof big; i++)
        feed_all(&p, big + i, 1, evs, &count, pasted, &plen);
    assert(count == 2 && evs[0].type == TAM_KEY_PASTE_BEGIN && evs[1].type == TAM_KEY_PASTE_END);
    assert(plen == 2988 && memcmp(pasted, big + 6, 2988) == 0);
    /* The same in one large chunk. */
    tam_key_init(&p);
    count = 0;
    plen = 0;
    feed_all(&p, big, sizeof big, evs, &count, pasted, &plen);
    assert(count == 2 && plen == 2988 && memcmp(pasted, big + 6, 2988) == 0);
    /* An ESC timeout inside a paste does not end it or eat the ESC. */
    tam_key_init(&p);
    assert(tam_key_feed(&p, "\x1b[200~z\x1b", 8) == 8);
    tam_key_event_t ev;
    assert(tam_key_next(&p, &ev) == 1 && ev.type == TAM_KEY_PASTE_BEGIN);
    assert(tam_key_next(&p, &ev) == 1 && ev.type == TAM_KEY_PASTE && ev.len == 1);
    assert(tam_key_next(&p, &ev) == 0);
    assert(tam_key_timeout(&p, &ev) == 0 && tam_key_pending(&p) == 1);
    assert(tam_key_feed(&p, "[201~", 5) == 5);
    assert(tam_key_next(&p, &ev) == 1 && ev.type == TAM_KEY_PASTE_END);

    /* SGR mouse reports. */
    tam_key_init(&p);
    const char *m = "\x1b[<0;10;5M\x1b[<0;10;5m\x1b[<64;3;4M\x1b[<65;3;4M"
                    "\x1b[<35;7;8M\x1b[<18;1;2M\x1b[<34;2;2M";
    assert(tam_key_feed(&p, m, 40) == 40);
    assert(tam_key_next(&p, &ev) == 1 && ev.type == TAM_KEY_MOUSE);
    assert(ev.button == TAM_MOUSE_LEFT && ev.col == 10 && ev.row == 5 && !ev.release && !ev.motion);
    assert(tam_key_next(&p, &ev) == 1 && ev.button == TAM_MOUSE_LEFT && ev.release);
    assert(tam_key_next(&p, &ev) == 1 && ev.button == TAM_MOUSE_WHEEL_UP && ev.col == 3 && ev.row == 4);
    assert(tam_key_next(&p, &ev) == 1 && ev.button == TAM_MOUSE_WHEEL_DOWN);
    assert(tam_key_feed(&p, m + 40, strlen(m) - 40) == (int)(strlen(m) - 40));
    assert(tam_key_next(&p, &ev) == 1 && ev.button == TAM_MOUSE_NONE && ev.motion && ev.col == 7);
    assert(tam_key_next(&p, &ev) == 1 && ev.button == TAM_MOUSE_RIGHT && ev.mods == TAM_MOD_CTRL);
    assert(tam_key_next(&p, &ev) == 1 && ev.button == TAM_MOUSE_RIGHT && ev.motion);
    assert(tam_key_next(&p, &ev) == 0);
    /* A key event carries no stale mouse or paste fields. */
    assert(tam_key_feed(&p, "k", 1) == 1);
    assert(tam_key_next(&p, &ev) == 1 && ev.type == TAM_KEY_CHAR && ev.col == 0 && ev.text == NULL);
    printf("  [PASS] test_paste_and_mouse\n");
}

/* name parses to exactly what tam_key decodes from bytes, and formats back
 * to canon. */
static void check_name(const char *name, const char *bytes, const char *canon)
{
    tam_key_event_t want, got;
    assert(tam_key_name_parse(name, &want));
    if (bytes != NULL) {
        tam_key_parser_t p;
        tam_key_init(&p);
        assert(tam_key_feed(&p, bytes, strlen(bytes)) == (int)strlen(bytes));
        assert(tam_key_next(&p, &got) == 1);
        if (got.type != want.type || got.codepoint != want.codepoint || got.mods != want.mods) {
            fprintf(stderr, "name %s: parsed %d/%u/%u, decoded %d/%u/%u\n", name,
                    want.type, want.codepoint, want.mods, got.type, got.codepoint, got.mods);
            abort();
        }
    }
    char out[64];
    size_t n = tam_key_name_format(&want, out, sizeof out);
    if (strcmp(out, canon) != 0 || n != strlen(canon)) {
        fprintf(stderr, "name %s formats as %s, not %s\n", name, out, canon);
        abort();
    }
    tam_key_event_t again;
    assert(tam_key_name_parse(out, &again));
    assert(again.type == want.type && again.codepoint == want.codepoint && again.mods == want.mods);
}

static void test_key_names(void)
{
    check_name("Ctrl+S", "\x13", "Ctrl+S");
    check_name("ctrl-s", "\x13", "Ctrl+S");
    check_name("Alt+x", "\x1bx", "Alt+X");
    check_name("Alt+Shift+A", "\x1b" "A", "Alt+Shift+A");
    check_name("Shift+Alt+a", "\x1b" "A", "Alt+Shift+A");
    check_name("A", "A", "Shift+A");
    check_name("Shift+a", "A", "Shift+A");
    check_name("a", "a", "a");
    check_name("Shift+1", "!", "!");
    check_name("Alt+Shift+1", "\x1b!", "Alt+!");
    check_name("Ctrl+\\", "\x1c", "Ctrl+\\");
    check_name("Ctrl+Space", NULL, "Ctrl+Space"); /* NUL, which strlen cannot pass */
    check_name("Space", " ", "Space");
    check_name("F5", "\x1b[15~", "F5");
    check_name("Shift+F5", "\x1b[15;2~", "Shift+F5");
    check_name("Ctrl+Up", "\x1b[1;5A", "Ctrl+Up");
    check_name("PgUp", "\x1b[5~", "PageUp");
    check_name("Shift+Tab", "\x1b[Z", "Shift+Tab");
    check_name("Backtab", "\x1b[Z", "Shift+Tab");
    check_name("Return", "\r", "Enter");
    check_name("Esc", NULL, "Escape");
    check_name("Ctrl++", NULL, "Ctrl++");
    check_name("U+00E9", "\xc3\xa9", "U+00E9");
    check_name("\xc3\xa9", "\xc3\xa9", "U+00E9");
    check_name("Ctrl+Alt+Delete", "\x1b[3;7~", "Ctrl+Alt+Delete");
    tam_key_event_t ev;
    assert(!tam_key_name_parse("Meta+X", &ev));
    assert(!tam_key_name_parse("Hyper+X", &ev));
    assert(!tam_key_name_parse("F13", &ev));
    assert(!tam_key_name_parse("", &ev));
    assert(!tam_key_name_parse("Ctrl+", &ev) || (ev.codepoint == '+' && ev.mods == 0));
    assert(!tam_key_name_parse("ab", &ev));
    char small[4];
    tam_key_event_t cs = { .type = TAM_KEY_CHAR, .codepoint = 's', .mods = TAM_MOD_CTRL };
    assert(tam_key_name_format(&cs, small, sizeof small) == 6 && strcmp(small, "Ctr") == 0);
    printf("  [PASS] test_key_names\n");
}

static void test_tty_modes(void)
{
    int slave;
    int master = open_pty(&slave);
    tam_tty_t t;
    assert(tam_tty_open(&t, slave, slave) == 0);
    struct termios before, now;
    assert(tcgetattr(slave, &before) == 0);

    /* cbreak keeps signals and output processing. */
    assert(tam_tty_cbreak(&t, true) == 0);
    assert(tcgetattr(slave, &now) == 0);
    assert((now.c_lflag & (ICANON | ECHO)) == 0 && (now.c_lflag & ISIG) != 0);
    assert((now.c_oflag & OPOST) != 0);
    assert(tam_tty_raw(&t, true) == 0 && !t.cbreak);
    assert(tcgetattr(slave, &now) == 0 && (now.c_lflag & ISIG) == 0);

    /* Suspend turns everything off; resume brings back exactly that. */
    char out[512];
    drain_master(master, out, sizeof out);
    assert(tam_tty_alt_screen(&t, true) == 0 && tam_tty_mouse(&t, true) == 0);
    assert(tam_tty_paste(&t, true) == 0 && tam_tty_cursor_visible(&t, false) == 0);
    drain_master(master, out, sizeof out);
    assert(tam_tty_suspend(&t) == 0 && tam_tty_suspend(&t) == 0);
    drain_master(master, out, sizeof out);
    assert(strstr(out, "\x1b[?1049l") && strstr(out, "\x1b[?1000l") &&
           strstr(out, "\x1b[?2004l") && strstr(out, "\x1b[?25h"));
    assert(tcgetattr(slave, &now) == 0 && now.c_lflag == before.c_lflag);
    assert(!t.raw && !t.alt_screen && !t.mouse_on && t.suspended);
    assert(tam_tty_resume(&t) == 0 && tam_tty_resume(&t) == 0);
    drain_master(master, out, sizeof out);
    assert(strstr(out, "\x1b[?1049h") && strstr(out, "\x1b[?1000h") &&
           strstr(out, "\x1b[?2004h") && strstr(out, "\x1b[?25l"));
    assert(t.raw && t.alt_screen && t.mouse_on && t.paste_on && t.cursor_hidden);
    assert(tcgetattr(slave, &now) == 0 && (now.c_lflag & ICANON) == 0);

    /* Resize: the pipe wakes tam_tty_read with the new size in place. */
    assert(tam_tty_watch_resize(&t) >= 0);
    struct winsize ws = { .ws_row = 33, .ws_col = 101 };
    assert(ioctl(master, TIOCSWINSZ, &ws) == 0);
    raise(SIGWINCH);
    char b;
    assert(tam_tty_read(&t, &b, 1, 1000) == TAM_TTY_RESIZED);
    assert(t.rows == 33 && t.cols == 101);
    assert(!tam_tty_resized(&t));
    assert(write(master, "y", 1) == 1);
    assert(tam_tty_read(&t, &b, 1, 1000) == 1 && b == 'y');

    tam_tty_close(&t);
    struct sigaction cur;
    assert(sigaction(SIGWINCH, NULL, &cur) == 0 && cur.sa_handler == SIG_DFL);
    assert(tcgetattr(slave, &now) == 0 && now.c_lflag == before.c_lflag);
    close(slave);
    close(master);

    /* Mouse reporting is not sent to a pipe. */
    int fds[2];
    assert(pipe(fds) == 0);
    tam_tty_t pt;
    tam_tty_open(&pt, fds[0], fds[1]);
    assert(tam_tty_mouse(&pt, true) == 0 && pt.mouse_on);
    close(fds[1]);
    char none[8];
    assert(read(fds[0], none, sizeof none) == 0);
    /* End of input is told apart from an error. */
    int eof[2];
    assert(pipe(eof) == 0);
    tam_tty_t et;
    tam_tty_open(&et, eof[0], eof[1]);
    close(eof[1]);
    assert(tam_tty_read(&et, none, sizeof none, 100) == TAM_TTY_EOF);
    close(eof[0]);
    close(fds[0]);
    printf("  [PASS] test_tty_modes\n");
}

static void test_tty_job_stop_and_restore(void)
{
    fflush(stdout); /* the children exit through stdio */
    /* Ctrl+Z: the child stops with the terminal suspended, and resumes. */
    int slave;
    int master = open_pty(&slave);
    pid_t pid = fork();
    if (pid == 0) {
        tam_tty_t t;
        tam_tty_open(&t, slave, slave);
        tam_tty_raw(&t, true);
        tam_tty_alt_screen(&t, true);
        tam_tty_job_stop(&t);
        int ok = t.raw && t.alt_screen && !t.suspended;
        tam_tty_close(&t);
        _exit(ok ? 0 : 1);
    }
    int status;
    assert(waitpid(pid, &status, WUNTRACED) == pid && WIFSTOPPED(status));
    struct termios now;
    assert(tcgetattr(slave, &now) == 0 && (now.c_lflag & ICANON) != 0);
    char out[512];
    drain_master(master, out, sizeof out);
    assert(strstr(out, "\x1b[?1049l") != NULL);
    kill(pid, SIGCONT);
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    drain_master(master, out, sizeof out);
    close(slave);
    close(master);

    /* A program killed in raw mode on the alternate screen, or one that
     * crashes, leaves the terminal as it found it. (SIGSEGV is not tried:
     * under AddressSanitizer its handler owns that signal, and
     * tam_tty_install_restore leaves a handler it did not set alone.) */
    static const int sigs[] = { SIGTERM, SIGILL, SIGABRT };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        master = open_pty(&slave);
        struct termios before;
        assert(tcgetattr(slave, &before) == 0);
        pid = fork();
        if (pid == 0) {
            tam_tty_t t;
            tam_tty_open(&t, slave, slave);
            tam_tty_raw(&t, true);
            tam_tty_alt_screen(&t, true);
            tam_tty_mouse(&t, true);
            tam_tty_cursor_visible(&t, false);
            tam_tty_install_restore(&t);
            raise(sigs[i]);
            _exit(99);
        }
        assert(waitpid(pid, &status, 0) == pid);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == sigs[i]);
        assert(tcgetattr(slave, &now) == 0);
        assert(now.c_lflag == before.c_lflag && now.c_iflag == before.c_iflag);
        drain_master(master, out, sizeof out);
        const char *alt_off = strstr(out, "\x1b[?1049l");
        assert(alt_off && strstr(out, "\x1b[?1000l") && strstr(out, "\x1b[?25h"));
        close(slave);
        close(master);
    }

    /* Exiting without tam_tty_close restores too; a program's own handler
     * is left alone. */
    master = open_pty(&slave);
    pid = fork();
    if (pid == 0) {
        signal(SIGHUP, SIG_IGN);
        tam_tty_t t;
        tam_tty_open(&t, slave, slave);
        tam_tty_raw(&t, true);
        tam_tty_install_restore(&t);
        struct sigaction cur;
        sigaction(SIGHUP, NULL, &cur);
        exit(cur.sa_handler == SIG_IGN ? 0 : 1);
    }
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(tcgetattr(slave, &now) == 0 && (now.c_lflag & ICANON) != 0);
    close(slave);
    close(master);
    printf("  [PASS] test_tty_job_stop_and_restore\n");
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
    test_pager_primitives();
    test_paste_and_mouse();
    test_key_names();
    test_tty_modes();
    test_tty_job_stop_and_restore();
    printf("terminal tests passed\n");
    return 0;
}
