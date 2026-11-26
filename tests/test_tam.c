#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdint.h>

static void test_version(void) {
    const char *v = tam_version();
    assert(v != NULL);
    assert(strcmp(v, TAM_VERSION_STRING) == 0);
    printf("  [PASS] test_version: %s\n", v);
}

static void test_str(void) {
    tam_str_t s = tam_str_new("Hello");
    assert(tam_str_len(s) == 5);
    assert(strcmp(s, "Hello") == 0);

    s = tam_str_cat(s, ", ");
    s = tam_str_cat(s, "Tamlinux!");
    assert(tam_str_len(s) == 16);
    assert(strcmp(s, "Hello, Tamlinux!") == 0);

    s = tam_str_cat_printf(s, " Ver: %d.%d", 0, 1);
    assert(strstr(s, "Ver: 0.1") != NULL);

    tam_str_t trimmed = tam_str_new("  \t padded text \n ");
    trimmed = tam_str_trim(trimmed, " \t\n\r");
    assert(strcmp(trimmed, "padded text") == 0);
    tam_str_free(trimmed);

    tam_str_free(s);

    /* Edge cases: trim to nothing, trim "", length helpers, clear, dup. */
    tam_str_t e = tam_str_new("");
    e = tam_str_trim(e, " ");
    assert(e && tam_str_len(e) == 0 && e[0] == '\0');
    e = tam_str_cat(e, "   ");
    e = tam_str_trim(e, " ");
    assert(tam_str_len(e) == 0);
    e = tam_str_cat_len(e, "ab\0cd", 5);
    assert(tam_str_len(e) == 5 && memcmp(e, "ab\0cd", 5) == 0);
    tam_str_t d = tam_str_dup(e);
    assert(d && tam_str_len(d) == 5 && memcmp(d, "ab\0cd", 6) == 0);
    tam_str_clear(e);
    assert(tam_str_len(e) == 0 && e[0] == '\0' && tam_str_avail(e) >= 5);
    tam_str_free(d);
    tam_str_t n = tam_str_new_len("xyz", 2);
    assert(n && strcmp(n, "xy") == 0);
    tam_str_free(n);

    /* An impossible size fails with ENOMEM and leaves the string intact. */
    errno = 0;
    tam_str_t big = tam_str_cat_len(e, "x", SIZE_MAX - 8);
    assert(big == NULL && errno == ENOMEM);
    e = tam_str_cat(e, "kept");
    assert(e && strcmp(e, "kept") == 0);
    assert(tam_str_new_len(NULL, SIZE_MAX - 4) == NULL);
    tam_str_free(e);
    printf("  [PASS] test_str\n");
}

static void test_fs_read_kinds(void) {
    char dir[] = "/tmp/test_tam_read.XXXXXX";
    assert(mkdtemp(dir));
    char path[256];

    /* /proc files report size 0 but have contents. */
    size_t len = 0;
    char *proc = tam_fs_read("/proc/self/status", 0, &len);
    assert(proc && len > 0 && strstr(proc, "Name:"));
    free(proc);

    /* A FIFO is refused at once instead of blocking. */
    snprintf(path, sizeof path, "%s/fifo", dir);
    assert(mkfifo(path, 0600) == 0);
    errno = 0;
    assert(tam_fs_read(path, 0, &len) == NULL && errno == EINVAL);
    unlink(path);

    /* A directory is refused. */
    errno = 0;
    assert(tam_fs_read(dir, 0, &len) == NULL && errno == EISDIR);

    /* The cap. */
    snprintf(path, sizeof path, "%s/capped", dir);
    assert(tam_fs_write_atomic(path, "0123456789", 10, 0, 0600) == 0);
    errno = 0;
    assert(tam_fs_read(path, 9, &len) == NULL && errno == EFBIG);
    char *ok = tam_fs_read(path, 10, &len);
    assert(ok && len == 10 && strcmp(ok, "0123456789") == 0);
    free(ok);
    unlink(path);

    /* A missing file keeps open's errno. */
    snprintf(path, sizeof path, "%s/missing", dir);
    errno = 0;
    assert(tam_fs_read(path, 0, &len) == NULL && errno == ENOENT);

    /* A pipe through tam_fd_read_all. */
    int fds[2];
    assert(pipe(fds) == 0);
    assert(write(fds[1], "piped", 5) == 5);
    close(fds[1]);
    char *piped = tam_fd_read_all(fds[0], 0, &len);
    assert(piped && len == 5 && strcmp(piped, "piped") == 0);
    free(piped);
    close(fds[0]);
    assert(pipe(fds) == 0);
    assert(write(fds[1], "toolong", 7) == 7);
    close(fds[1]);
    errno = 0;
    assert(tam_fd_read_all(fds[0], 3, &len) == NULL && errno == EFBIG);
    close(fds[0]);

    rmdir(dir);
    printf("  [PASS] test_fs_read_kinds\n");
}

static void test_write_atomic_modes(void) {
    char dir[] = "/tmp/test_tam_write.XXXXXX";
    assert(mkdtemp(dir));
    char path[256], link[256], real[256];
    struct stat st;

    /* KEEP_MODE keeps an executable script executable and a private file
     * private; without it the given mode applies. */
    snprintf(path, sizeof path, "%s/script", dir);
    assert(tam_fs_write_atomic(path, "#!/bin/sh\n", 10, 0, 0755) == 0);
    assert(tam_fs_write_atomic(path, "#!/bin/sh\n:\n", 12, TAM_FS_KEEP_MODE, 0644) == 0);
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0755);
    assert(tam_fs_write_atomic(path, "x", 1, 0, 0644) == 0);
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0644);
    snprintf(real, sizeof real, "%s/private", dir);
    assert(tam_fs_write_atomic(real, "s", 1, 0, 0600) == 0);
    assert(tam_fs_write_atomic(real, "t", 1, TAM_FS_KEEP_MODE, 0644) == 0);
    assert(stat(real, &st) == 0 && (st.st_mode & 0777) == 0600);
    /* A new file under KEEP_MODE takes the given mode. */
    snprintf(path, sizeof path, "%s/fresh", dir);
    assert(tam_fs_write_atomic(path, "f", 1, TAM_FS_KEEP_MODE, 0640) == 0);
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0640);
    unlink(path);

    /* Writing through a symbolic link changes its target, not the link. */
    snprintf(link, sizeof link, "%s/link", dir);
    assert(symlink("private", link) == 0);
    assert(tam_fs_write_atomic(link, "via link", 8, TAM_FS_KEEP_MODE, 0644) == 0);
    assert(lstat(link, &st) == 0 && S_ISLNK(st.st_mode));
    size_t len = 0;
    char *got = tam_fs_read(real, 0, &len);
    assert(got && strcmp(got, "via link") == 0);
    free(got);
    assert(stat(real, &st) == 0 && (st.st_mode & 0777) == 0600);

    /* A dangling link creates its target. */
    snprintf(path, sizeof path, "%s/dangling", dir);
    assert(symlink("created", path) == 0);
    assert(tam_fs_write_atomic(path, "new", 3, 0, 0600) == 0);
    snprintf(real, sizeof real, "%s/created", dir);
    got = tam_fs_read(real, 0, &len);
    assert(got && strcmp(got, "new") == 0);
    free(got);
    unlink(real);
    unlink(path);

    /* REPLACE_LINK replaces the link itself. */
    assert(tam_fs_write_atomic(link, "plain", 5, TAM_FS_REPLACE_LINK, 0600) == 0);
    assert(lstat(link, &st) == 0 && S_ISREG(st.st_mode));
    snprintf(real, sizeof real, "%s/private", dir);
    got = tam_fs_read(real, 0, &len);
    assert(got && strcmp(got, "via link") == 0);
    free(got);

    /* A link loop fails with ELOOP and nothing is written. */
    snprintf(path, sizeof path, "%s/loop", dir);
    assert(symlink("loop", path) == 0);
    errno = 0;
    assert(tam_fs_write_atomic(path, "x", 1, 0, 0600) == -1 && errno == ELOOP);
    unlink(path);

    unlink(link);
    unlink(real);
    snprintf(path, sizeof path, "%s/script", dir);
    unlink(path);
    rmdir(dir);
    printf("  [PASS] test_write_atomic_modes\n");
}

static void test_fs(void) {
    const char *test_path = "/tmp/test_tam_fs.txt";
    const char *data = "Tamlinux atomic write test data\n";
    size_t len = strlen(data);

    assert(tam_fs_write_atomic(test_path, data, len, 0, 0644) == 0);
    assert(tam_fs_file_exists(test_path));

    size_t read_len = 0;
    char *content = tam_fs_read(test_path, 0, &read_len);
    assert(content != NULL);
    assert(read_len == len);
    assert(strcmp(content, data) == 0);

    free(content);
    unlink(test_path);
    assert(!tam_fs_file_exists(test_path));
    printf("  [PASS] test_fs\n");
}

static int ini_callback(void *user, const char *section, const char *name, const char *value) {
    int *count = (int *)user;
    (*count)++;
    if (strcmp(section, "desktop") == 0 && strcmp(name, "mode") == 0) {
        assert(strcmp(value, "windows") == 0);
    }
    return 0;
}

static int ini_stop_at_b(void *user, const char *section, const char *name,
                         const char *value) {
    (void)user; (void)section; (void)value;
    return strcmp(name, "b") == 0;
}

static void test_ini(void) {
    const char *ini_data =
        "# Tamlinux configuration\n"
        "[desktop]\n"
        "mode = windows\n"
        "monitors = 3\n"
        "[theme]\n"
        "variant = dark\n";

    int count = 0;
    int res = tam_ini_parse_string(ini_data, ini_callback, &count);
    assert(res == 0);
    assert(count == 3);

    /* Errors: parsing continues, the first error line is returned, and a
     * bad section's keys are not passed on. */
    char long_section[200];
    memset(long_section, 's', sizeof long_section);
    long_section[0] = '[';
    long_section[sizeof long_section - 2] = ']';
    long_section[sizeof long_section - 1] = '\0';
    char bad[512];
    snprintf(bad, sizeof bad,
             "[desktop]\n"
             "mode = windows\n"
             "stray words\n"         /* line 3: no separator */
             "%s\n"                  /* line 4: section name too long */
             "hidden = 1\n"
             "[unclosed\n"           /* line 6 */
             "also = hidden\n"
             "[theme]\n"
             "variant: dark\n",
             long_section);
    count = 0;
    assert(tam_ini_parse_string(bad, ini_callback, &count) == 3);
    assert(count == 2);

    /* A handler's non-zero return stops at its line. */
    assert(tam_ini_parse_string("a=1\nb=2\n", ini_stop_at_b, NULL) == 2);

    /* Reading a file. */
    const char *path = "/tmp/test_tam_ini.ini";
    assert(tam_fs_write_atomic(path, ini_data, strlen(ini_data), 0, 0600) == 0);
    count = 0;
    assert(tam_ini_parse_file(path, ini_callback, &count) == 0 && count == 3);
    unlink(path);
    assert(tam_ini_parse_file(path, ini_callback, &count) == -1);
    printf("  [PASS] test_ini\n");
}

static void test_exec(void) {
    const char *const argv_echo[] = {"echo", "tamlinux-exec-ok", NULL};
    tam_exec_result_t r1 = tam_exec(argv_echo, 2000);
    assert(r1.exit_code == 0);
    assert(!r1.timed_out);
    assert(strstr(r1.stdout_buf, "tamlinux-exec-ok") != NULL);
    tam_exec_result_free(&r1);

    /* Test timeout: run sleep 5 with 100ms timeout */
    const char *const argv_sleep[] = {"sleep", "5", NULL};
    tam_exec_result_t r2 = tam_exec(argv_sleep, 100);
    assert(r2.timed_out);
    tam_exec_result_free(&r2);

    /* A missing program reports ENOENT, apart from a real exit 127. */
    const char *const argv_missing[] = {"/nonexistent/tam-no-such-program", NULL};
    tam_exec_result_t r3 = tam_exec(argv_missing, 2000);
    assert(r3.exit_code == 127 && r3.error == ENOENT);
    tam_exec_result_free(&r3);
    const char *const argv_127[] = {"sh", "-c", "exit 127", NULL};
    tam_exec_result_t r4 = tam_exec(argv_127, 2000);
    assert(r4.exit_code == 127 && r4.error == 0);
    tam_exec_result_free(&r4);

    /* A caller's blocked and ignored signals do not reach the child. */
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGTERM);
    sigaddset(&block, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block, &old);
    void (*old_int)(int) = signal(SIGINT, SIG_IGN);
    const char *const argv_sig[] = {"sh", "-c",
        "grep -E '^(SigBlk|SigIgn):' /proc/self/status", NULL};
    tam_exec_result_t r5 = tam_exec(argv_sig, 2000);
    assert(r5.exit_code == 0);
    char *blk = strstr(r5.stdout_buf, "SigBlk:");
    assert(blk && strncmp(blk, "SigBlk:\t0000000000000000", 24) == 0);
    char *ign = strstr(r5.stdout_buf, "SigIgn:");
    /* SIGINT is signal 2, bit 1 of the mask. */
    assert(ign);
    unsigned long long ign_mask = strtoull(ign + 7, NULL, 16);
    assert((ign_mask & (1ull << (SIGINT - 1))) == 0);
    tam_exec_result_free(&r5);
    signal(SIGINT, old_int);
    sigprocmask(SIG_SETMASK, &old, NULL);

    printf("  [PASS] test_exec\n");
}

/* A child that writes many small pieces over about a second must finish:
 * libtam 0.3.0 charged 100 ms per poll and killed it early. */
static void test_exec_chatty(void) {
    const char *const argv[] = {"sh", "-c",
        "i=0; while [ $i -lt 200 ]; do echo $i; sleep 0.005; i=$((i+1)); done", NULL};
    tam_exec_result_t r = tam_exec(argv, 5000);
    assert(!r.timed_out);
    assert(r.exit_code == 0);
    assert(strstr(r.stdout_buf, "199\n") != NULL);
    tam_exec_result_free(&r);
}

static long long ms_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void test_exec_opts(void) {
    test_exec_chatty();

    /* Standard input is /dev/null by default, never the caller's terminal. */
    const char *const cat[] = {"cat", NULL};
    tam_exec_result_t r1 = tam_exec(cat, 2000);
    assert(!r1.timed_out && r1.exit_code == 0 && r1.stdout_len == 0);
    tam_exec_result_free(&r1);

    /* Supplied input reaches the child, even more than a pipe buffer holds. */
    size_t big = 300000;
    char *data = malloc(big);
    assert(data);
    for (size_t i = 0; i < big; i++) data[i] = (char)('a' + i % 26);
    tam_exec_opts_t o2 = { .timeout_ms = 5000, .input = data, .input_len = big };
    tam_exec_result_t r2 = tam_exec_opts(cat, &o2);
    assert(!r2.timed_out && r2.exit_code == 0);
    assert(r2.stdout_len == big && memcmp(r2.stdout_buf, data, big) == 0);
    tam_exec_result_free(&r2);

    /* A child that ignores its input exits cleanly; no SIGPIPE in the caller. */
    const char *const truebin[] = {"true", NULL};
    tam_exec_result_t r3 = tam_exec_opts(truebin, &o2);
    assert(!r3.timed_out && r3.exit_code == 0);
    tam_exec_result_free(&r3);
    free(data);

    /* The output cap stops a runaway child quickly and keeps what fits. */
    const char *const yes[] = {"yes", NULL};
    tam_exec_opts_t o4 = { .timeout_ms = 5000, .max_output = 10000 };
    long long t0 = ms_now();
    tam_exec_result_t r4 = tam_exec_opts(yes, &o4);
    assert(r4.truncated && !r4.timed_out && r4.exit_code == -1);
    assert(r4.stdout_len == 10000);
    assert(ms_now() - t0 < 2000);
    tam_exec_result_free(&r4);

    /* A child that closes its output but keeps running is still bounded. */
    const char *const silent[] = {"sh", "-c", "exec >&- 2>&-; sleep 5", NULL};
    t0 = ms_now();
    tam_exec_result_t r5 = tam_exec(silent, 300);
    assert(r5.timed_out && r5.exit_code == -1);
    assert(ms_now() - t0 < 2000);
    tam_exec_result_free(&r5);

    /* A timeout kills helpers the child started, which hold its output open. */
    const char *const group[] = {"sh", "-c", "sleep 5 & sleep 5; wait", NULL};
    t0 = ms_now();
    tam_exec_result_t r6 = tam_exec(group, 300);
    assert(r6.timed_out);
    assert(ms_now() - t0 < 2000);
    tam_exec_result_free(&r6);

    /* A missing program reports 127. */
    const char *const missing[] = {"tamlinux-no-such-program", NULL};
    tam_exec_result_t r7 = tam_exec(missing, 2000);
    assert(r7.exit_code == 127 && !r7.timed_out);
    tam_exec_result_free(&r7);

    printf("  [PASS] test_exec_opts\n");
}

static void test_paths(void) {
    char *p;
    p = tam_path_join("/a/b", "c"); assert(strcmp(p, "/a/b/c") == 0); free(p);
    p = tam_path_join("/a/b/", "/c"); assert(strcmp(p, "/a/b/c") == 0); free(p);
    p = tam_path_join("/", "c"); assert(strcmp(p, "/c") == 0); free(p);
    p = tam_path_join("/a", ""); assert(strcmp(p, "/a") == 0); free(p);

    p = tam_path_join_under("/k", "shell/pipes.html");
    assert(p && strcmp(p, "/k/shell/pipes.html") == 0); free(p);
    p = tam_path_join_under("/k", "a..b/c"); assert(p != NULL); free(p);
    assert(tam_path_join_under("/k", "../etc/passwd") == NULL);
    assert(tam_path_join_under("/k", "a/../../b") == NULL);
    assert(tam_path_join_under("/k", "a/..") == NULL);
    assert(tam_path_join_under("/k", "/etc/passwd") == NULL);
    assert(tam_path_join_under("/k", "") == NULL);

    char tmpl[] = "/tmp/libtam-test-XXXXXX";
    char *root = mkdtemp(tmpl);
    assert(root);
    char *deep = tam_path_join(root, "x/y/z");
    assert(tam_fs_mkdir_p(deep, 0700));
    assert(tam_fs_mkdir_p(deep, 0700));
    free(deep);

    setenv("XDG_STATE_HOME", root, 1);
    p = tam_xdg_path(TAM_XDG_STATE, "tam", "level", true);
    char *want = tam_path_join(root, "tamlinux/tam/level");
    assert(p && strcmp(p, want) == 0);
    free(want);
    free(p);
    p = tam_xdg_path(TAM_XDG_STATE, "tam", NULL, false);
    struct stat st;
    assert(p && stat(p, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700);
    free(p);
    assert(tam_xdg_path(TAM_XDG_STATE, "tam", "../escape", false) == NULL);
    assert(tam_xdg_path(TAM_XDG_STATE, "a/b", NULL, false) == NULL);

    /* A relative base is ignored, as the XDG specification says. */
    setenv("XDG_STATE_HOME", "relative", 1);
    setenv("HOME", root, 1);
    p = tam_xdg_path(TAM_XDG_STATE, "tam", NULL, false);
    want = tam_path_join(root, ".local/state/tamlinux/tam");
    assert(p && strcmp(p, want) == 0);
    free(want);
    free(p);
    unsetenv("XDG_RUNTIME_DIR");
    assert(tam_xdg_path(TAM_XDG_RUNTIME, "tam", NULL, false) == NULL);

    const char *const rm[] = {"rm", "-rf", root, NULL};
    tam_exec_result_t r = tam_exec(rm, 5000);
    tam_exec_result_free(&r);
    printf("  [PASS] test_paths\n");
}

static void test_json(void) {
    tam_str_t s = tam_str_new("{\"v\":\"");
    s = tam_str_cat_json(s, "a\"b\\c\nd\te\x01" "f caf\xc3\xa9");
    s = tam_str_cat(s, "\"}");
    assert(strcmp(s, "{\"v\":\"a\\\"b\\\\c\\nd\\te\\u0001f caf\xc3\xa9\"}") == 0);
    tam_str_free(s);
    printf("  [PASS] test_json\n");
}

int main(void) {
    printf("Running libtam test suite...\n");
    tam_log_set_level(TAM_LOG_WARN);

    test_version();
    test_str();
    test_fs();
    test_fs_read_kinds();
    test_write_atomic_modes();
    test_ini();
    test_exec();
    test_exec_opts();
    test_paths();
    test_json();

    printf("All libtam tests passed successfully!\n");
    return 0;
}
