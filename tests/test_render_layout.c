#include "tam_render.h"

#include <assert.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>

static void rows(const char *input, size_t length, int width,
                 const char *const *expected, size_t count)
{
    FILE *fp = tmpfile();
    assert(fp);
    assert(fwrite(input, 1, length, fp) == length);
    assert(fflush(fp) == 0);
    rewind(fp);
    struct tam_source s;
    assert(tam_source_open(&s, fileno(fp)) == 0);

    struct tam_position row = {0};
    char out[1024];
    for (size_t i = 0; i < count; i++) {
        struct tam_position next;
        assert(tam_render_row(&s, row, width, out, &next) == TAM_RENDER_ROW);
        assert(tam_position_compare(row, next) < 0);
        if (strcmp(out, expected[i]))
            fprintf(stderr, "row %zu: got [%s], expected [%s]\n",
                    i + 1, out, expected[i]);
        assert(!strcmp(out, expected[i]));
        row = next;
    }
    struct tam_position next;
    assert(tam_render_row(&s, row, width, out, &next) == TAM_RENDER_EOF);
    tam_source_close(&s);
    fclose(fp);
}

static void rows_h(const char *input, size_t length, int width, int skip,
                   bool chop, const char *const *expected, size_t count)
{
    FILE *fp = tmpfile();
    assert(fp);
    assert(fwrite(input, 1, length, fp) == length);
    assert(fflush(fp) == 0);
    rewind(fp);
    struct tam_source s;
    assert(tam_source_open(&s, fileno(fp)) == 0);

    struct tam_position row = {0};
    char out[1024];
    for (size_t i = 0; i < count; i++) {
        struct tam_position next;
        assert(tam_render_row_h(&s, row, width, skip, chop, out, &next) == TAM_RENDER_ROW);
        assert(tam_position_compare(row, next) < 0);
        if (strcmp(out, expected[i]))
            fprintf(stderr, "row_h %zu: got [%s], expected [%s]\n",
                    i + 1, out, expected[i]);
        assert(!strcmp(out, expected[i]));
        row = next;
    }
    struct tam_position next;
    assert(tam_render_row_h(&s, row, width, skip, chop, out, &next) == TAM_RENDER_EOF);
    tam_source_close(&s);
    fclose(fp);
}

#define ROWS(input, width, ...) do { \
    const char *const expected[] = {__VA_ARGS__}; \
    rows((input), sizeof(input) - 1, width, expected, \
         sizeof(expected) / sizeof(expected[0])); \
} while (0)

#define ROWS_H(input, width, skip, chop, ...) do { \
    const char *const expected[] = {__VA_ARGS__}; \
    rows_h((input), sizeof(input) - 1, width, skip, chop, expected, \
           sizeof(expected) / sizeof(expected[0])); \
} while (0)

int main(void)
{
    assert(setlocale(LC_CTYPE, "C.UTF-8") || setlocale(LC_CTYPE, ""));

    rows("", 0, 5, NULL, 0);
    ROWS("12345\r\n12345\nx\n\nend", 5, "12345", "12345", "x", "", "end");
    ROWS("\ta\tb", 5, "     ", "   a ", "     ", " b");
    ROWS("\t\t", 1, " ", " ", " ", " ", " ", " ", " ", " ",
                     " ", " ", " ", " ", " ", " ", " ", " ");
    ROWS("é中a\n", 2, "é", "中", "a");
    ROWS("x\033\x9b\0\177\xc2\x9b\xc3\r", 24,
         "x^[�^@^??�^M");
    ROWS("a\xcc\x81" "bc\n", 2, "a\xcc\x81" "b", "c");
    ROWS("a\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81"
         "\xcc\x81\xcc\x81\xcc\x81\xcc\x81", 1,
         "a\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81"
         "\xcc\x81\xcc\x81\xcc\x81", "?");

    char label[32];
    tam_render_label(label, sizeof(label), 12, "é\033\nxyz");
    assert(!strcmp(label, "é^[^Jxyz"));

    ROWS_H("abcdefgh\n1234\n", 4, 0, true, "abcd", "1234");
    ROWS_H("abcdefgh\n12\n", 4, 3, false, "defg", "");
    ROWS_H("\txyz\n", 5, 6, false, "  xyz");
    ROWS_H("a中bc\n", 4, 2, false, " bc");
    ROWS_H("ab中c\n", 3, 0, true, "ab");

    // Styled rows: document spans, search override, horizontal scroll, linux_vt, and ESC sanitization.
    {
        FILE *fp = tmpfile();
        assert(fp);
        const char *txt = "hello \033world\n";
        assert(fwrite(txt, 1, strlen(txt), fp) == strlen(txt));
        assert(fflush(fp) == 0);
        rewind(fp);
        struct tam_source s;
        assert(tam_source_open(&s, fileno(fp)) == 0);

        struct tam_render_styles doc = {0};
        assert(tam_render_styles_push(&doc, 0, 5, TAM_RENDER_STYLE_BOLD));
        assert(tam_render_styles_push(&doc, 6, 12, TAM_RENDER_STYLE_ITALIC));
        struct tam_render_styles srch = {0};
        assert(tam_render_styles_push(&srch, 2, 4, TAM_RENDER_STYLE_SEARCH_CUR));

        struct tam_render_row_style rs = {
            .doc_spans = doc.spans,
            .doc_count = doc.count,
            .search_spans = srch.spans,
            .search_count = srch.count,
            .focused_link = -1,
            .linux_vt = false
        };
        char out[1024];
        struct tam_position next;
        assert(tam_render_row_styled(&s, (struct tam_position){0}, 40, 0, false, &rs,
                               out, &next) == TAM_RENDER_ROW);
        // "he" bold, "ll" search_cur, "o" bold, " " none, "^[world" italic (ESC sanitized!).
        assert(!strcmp(out, "\033[1mhe\033[0m\033[1;30;43mll\033[0m\033[1mo\033[0m \033[3m^[world\033[0m"));

        // Linux VT maps italic to cyan (\033[36m) instead of \033[3m.
        rs.linux_vt = true;
        assert(tam_render_row_styled(&s, (struct tam_position){0}, 40, 0, false, &rs,
                               out, &next) == TAM_RENDER_ROW);
        assert(strstr(out, "\033[36m^[world\033[0m") != NULL);

        // Horizontal scroll preserves styles on visible slice.
        rs.linux_vt = false;
        assert(tam_render_row_styled(&s, (struct tam_position){0}, 6, 2, false, &rs,
                               out, &next) == TAM_RENDER_ROW);
        assert(!strcmp(out, "\033[1;30;43mll\033[0m\033[1mo\033[0m \033[3m^[\033[0m"));

        tam_render_styles_free(&doc);
        tam_render_styles_free(&srch);
        tam_source_close(&s);
        fclose(fp);
    }

    // Attention line, focused link highlight, and OSC 8 hyperlinks.
    {
        FILE *fp = tmpfile();
        assert(fp);
        const char *txt = "See Tamlinux docs\n";
        assert(fwrite(txt, 1, strlen(txt), fp) == strlen(txt));
        assert(fflush(fp) == 0);
        rewind(fp);
        struct tam_source s;
        assert(tam_source_open(&s, fileno(fp)) == 0);

        struct tam_render_links lk = {0};
        ssize_t idx = tam_render_links_add_item(&lk, "https://tamlinux.org", 20, 4);
        assert(idx == 0);
        assert(tam_render_links_push_span(&lk, 4, 12, 0));

        struct tam_render_row_style rs = {
            .links = &lk,
            .focused_link = 0,
            .attention = true,
            .linux_vt = false,
            .osc8 = true
        };
        char out[8192];
        struct tam_position next;
        assert(tam_render_row_styled(&s, (struct tam_position){0}, 20, 0, false, &rs,
                               out, &next) == TAM_RENDER_ROW);
        assert(strstr(out, "\033[38;5;15;48;5;236m") != NULL);
        assert(strstr(out, "\033]8;;https://tamlinux.org\033\\") != NULL);
        assert(strstr(out, "\033[1;30;46mTamlinux") != NULL);
        assert(strstr(out, "\033]8;;\033\\") != NULL);
        assert(strcmp(out + strlen(out) - 4, "\033[0m") == 0);

        // Linux VT attention uses reverse (\033[7m) and no OSC 8 when osc8=false.
        rs.linux_vt = true;
        rs.osc8 = false;
        assert(tam_render_row_styled(&s, (struct tam_position){0}, 20, 0, false, &rs,
                               out, &next) == TAM_RENDER_ROW);
        assert(strstr(out, "\033[7m") != NULL);
        assert(strstr(out, "\033]8;;") == NULL);

        tam_render_links_free(&lk);
        tam_source_close(&s);
        fclose(fp);
    }

    puts("layout: ok");
    return 0;
}
