/* Tests for the libtam 0.11.0 primitives: tam_io, tam_proc, tam_sock,
 * tam_b64, the tam_path XDG additions, the UTF-8 helpers, the install guard
 * and the JSON helpers. */
#include "tam.h"
#include "tam_tty.h"
#include "tam_uni.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char base[256];

static char *at(const char *rel)
{
    static char buf[4][512];
    static int next;
    char *b = buf[next++ % 4];
    snprintf(b, sizeof buf[0], "%s/%s", base, rel);
    return b;
}

static void put(const char *rel, const char *text)
{
    assert(tam_fs_write_atomic(at(rel), text, strlen(text), 0, 0600) == 0);
}

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Wait up to two seconds for a file a detached program writes. */
static char *wait_for(const char *path)
{
    for (int i = 0; i < 200; i++) {
        size_t len = 0;
        char *text = tam_fs_read(path, 0, &len);
        if (text && len > 0 && text[len - 1] == '\n') return text;
        free(text);
        sleep_ms(10);
    }
    return NULL;
}

static void test_io(void)
{
    int fds[2];
    assert(pipe(fds) == 0);
    assert(tam_write_all(fds[1], "hello", 5) == 0);
    close(fds[1]);
    char got[8] = {0};
    assert(read(fds[0], got, sizeof got) == 5 && strcmp(got, "hello") == 0);
    close(fds[0]);
    errno = 0;
    assert(tam_write_all(-1, "x", 1) == -1 && errno == EBADF);

    /* A non-blocking pipe that fills is waited on, not failed. */
    assert(pipe2(fds, O_NONBLOCK) == 0);
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[1]);
        char sink[4096];
        sleep_ms(50);
        while (read(fds[0], sink, sizeof sink) != 0) {
        }
        _exit(0);
    }
    close(fds[0]);
    static char big[256 * 1024];
    memset(big, 'z', sizeof big);
    assert(tam_write_all(fds[1], big, sizeof big) == 0);
    close(fds[1]);
    assert(waitpid(pid, NULL, 0) == pid);

    char line[16];
    put("line", "  42 \t\r\nsecond\n");
    assert(tam_read_line(at("line"), line, sizeof line) == 0 && strcmp(line, "42") == 0);
    put("noeol", "abc");
    assert(tam_read_line(at("noeol"), line, sizeof line) == 0 && strcmp(line, "abc") == 0);
    put("empty", "\n");
    assert(tam_read_line(at("empty"), line, sizeof line) == 0 && line[0] == '\0');
    put("long", "0123456789abcdefXYZ\n");
    errno = 0;
    assert(tam_read_line(at("long"), line, sizeof line) == -1 && errno == ERANGE);
    /* Trailing blanks beyond the buffer do not count against it. */
    put("fits", "0123456789abcde              \n");
    assert(tam_read_line(at("fits"), line, sizeof line) == 0 &&
           strcmp(line, "0123456789abcde") == 0);
    errno = 0;
    assert(tam_read_line(base, line, sizeof line) == -1 && errno == EISDIR);
    assert(mkfifo(at("fifo"), 0600) == 0);
    errno = 0;
    assert(tam_read_line(at("fifo"), line, sizeof line) == -1 && errno == EINVAL);
    unlink(at("fifo"));
    errno = 0;
    assert(tam_read_line(at("missing"), line, sizeof line) == -1 && errno == ENOENT);
    assert(tam_read_line("/proc/self/stat", line, 2) == -1 && errno == ERANGE);
    char comm[64];
    assert(tam_read_line("/proc/self/comm", comm, sizeof comm) == 0 && comm[0] != '\0');

    int64_t a = tam_monotonic_ms(), an = tam_monotonic_ns();
    sleep_ms(20);
    assert(tam_monotonic_ms() - a >= 19);
    assert(tam_monotonic_ns() - an >= 19000000);
    printf("  [PASS] test_io\n");
}

static void test_which(void)
{
    char *sh = tam_which("sh");
    assert(sh && sh[0] == '/' && access(sh, X_OK) == 0);
    char *again = tam_which(sh);
    assert(again && strcmp(again, sh) == 0);
    free(again);
    free(sh);
    errno = 0;
    assert(tam_which("tam-no-such-program-here") == NULL && errno == ENOENT);
    assert(tam_which("") == NULL);

    /* PATH order, an empty entry meaning ".", and non-executables skipped. */
    put("prog", "#!/bin/sh\n");
    char *old = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
    char path[600];
    snprintf(path, sizeof path, "/nonexistent:%s", base);
    setenv("PATH", path, 1);
    errno = 0;
    assert(tam_which("prog") == NULL && errno == ENOENT); /* mode 0600 */
    assert(chmod(at("prog"), 0700) == 0);
    char *found = tam_which("prog");
    assert(found && strcmp(found, at("prog")) == 0);
    free(found);
    char cwd[512];
    assert(getcwd(cwd, sizeof cwd));
    assert(chdir(base) == 0);
    setenv("PATH", "/nonexistent:", 1);
    found = tam_which("prog");
    assert(found && strcmp(found, "./prog") == 0);
    free(found);
    assert(chdir(cwd) == 0);
    if (old) setenv("PATH", old, 1); else unsetenv("PATH");
    free(old);
    unlink(at("prog"));
    printf("  [PASS] test_which\n");
}

static void test_spawn(void)
{
    /* A detached program runs, holds none of our descriptors, and starts
     * with default signal handling even though we ignore SIGINT. */
    /* Not close-on-exec, and high enough not to collide with the
     * descriptors ls opens itself. */
    int leak = dup2(open("/dev/null", O_RDONLY), 50);
    assert(leak == 50);
    void (*old_int)(int) = signal(SIGINT, SIG_IGN);
    char script[1024];
    snprintf(script, sizeof script,
             "ls /proc/self/fd > '%s.tmp'; grep -E '^SigIgn' /proc/self/status >> '%s.tmp';"
             " echo done >> '%s.tmp'; mv '%s.tmp' '%s'",
             at("spawned"), at("spawned"), at("spawned"), at("spawned"), at("spawned"));
    const char *argv[] = {"sh", "-c", script, NULL};
    assert(tam_spawn_detached(argv) == 0);
    char *out = wait_for(at("spawned"));
    assert(out != NULL);
    char want[16];
    snprintf(want, sizeof want, "\n%d\n", leak);
    assert(strstr(out, want) == NULL);
    char *ign = strstr(out, "SigIgn:");
    assert(ign);
    unsigned long long mask = strtoull(ign + 7, NULL, 16);
    assert((mask & (1ull << (SIGINT - 1))) == 0);
    free(out);
    signal(SIGINT, old_int);
    close(leak);

    const char *missing[] = {"tam-no-such-program-here", NULL};
    errno = 0;
    assert(tam_spawn_detached(missing) == -1 && errno == ENOENT);
    const char *none[] = {NULL};
    assert(tam_spawn_detached(none) == -1 && errno == EINVAL);
    printf("  [PASS] test_spawn\n");
}

static void test_foreground(void)
{
    int code = -5;
    const char *ok[] = {"sh", "-c", "exit 3", NULL};
    assert(tam_run_foreground(ok, &code) == 0 && code == 3);
    const char *killed[] = {"sh", "-c", "kill -9 $$", NULL};
    assert(tam_run_foreground(killed, &code) == 0 && code == 128 + SIGKILL);
    const char *missing[] = {"tam-no-such-program-here", NULL};
    errno = 0;
    assert(tam_run_foreground(missing, &code) == -1 && errno == ENOENT);

    /* Our own SIGINT handling is back afterwards. */
    struct sigaction now;
    void (*mine)(int) = signal(SIGINT, SIG_DFL);
    assert(tam_run_foreground(ok, NULL) == 0);
    assert(sigaction(SIGINT, NULL, &now) == 0 && now.sa_handler == SIG_DFL);
    signal(SIGINT, mine);
    printf("  [PASS] test_foreground\n");
}

static int collect(const char *dir, void *user)
{
    tam_str_t *s = user;
    *s = tam_str_cat_printf(*s, "%s;", dir);
    return 0;
}

static int stop_second(const char *dir, void *user)
{
    (void)dir;
    int *n = user;
    return ++*n == 2 ? 7 : 0;
}

static void test_xdg(void)
{
    setenv("HOME", base, 1);
    unsetenv("XDG_DATA_HOME");
    char *d = tam_xdg_base(TAM_XDG_DATA);
    assert(d && strcmp(d, at(".local/share")) == 0);
    free(d);
    setenv("XDG_DATA_HOME", "relative/ignored", 1);
    d = tam_xdg_base(TAM_XDG_DATA);
    assert(d && strcmp(d, at(".local/share")) == 0);
    free(d);
    setenv("XDG_DATA_HOME", "/data/home", 1);
    setenv("XDG_DATA_DIRS", "/a:relative:/b::", 1);
    tam_str_t seen = tam_str_new("");
    assert(tam_xdg_search(TAM_XDG_DATA, collect, &seen) == 0);
    assert(strcmp(seen, "/data/home;/a;/b;") == 0);
    tam_str_free(seen);
    unsetenv("XDG_DATA_DIRS");
    seen = tam_str_new("");
    assert(tam_xdg_search(TAM_XDG_DATA, collect, &seen) == 0);
    assert(strcmp(seen, "/data/home;/usr/local/share;/usr/share;") == 0);
    tam_str_free(seen);
    unsetenv("XDG_CONFIG_DIRS");
    unsetenv("XDG_CONFIG_HOME");
    seen = tam_str_new("");
    assert(tam_xdg_search(TAM_XDG_CONFIG, collect, &seen) == 0);
    char want[600];
    snprintf(want, sizeof want, "%s;/etc/xdg;", at(".config"));
    assert(strcmp(seen, want) == 0);
    tam_str_free(seen);
    int n = 0;
    assert(tam_xdg_search(TAM_XDG_DATA, stop_second, &n) == 7 && n == 2);
    unsetenv("XDG_DATA_HOME");

    /* tam_xdg_path still builds <base>/tamlinux/<command>/<leaf>. */
    unsetenv("XDG_STATE_HOME");
    char *p = tam_xdg_path(TAM_XDG_STATE, "demo", "f", true);
    assert(p && strcmp(p, at(".local/state/tamlinux/demo/f")) == 0);
    free(p);

    /* The runtime directory must be private. */
    unsetenv("XDG_RUNTIME_DIR");
    errno = 0;
    assert(tam_runtime_dir("demo") == NULL && errno == ENOENT);
    assert(mkdir(at("run"), 0755) == 0);
    setenv("XDG_RUNTIME_DIR", at("run"), 1);
    errno = 0;
    assert(tam_runtime_dir("demo") == NULL && errno == EPERM);
    assert(chmod(at("run"), 0700) == 0);
    char *r = tam_runtime_dir("demo");
    struct stat st;
    assert(r && strcmp(r, at("run/demo")) == 0);
    assert(stat(r, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700);
    free(r);
    r = tam_runtime_dir("demo"); /* already there */
    assert(r);
    free(r);
    assert(mkdir(at("run/open"), 0755) == 0);
    assert(chmod(at("run/open"), 0755) == 0);
    errno = 0;
    assert(tam_runtime_dir("open") == NULL && errno == EPERM);
    assert(symlink("demo", at("run/link")) == 0);
    errno = 0;
    assert(tam_runtime_dir("link") == NULL && errno == ENOTDIR);
    errno = 0;
    assert(tam_runtime_dir("../x") == NULL && errno == EINVAL);
    assert(tam_runtime_dir("..") == NULL && errno == EINVAL);
    printf("  [PASS] test_xdg\n");
}

static void test_sock(void)
{
    char *dir = tam_runtime_dir("socktest");
    assert(dir);
    char path[512];
    snprintf(path, sizeof path, "%s/ctl.sock", dir);

    int lfd = tam_sock_listen(path, 0600, 4);
    assert(lfd >= 0);
    struct stat st;
    assert(stat(path, &st) == 0 && S_ISSOCK(st.st_mode) && (st.st_mode & 0777) == 0600);
    assert((fcntl(lfd, F_GETFD) & FD_CLOEXEC) != 0);

    /* A live socket is never taken over. */
    errno = 0;
    assert(tam_sock_listen(path, 0600, 4) == -1 && errno == EADDRINUSE);

    /* That check connected once to see the listener was live; the
     * listener sees an empty connection that ends at once. */
    int probe = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
    char pb[4];
    assert(probe >= 0 && tam_sock_read_line(probe, pb, sizeof pb, 1000) == -1 && errno == ENODATA);
    close(probe);

    int c = tam_sock_connect(path, 1000);
    assert(c >= 0);
    int s = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
    assert(s >= 0);
    uid_t uid = (uid_t)-1;
    assert(tam_sock_peer_uid(s, &uid) == 0 && uid == geteuid());

    /* Two lines in one write: each read takes one and leaves the rest. */
    char buf[16];
    assert(tam_write_all(c, "one\ntwo\n", 8) == 0);
    assert(tam_sock_read_line(s, buf, sizeof buf, 1000) == 3 && strcmp(buf, "one") == 0);
    assert(tam_sock_read_line(s, buf, sizeof buf, 1000) == 3 && strcmp(buf, "two") == 0);
    /* An empty line is length 0. */
    assert(tam_write_all(c, "\n", 1) == 0);
    assert(tam_sock_read_line(s, buf, sizeof buf, 1000) == 0 && buf[0] == '\0');
    /* A silent peer: the deadline covers the whole line. */
    assert(tam_write_all(c, "par", 3) == 0);
    int64_t t0 = tam_monotonic_ms();
    errno = 0;
    assert(tam_sock_read_line(s, buf, sizeof buf, 150) == -1 && errno == ETIMEDOUT);
    int64_t took = tam_monotonic_ms() - t0;
    assert(took >= 140 && took < 1000);
    /* A slow drip still ends at the deadline, not per byte. */
    pid_t pid = fork();
    if (pid == 0) {
        for (int i = 0; i < 20; i++) {
            (void)write(c, "x", 1);
            sleep_ms(30);
        }
        _exit(0);
    }
    t0 = tam_monotonic_ms();
    char big[64];
    errno = 0;
    assert(tam_sock_read_line(s, big, sizeof big, 200) == -1 && errno == ETIMEDOUT);
    took = tam_monotonic_ms() - t0;
    assert(took < 450);
    assert(waitpid(pid, NULL, 0) == pid);
    close(s);
    close(c);

    /* A line too long for the buffer. */
    c = tam_sock_connect(path, 1000);
    s = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
    assert(c >= 0 && s >= 0);
    assert(tam_write_all(c, "0123456789abcdefghij\n", 21) == 0);
    errno = 0;
    assert(tam_sock_read_line(s, buf, sizeof buf, 1000) == -1 && errno == EMSGSIZE);
    close(s);
    close(c);

    /* End of stream: a partial line, then ENODATA. */
    c = tam_sock_connect(path, 1000);
    s = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
    assert(tam_write_all(c, "tail", 4) == 0);
    close(c);
    assert(tam_sock_read_line(s, buf, sizeof buf, 1000) == 4 && strcmp(buf, "tail") == 0);
    errno = 0;
    assert(tam_sock_read_line(s, buf, sizeof buf, 1000) == -1 && errno == ENODATA);
    close(s);

    /* A stale socket left by a dead listener is replaced. */
    close(lfd);
    assert(stat(path, &st) == 0);
    lfd = tam_sock_listen(path, 0600, 4);
    assert(lfd >= 0);
    close(lfd);
    errno = 0;
    assert(tam_sock_connect(path, 500) == -1 && errno == ECONNREFUSED);
    unlink(path);

    /* Anything else at the path is left alone. */
    snprintf(path, sizeof path, "%s/plain", dir);
    assert(tam_fs_write_atomic(path, "x", 1, 0, 0600) == 0);
    errno = 0;
    assert(tam_sock_listen(path, 0600, 4) == -1 && errno == EEXIST);
    unlink(path);
    errno = 0;
    assert(tam_sock_connect(path, 500) == -1 && errno == ENOENT);

    char longp[200];
    memset(longp, 'a', sizeof longp - 1);
    longp[0] = '/';
    longp[sizeof longp - 1] = '\0';
    errno = 0;
    assert(tam_sock_listen(longp, 0600, 4) == -1 && errno == ENAMETOOLONG);
    rmdir(dir);
    free(dir);
    printf("  [PASS] test_sock\n");
}

static void test_b64(void)
{
    static const char *vec[][2] = {
        {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"},
    };
    for (size_t i = 0; i < sizeof vec / sizeof vec[0]; i++) {
        char out[16];
        size_t n = tam_b64_encode(vec[i][0], strlen(vec[i][0]), out);
        assert(n == tam_b64_encoded_len(strlen(vec[i][0])) && strcmp(out, vec[i][1]) == 0);
        size_t len = 99;
        unsigned char *back = tam_b64_decode(vec[i][1], strlen(vec[i][1]), &len);
        assert(back && len == strlen(vec[i][0]) && memcmp(back, vec[i][0], len) == 0);
        free(back);
    }
    size_t len = 0;
    unsigned char *np = tam_b64_decode("Zm9vYg", 6, &len); /* no padding */
    assert(np && len == 4 && memcmp(np, "foob", 4) == 0);
    free(np);
    unsigned char bin[256];
    for (int i = 0; i < 256; i++) bin[i] = (unsigned char)i;
    char enc[400];
    tam_b64_encode(bin, sizeof bin, enc);
    unsigned char *rt = tam_b64_decode(enc, strlen(enc), &len);
    assert(rt && len == 256 && memcmp(rt, bin, 256) == 0);
    free(rt);
    errno = 0;
    assert(tam_b64_decode("Zm9v!", 5, &len) == NULL && errno == EINVAL);
    assert(tam_b64_decode("Z", 1, &len) == NULL && errno == EINVAL);
    assert(tam_b64_decode("Zg=", 3, &len) == NULL && errno == EINVAL);
    printf("  [PASS] test_b64\n");
}

static void test_utf8_and_locale(void)
{
    const char *s = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z"; /* a é € 😀 z */
    size_t len = strlen(s);
    assert(tam_utf8_prev(s, len) == len - 1);
    assert(tam_utf8_prev(s, len - 1) == len - 5);
    assert(tam_utf8_prev(s, len - 5) == 3);
    assert(tam_utf8_prev(s, 3) == 1);
    assert(tam_utf8_prev(s, 1) == 0);
    assert(tam_utf8_prev(s, 0) == 0);
    /* Inside a sequence or after a stray continuation byte: one step back. */
    assert(tam_utf8_prev(s, 2) == 1);
    assert(tam_utf8_prev("\x80\x80", 2) == 1);
    assert(tam_utf8_valid(s, len));
    assert(tam_utf8_valid("\xef\xbf\xbd", 3)); /* a real U+FFFD */
    assert(tam_utf8_valid("", 0));
    assert(!tam_utf8_valid("\xc3", 1));
    assert(!tam_utf8_valid("\xc0\xaf", 2)); /* overlong */
    assert(!tam_utf8_valid("\xed\xa0\x80", 3)); /* surrogate */
    assert(!tam_utf8_valid("ok\xff", 3));

    setenv("LC_ALL", "C", 1);
    assert(tam_locale_init());
    assert(tam_uni_width(0x4E2D) == 2); /* 中: wide only under a UTF-8 locale */
    unsetenv("LC_ALL");
    printf("  [PASS] test_utf8_and_locale\n");
}

static void test_guard_and_json(void)
{
    int pending = tam_install_pending(NULL);
    int guard = tam_install_guard("test");
    assert(pending == 0 ? guard == 0 : guard != 0);

    tam_str_t s = tam_str_new("{");
    s = tam_str_cat_json_kv(s, "name", "a\"b\n\xc3\xa9");
    s = tam_str_cat(s, ",");
    s = tam_str_cat_json_kv(s, "none", NULL);
    s = tam_str_cat(s, ",\"list\":[");
    s = tam_str_cat_json_str(s, "x");
    s = tam_str_cat(s, ",");
    s = tam_str_cat_json_str(s, NULL);
    s = tam_str_cat(s, "]}");
    assert(s && strcmp(s, "{\"name\":\"a\\\"b\\n\xc3\xa9\",\"none\":null,\"list\":[\"x\",null]}") == 0);
    tam_str_free(s);
    printf("  [PASS] test_guard_and_json\n");
}

static void test_osc52(void)
{
    int master, slave;
    assert(openpty(&master, &slave, NULL, NULL, NULL) == 0);
    tam_tty_t t;
    assert(tam_tty_open(&t, slave, slave) == 0);
    assert(tam_tty_osc52_copy(&t, "hi", 2) == 0);
    char got[64] = {0};
    sleep_ms(10);
    ssize_t n = read(master, got, sizeof got - 1);
    assert(n > 0 && strcmp(got, "\033]52;c;aGk=\033\\") == 0);
    static char huge[TAM_TTY_OSC52_MAX + 1];
    errno = 0;
    assert(tam_tty_osc52_copy(&t, huge, sizeof huge) == -1 && errno == EMSGSIZE);
    close(master);
    close(slave);

    int fds[2];
    assert(pipe(fds) == 0);
    tam_tty_t p;
    tam_tty_open(&p, fds[0], fds[1]);
    errno = 0;
    assert(tam_tty_osc52_copy(&p, "hi", 2) == -1 && errno == ENOTTY);
    close(fds[0]);
    close(fds[1]);
    printf("  [PASS] test_osc52\n");
}

static void remove_base(void)
{
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", base);
    assert(system(cmd) == 0);
}

int main(void)
{
    /* /tmp, not $TMPDIR: the socket path must fit in 108 bytes. */
    const char *dir = "/tmp";
    snprintf(base, sizeof base, "%s/test_tam_primitives.XXXXXX", dir);
    assert(mkdtemp(base));
    printf("Running libtam primitive tests in %s...\n", base);
    test_io();
    test_which();
    test_spawn();
    test_foreground();
    test_xdg();
    test_sock();
    test_b64();
    test_utf8_and_locale();
    test_guard_and_json();
    test_osc52();
    remove_base();
    printf("All libtam primitive tests passed.\n");
    return 0;
}
