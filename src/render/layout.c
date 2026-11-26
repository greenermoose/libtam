#include "tam_render.h"

#include <tam_pager.h>
#include <tam_uni.h>

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TAB_SIZE 8
#define MAX_MARKS 8

struct glyph {
    char bytes[4];
    size_t len;
    int width;
};

void tam_render_styles_free(struct tam_render_styles *st)
{
    if (!st)
        return;
    free(st->spans);
    *st = (struct tam_render_styles){0};
}

bool tam_render_styles_push(struct tam_render_styles *st, off_t start, off_t end,
                      unsigned char style)
{
    if (!st || style == TAM_RENDER_STYLE_NONE || end <= start)
        return true;
    if (st->count > 0) {
        struct tam_render_span *prev = &st->spans[st->count - 1];
        if (start < prev->end) {
            start = prev->end;
            if (end <= start)
                return true;
        }
        if (prev->end == start && prev->style == style) {
            prev->end = end;
            return true;
        }
    }
    if (st->count == st->cap) {
        size_t ncap = st->cap ? st->cap * 2 : 64;
        struct tam_render_span *grown = realloc(st->spans, ncap * sizeof(*grown));
        if (!grown)
            return false;
        st->spans = grown;
        st->cap = ncap;
    }
    st->spans[st->count++] = (struct tam_render_span){
        .start = start,
        .end = end,
        .style = style
    };
    return true;
}

void tam_render_links_free(struct tam_render_links *lk)
{
    if (!lk)
        return;
    for (size_t i = 0; i < lk->count; i++)
        free(lk->items[i].url);
    free(lk->items);
    free(lk->spans);
    for (size_t i = 0; i < lk->anchor_count; i++)
        free(lk->anchors[i].id);
    free(lk->anchors);
    *lk = (struct tam_render_links){0};
}

ssize_t tam_render_links_add_item(struct tam_render_links *lk, const char *url,
                            size_t url_len, off_t first_byte)
{
    if (!lk || !url || url_len == 0)
        return -1;
    if (lk->count == lk->cap) {
        size_t ncap = lk->cap ? lk->cap * 2 : 16;
        struct tam_render_link *grown = realloc(lk->items, ncap * sizeof(*grown));
        if (!grown)
            return -1;
        lk->items = grown;
        lk->cap = ncap;
    }
    char *copy = malloc(url_len + 1);
    if (!copy)
        return -1;
    memcpy(copy, url, url_len);
    copy[url_len] = '\0';
    size_t idx = lk->count++;
    lk->items[idx] = (struct tam_render_link){
        .url = copy,
        .first_byte = first_byte
    };
    return (ssize_t)idx;
}

bool tam_render_links_push_span(struct tam_render_links *lk, off_t start, off_t end,
                          size_t link_index)
{
    if (!lk || end <= start || link_index >= lk->count)
        return true;
    if (lk->span_count > 0) {
        struct tam_render_link_span *prev = &lk->spans[lk->span_count - 1];
        if (start < prev->end) {
            start = prev->end;
            if (end <= start)
                return true;
        }
        if (prev->end == start && prev->link_index == link_index) {
            prev->end = end;
            return true;
        }
    }
    if (lk->span_count == lk->span_cap) {
        size_t ncap = lk->span_cap ? lk->span_cap * 2 : 32;
        struct tam_render_link_span *grown = realloc(lk->spans, ncap * sizeof(*grown));
        if (!grown)
            return false;
        lk->spans = grown;
        lk->span_cap = ncap;
    }
    lk->spans[lk->span_count++] = (struct tam_render_link_span){
        .start = start,
        .end = end,
        .link_index = link_index
    };
    if (start < lk->items[link_index].first_byte)
        lk->items[link_index].first_byte = start;
    return true;
}

bool tam_render_links_add_anchor(struct tam_render_links *lk, const char *id,
                           size_t id_len, off_t byte)
{
    if (!lk || !id || id_len == 0)
        return true;
    if (lk->anchor_count == lk->anchor_cap) {
        size_t ncap = lk->anchor_cap ? lk->anchor_cap * 2 : 16;
        struct tam_render_anchor *grown = realloc(lk->anchors, ncap * sizeof(*grown));
        if (!grown)
            return false;
        lk->anchors = grown;
        lk->anchor_cap = ncap;
    }
    char *copy = malloc(id_len + 1);
    if (!copy)
        return false;
    memcpy(copy, id, id_len);
    copy[id_len] = '\0';
    lk->anchors[lk->anchor_count++] = (struct tam_render_anchor){
        .id = copy,
        .byte = byte
    };
    return true;
}

static const char *style_sgr(unsigned char style, bool linux_vt)
{
    switch (style) {
    case TAM_RENDER_STYLE_H1:         return "\033[1;36m";
    case TAM_RENDER_STYLE_H2:         return "\033[1;34m";
    case TAM_RENDER_STYLE_H3:         return "\033[1;33m";
    case TAM_RENDER_STYLE_BOLD:       return "\033[1m";
    case TAM_RENDER_STYLE_ITALIC:     return linux_vt ? "\033[36m" : "\033[3m";
    case TAM_RENDER_STYLE_STRIKE:     return linux_vt ? "\033[2m" : "\033[9;2m";
    case TAM_RENDER_STYLE_CODE:       return "\033[33m";
    case TAM_RENDER_STYLE_LINK:       return "\033[4;34m";
    case TAM_RENDER_STYLE_LINK_URL:   return "\033[2m";
    case TAM_RENDER_STYLE_LINK_CUR:   return tam_pager_link_focus_sgr(linux_vt);
    case TAM_RENDER_STYLE_QUOTE:      return linux_vt ? "\033[32m" : "\033[2;32m";
    case TAM_RENDER_STYLE_RULE:       return "\033[2m";
    case TAM_RENDER_STYLE_TABLE_HDR:  return "\033[1m";
    case TAM_RENDER_STYLE_NODE:       return "\033[1;36m";
    case TAM_RENDER_STYLE_EDGE:       return "\033[33m";
    case TAM_RENDER_STYLE_SEARCH:     return "\033[7m";
    case TAM_RENDER_STYLE_SEARCH_CUR: return "\033[1;30;43m";
    default:                    return "";
    }
}

static size_t span_lower_bound(const struct tam_render_span *spans, size_t count,
                               off_t byte)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (spans[mid].end <= byte)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static unsigned char span_style_at(const struct tam_render_span *spans, size_t count,
                                   size_t *idx, off_t byte)
{
    while (*idx < count && spans[*idx].end <= byte)
        (*idx)++;
    if (*idx < count && spans[*idx].start <= byte && byte < spans[*idx].end)
        return spans[*idx].style;
    return TAM_RENDER_STYLE_NONE;
}

static size_t link_span_lower_bound(const struct tam_render_link_span *spans,
                                    size_t count, off_t byte)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (spans[mid].end <= byte)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static ssize_t link_index_at(const struct tam_render_links *lk, size_t *idx,
                             off_t byte)
{
    if (!lk || lk->span_count == 0)
        return -1;
    while (*idx < lk->span_count && lk->spans[*idx].end <= byte)
        (*idx)++;
    if (*idx < lk->span_count &&
        lk->spans[*idx].start <= byte && byte < lk->spans[*idx].end)
        return (ssize_t)lk->spans[*idx].link_index;
    return -1;
}

static void emit_raw(char *out, size_t *used, const char *seq)
{
    if (!out || !seq || !seq[0])
        return;
    size_t slen = strlen(seq);
    memcpy(out + *used, seq, slen);
    *used += slen;
}

static void emit_style(char *out, size_t *used, unsigned char *active,
                       unsigned char want, bool linux_vt, bool attention)
{
    if (*active == want)
        return;
    if (out) {
        if (*active != TAM_RENDER_STYLE_NONE || attention)
            emit_raw(out, used, "\033[0m");
        if (want == TAM_RENDER_STYLE_NONE) {
            if (attention)
                emit_raw(out, used, tam_pager_attention_sgr(linux_vt));
        } else {
            if (attention && !linux_vt &&
                want != TAM_RENDER_STYLE_SEARCH &&
                want != TAM_RENDER_STYLE_SEARCH_CUR &&
                want != TAM_RENDER_STYLE_LINK_CUR) {
                emit_raw(out, used, "\033[48;5;236m");
            }
            emit_raw(out, used, style_sgr(want, linux_vt));
        }
    }
    *active = want;
}

#define OSC8_ROW_BUDGET 4096

static void emit_osc8(char *out, size_t *used, size_t *osc8_used,
                      ssize_t *active_link, ssize_t want_link,
                      const struct tam_render_links *lk)
{
    if (*active_link == want_link)
        return;
    if (*active_link >= 0) {
        if (out) {
            char close_buf[16];
            size_t n = tam_osc8_close(close_buf, sizeof(close_buf));
            memcpy(out + *used, close_buf, n);
            *used += n;
            *osc8_used += n;
        }
        *active_link = -1;
    }
    if (want_link >= 0 && lk && (size_t)want_link < lk->count) {
        const char *url = lk->items[want_link].url;
        if (tam_osc8_url_safe(url, 1024)) {
            size_t open_len = 5 + strlen(url) + 2;
            if (*osc8_used + open_len + 7 <= OSC8_ROW_BUDGET) {
                if (out) {
                    char open_buf[1040];
                    size_t n = tam_osc8_open(open_buf, sizeof(open_buf), url);
                    if (n > 0) {
                        memcpy(out + *used, open_buf, n);
                        *used += n;
                        *osc8_used += n;
                        *active_link = want_link;
                    }
                } else {
                    *active_link = want_link;
                }
            }
        }
    }
}

bool tam_position_equal(struct tam_position a, struct tam_position b)
{
    return a.byte == b.byte && a.tab_left == b.tab_left;
}

int tam_position_compare(struct tam_position a, struct tam_position b)
{
    if (a.byte != b.byte)
        return a.byte < b.byte ? -1 : 1;
    return (a.tab_left < b.tab_left) - (a.tab_left > b.tab_left);
}

static struct glyph glyph(uint32_t cp, int width)
{
    struct glyph result = {0};
    if (cp < 0x20 || cp == 0x7f) {
        result.bytes[0] = '^';
        result.bytes[1] = cp == 0x7f ? '?' : (char)(cp + '@');
        result.len = 2;
        result.width = 2;
    } else if ((cp >= 0x80 && cp <= 0x9f) ||
               (cp >= 0x202a && cp <= 0x202e) ||
               (cp >= 0x2066 && cp <= 0x2069)) {
        result.bytes[0] = '?';
        result.len = 1;
        result.width = 1;
    } else {
        result.len = tam_utf8_encode(cp, result.bytes);
        result.width = tam_uni_width(cp);
    }
    if (result.width > width)
        return (struct glyph){.bytes = "?", .len = 1, .width = 1};
    return result;
}

static int decode(struct tam_source *s, off_t offset, int first, uint32_t *cp)
{
    char bytes[4] = {(char)first};
    size_t count = 1;
    size_t need = tam_utf8_seq_len((unsigned char)first);
    for (; count < need; count++) {
        int c = tam_source_byte(s, offset + (off_t)count);
        if (c == TAM_RENDER_ERROR || c == TAM_RENDER_WAIT)
            return c;
        if (c < 0 || (c & 0xc0) != 0x80)
            break;
        bytes[count] = (char)c;
    }
    return (int)tam_utf8_decode(bytes, count, cp);
}

size_t tam_render_row_capacity(int width)
{
    return (size_t)width * 4 * (1 + MAX_MARKS) + 1;
}

size_t tam_render_row_styled_capacity(int width)
{
    return tam_render_row_capacity(width) + (size_t)width * 28 + OSC8_ROW_BUDGET + 64;
}

static unsigned char resolve_style(const struct tam_render_row_style *st,
                                   size_t *doc_idx, size_t *search_idx,
                                   ssize_t link_idx, off_t byte)
{
    if (!st)
        return TAM_RENDER_STYLE_NONE;
    if (st->search_count > 0) {
        unsigned char s = span_style_at(st->search_spans, st->search_count,
                                        search_idx, byte);
        if (s != TAM_RENDER_STYLE_NONE)
            return s;
    }
    if (st->focused_link >= 0 && link_idx == st->focused_link)
        return TAM_RENDER_STYLE_LINK_CUR;
    if (st->doc_count > 0)
        return span_style_at(st->doc_spans, st->doc_count, doc_idx, byte);
    return TAM_RENDER_STYLE_NONE;
}

int tam_render_row_styled(struct tam_source *s, struct tam_position start, int width,
                    int skip, bool chop, const struct tam_render_row_style *style,
                    char *out, struct tam_position *next)
{
    bool has_links = style && style->links && style->links->span_count > 0;
    bool has_style = style && (style->doc_count > 0 || style->search_count > 0 ||
                               style->attention ||
                               (has_links && (style->focused_link >= 0 || style->osc8)));
    const struct tam_render_row_style *st = has_style ? style : NULL;
    size_t doc_idx = st && st->doc_count
                     ? span_lower_bound(st->doc_spans, st->doc_count, start.byte)
                     : 0;
    size_t search_idx = st && st->search_count
                        ? span_lower_bound(st->search_spans, st->search_count, start.byte)
                        : 0;
    size_t link_cursor = st && has_links
                         ? link_span_lower_bound(st->links->spans,
                                                 st->links->span_count, start.byte)
                         : 0;
    bool linux_vt = st ? st->linux_vt : false;
    bool attention = st ? st->attention : false;
    bool want_osc8 = st && st->osc8 && has_links;
    unsigned char active = TAM_RENDER_STYLE_NONE;
    ssize_t active_link = -1;
    size_t osc8_used = 0;

    if (skip == 0 && !chop) {
        struct tam_position p = start;
        int cells = 0;
        unsigned marks = 0;
        size_t used = 0;
        bool attn_started = false;

        for (;;) {
            uint32_t cp;
            int count = 0;
            bool tab = p.tab_left > 0;
            off_t glyph_byte = p.byte;
            if (tab) {
                cp = ' ';
                if (glyph_byte > 0)
                    glyph_byte--;
            } else {
                int b = tam_source_byte(s, p.byte);
                if (b == TAM_RENDER_EOF) {
                    if (tam_position_equal(p, start))
                        return TAM_RENDER_EOF;
                    break;
                }
                if (b < 0)
                    return b;

                if (b == '\r') {
                    int after = tam_source_byte(s, p.byte + 1);
                    if (after == TAM_RENDER_ERROR || after == TAM_RENDER_WAIT)
                        return after;
                    if (after == '\n') {
                        b = '\n';
                        p.byte++;
                    }
                }
                if (b == '\n') {
                    p.byte++;
                    p.tab_column = 0;
                    break;
                }
                if (b == '\t') {
                    p.byte++;
                    p.tab_left = TAB_SIZE - p.tab_column;
                    continue;
                }
                count = decode(s, p.byte, b, &cp);
                if (count < 0)
                    return count;
            }

            struct glyph g = glyph(cp, width);
            if (g.width == 0 && (cells == 0 || marks >= MAX_MARKS))
                g = (struct glyph){.bytes = "?", .len = 1, .width = 1};
            if (g.width > width - cells)
                break;
            p.byte += count;
            if (tab)
                p.tab_left--;
            p.tab_column = (unsigned char)((p.tab_column + g.width) % TAB_SIZE);
            if (g.width != 0)
                marks = 0;
            else
                marks++;
            cells += g.width;
            if (out) {
                if (attention && !attn_started) {
                    emit_raw(out, &used, tam_pager_attention_sgr(linux_vt));
                    attn_started = true;
                }
                if (st) {
                    ssize_t lidx = has_links
                                   ? link_index_at(st->links, &link_cursor, glyph_byte)
                                   : -1;
                    if (want_osc8)
                        emit_osc8(out, &used, &osc8_used, &active_link, lidx, st->links);
                    unsigned char want = resolve_style(st, &doc_idx, &search_idx,
                                                       lidx, glyph_byte);
                    emit_style(out, &used, &active, want, linux_vt, attention);
                }
                memcpy(out + used, g.bytes, g.len);
                used += g.len;
            }
        }
        if (out) {
            if (want_osc8)
                emit_osc8(out, &used, &osc8_used, &active_link, -1, st->links);
            if (attention) {
                if (!attn_started) {
                    emit_raw(out, &used, tam_pager_attention_sgr(linux_vt));
                    attn_started = true;
                } else {
                    emit_style(out, &used, &active, TAM_RENDER_STYLE_NONE, linux_vt, true);
                }
                if (cells < width) {
                    memset(out + used, ' ', (size_t)(width - cells));
                    used += (size_t)(width - cells);
                }
                emit_raw(out, &used, "\033[0m");
            } else {
                emit_style(out, &used, &active, TAM_RENDER_STYLE_NONE, linux_vt, false);
            }
            out[used] = '\0';
        }
        *next = p;
        return TAM_RENDER_ROW;
    }

    struct tam_position p = {.byte = start.byte};
    int col = 0;
    int cells = 0;
    unsigned marks = 0;
    size_t used = 0;
    bool attn_started = false;

    for (;;) {
        int b = tam_source_byte(s, p.byte);
        if (b == TAM_RENDER_EOF) {
            if (p.byte == start.byte)
                return TAM_RENDER_EOF;
            break;
        }
        if (b < 0)
            return b;

        if (b == '\r') {
            int after = tam_source_byte(s, p.byte + 1);
            if (after == TAM_RENDER_ERROR || after == TAM_RENDER_WAIT)
                return after;
            if (after == '\n') {
                b = '\n';
                p.byte++;
            }
        }
        if (b == '\n') {
            p.byte++;
            break;
        }
        if (b == '\t') {
            off_t glyph_byte = p.byte;
            p.byte++;
            int tab_w = TAB_SIZE - (col % TAB_SIZE);
            int start_c = col;
            int end_c = col + tab_w;
            col = end_c;
            if (end_c > skip && cells < width) {
                int vis_start = start_c > skip ? start_c : skip;
                int vis_end = end_c < skip + width ? end_c : skip + width;
                int spaces = vis_end - vis_start;
                if (spaces > width - cells)
                    spaces = width - cells;
                if (out) {
                    if (attention && !attn_started) {
                        emit_raw(out, &used, tam_pager_attention_sgr(linux_vt));
                        attn_started = true;
                    }
                    if (st) {
                        ssize_t lidx = has_links
                                       ? link_index_at(st->links, &link_cursor, glyph_byte)
                                       : -1;
                        if (want_osc8)
                            emit_osc8(out, &used, &osc8_used, &active_link, lidx, st->links);
                        unsigned char want = resolve_style(st, &doc_idx, &search_idx,
                                                           lidx, glyph_byte);
                        emit_style(out, &used, &active, want, linux_vt, attention);
                    }
                    memset(out + used, ' ', (size_t)spaces);
                    used += (size_t)spaces;
                }
                cells += spaces;
                marks = 0;
            }
            continue;
        }

        off_t glyph_byte = p.byte;
        uint32_t cp;
        int count = decode(s, p.byte, b, &cp);
        if (count < 0)
            return count;
        p.byte += count;

        struct glyph g = glyph(cp, width);
        if (g.width == 0) {
            if (col < skip || cells >= width)
                continue;
            if (cells == 0 || marks >= MAX_MARKS)
                g = (struct glyph){.bytes = "?", .len = 1, .width = 1};
        }

        int start_c = col;
        int end_c = col + g.width;
        col = end_c;

        if (cells >= width)
            continue;
        if (end_c <= skip)
            continue;
        if (start_c < skip && end_c > skip) {
            int spaces = end_c - skip;
            if (spaces > width - cells)
                spaces = width - cells;
            if (out) {
                if (attention && !attn_started) {
                    emit_raw(out, &used, tam_pager_attention_sgr(linux_vt));
                    attn_started = true;
                }
                if (st) {
                    ssize_t lidx = has_links
                                   ? link_index_at(st->links, &link_cursor, glyph_byte)
                                   : -1;
                    if (want_osc8)
                        emit_osc8(out, &used, &osc8_used, &active_link, lidx, st->links);
                    unsigned char want = resolve_style(st, &doc_idx, &search_idx,
                                                       lidx, glyph_byte);
                    emit_style(out, &used, &active, want, linux_vt, attention);
                }
                memset(out + used, ' ', (size_t)spaces);
                used += (size_t)spaces;
            }
            cells += spaces;
            marks = 0;
            continue;
        }
        if (g.width > width - cells) {
            cells = width;
            continue;
        }
        if (out) {
            if (attention && !attn_started) {
                emit_raw(out, &used, tam_pager_attention_sgr(linux_vt));
                attn_started = true;
            }
            if (st) {
                ssize_t lidx = has_links
                               ? link_index_at(st->links, &link_cursor, glyph_byte)
                               : -1;
                if (want_osc8)
                    emit_osc8(out, &used, &osc8_used, &active_link, lidx, st->links);
                unsigned char want = resolve_style(st, &doc_idx, &search_idx,
                                                   lidx, glyph_byte);
                emit_style(out, &used, &active, want, linux_vt, attention);
            }
            memcpy(out + used, g.bytes, g.len);
            used += g.len;
        }
        cells += g.width;
        if (g.width != 0)
            marks = 0;
        else
            marks++;
    }
    if (out) {
        if (want_osc8)
            emit_osc8(out, &used, &osc8_used, &active_link, -1, st->links);
        if (attention) {
            if (!attn_started) {
                emit_raw(out, &used, tam_pager_attention_sgr(linux_vt));
                attn_started = true;
            } else {
                emit_style(out, &used, &active, TAM_RENDER_STYLE_NONE, linux_vt, true);
            }
            if (cells < width) {
                memset(out + used, ' ', (size_t)(width - cells));
                used += (size_t)(width - cells);
            }
            emit_raw(out, &used, "\033[0m");
        } else {
            emit_style(out, &used, &active, TAM_RENDER_STYLE_NONE, linux_vt, false);
        }
        out[used] = '\0';
    }
    *next = p;
    return TAM_RENDER_ROW;
}

int tam_render_row_h(struct tam_source *s, struct tam_position start, int width,
               int skip, bool chop, char *out, struct tam_position *next)
{
    return tam_render_row_styled(s, start, width, skip, chop, NULL, out, next);
}

int tam_render_row(struct tam_source *s, struct tam_position start, int width,
             char *out, struct tam_position *next)
{
    return tam_render_row_styled(s, start, width, 0, false, NULL, out, next);
}

void tam_render_label(char *out, size_t cap, int width, const char *text)
{
    size_t used = 0;
    size_t remaining = strlen(text);
    bool have_base = false;
    while (remaining) {
        uint32_t cp;
        size_t read = tam_utf8_decode(text, remaining, &cp);
        struct glyph g = glyph(cp, width > 1 ? width : 1);
        if (!have_base && g.width == 0)
            g = (struct glyph){.bytes = "?", .len = 1, .width = 1};
        if (g.width > width)
            break;
        if (g.len >= cap - used)
            break;
        memcpy(out + used, g.bytes, g.len);
        used += g.len;
        width -= g.width;
        have_base = true;
        text += read;
        remaining -= read;
    }
    out[used] = '\0';
}

int tam_render_dump_styled(struct tam_source *s, int width,
                     const struct tam_render_styles *styles,
                     const struct tam_render_links *links,
                     bool linux_vt, bool osc8)
{
    // A terminal without cursor controls can still safely show plain or styled text.
    char *buf = malloc(tam_render_row_styled_capacity(width));
    if (!buf)
        return -1;
    struct tam_render_row_style rs = {
        .doc_spans = styles ? styles->spans : NULL,
        .doc_count = styles ? styles->count : 0,
        .links = links,
        .focused_link = -1,
        .attention = false,
        .linux_vt = linux_vt,
        .osc8 = osc8
    };
    const struct tam_render_row_style *rsp =
        (rs.doc_count > 0 || (osc8 && links && links->span_count > 0)) ? &rs : NULL;
    struct tam_position p = {0}, next;
    int rc = 0;
    for (;;) {
        int result = tam_render_row_styled(s, p, width, 0, false, rsp, buf, &next);
        if (result == TAM_RENDER_WAIT) {
            struct pollfd fd = {.fd = s->input, .events = POLLIN};
            int ready = poll(&fd, 1, -1);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready < 0 || tam_source_pump(s) < 0) {
                rc = -1;
                break;
            }
        } else if (result == TAM_RENDER_ROW) {
            if (puts(buf) == EOF) {
                rc = -1;
                break;
            }
            p = next;
        } else {
            rc = result == TAM_RENDER_EOF ? fflush(stdout) : -1;
            break;
        }
    }
    free(buf);
    return rc;
}

int tam_render_dump(struct tam_source *s, int width)
{
    return tam_render_dump_styled(s, width, NULL, NULL, false, false);
}
