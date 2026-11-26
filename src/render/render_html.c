#include "render_internal.h"

#include <tam_uni.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void decode_entity(struct tam_render_buf *b, const char *s, size_t len,
                          size_t *consumed, bool ascii)
{
    size_t max_scan = (len > 1 && s[1] == '#') ? 128 : 16;
    size_t i = 1;
    while (i < len && i < max_scan && s[i] != ';' && !isspace((unsigned char)s[i]))
        i++;
    if (i >= len || s[i] != ';') {
        tam__rbuf_putc(b, '&');
        *consumed = 1;
        return;
    }
    *consumed = i + 1;
    const char *ent = s + 1;
    size_t elen = i - 1;

    if (elen > 0 && ent[0] == '#') {
        uint32_t cp = 0;
        bool valid = true;
        if (elen > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
            for (size_t k = 2; k < elen; k++) {
                char c = ent[k];
                if (!isxdigit((unsigned char)c)) {
                    valid = false;
                    break;
                }
                if (cp <= 0x10ffff)
                    cp = cp * 16 + (uint32_t)(isdigit((unsigned char)c)
                         ? c - '0' : tolower((unsigned char)c) - 'a' + 10);
            }
        } else {
            for (size_t k = 1; k < elen; k++) {
                if (!isdigit((unsigned char)ent[k])) {
                    valid = false;
                    break;
                }
                if (cp <= 0x10ffff)
                    cp = cp * 10 + (uint32_t)(ent[k] - '0');
            }
        }
        if (valid && elen > 1 && (isdigit((unsigned char)ent[1]) ||
            ((ent[1] == 'x' || ent[1] == 'X') && elen > 2 &&
             isxdigit((unsigned char)ent[2])))) {
            if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
                cp = 0xfffd;
            if (ascii && cp >= 0x80) {
                tam__rbuf_putc(b, cp == 0xa0 ? ' ' : '?');
            } else {
                char utf8[4];
                size_t n = tam_utf8_encode(cp, utf8);
                tam__rbuf_putn(b, utf8, n);
            }
            return;
        }
    }

    struct {
        const char *name;
        const char *utf8;
        const char *ascii_alt;
    } map[] = {
        {"amp", "&", "&"},
        {"lt", "<", "<"},
        {"gt", ">", ">"},
        {"quot", "\"", "\""},
        {"apos", "'", "'"},
        {"nbsp", " ", " "},
        {"mdash", "\xe2\x80\x94", "--"},
        {"ndash", "\xe2\x80\x93", "-"},
        {"hellip", "\xe2\x80\xa6", "..."},
        {"ldquo", "\xe2\x80\x9c", "\""},
        {"rdquo", "\xe2\x80\x9d", "\""},
        {"lsquo", "\xe2\x80\x98", "'"},
        {"rsquo", "\xe2\x80\x99", "'"},
        {"laquo", "\xc2\xab", "<<"},
        {"raquo", "\xc2\xbb", ">>"},
        {"middot", "\xc2\xb7", "*"},
        {"deg", "\xc2\xb0", " deg"},
        {"plusmn", "\xc2\xb1", "+/-"},
        {"times", "\xc3\x97", "x"},
        {"divide", "\xc3\xb7", "/"},
        {"sect", "\xc2\xa7", "S"},
        {"para", "\xc2\xb6", "P"},
        {"bull", "\xe2\x80\xa2", "*"},
        {"copy", "\xc2\xa9", "(c)"},
        {"reg", "\xc2\xae", "(R)"},
        {"trade", "\xe2\x84\xa2", "(TM)"},
        {"euro", "\xe2\x82\xac", "EUR"},
        {"pound", "\xc2\xa3", "GBP"},
        {"yen", "\xc2\xa5", "JPY"},
        {"cent", "\xc2\xa2", "c"}
    };
    for (size_t m = 0; m < sizeof(map) / sizeof(map[0]); m++) {
        if (strlen(map[m].name) == elen &&
            memcmp(ent, map[m].name, elen) == 0) {
            tam__rbuf_puts(b, ascii ? map[m].ascii_alt : map[m].utf8);
            return;
        }
    }
    tam__rbuf_putn(b, s, *consumed);
}

static void extract_attr(const char *tag, size_t tlen, const char *attr,
                         struct tam_render_buf *out, bool ascii)
{
    size_t alen = strlen(attr);
    for (size_t i = 0; i + alen + 1 < tlen; i++) {
        if ((i == 0 || isspace((unsigned char)tag[i - 1])) &&
            strncasecmp(tag + i, attr, alen) == 0) {
            size_t j = i + alen;
            while (j < tlen && isspace((unsigned char)tag[j]))
                j++;
            if (j >= tlen || tag[j] != '=')
                continue;
            j++;
            while (j < tlen && isspace((unsigned char)tag[j]))
                j++;
            if (j >= tlen)
                return;
            char q = (tag[j] == '"' || tag[j] == '\'') ? tag[j++] : 0;
            size_t start = j;
            while (j < tlen && (q ? tag[j] != q : !isspace((unsigned char)tag[j])))
                j++;
            for (size_t p = start; p < j;) {
                if (tag[p] == '&') {
                    size_t used = 1;
                    decode_entity(out, tag + p, j - p, &used, ascii);
                    p += used;
                } else {
                    tam__rbuf_putc(out, tag[p++]);
                }
            }
            return;
        }
    }
}

static bool tag_eq(const char *name, const char *target)
{
    return strcasecmp(name, target) == 0;
}

static unsigned char inline_tag_style(const char *name)
{
    if (tag_eq(name, "strong") || tag_eq(name, "b"))
        return TAM_RENDER_STYLE_BOLD;
    if (tag_eq(name, "em") || tag_eq(name, "i"))
        return TAM_RENDER_STYLE_ITALIC;
    if (tag_eq(name, "code") || tag_eq(name, "kbd") || tag_eq(name, "samp"))
        return TAM_RENDER_STYLE_CODE;
    if (tag_eq(name, "s") || tag_eq(name, "del"))
        return TAM_RENDER_STYLE_STRIKE;
    return TAM_RENDER_STYLE_NONE;
}

struct html_istyle {
    struct tam_render_buf *buf;
    size_t start;
    unsigned char style;
};

static void flush_istyles_for(struct html_istyle *stack, int *depth,
                              struct tam_render_buf *buf)
{
    int keep = 0;
    for (int idx = 0; idx < *depth; idx++) {
        if (stack[idx].buf == buf) {
            tam__rbuf_fill_style(buf, stack[idx].start, buf->len, stack[idx].style);
        } else {
            stack[keep++] = stack[idx];
        }
    }
    *depth = keep;
}

void tam_render_html(struct tam_render_buf *b, const char *src, size_t len,
                 int width, bool ascii)
{
    const struct tam_render_box *box = tam_render_box_chars(ascii);
    if (width < 20)
        width = 20;

    struct tam_render_buf text;
    tam_render_buf_init(&text);
    struct tam_render_buf link_href;
    tam_render_buf_init(&link_href);
    struct tam_render_buf link_text;
    tam_render_buf_init(&link_text);
    struct tam_render_buf attr_id;
    tam_render_buf_init(&attr_id);
    struct tam_render_buf heading_id;
    tam_render_buf_init(&heading_id);

    bool in_pre = false;
    size_t pre_start = 0;
    bool in_link = false;
    bool in_quote = false;
    bool in_li = false;
    char li_fp[32] = {0};
    char li_cp[32] = {0};
    int heading = 0;
    int list_depth = 0;
    bool list_ordered[8] = {false};
    int list_index[8] = {0};

    bool in_table = false;
    bool in_cell = false;
    struct tam__rtable table = {0};
    struct tam_render_buf cell_buf;
    tam_render_buf_init(&cell_buf);

    struct html_istyle istyle[16];
    int istyle_depth = 0;

    for (size_t i = 0; i < len;) {
        if (src[i] == '<') {
            if (i + 3 < len && memcmp(src + i, "<!--", 4) == 0) {
                size_t cend = i + 4;
                while (cend + 2 < len && memcmp(src + cend, "-->", 3) != 0)
                    cend++;
                i = (cend + 2 < len) ? cend + 3 : len;
                continue;
            }
            size_t j = i + 1;
            char quote = 0;
            while (j < len) {
                if (quote) {
                    if (src[j] == quote)
                        quote = 0;
                } else if (src[j] == '"' || src[j] == '\'') {
                    quote = src[j];
                } else if (src[j] == '>') {
                    break;
                }
                j++;
            }
            if (j >= len) {
                tam__rbuf_putc(&text, src[i++]);
                continue;
            }

            const char *tag = src + i + 1;
            size_t tlen = j - (i + 1);
            i = j + 1;

            bool closing = false;
            size_t k = 0;
            while (k < tlen && isspace((unsigned char)tag[k]))
                k++;
            if (k < tlen && tag[k] == '/') {
                closing = true;
                k++;
            }
            char name[32];
            size_t nlen = 0;
            while (k < tlen && !isspace((unsigned char)tag[k]) && tag[k] != '/') {
                if (nlen + 1 < sizeof(name))
                    name[nlen++] = tag[k];
                k++;
            }
            name[nlen] = '\0';

            if (!closing && (tag_eq(name, "script") || tag_eq(name, "style") ||
                             tag_eq(name, "head"))) {
                char end_tag[40];
                snprintf(end_tag, sizeof(end_tag), "</%s", name);
                size_t elen = strlen(end_tag);
                while (i < len) {
                    if (src[i] == '<' && i + elen < len &&
                        strncasecmp(src + i, end_tag, elen) == 0) {
                        while (i < len && src[i] != '>')
                            i++;
                        if (i < len)
                            i++;
                        break;
                    }
                    i++;
                }
                continue;
            }

            // Flush accumulated paragraph/heading text on block boundaries.
            bool is_heading = (name[0] == 'h' || name[0] == 'H') &&
                              name[1] >= '1' && name[1] <= '6' && name[2] == '\0';
            bool is_block = is_heading || tag_eq(name, "p") || tag_eq(name, "div") ||
                            tag_eq(name, "section") || tag_eq(name, "article") ||
                            tag_eq(name, "blockquote") || tag_eq(name, "pre") ||
                            tag_eq(name, "ul") || tag_eq(name, "ol") ||
                            tag_eq(name, "li") || tag_eq(name, "hr") ||
                            tag_eq(name, "table") || tag_eq(name, "tr");

            if (is_block && text.len > 0 && !in_table) {
                flush_istyles_for(istyle, &istyle_depth, &text);
                if (heading > 0) {
                    unsigned char hstyle = heading == 1 ? TAM_RENDER_STYLE_H1
                                         : heading == 2 ? TAM_RENDER_STYLE_H2
                                                        : TAM_RENDER_STYLE_H3;
                    tam__rbuf_blank_line(b);
                    size_t h_anchor = b->len;
                    if (heading_id.len > 0) {
                        tam__rbuf_add_anchor(b, heading_id.data, heading_id.len, h_anchor);
                        tam_render_buf_clear(&heading_id);
                    }
                    tam__rbuf_add_heading_anchor(b, text.data ? text.data : "", text.len,
                                            h_anchor);
                    tam__rbuf_fill_style(&text, 0, text.len, hstyle);
                    tam__rbuf_wrap_styled(b, &text, width, "", "", TAM_RENDER_STYLE_NONE);
                    if (heading <= 2) {
                        int w = tam__render_str_width(text.data, text.len);
                        if (w > width)
                            w = width;
                        if (w < 3)
                            w = 3;
                        size_t rs = b->len;
                        tam__rbuf_repeat(b, heading == 1 ? box->d_h : box->h, w);
                        tam__rbuf_push_span(b, rs, b->len, TAM_RENDER_STYLE_RULE);
                        tam__rbuf_putc(b, '\n');
                    }
                    heading = 0;
                } else if (in_quote) {
                    char pfx[8];
                    snprintf(pfx, sizeof(pfx), "%s ", box->v);
                    tam__rbuf_blank_line(b);
                    tam__rbuf_fill_style(&text, 0, text.len, TAM_RENDER_STYLE_QUOTE);
                    tam__rbuf_wrap_styled(b, &text, width, pfx, pfx, TAM_RENDER_STYLE_QUOTE);
                } else if (in_li) {
                    tam__rbuf_newline(b);
                    tam__rbuf_wrap_styled(b, &text, width, li_fp, li_cp, TAM_RENDER_STYLE_BOLD);
                    in_li = false;
                } else {
                    tam__rbuf_blank_line(b);
                    tam__rbuf_wrap_styled(b, &text, width, "", "", TAM_RENDER_STYLE_NONE);
                }
                tam_render_buf_clear(&text);
            }

            if (!closing) {
                tam_render_buf_clear(&attr_id);
                extract_attr(tag, tlen, "id", &attr_id, ascii);
                if (attr_id.len == 0 && tag_eq(name, "a"))
                    extract_attr(tag, tlen, "name", &attr_id, ascii);
                if (attr_id.len > 0) {
                    if (is_heading) {
                        tam_render_buf_clear(&heading_id);
                        tam__rbuf_putn(&heading_id, attr_id.data, attr_id.len);
                    } else {
                        tam__rbuf_add_anchor(b, attr_id.data, attr_id.len, b->len);
                    }
                }
            }

            unsigned char isty = inline_tag_style(name);
            if (isty != TAM_RENDER_STYLE_NONE && !in_pre) {
                struct tam_render_buf *cur_dst = in_link ? &link_text
                                     : in_cell ? &cell_buf
                                               : &text;
                if (!closing) {
                    if (istyle_depth < 16) {
                        istyle[istyle_depth++] = (struct html_istyle){
                            .buf = cur_dst,
                            .start = cur_dst->len,
                            .style = isty
                        };
                    }
                } else {
                    for (int idx = istyle_depth - 1; idx >= 0; idx--) {
                        if (istyle[idx].style == isty &&
                            istyle[idx].buf == cur_dst) {
                            tam__rbuf_fill_style(cur_dst, istyle[idx].start,
                                            cur_dst->len, isty);
                            memmove(&istyle[idx], &istyle[idx + 1],
                                    (size_t)(istyle_depth - 1 - idx) * sizeof(istyle[0]));
                            istyle_depth--;
                            break;
                        }
                    }
                }
                continue;
            }

            if (is_heading) {
                if (!closing) {
                    heading = name[1] - '0';
                }
            } else if (tag_eq(name, "br")) {
                if (in_cell)
                    tam__rbuf_putc(&cell_buf, ' ');
                else if (in_pre)
                    tam__rbuf_putc(b, '\n');
                else if (text.len > 0) {
                    flush_istyles_for(istyle, &istyle_depth, &text);
                    tam__rbuf_newline(b);
                    tam__rbuf_wrap_styled(b, &text, width, "", "", TAM_RENDER_STYLE_NONE);
                    tam_render_buf_clear(&text);
                }
            } else if (tag_eq(name, "hr")) {
                tam__rbuf_blank_line(b);
                size_t rs = b->len;
                tam__rbuf_repeat(b, box->h, width > 72 ? 72 : width);
                tam__rbuf_push_span(b, rs, b->len, TAM_RENDER_STYLE_RULE);
                tam__rbuf_putc(b, '\n');
            } else if (tag_eq(name, "pre")) {
                if (!closing) {
                    in_pre = true;
                    tam__rbuf_blank_line(b);
                    pre_start = b->len;
                } else {
                    if (in_pre && b->len > pre_start)
                        tam__rbuf_fill_style(b, pre_start, b->len, TAM_RENDER_STYLE_CODE);
                    in_pre = false;
                    tam__rbuf_newline(b);
                }
            } else if (tag_eq(name, "blockquote")) {
                in_quote = !closing;
            } else if (tag_eq(name, "ul") || tag_eq(name, "ol")) {
                if (!closing && list_depth < 8) {
                    list_ordered[list_depth] = tag_eq(name, "ol");
                    list_index[list_depth] = 0;
                    list_depth++;
                    if (list_depth == 1)
                        tam__rbuf_blank_line(b);
                } else if (closing && list_depth > 0) {
                    list_depth--;
                }
            } else if (tag_eq(name, "li")) {
                if (!closing) {
                    if (list_depth > 0 && list_ordered[list_depth - 1])
                        list_index[list_depth - 1]++;
                    int lvl = list_depth > 0 ? list_depth - 1 : 0;
                    int p = 0;
                    for (int s = 0; s < lvl * 2; s++)
                        li_fp[p++] = ' ';
                    if (list_depth > 0 && list_ordered[list_depth - 1]) {
                        int nw = snprintf(li_fp + p, sizeof(li_fp) - (size_t)p,
                                          "%d. ", list_index[list_depth - 1]);
                        int cw = p + (nw > 0 ? nw : 3);
                        memset(li_cp, ' ', (size_t)cw);
                        li_cp[cw] = '\0';
                    } else {
                        snprintf(li_fp + p, sizeof(li_fp) - (size_t)p, "%s ", box->bullet);
                        int cw = p + 2;
                        memset(li_cp, ' ', (size_t)cw);
                        li_cp[cw] = '\0';
                    }
                    in_li = true;
                } else if (in_li) {
                    if (text.len > 0) {
                        flush_istyles_for(istyle, &istyle_depth, &text);
                        tam__rbuf_newline(b);
                        tam__rbuf_wrap_styled(b, &text, width, li_fp, li_cp, TAM_RENDER_STYLE_BOLD);
                        tam_render_buf_clear(&text);
                    }
                    in_li = false;
                }
            } else if (tag_eq(name, "a")) {
                if (!closing) {
                    in_link = true;
                    tam_render_buf_clear(&link_href);
                    tam_render_buf_clear(&link_text);
                    extract_attr(tag, tlen, "href", &link_href, ascii);
                } else if (in_link) {
                    flush_istyles_for(istyle, &istyle_depth, &link_text);
                    in_link = false;
                    struct tam_render_buf *dst = in_cell ? &cell_buf : &text;
                    size_t l0 = dst->len;
                    size_t l1 = l0;
                    if (link_text.len > 0) {
                        tam__rbuf_putn(dst, link_text.data, link_text.len);
                        tam__rbuf_copy_spans(dst, l0, &link_text, 0, link_text.len);
                        tam__rbuf_fill_style(dst, l0, dst->len, TAM_RENDER_STYLE_LINK);
                        l1 = dst->len;
                        if (link_href.len > 0)
                            tam__rbuf_add_link(dst, l0, l1, link_href.data, link_href.len);
                    }
                    if (link_href.len > 0 &&
                        !(link_href.len == link_text.len &&
                          memcmp(link_href.data, link_text.data, link_href.len) == 0)) {
                        size_t u0 = dst->len;
                        tam__rbuf_puts(dst, " (");
                        tam__rbuf_putn(dst, link_href.data, link_href.len);
                        tam__rbuf_putc(dst, ')');
                        tam__rbuf_push_span(dst, u0, dst->len, TAM_RENDER_STYLE_LINK_URL);
                        if (l1 == l0)
                            tam__rbuf_add_link(dst, u0 + 2, dst->len - 1,
                                          link_href.data, link_href.len);
                    }
                }
            } else if (tag_eq(name, "table")) {
                if (!closing) {
                    in_table = true;
                    table = (struct tam__rtable){0};
                } else if (in_table) {
                    in_table = false;
                    tam__rbuf_blank_line(b);
                    tam__render_table(b, &table, width, ascii);
                    tam__rtable_free(&table);
                }
            } else if (tag_eq(name, "tr") && in_table) {
                if (!closing) {
                    struct tam__rtable_row *rows = realloc(
                        table.rows, (table.nrows + 1) * sizeof(*rows));
                    if (rows) {
                        table.rows = rows;
                        table.rows[table.nrows++] = (struct tam__rtable_row){0};
                    }
                }
            } else if ((tag_eq(name, "th") || tag_eq(name, "td")) && in_table) {
                if (!closing) {
                    in_cell = true;
                    tam_render_buf_clear(&cell_buf);
                    if (tag_eq(name, "th") && table.nrows == 1)
                        table.has_header = true;
                } else if (in_cell) {
                    flush_istyles_for(istyle, &istyle_depth, &cell_buf);
                    in_cell = false;
                    if (table.nrows > 0) {
                        struct tam__rtable_row *row = &table.rows[table.nrows - 1];
                        struct tam_render_buf *cells = realloc(
                            row->cells, (row->ncols + 1) * sizeof(*cells));
                        if (cells) {
                            row->cells = cells;
                            const char *cs = cell_buf.data ? cell_buf.data : "";
                            size_t cstart = 0;
                            size_t cl = cell_buf.len;
                            while (cl > 0 && isspace((unsigned char)cs[cstart])) {
                                cstart++;
                                cl--;
                            }
                            while (cl > 0 && isspace((unsigned char)cs[cstart + cl - 1]))
                                cl--;
                            struct tam_render_buf trimmed;
                            tam_render_buf_init(&trimmed);
                            if (cl > 0) {
                                tam__rbuf_putn(&trimmed, cs + cstart, cl);
                                tam__rbuf_copy_spans(&trimmed, 0, &cell_buf, cstart, cl);
                                tam__rbuf_copy_links(&trimmed, 0, &cell_buf, cstart, cl, NULL, NULL);
                            }
                            row->cells[row->ncols++] = trimmed;
                            if (row->ncols > table.max_cols)
                                table.max_cols = row->ncols;
                        }
                    }
                }
            }
            continue;
        }

        struct tam_render_buf *dst = in_link ? &link_text
                         : in_cell ? &cell_buf
                         : in_pre  ? b
                                   : &text;
        if (src[i] == '&') {
            size_t used = 1;
            decode_entity(dst, src + i, len - i, &used, ascii);
            i += used;
        } else if (!in_pre && isspace((unsigned char)src[i])) {
            if (dst->len > 0 && dst->data[dst->len - 1] != ' ')
                tam__rbuf_putc(dst, ' ');
            i++;
        } else {
            if (in_pre && src[i] == '\n' && dst == b) {
                tam__rbuf_putc(b, '\n');
            } else {
                tam__rbuf_putc(dst, src[i]);
            }
            i++;
        }
    }

    if (text.len > 0) {
        flush_istyles_for(istyle, &istyle_depth, &text);
        tam__rbuf_blank_line(b);
        tam__rbuf_wrap_styled(b, &text, width, "", "", TAM_RENDER_STYLE_NONE);
    }
    if (in_table) {
        tam__rbuf_blank_line(b);
        tam__render_table(b, &table, width, ascii);
        tam__rtable_free(&table);
    }

    tam_render_buf_free(&cell_buf);
    tam_render_buf_free(&heading_id);
    tam_render_buf_free(&attr_id);
    tam_render_buf_free(&link_text);
    tam_render_buf_free(&link_href);
    tam_render_buf_free(&text);
}
