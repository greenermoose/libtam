#define _GNU_SOURCE
#include "tam.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkdir_p(const char *dir) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", dir);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

int main(void) {
    char tmpl[] = "/tmp/test-tam-cultivar-XXXXXX";
    char *root = mkdtemp(tmpl);
    assert(root != NULL);

    char pending[512];
    snprintf(pending, sizeof(pending), "%s/pending.json", root);
    assert(tam_install_pending(pending) == 0);
    write_text(pending, "{}\n");
    assert(tam_install_pending(pending) == 1);
    assert(unlink(pending) == 0);
    assert(tam_install_pending(pending) == 0);

    char ver[64];
    assert(tam_cultivar_read_version(root, ver, sizeof(ver)) == -1);

    char cur_dir[512], file[600];
    snprintf(cur_dir, sizeof(cur_dir), "%s/usr/lib/tamlinux/current", root);
    mkdir_p(cur_dir);
    snprintf(file, sizeof(file), "%s/VERSION", cur_dir);
    write_text(file, "0.6.37\n");
    assert(tam_cultivar_read_version(root, ver, sizeof(ver)) == 0);
    assert(strcmp(ver, "0.6.37") == 0);

    puts("test_cultivar: OK");
    return 0;
}
