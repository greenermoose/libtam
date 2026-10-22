#include "tam_portal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_portal(void) {
    /* Test URI percent decoding and conversion */
    char *p1 = tam_portal_uri_to_path("file:///home/fred/test%20file.txt");
    assert(p1 && strcmp(p1, "/home/fred/test file.txt") == 0);
    free(p1);

    char *p2 = tam_portal_uri_to_path("file://localhost/tmp/hello%2C%20world%21");
    assert(p2 && strcmp(p2, "/tmp/hello, world!") == 0);
    free(p2);

    char *p3 = tam_portal_uri_to_path("/regular/path");
    assert(p3 && strcmp(p3, "/regular/path") == 0);
    free(p3);

    assert(tam_portal_uri_to_path(NULL) == NULL);

    /* Test result freeing with NULL and allocated buffers */
    tam_portal_file_result_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.status = 2;
    dummy.error_message = strdup("Test error message");
    dummy.count = 2;
    dummy.paths = calloc(3, sizeof(char *));
    dummy.paths[0] = strdup("/path/one");
    dummy.paths[1] = strdup("/path/two");
    tam_portal_file_result_free(&dummy);
    assert(dummy.paths == NULL);
    assert(dummy.error_message == NULL);
    assert(dummy.count == 0);

    /* Also verify result_free handles NULL safely */
    tam_portal_file_result_free(NULL);

    printf("  [PASS] test_portal\n");
}

int main(void) {
    printf("Running libtam-portal tests...\n");
    test_portal();
    printf("All libtam-portal tests passed successfully!\n");
    return 0;
}
