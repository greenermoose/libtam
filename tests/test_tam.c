#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

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
    printf("  [PASS] test_str\n");
}

static void test_fs(void) {
    const char *test_path = "/tmp/test_tam_fs.txt";
    const char *data = "Tamlinux atomic write test data\n";
    size_t len = strlen(data);

    assert(tam_fs_write_atomic(test_path, data, len, 0644));
    assert(tam_fs_file_exists(test_path));

    size_t read_len = 0;
    char *content = tam_fs_read_file(test_path, &read_len);
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
    test_ini();
    test_exec();
    test_exec_opts();
    test_paths();
    test_json();

    printf("All libtam tests passed successfully!\n");
    return 0;
}
