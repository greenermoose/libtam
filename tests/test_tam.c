#include "tam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

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

int main(void) {
    printf("Running libtam test suite...\n");
    tam_log_set_level(TAM_LOG_WARN);

    test_version();
    test_str();
    test_fs();
    test_ini();
    test_exec();

    printf("All libtam tests passed successfully!\n");
    return 0;
}
