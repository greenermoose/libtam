#ifndef RENDER_INTERNAL_H
#define RENDER_INTERNAL_H

#include "tam_render.h"

#include <stdbool.h>
#include <stddef.h>




struct tam__rtable_row {
    struct tam_render_buf *cells;
    size_t ncols;
};

struct tam__rtable {
    struct tam__rtable_row *rows;
    size_t nrows;
    size_t max_cols;
    bool has_header;
};

void tam__rbuf_putc(struct tam_render_buf *b, char c);
void tam__rbuf_putn(struct tam_render_buf *b, const char *s, size_t n);
void tam__rbuf_puts(struct tam_render_buf *b, const char *s);
void tam__rbuf_printf(struct tam_render_buf *b, const char *fmt, ...);
void tam__rbuf_repeat(struct tam_render_buf *b, const char *s, int count);
void tam__rbuf_newline(struct tam_render_buf *b);
void tam__rbuf_blank_line(struct tam_render_buf *b);
void tam__rbuf_push_span(struct tam_render_buf *b, size_t start, size_t end,
                    unsigned char style);
void tam__rbuf_fill_style(struct tam_render_buf *b, size_t start, size_t end,
                     unsigned char style);
void tam__rbuf_copy_spans(struct tam_render_buf *dst, size_t dst_off, const struct tam_render_buf *src,
                     size_t src_off, size_t len);
void tam__rbuf_add_link(struct tam_render_buf *b, size_t start, size_t end,
                   const char *url, size_t url_len);
void tam__rbuf_add_anchor(struct tam_render_buf *b, const char *id, size_t id_len,
                     size_t byte);
void tam__rbuf_add_heading_anchor(struct tam_render_buf *b, const char *text, size_t len,
                             size_t byte);
void tam__rbuf_copy_links(struct tam_render_buf *dst, size_t dst_off, const struct tam_render_buf *src,
                     size_t src_off, size_t len,
                     ssize_t *last_src_link, size_t *last_dst_link);
void tam__rbuf_wrap(struct tam_render_buf *b, const char *text, int width,
               const char *first_prefix, const char *cont_prefix);
void tam__rbuf_wrap_styled(struct tam_render_buf *b, const struct tam_render_buf *src, int width,
                      const char *first_prefix, const char *cont_prefix,
                      unsigned char pfx_style);

int tam__render_str_width(const char *s, size_t len);
void tam__render_table(struct tam_render_buf *b, const struct tam__rtable *t, int width, bool ascii);
void tam__rtable_free(struct tam__rtable *t);


#endif
