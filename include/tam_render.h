#ifndef TAM_RENDER_H
#define TAM_RENDER_H

/* Document rendering for terminal programs (libtam-render.a): a buffered
 * input source, UTF-8 row layout with styles and hyperlinks, and renderers
 * that turn Markdown, HTML and Mermaid into styled text. Moved from tam-view
 * in libtam 0.13.0. Link libtam-render.a before libtam.a. */

#include "tam.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

enum tam_render_result {
    TAM_RENDER_ERROR = -3,
    TAM_RENDER_WAIT = -2,
    TAM_RENDER_EOF = -1,
    TAM_RENDER_ROW = 0
};

#define TAM_SOURCE_BLOCK (64 * 1024)

struct tam_source {
    int input;
    int fd;
    FILE *spool;
    bool eof;
    bool bounded;
    off_t size;
    off_t block;
    size_t used;
    unsigned char data[TAM_SOURCE_BLOCK];
};

int tam_source_open(struct tam_source *s, int input);
int tam_source_open_file(const char *path);
int tam_source_snapshot(struct tam_source *s, int input);
void tam_source_close(struct tam_source *s);
int tam_source_pump(struct tam_source *s);
int tam_source_byte(struct tam_source *s, off_t offset);
off_t tam_source_size(struct tam_source *s);
int tam_source_refresh(struct tam_source *s, off_t *new_size);


struct tam_position {
    off_t byte;
    unsigned char tab_left;
    unsigned char tab_column;
};


enum tam_render_format {
    TAM_RENDER_FORMAT_RAW = 0,
    TAM_RENDER_FORMAT_MARKDOWN,
    TAM_RENDER_FORMAT_HTML,
    TAM_RENDER_FORMAT_MERMAID
};




enum tam_render_style {
    TAM_RENDER_STYLE_NONE = 0,
    TAM_RENDER_STYLE_H1,
    TAM_RENDER_STYLE_H2,
    TAM_RENDER_STYLE_H3,
    TAM_RENDER_STYLE_BOLD,
    TAM_RENDER_STYLE_ITALIC,
    TAM_RENDER_STYLE_STRIKE,
    TAM_RENDER_STYLE_CODE,
    TAM_RENDER_STYLE_LINK,
    TAM_RENDER_STYLE_LINK_URL,
    TAM_RENDER_STYLE_LINK_CUR,
    TAM_RENDER_STYLE_QUOTE,
    TAM_RENDER_STYLE_RULE,
    TAM_RENDER_STYLE_TABLE_HDR,
    TAM_RENDER_STYLE_NODE,
    TAM_RENDER_STYLE_EDGE,
    TAM_RENDER_STYLE_SEARCH,
    TAM_RENDER_STYLE_SEARCH_CUR
};

struct tam_render_span {
    off_t start;
    off_t end;
    unsigned char style;
};

struct tam_render_styles {
    struct tam_render_span *spans;
    size_t count;
    size_t cap;
};

struct tam_render_link {
    char *url;
    off_t first_byte;
};

struct tam_render_link_span {
    off_t start;
    off_t end;
    size_t link_index;
};

struct tam_render_anchor {
    char *id;
    off_t byte;
};

struct tam_render_links {
    struct tam_render_link *items;
    size_t count;
    size_t cap;
    struct tam_render_link_span *spans;
    size_t span_count;
    size_t span_cap;
    struct tam_render_anchor *anchors;
    size_t anchor_count;
    size_t anchor_cap;
};

struct tam_render_row_style {
    const struct tam_render_span *doc_spans;
    size_t doc_count;
    const struct tam_render_span *search_spans;
    size_t search_count;
    const struct tam_render_links *links;
    ssize_t focused_link;
    bool attention;
    bool linux_vt;
    bool osc8;
};


void tam_render_styles_free(struct tam_render_styles *st);
bool tam_render_styles_push(struct tam_render_styles *st, off_t start, off_t end,
                      unsigned char style);
void tam_render_links_free(struct tam_render_links *lk);
ssize_t tam_render_links_add_item(struct tam_render_links *lk, const char *url,
                            size_t url_len, off_t first_byte);
bool tam_render_links_push_span(struct tam_render_links *lk, off_t start, off_t end,
                          size_t link_index);
bool tam_render_links_add_anchor(struct tam_render_links *lk, const char *id,
                           size_t id_len, off_t byte);

bool tam_position_equal(struct tam_position a, struct tam_position b);
int tam_position_compare(struct tam_position a, struct tam_position b);
int tam_render_row(struct tam_source *s, struct tam_position start, int width,
             char *out, struct tam_position *next);
int tam_render_row_h(struct tam_source *s, struct tam_position start, int width,
               int skip, bool chop, char *out, struct tam_position *next);
int tam_render_row_styled(struct tam_source *s, struct tam_position start, int width,
                    int skip, bool chop, const struct tam_render_row_style *style,
                    char *out, struct tam_position *next);
size_t tam_render_row_capacity(int width);
size_t tam_render_row_styled_capacity(int width);
void tam_render_label(char *out, size_t cap, int width, const char *text);
int tam_render_dump(struct tam_source *s, int width);
int tam_render_dump_styled(struct tam_source *s, int width,
                     const struct tam_render_styles *styles,
                     const struct tam_render_links *links,
                     bool linux_vt, bool osc8);
enum tam_render_format tam_render_detect_format(const char *path, struct tam_source *s,
                                    bool force_render);
int tam_render_source(struct tam_source *raw, const char *path, int width,
                       bool ascii, bool force_render, struct tam_source *out,
                       struct tam_render_styles *styles_out,
                       struct tam_render_links *links_out,
                       enum tam_render_format *fmt_out);

/* off_t is part of these structures, so every user must agree on its size. */
_Static_assert(sizeof(off_t) == 8, "build with -D_FILE_OFFSET_BITS=64");

/* ---- Rendering into memory --------------------------------------------- */

#define TAM_RENDER_MAX_INPUT (16 * 1024 * 1024)

/* Rendered text with its styles and links. Only data, len, styles and links
 * are for callers; the rest belongs to the renderers. */
struct tam_render_buf {
    char *data;
    size_t len;
    size_t cap;
    struct tam_render_styles styles;
    struct tam_render_links links;
    size_t *anchor_index;
    size_t anchor_index_cap;
    size_t *anchor_next;
    size_t anchor_next_cap;
    bool oom;
};

struct tam_render_box {
    const char *tl;
    const char *tr;
    const char *bl;
    const char *br;
    const char *h;
    const char *v;
    const char *lt;
    const char *rt;
    const char *tt;
    const char *bt;
    const char *cross;
    const char *r_tl;
    const char *r_tr;
    const char *r_bl;
    const char *r_br;
    const char *arrow_r;
    const char *arrow_d;
    const char *arrow_l;
    const char *arrow_u;
    const char *bullet;
    const char *d_h;
};

void tam_render_buf_init(struct tam_render_buf *b);
void tam_render_buf_clear(struct tam_render_buf *b);
void tam_render_buf_free(struct tam_render_buf *b);
const struct tam_render_box *tam_render_box_chars(bool ascii);

/* Render src into b at the given width; ascii draws with ASCII only. */
void tam_render_markdown(struct tam_render_buf *b, const char *src, size_t len,
                     int width, bool ascii);
void tam_render_html(struct tam_render_buf *b, const char *src, size_t len,
                 int width, bool ascii);
void tam_render_mermaid(struct tam_render_buf *b, const char *src, size_t len,
                    int width, bool ascii);

#endif
