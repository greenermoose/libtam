#define _GNU_SOURCE
#include "tam.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_record(const char *path, const char *text) {
    FILE *stream = fopen(path, "w");
    assert(stream);
    assert(fputs(text, stream) >= 0);
    assert(fclose(stream) == 0);
}

int main(void) {
    char directory[] = "/tmp/tam-bundle-guard-XXXXXX";
    assert(mkdtemp(directory));
    char path[256], link[256], text[256];
    snprintf(path, sizeof path, "%s/transaction", directory);
    snprintf(link, sizeof link, "%s/link", directory);
    assert(tam_bundle_guard(path) == 0);
    const char *states[] = {"staging", "ready", "complete", "applying", "activating"};
    for (size_t i = 0; i < sizeof states / sizeof *states; i++) {
        snprintf(text, sizeof text, "format 1\nid fixture\nstate %s\n", states[i]);
        write_record(path, text);
        assert((tam_bundle_guard(path) == 0) == (i < 3));
        if (i >= 3) assert(errno == EBUSY);
    }
    const char *invalid[] = {"format 1\n", "format 1\nstate unknown\n",
                             "format 1\nstate ready\nstate complete\n",
                             "format 2\nstate complete\n"};
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        write_record(path, invalid[i]);
        assert(tam_bundle_guard(path) == -1);
    }
    write_record(path, "format 1\nstate complete\n");
    assert(chmod(path, 0666) == 0);
    assert(tam_bundle_guard(path) == -1);
    assert(chmod(path, 0600) == 0);
    assert(symlink(path, link) == 0);
    assert(tam_bundle_guard(link) == -1);
    assert(unlink(link) == 0);
    assert(truncate(path, 4097) == 0);
    assert(tam_bundle_guard(path) == -1);
    assert(unlink(path) == 0);
    assert(mkfifo(path, 0600) == 0);
    assert(tam_bundle_guard(path) == -1);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    puts("bundle startup guard passed");
    return 0;
}
