#include "render_internal.h"
#include "tam_render.h"

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *render_str_all(enum tam_render_format fmt, const char *input,
                            int width, bool ascii,
                            struct tam_render_styles *styles_out,
                            struct tam_render_links *links_out)
{
    struct tam_render_buf b;
    tam_render_buf_init(&b);
    size_t len = strlen(input);
    if (fmt == TAM_RENDER_FORMAT_MARKDOWN)
        tam_render_markdown(&b, input, len, width, ascii);
    else if (fmt == TAM_RENDER_FORMAT_HTML)
        tam_render_html(&b, input, len, width, ascii);
    else
        tam_render_mermaid(&b, input, len, width, ascii);
    assert(!b.oom);
    // Rendered text must never contain raw ESC bytes.
    if (b.data)
        assert(memchr(b.data, '\033', b.len) == NULL);
    // Verify style spans are sorted and non-overlapping.
    for (size_t i = 0; i < b.styles.count; i++) {
        assert(b.styles.spans[i].start < b.styles.spans[i].end);
        assert((size_t)b.styles.spans[i].end <= b.len);
        if (i > 0)
            assert(b.styles.spans[i - 1].end <= b.styles.spans[i].start);
    }
    // Verify link spans are sorted, non-overlapping, and in bounds.
    for (size_t i = 0; i < b.links.span_count; i++) {
        assert(b.links.spans[i].start < b.links.spans[i].end);
        assert((size_t)b.links.spans[i].end <= b.len);
        assert(b.links.spans[i].link_index < b.links.count);
        if (i > 0)
            assert(b.links.spans[i - 1].end <= b.links.spans[i].start);
    }
    if (styles_out) {
        *styles_out = b.styles;
        b.styles = (struct tam_render_styles){0};
    } else {
        tam_render_styles_free(&b.styles);
    }
    if (links_out) {
        *links_out = b.links;
        b.links = (struct tam_render_links){0};
    } else {
        tam_render_links_free(&b.links);
    }
    char *result = b.data ? b.data : strdup("");
    b.data = NULL;
    tam_render_buf_free(&b);
    return result;
}

static char *render_str_styles(enum tam_render_format fmt, const char *input,
                               int width, bool ascii,
                               struct tam_render_styles *styles_out)
{
    return render_str_all(fmt, input, width, ascii, styles_out, NULL);
}

static char *render_str(enum tam_render_format fmt, const char *input, int width, bool ascii)
{
    return render_str_styles(fmt, input, width, ascii, NULL);
}

static bool has_style(const struct tam_render_styles *st, unsigned char style)
{
    for (size_t i = 0; i < st->count; i++) {
        if (st->spans[i].style == style)
            return true;
    }
    return false;
}

static void test_detect(void)
{
    assert(tam_render_detect_format("doc.md", NULL, false) == TAM_RENDER_FORMAT_MARKDOWN);
    assert(tam_render_detect_format("page.HTML", NULL, false) == TAM_RENDER_FORMAT_HTML);
    assert(tam_render_detect_format("flow.mmd", NULL, false) == TAM_RENDER_FORMAT_MERMAID);
    assert(tam_render_detect_format("notes.txt", NULL, false) == TAM_RENDER_FORMAT_RAW);
    assert(tam_render_detect_format("notes.txt", NULL, true) == TAM_RENDER_FORMAT_MARKDOWN);

    FILE *fp = tmpfile();
    assert(fp);
    const char *html = "<!DOCTYPE html><html><body><p>Hi</p></body></html>";
    assert(fwrite(html, 1, strlen(html), fp) == strlen(html));
    assert(fflush(fp) == 0);
    rewind(fp);
    struct tam_source s;
    assert(tam_source_open(&s, fileno(fp)) == 0);
    assert(tam_render_detect_format("-", &s, false) == TAM_RENDER_FORMAT_HTML);
    tam_source_close(&s);
    fclose(fp);
}

static void test_short_source(void)
{
    FILE *fp = tmpfile();
    assert(fp);
    assert(fputs("# short\n", fp) >= 0 && fflush(fp) == 0);
    struct tam_source source = {.fd = fileno(fp), .spool = fp, .eof = true,
                            .size = 32, .block = -1};
    struct tam_source out;
    assert(tam_render_source(&source, "test.md", 80, true, true,
                              &out, NULL, NULL, NULL) == 0);
    tam_source_close(&out);
    tam_source_close(&source);

    // Regular file that caches block 0 and then shrinks on disk before re-render.
    FILE *reg = tmpfile();
    assert(reg);
    assert(fputs("# Heading\n\nFirst paragraph.\n\nSecond paragraph.\n", reg) >= 0 &&
           fflush(reg) == 0);
    rewind(reg);
    struct tam_source reg_src;
    assert(tam_source_open(&reg_src, fileno(reg)) == 0);
    assert(reg_src.spool == NULL);
    assert(tam_render_source(&reg_src, "doc.md", 80, true, true,
                              &out, NULL, NULL, NULL) == 0);
    tam_source_close(&out);
    assert(ftruncate(fileno(reg), 9) == 0);
    assert(tam_render_source(&reg_src, "doc.md", 80, true, true,
                              &out, NULL, NULL, NULL) == 0);
    char row[128];
    struct tam_position pos = {0}, next;
    assert(tam_render_row(&out, pos, 80, row, &next) == TAM_RENDER_ROW);
    assert(strcmp(row, "Heading") == 0);
    pos = next;
    assert(tam_render_row(&out, pos, 80, row, &next) == TAM_RENDER_ROW);
    pos = next;
    assert(tam_render_row(&out, pos, 80, row, &next) == TAM_RENDER_EOF);
    tam_source_close(&out);
    tam_source_close(&reg_src);
    fclose(reg);
}

static void test_invalid_entities_and_duplicate_headings(void)
{
    char *html = render_str(TAM_RENDER_FORMAT_HTML,
        "<p>&#4294967361; &#0; &#x110000; &#xD800; &#99999999999999999; &#0000000000000065; &#xZZ;</p>",
        80, true);
    assert(strstr(html, "? ? ? ? ? A &#xZZ;"));
    free(html);

    char *html_utf8 = render_str(TAM_RENDER_FORMAT_HTML,
        "<p>&#4294967361; &#0; &#x110000; &#xD800; &#99999999999999999; &#0000000000000065; &#xZZ;</p>",
        80, false);
    assert(strstr(html_utf8,
        "\xef\xbf\xbd \xef\xbf\xbd \xef\xbf\xbd \xef\xbf\xbd \xef\xbf\xbd A &#xZZ;"));
    free(html_utf8);

    struct tam_render_buf b;
    tam_render_buf_init(&b);
    for (size_t i = 0; i < 3000; i++)
        tam__rbuf_add_heading_anchor(&b, "Same", 4, i);
    assert(!b.oom && b.links.anchor_count == 3000);
    assert(strcmp(b.links.anchors[2999].id, "same-2999") == 0);
    tam_render_buf_free(&b);
}

static void test_markdown(void)
{
    const char *md =
        "# Title\n\n"
        "A **bold** and `code` [link](https://example.com) paragraph that wraps nicely.\n\n"
        "- item one\n"
        "- item two\n\n"
        "> quoted line\n\n"
        "| Col1 | Col2 |\n"
        "| --- | --- |\n"
        "| a | b |\n\n"
        "```mermaid\n"
        "flowchart TD\n"
        "  A[Start] --> B[End]\n"
        "```\n";

    struct tam_render_styles st = {0};
    char *uni = render_str_styles(TAM_RENDER_FORMAT_MARKDOWN, md, 60, false, &st);
    assert(strstr(uni, "Title\n"));
    assert(strstr(uni, "\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90"));
    assert(strstr(uni, "bold and code link (https://example.com)"));
    assert(strstr(uni, "\xe2\x80\xa2 item one"));
    assert(strstr(uni, "\xe2\x94\x82 quoted line"));
    assert(strstr(uni, "\xe2\x94\x8c"));
    assert(strstr(uni, "Start"));
    assert(strstr(uni, "End"));
    assert(has_style(&st, TAM_RENDER_STYLE_H1));
    assert(has_style(&st, TAM_RENDER_STYLE_BOLD));
    assert(has_style(&st, TAM_RENDER_STYLE_CODE));
    assert(has_style(&st, TAM_RENDER_STYLE_LINK));
    assert(has_style(&st, TAM_RENDER_STYLE_LINK_URL));
    assert(has_style(&st, TAM_RENDER_STYLE_QUOTE));
    assert(has_style(&st, TAM_RENDER_STYLE_RULE));
    assert(has_style(&st, TAM_RENDER_STYLE_TABLE_HDR));
    assert(has_style(&st, TAM_RENDER_STYLE_NODE));
    tam_render_styles_free(&st);
    free(uni);

    char *asc = render_str(TAM_RENDER_FORMAT_MARKDOWN, md, 60, true);
    assert(strstr(asc, "====="));
    assert(strstr(asc, "* item one"));
    assert(strstr(asc, "| quoted line"));
    assert(strstr(asc, "+------"));
    free(asc);
}

static void test_html(void)
{
    const char *html =
        "<html><head><title>Ignored</title><style>body{}</style></head>"
        "<body><h1>Hello &amp; Welcome</h1>"
        "<script>alert(1)</script>"
        "<p>Visit <a href=\"https://tamlinux.org\">Tamlinux</a> &mdash; &ldquo;ok&rdquo; &lsquo;y&rsquo;.</p>"
        "<ul><li>One</li><li>Two</li></ul>"
        "<table><tr><th>K</th><th>V</th></tr><tr><td>x</td><td>1</td></tr></table>"
        "</body></html>";

    struct tam_render_styles st = {0};
    char *out = render_str_styles(TAM_RENDER_FORMAT_HTML, html, 72, true, &st);
    assert(!strstr(out, "Ignored"));
    assert(!strstr(out, "alert(1)"));
    assert(strstr(out, "Hello & Welcome"));
    assert(strstr(out, "Tamlinux (https://tamlinux.org) -- \"ok\" 'y'."));
    assert(strstr(out, "* One"));
    assert(strstr(out, "* Two"));
    assert(strstr(out, "+---+---+"));
    assert(has_style(&st, TAM_RENDER_STYLE_H1));
    assert(has_style(&st, TAM_RENDER_STYLE_LINK));
    assert(has_style(&st, TAM_RENDER_STYLE_LINK_URL));
    assert(has_style(&st, TAM_RENDER_STYLE_TABLE_HDR));
    assert(has_style(&st, TAM_RENDER_STYLE_RULE));
    tam_render_styles_free(&st);
    free(out);

    char *out_u = render_str(TAM_RENDER_FORMAT_HTML, "<p>&ldquo;hi&rdquo;</p>", 72, false);
    assert(strstr(out_u, "\xe2\x80\x9c" "hi" "\xe2\x80\x9d"));
    free(out_u);
}

static void test_mermaid(void)
{
    const char *td =
        "flowchart TD\n"
        "  A[Input] -->|ok| B(Process)\n"
        "  B --> C{Valid?}\n"
        "  C -->|yes| D[Done]\n"
        "  C -->|no| A\n";

    struct tam_render_styles st = {0};
    char *out_td = render_str_styles(TAM_RENDER_FORMAT_MERMAID, td, 72, true, &st);
    assert(strstr(out_td, "+-------+"));
    assert(strstr(out_td, "| Input |"));
    assert(strstr(out_td, "< Valid? >"));
    assert(strstr(out_td, "(ok)"));
    assert(strstr(out_td, "(yes) > [Done]"));
    assert(strstr(out_td, "(no) > [Input]"));
    // Cycle back-edge A <- C must not push Input below Valid?.
    assert(strstr(out_td, "| Input |") < strstr(out_td, "< Valid? >"));
    assert(strstr(out_td, "< Valid? >") < strstr(out_td, "| Done |"));
    assert(has_style(&st, TAM_RENDER_STYLE_RULE));
    assert(has_style(&st, TAM_RENDER_STYLE_NODE));
    assert(has_style(&st, TAM_RENDER_STYLE_EDGE));
    tam_render_styles_free(&st);
    free(out_td);

    const char *lr =
        "graph LR\n"
        "  S([Begin]) --> M[Middle] --> E[Finish]\n";
    char *out_lr = render_str(TAM_RENDER_FORMAT_MERMAID, lr, 80, false);
    assert(strstr(out_lr, "\xe2\x95\xad"));
    assert(strstr(out_lr, "\xe2\x96\xb6"));
    free(out_lr);

    // Narrow width triggers TD fallback for LR without overflowing.
    char *out_narrow = render_str(TAM_RENDER_FORMAT_MERMAID, lr, 24, true);
    assert(strstr(out_narrow, "Begin"));
    assert(strstr(out_narrow, "Finish"));
    free(out_narrow);

    // stateDiagram-v2 support (start/end pseudostates, labeled transitions, cycles).
    const char *sd =
        "stateDiagram-v2\n"
        "  [*] --> IDLE\n"
        "  state \"Chord Builder\" as BUILDING\n"
        "  IDLE --> BUILDING : Right Alt tap\n"
        "  BUILDING --> IDLE : Escape\n"
        "  IDLE --> [*]\n";
    char *out_sd = render_str(TAM_RENDER_FORMAT_MERMAID, sd, 80, true);
    assert(!strstr(out_sd, "unsupported or invalid"));
    assert(strstr(out_sd, "| [*] |"));
    assert(strstr(out_sd, "| IDLE |"));
    assert(strstr(out_sd, "| Chord Builder |"));
    assert(strstr(out_sd, "(Right Alt tap)"));
    assert(strstr(out_sd, "(Escape) > [IDLE]"));
    free(out_sd);

    // Unsupported diagram emits diagnostic + raw fallback.
    char *bad = render_str(TAM_RENDER_FORMAT_MERMAID, "sequenceDiagram\nA->>B: Hi\n", 80, true);
    assert(strstr(bad, "unsupported or invalid mermaid diagram"));
    assert(strstr(bad, "A->>B: Hi"));
    free(bad);
}

static void test_links_and_anchors(void)
{
    const char *md =
        "# Intro Section\n\n"
        "See [Tamlinux home page](https://tamlinux.org \"Title\") and <https://example.org>.\n\n"
        "## Intro Section\n\n"
        "| Name | Link |\n"
        "| --- | --- |\n"
        "| Local | [page](page.html#top) |\n";

    struct tam_render_links lk = {0};
    char *out = render_str_all(TAM_RENDER_FORMAT_MARKDOWN, md, 30, true, NULL, &lk);
    assert(lk.count == 3);
    assert(strcmp(lk.items[0].url, "https://tamlinux.org") == 0);
    assert(strcmp(lk.items[1].url, "https://example.org") == 0);
    assert(strcmp(lk.items[2].url, "page.html#top") == 0);
    // Wrapped multi-word link still produces one logical link item.
    assert(lk.span_count >= 3);
    // Duplicate heading slugs get deduplicated.
    assert(lk.anchor_count == 2);
    assert(strcmp(lk.anchors[0].id, "intro-section") == 0);
    assert(strcmp(lk.anchors[1].id, "intro-section-1") == 0);
    tam_render_links_free(&lk);
    free(out);

    const char *html =
        "<html><body>"
        "<h1 id=\"custom\">Top</h1>"
        "<p><a name=\"mark\"></a>Go to <a href=\"https://ex.com/?a=1&amp;b=2\">Query</a>.</p>"
        "<h2>Checklist</h2>"
        "</body></html>";
    out = render_str_all(TAM_RENDER_FORMAT_HTML, html, 60, true, NULL, &lk);
    assert(lk.count == 1);
    assert(strcmp(lk.items[0].url, "https://ex.com/?a=1&b=2") == 0);
    assert(lk.anchor_count >= 3);
    assert(strcmp(lk.anchors[0].id, "custom") == 0);
    tam_render_links_free(&lk);
    free(out);
}

static void test_markdown_lists_and_headings(void)
{
    const char *md =
        "### Adopt\n"
        "- **Strict one-owner-per-layer modifier separation** (`a.md`,\n"
        "  `b.md`, `c.md`):\n"
        "  - `Meta` = compositor exclusively. Never bound by apps;\n"
        "    compositor never steals non-`Meta` chords.\n"
        "  - **Multi-line\n"
        "    bold** item.\n"
        "\n"
        "#### Level Four ###\n"
        "##### C# Notes\n"
        "###### Deepest\n"
        "\n"
        "3. **Physical-ergonomics breakdown.**\n"
        "   Squeezing modifier richness onto a keyboard\n"
        "   produces contortions.\n"
        "4. **Next item.**\n";

    struct tam_render_styles st = {0};
    struct tam_render_links lk = {0};
    char *out = render_str_all(TAM_RENDER_FORMAT_MARKDOWN, md, 60, false, &st, &lk);

    // H3..H6 render without literal '#' prefixes and carry TAM_RENDER_STYLE_H3.
    assert(!strstr(out, "### Adopt"));
    assert(!strstr(out, "#### Level Four"));
    assert(!strstr(out, "##### C# Notes"));
    assert(!strstr(out, "###### Deepest"));
    assert(strstr(out, "Adopt\n"));
    assert(strstr(out, "Level Four\n"));
    assert(strstr(out, "C# Notes\n"));
    assert(strstr(out, "Deepest\n"));
    assert(has_style(&st, TAM_RENDER_STYLE_H3));
    assert(lk.anchor_count == 4);
    assert(strcmp(lk.anchors[0].id, "adopt") == 0);
    assert(strcmp(lk.anchors[1].id, "level-four") == 0);
    assert(strcmp(lk.anchors[3].id, "deepest") == 0);

    // Wrapped list items join continuation lines without stray blank lines
    // and wrap with hanging indentation.
    assert(!strstr(out, "(a.md,\n\nb.md"));
    assert(strstr(out, "\xe2\x80\xa2 Strict one-owner-per-layer modifier separation (a.md,\n  b.md, c.md):\n"));
    assert(strstr(out, "  \xe2\x80\xa2 Meta = compositor exclusively. Never bound by apps;\n    compositor never steals non-Meta chords.\n"));
    assert(strstr(out, "  \xe2\x80\xa2 Multi-line bold item.\n"));
    assert(!strstr(out, "breakdown.\n\nSqueezing"));
    assert(strstr(out, "3. Physical-ergonomics breakdown. Squeezing modifier\n   richness onto a keyboard produces contortions.\n4. Next item.\n"));

    tam_render_styles_free(&st);
    tam_render_links_free(&lk);
    free(out);
}

int main(void)
{
    test_short_source();
    test_invalid_entities_and_duplicate_headings();
    assert(setlocale(LC_CTYPE, "C.UTF-8") || setlocale(LC_CTYPE, ""));
    test_detect();
    test_markdown();
    test_markdown_lists_and_headings();
    test_html();
    test_mermaid();
    test_links_and_anchors();
    puts("render: ok");
    return 0;
}
