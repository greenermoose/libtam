/* Tests for file bundle primitives: SHA-256, safe relative paths, line
 * records, the directory walk, exchange, clone or copy, tree removal, locks
 * and atomic writes. Works in $TAM_TEST_DIR, else $TMPDIR, else /tmp. */
#include "tam.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char base[4096];
static int base_fd = -1;

static void put(const char *rel, const char *text) {
    int fd = openat(base_fd, rel, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    assert(fd >= 0);
    size_t len = strlen(text);
    assert(write(fd, text, len) == (ssize_t)len);
    close(fd);
}

static char *get(const char *rel) {
    int fd = openat(base_fd, rel, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    static char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    assert(n >= 0);
    buf[n] = '\0';
    return buf;
}

static bool exists(const char *rel) {
    struct stat st;
    return fstatat(base_fd, rel, &st, AT_SYMLINK_NOFOLLOW) == 0;
}

static void hex_of(const void *data, size_t len, char out[65]) {
    tam_sha256_t ctx;
    uint8_t d[TAM_SHA256_LEN];
    tam_sha256_init(&ctx);
    tam_sha256_update(&ctx, data, len);
    tam_sha256_final(&ctx, d);
    tam_sha256_hex(d, out);
}

static void test_sha256(void) {
    char hex[65];
    hex_of("", 0, hex);
    assert(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    hex_of("abc", 3, hex);
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    hex_of(m, strlen(m), hex);
    assert(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);

    /* One million 'a', fed in uneven pieces across block boundaries. */
    static char a[1000000];
    memset(a, 'a', sizeof a);
    tam_sha256_t ctx;
    uint8_t d[TAM_SHA256_LEN];
    tam_sha256_init(&ctx);
    for (size_t off = 0, step = 1; off < sizeof a; off += step, step = step * 3 % 997 + 1) {
        size_t n = off + step > sizeof a ? sizeof a - off : step;
        tam_sha256_update(&ctx, a + off, n);
    }
    tam_sha256_final(&ctx, d);
    tam_sha256_hex(d, hex);
    assert(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);

    /* Byte at a time equals all at once, for every length up to 200. */
    unsigned char buf[200];
    for (int i = 0; i < 200; i++) buf[i] = (unsigned char)(i * 7 + 1);
    for (size_t len = 0; len <= sizeof buf; len++) {
        char one[65], many[65];
        hex_of(buf, len, one);
        tam_sha256_init(&ctx);
        for (size_t i = 0; i < len; i++) tam_sha256_update(&ctx, buf + i, 1);
        tam_sha256_final(&ctx, d);
        tam_sha256_hex(d, many);
        assert(strcmp(one, many) == 0);
    }

    /* Files: by path, and a final symbolic link is refused. */
    put("hash-me", "abc");
    char path[4200];
    snprintf(path, sizeof path, "%s/hash-me", base);
    assert(tam_sha256_file(path, d) == 0);
    tam_sha256_hex(d, hex);
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    assert(symlinkat("hash-me", base_fd, "hash-link") == 0);
    snprintf(path, sizeof path, "%s/hash-link", base);
    assert(tam_sha256_file(path, d) == -1 && errno == ELOOP);
    printf("  [PASS] test_sha256\n");
}

static void test_safe_rel(void) {
    const char *good[] = {"a", "a/b", "share/man/man1/tam.1", "with space/x", ".hidden", "a..b", "...", NULL};
    const char *bad[] = {"", "/abs", "a/", "a//b", ".", "..", "a/./b", "a/../b", "../a", "a/..",
                         "tab\there", "new\nline", "del\x7f", NULL};
    for (int i = 0; good[i]; i++) assert(tam_path_is_safe_rel(good[i]));
    for (int i = 0; bad[i]; i++) assert(!tam_path_is_safe_rel(bad[i]));
    assert(!tam_path_is_safe_rel(NULL));
    printf("  [PASS] test_safe_rel\n");
}

typedef struct {
    int    count;
    char   seen[8][256];
    int    stop_at;
} rec_seen_t;

static int rec_cb(void *user, size_t line, int format, char **fields, size_t n) {
    rec_seen_t *s = user;
    (void)line;
    assert(format >= 1);
    if (s->stop_at && s->count + 1 == s->stop_at) return 7;
    char *out = s->seen[s->count++];
    out[0] = '\0';
    for (size_t i = 0; i < n; i++) {
        strcat(out, "[");
        strcat(out, fields[i]);
        strcat(out, "]");
    }
    return 0;
}

static int parse(const char *text, size_t max_fields, int max_format, rec_seen_t *s,
                 tam_rec_error_t *err) {
    memset(s, 0, sizeof *s);
    return tam_rec_parse(text, strlen(text), max_fields, max_format, rec_cb, s, err);
}

static void test_records(void) {
    rec_seen_t s;
    tam_rec_error_t err;
    const char *ok = "# comment\n\nformat 1\n"
                     "f 0755  sha256:ab   bin/tam\n"
                     "f 0644 sha256:cd share/with space/x  \n"
                     "\t# indented comment\n"
                     "d 0755 -\n";
    assert(parse(ok, 4, 1, &s, &err) == 0);
    assert(err.format == 1 && s.count == 3);
    assert(strcmp(s.seen[0], "[f][0755][sha256:ab][bin/tam]") == 0);
    assert(strcmp(s.seen[1], "[f][0644][sha256:cd][share/with space/x]") == 0);
    assert(strcmp(s.seen[2], "[d][0755][-]") == 0);

    /* max_fields 1: the whole line is one field. */
    assert(parse("format 2\nkey with  spaces\n", 1, 2, &s, &err) == 0);
    assert(strcmp(s.seen[0], "[key with  spaces]") == 0);

    assert(parse("format 1\nf a b", 4, 1, &s, &err) == -1 && strstr(err.reason, "final newline"));
    assert(parse("f a b\n", 4, 1, &s, &err) == -1 && err.line == 1 && strstr(err.reason, "format N"));
    assert(parse("format 3\n", 4, 2, &s, &err) == -1 && err.format == 3 && strstr(err.reason, "newer"));
    assert(parse("format 0\n", 4, 2, &s, &err) == -1);
    assert(parse("format x\n", 4, 2, &s, &err) == -1);
    assert(parse("format 1\nf a\rb\n", 4, 1, &s, &err) == -1 && err.line == 2);
    assert(parse("# only a comment\n", 4, 1, &s, &err) == -1 && strstr(err.reason, "no format"));
    assert(tam_rec_parse("format 1\n\0x\n", 11, 4, 1, rec_cb, &s, &err) == -1 && strstr(err.reason, "NUL"));
    assert(parse("", 4, 1, &s, &err) == -1);

    memset(&s, 0, sizeof s);
    s.stop_at = 2;
    assert(tam_rec_parse("format 1\na\nb\nc\n", 15, 2, 1, rec_cb, &s, NULL) == 7 && s.count == 1);

    /* Writer, and a round trip. */
    tam_str_t out = tam_str_new("format 1\n");
    const char *rec1[] = {"f", "0755", "sha256:ab", "share/with space/x"};
    tam_str_t r = tam_rec_cat(out, rec1, 4);
    assert(r);
    out = r;
    const char *bad1[] = {"f", "has space", "x"};
    const char *bad2[] = {"f", ""};
    const char *bad3[] = {"f", "trailing "};
    const char *bad4[] = {"f", "tab\tin"};
    const char *bad5[] = {"#f", "x"};
    size_t before = tam_str_len(out);
    assert(!tam_rec_cat(out, bad1, 3));
    assert(!tam_rec_cat(out, bad2, 2));
    assert(!tam_rec_cat(out, bad3, 2));
    assert(!tam_rec_cat(out, bad4, 2));
    assert(!tam_rec_cat(out, bad5, 2));
    assert(tam_str_len(out) == before);
    assert(strcmp(out, "format 1\nf 0755 sha256:ab share/with space/x\n") == 0);
    assert(parse(out, 4, 1, &s, &err) == 0);
    assert(strcmp(s.seen[0], "[f][0755][sha256:ab][share/with space/x]") == 0);
    tam_str_free(out);
    printf("  [PASS] test_records\n");
}

typedef struct {
    char order[1024];
    int  skip_dir;
    int  stop_after;
    int  seen;
} walk_seen_t;

static int walk_cb(const tam_walk_entry_t *e, void *user) {
    walk_seen_t *w = user;
    w->seen++;
    char mark = S_ISDIR(e->st->st_mode) ? 'd' : S_ISLNK(e->st->st_mode) ? 'l' : 'f';
    size_t len = strlen(w->order);
    snprintf(w->order + len, sizeof w->order - len, "%c:%s:%d ", mark, e->path, e->depth);
    /* dirfd/name reach the same entry. */
    struct stat st;
    assert(fstatat(e->dirfd, e->name, &st, AT_SYMLINK_NOFOLLOW) == 0 && st.st_ino == e->st->st_ino);
    if (w->stop_after && w->seen == w->stop_after) return -5;
    if (w->skip_dir && S_ISDIR(e->st->st_mode) && strcmp(e->name, "a") == 0) return TAM_WALK_SKIP;
    return TAM_WALK_CONTINUE;
}

static void test_walk(void) {
    assert(mkdirat(base_fd, "w", 0755) == 0);
    assert(mkdirat(base_fd, "w/a", 0755) == 0);
    put("w/a/b", "1");
    put("w/a-c", "2");
    assert(mkdirat(base_fd, "w/z", 0755) == 0);
    assert(symlinkat("../a", base_fd, "w/z/link") == 0);
    char root[4200];
    snprintf(root, sizeof root, "%s/w", base);

    walk_seen_t w = {0};
    assert(tam_fs_walk(root, TAM_WALK_ONE_DEVICE, walk_cb, &w) == 0);
    assert(strcmp(w.order, "d:a:1 f:a/b:2 f:a-c:1 d:z:1 l:z/link:2 ") == 0);

    memset(&w, 0, sizeof w);
    w.skip_dir = 1;
    assert(tam_fs_walk(root, 0, walk_cb, &w) == 0);
    assert(strcmp(w.order, "d:a:1 f:a-c:1 d:z:1 l:z/link:2 ") == 0);

    memset(&w, 0, sizeof w);
    w.stop_after = 2;
    assert(tam_fs_walk(root, 0, walk_cb, &w) == -5 && w.seen == 2);

    snprintf(root, sizeof root, "%s/missing", base);
    assert(tam_fs_walk(root, 0, walk_cb, &w) == -1 && errno == ENOENT);
    printf("  [PASS] test_walk\n");
}

static void test_exchange(void) {
    assert(mkdirat(base_fd, "live", 0755) == 0 && mkdirat(base_fd, "staging", 0755) == 0);
    assert(mkdirat(base_fd, "live/sub", 0755) == 0 && mkdirat(base_fd, "staging/sub", 0755) == 0);
    put("live/sub/f", "old");
    put("live/only-old", "x");
    put("staging/sub/f", "new");
    int held = openat(base_fd, "live/sub/f", O_RDONLY | O_CLOEXEC);
    assert(held >= 0);

    assert(tam_fs_exchange(base_fd, "staging", base_fd, "live") == 0);
    assert(tam_fs_sync_dirat(base_fd, "."));
    assert(strcmp(get("live/sub/f"), "new") == 0);
    assert(!exists("live/only-old"));
    assert(strcmp(get("staging/sub/f"), "old") == 0 && exists("staging/only-old"));
    char buf[4] = {0};
    assert(pread(held, buf, 3, 0) == 3 && memcmp(buf, "old", 3) == 0);
    close(held);

    assert(tam_fs_exchange(base_fd, "staging", base_fd, "live") == 0);
    assert(strcmp(get("live/sub/f"), "old") == 0);
    assert(tam_fs_exchange(base_fd, "staging", base_fd, "absent") == -1 && errno == ENOENT);
    printf("  [PASS] test_exchange\n");
}

static void test_clone_or_copy(void) {
    static char big[300000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (char)('a' + i % 26);
    int src = openat(base_fd, "src", O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    assert(src >= 0 && write(src, big, sizeof big) == (ssize_t)sizeof big);
    /* Leave src's offset at the end: the copy must still start at 0. */
    put("dst", "a much longer file that must be replaced, not merged, by the copy................");
    int dst = openat(base_fd, "dst", O_RDWR | O_CLOEXEC);
    bool cloned = true;
    assert(tam_fs_clone_or_copy(src, dst, &cloned) == 0);
    struct stat st;
    assert(fstat(dst, &st) == 0 && st.st_size == (off_t)sizeof big);
    uint8_t a[TAM_SHA256_LEN], b[TAM_SHA256_LEN];
    assert(lseek(src, 0, SEEK_SET) == 0 && tam_sha256_fd(src, a) == 0);
    assert(lseek(dst, 0, SEEK_SET) == 0 && tam_sha256_fd(dst, b) == 0);
    assert(memcmp(a, b, sizeof a) == 0);
    close(dst);

    /* A small source over a large destination leaves it the source's size. */
    put("small", "hi");
    int small = openat(base_fd, "small", O_RDONLY | O_CLOEXEC);
    dst = openat(base_fd, "dst", O_RDWR | O_CLOEXEC);
    assert(tam_fs_clone_or_copy(small, dst, NULL) == 0);
    close(dst);
    close(small);
    close(src);
    assert(strcmp(get("dst"), "hi") == 0);
    printf("  [PASS] test_clone_or_copy (%s)\n", cloned ? "cloned" : "copied");
}

static void test_remove_tree(void) {
    assert(mkdirat(base_fd, "keep", 0755) == 0);
    put("keep/precious", "do not delete");
    assert(mkdirat(base_fd, "doomed", 0755) == 0);
    assert(mkdirat(base_fd, "doomed/deep", 0755) == 0);
    assert(mkdirat(base_fd, "doomed/deep/er", 0755) == 0);
    for (int i = 0; i < 40; i++) {
        char name[64];
        snprintf(name, sizeof name, "doomed/deep/f%02d", i);
        put(name, "x");
    }
    assert(symlinkat("../keep", base_fd, "doomed/link-to-dir") == 0);
    assert(symlinkat("../keep/precious", base_fd, "doomed/deep/link-to-file") == 0);

    assert(tam_fs_remove_tree(base_fd, "doomed") == 0);
    assert(!exists("doomed"));
    assert(strcmp(get("keep/precious"), "do not delete") == 0);
    assert(tam_fs_remove_tree(base_fd, "doomed") == 0);       /* missing is success */
    put("plain", "x");
    assert(tam_fs_remove_tree(base_fd, "plain") == 0 && !exists("plain"));
    assert(tam_fs_remove_tree(base_fd, "..") == -1 && errno == EINVAL);
    assert(tam_fs_remove_tree(base_fd, "keep/precious") == -1 && errno == EINVAL);
    assert(tam_fs_remove_tree(base_fd, "") == -1 && errno == EINVAL);
    printf("  [PASS] test_remove_tree\n");
}

static void test_lock(void) {
    char path[4200];
    snprintf(path, sizeof path, "%s/lock", base);
    int a = tam_fs_lock(path, 0);
    assert(a >= 0);
    int b = tam_fs_lock(path, 120);
    assert(b == -1 && errno == EWOULDBLOCK);
    tam_fs_unlock(a);
    b = tam_fs_lock(path, 0);
    assert(b >= 0);
    tam_fs_unlock(b);
    struct stat st;
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    printf("  [PASS] test_lock\n");
}

static void test_write_atomic(void) {
    char path[4200];
    snprintf(path, sizeof path, "%s/atomic", base);
    assert(tam_fs_write_atomic(path, "one", 3, 0640));
    assert(tam_fs_write_atomic(path, "two", 3, 0640));
    assert(strcmp(get("atomic"), "two") == 0);
    struct stat st;
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0640);
    assert(tam_fs_write_atomic(path, NULL, 0, 0644) && strcmp(get("atomic"), "") == 0);
    snprintf(path, sizeof path, "%s/no-such-dir/atomic", base);
    assert(!tam_fs_write_atomic(path, "x", 1, 0644));
    assert(tam_fs_sync_dir(base));
    printf("  [PASS] test_write_atomic\n");
}

int main(void) {
    const char *dir = getenv("TAM_TEST_DIR");
    if (!dir || !*dir) dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = "/tmp";
    snprintf(base, sizeof base, "%s/test_tam_files.XXXXXX", dir);
    assert(mkdtemp(base));
    base_fd = open(base, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(base_fd >= 0);
    printf("Running libtam file tests in %s...\n", base);

    test_sha256();
    test_safe_rel();
    test_records();
    test_walk();
    test_exchange();
    test_clone_or_copy();
    test_remove_tree();
    test_lock();
    test_write_atomic();

    close(base_fd);
    char *slash = strrchr(base, '/');
    *slash = '\0';
    int parent = open(base, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(parent >= 0 && tam_fs_remove_tree(parent, slash + 1) == 0);
    close(parent);
    printf("All libtam file tests passed.\n");
    return 0;
}
