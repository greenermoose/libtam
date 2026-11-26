#include "render_internal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum node_shape {
    SHAPE_RECT = 0,
    SHAPE_ROUND,
    SHAPE_DIAMOND,
    SHAPE_CIRCLE
};

struct mnode {
    char *id;
    char *label;
    enum node_shape shape;
    int layer;
    int order;
    int box_w;
};

struct medge {
    int from;
    int to;
    char *label;
    bool dotted;
    bool thick;
    bool arrow;
};

struct mgraph {
    struct mnode *nodes;
    size_t n_nodes;
    size_t cap_nodes;
    struct medge *edges;
    size_t n_edges;
    size_t cap_edges;
    bool lr;
    bool reverse;
};

static void mgraph_free(struct mgraph *g)
{
    for (size_t i = 0; i < g->n_nodes; i++) {
        free(g->nodes[i].id);
        free(g->nodes[i].label);
    }
    for (size_t i = 0; i < g->n_edges; i++)
        free(g->edges[i].label);
    free(g->nodes);
    free(g->edges);
}

static int mgraph_find_or_add(struct mgraph *g, const char *id,
                              const char *label, enum node_shape shape)
{
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (strcmp(g->nodes[i].id, id) == 0) {
            if (label && *label) {
                free(g->nodes[i].label);
                g->nodes[i].label = strdup(label);
                g->nodes[i].shape = shape;
            }
            return (int)i;
        }
    }
    if (g->n_nodes == g->cap_nodes) {
        size_t ncap = g->cap_nodes ? g->cap_nodes * 2 : 16;
        struct mnode *grown = realloc(g->nodes, ncap * sizeof(*grown));
        if (!grown)
            return -1;
        g->nodes = grown;
        g->cap_nodes = ncap;
    }
    struct mnode *n = &g->nodes[g->n_nodes];
    *n = (struct mnode){
        .id = strdup(id),
        .label = strdup(label && *label ? label : id),
        .shape = shape
    };
    if (!n->id || !n->label)
        return -1;
    return (int)(g->n_nodes++);
}

static bool mgraph_add_edge(struct mgraph *g, int from, int to,
                            const char *label, bool dotted, bool thick, bool arrow)
{
    if (from < 0 || to < 0)
        return false;
    if (g->n_edges == g->cap_edges) {
        size_t ncap = g->cap_edges ? g->cap_edges * 2 : 16;
        struct medge *grown = realloc(g->edges, ncap * sizeof(*grown));
        if (!grown)
            return false;
        g->edges = grown;
        g->cap_edges = ncap;
    }
    g->edges[g->n_edges++] = (struct medge){
        .from = from,
        .to = to,
        .label = label && *label ? strdup(label) : NULL,
        .dotted = dotted,
        .thick = thick,
        .arrow = arrow
    };
    return true;
}

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    return p;
}

static const char *parse_node_ref(const char *p, const char *end,
                                  struct mgraph *g, int *node_out)
{
    p = skip_ws(p, end);
    const char *id_start = p;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
        // Stop before edge operators like --> or ---
        if (*p == '-' && p + 1 < end &&
            (p[1] == '-' || p[1] == '>' || p[1] == '.'))
            break;
        p++;
    }
    if (p == id_start)
        return NULL;
    size_t id_len = (size_t)(p - id_start);
    char *id = malloc(id_len + 1);
    if (!id)
        return NULL;
    memcpy(id, id_start, id_len);
    id[id_len] = '\0';

    enum node_shape shape = SHAPE_RECT;
    const char *lbl_start = NULL;
    const char *lbl_end = NULL;
    const char *close_tok = NULL;

    if (p + 1 < end && p[0] == '(' && p[1] == '[') {
        shape = SHAPE_ROUND;
        lbl_start = p + 2;
        close_tok = "])";
    } else if (p + 1 < end && p[0] == '(' && p[1] == '(') {
        shape = SHAPE_CIRCLE;
        lbl_start = p + 2;
        close_tok = "))";
    } else if (p + 1 < end && p[0] == '[' && p[1] == '[') {
        shape = SHAPE_RECT;
        lbl_start = p + 2;
        close_tok = "]]";
    } else if (p < end && p[0] == '[') {
        shape = SHAPE_RECT;
        lbl_start = p + 1;
        close_tok = "]";
    } else if (p < end && p[0] == '(') {
        shape = SHAPE_ROUND;
        lbl_start = p + 1;
        close_tok = ")";
    } else if (p < end && p[0] == '{') {
        shape = SHAPE_DIAMOND;
        lbl_start = p + 1;
        close_tok = "}";
    }

    char *label = NULL;
    if (lbl_start && close_tok) {
        size_t clen = strlen(close_tok);
        const char *q = lbl_start;
        while (q + clen <= end && memcmp(q, close_tok, clen) != 0)
            q++;
        if (q + clen <= end) {
            lbl_end = q;
            p = q + clen;
            while (lbl_start < lbl_end && isspace((unsigned char)*lbl_start))
                lbl_start++;
            while (lbl_end > lbl_start && isspace((unsigned char)lbl_end[-1]))
                lbl_end--;
            if (lbl_end - lbl_start >= 2 &&
                ((lbl_start[0] == '"' && lbl_end[-1] == '"') ||
                 (lbl_start[0] == '\'' && lbl_end[-1] == '\''))) {
                lbl_start++;
                lbl_end--;
            }
            size_t llen = (size_t)(lbl_end - lbl_start);
            label = malloc(llen + 1);
            if (label) {
                memcpy(label, lbl_start, llen);
                label[llen] = '\0';
            }
        }
    }

    *node_out = mgraph_find_or_add(g, id, label, shape);
    free(id);
    free(label);
    return *node_out >= 0 ? p : NULL;
}

static const char *parse_edge_op(const char *p, const char *end,
                                 char **label_out, bool *dotted_out,
                                 bool *thick_out, bool *arrow_out)
{
    *label_out = NULL;
    *dotted_out = false;
    *thick_out = false;
    *arrow_out = true;

    p = skip_ws(p, end);
    if (p >= end)
        return NULL;

    // Forms: -->, ---, -.->, -.-, ==>, ===, -- text -->, -. text .->
    if (p + 2 < end && memcmp(p, "-.-", 3) == 0) {
        *dotted_out = true;
        p += 3;
        if (p < end && *p == '>') {
            p++;
        } else {
            *arrow_out = false;
        }
    } else if (p + 3 < end && memcmp(p, "-.->", 4) == 0) {
        *dotted_out = true;
        p += 4;
    } else if (p + 2 < end && (memcmp(p, "-->", 3) == 0 || memcmp(p, "---", 3) == 0)) {
        *arrow_out = (p[2] == '>');
        p += 3;
    } else if (p + 2 < end && (memcmp(p, "==>", 3) == 0 || memcmp(p, "===", 3) == 0)) {
        *thick_out = true;
        *arrow_out = (p[2] == '>');
        p += 3;
    } else if (p + 2 < end && p[0] == '-' && p[1] == '-' && p[2] == ' ') {
        const char *lstart = p + 3;
        const char *q = lstart;
        while (q + 3 <= end && memcmp(q, "-->", 3) != 0 && memcmp(q, "---", 3) != 0)
            q++;
        if (q + 3 > end)
            return NULL;
        *arrow_out = (q[2] == '>');
        const char *lend = q;
        while (lend > lstart && isspace((unsigned char)lend[-1]))
            lend--;
        size_t llen = (size_t)(lend - lstart);
        *label_out = malloc(llen + 1);
        if (*label_out) {
            memcpy(*label_out, lstart, llen);
            (*label_out)[llen] = '\0';
        }
        p = q + 3;
    } else {
        return NULL;
    }

    p = skip_ws(p, end);
    if (p < end && *p == '|') {
        const char *ls = p + 1;
        const char *le = ls;
        while (le < end && *le != '|')
            le++;
        if (le < end) {
            while (ls < le && isspace((unsigned char)*ls))
                ls++;
            while (le > ls && isspace((unsigned char)le[-1]))
                le--;
            size_t llen = (size_t)(le - ls);
            free(*label_out);
            *label_out = malloc(llen + 1);
            if (*label_out) {
                memcpy(*label_out, ls, llen);
                (*label_out)[llen] = '\0';
            }
            p = le + 1;
        }
    }
    return p;
}

static void dfs_classify(const struct mgraph *g, int u,
                         unsigned char *state, bool *is_back)
{
    state[u] = 1;
    for (size_t e = 0; e < g->n_edges; e++) {
        if (g->edges[e].from != u)
            continue;
        int v = g->edges[e].to;
        if (u == v || state[v] == 1)
            is_back[e] = true;
        else if (state[v] == 0)
            dfs_classify(g, v, state, is_back);
    }
    state[u] = 2;
}

static void assign_layers(struct mgraph *g)
{
    if (g->n_nodes == 0)
        return;

    size_t *indeg = calloc(g->n_nodes, sizeof(*indeg));
    unsigned char *state = calloc(g->n_nodes, sizeof(*state));
    bool *is_back = calloc(g->n_edges ? g->n_edges : 1, sizeof(*is_back));
    if (indeg && state && is_back) {
        for (size_t e = 0; e < g->n_edges; e++)
            indeg[g->edges[e].to]++;
        for (size_t i = 0; i < g->n_nodes; i++) {
            if (indeg[i] == 0 && state[i] == 0)
                dfs_classify(g, (int)i, state, is_back);
        }
        for (size_t i = 0; i < g->n_nodes; i++) {
            if (state[i] == 0)
                dfs_classify(g, (int)i, state, is_back);
        }
    }

    // Longest-path relaxation over the acyclic forward edges.
    for (size_t iter = 0; iter < g->n_nodes; iter++) {
        bool changed = false;
        for (size_t e = 0; e < g->n_edges; e++) {
            if (is_back && is_back[e])
                continue;
            int u = g->edges[e].from;
            int v = g->edges[e].to;
            if (u == v)
                continue;
            if (g->nodes[v].layer <= g->nodes[u].layer &&
                g->nodes[u].layer + 1 < (int)g->n_nodes) {
                g->nodes[v].layer = g->nodes[u].layer + 1;
                changed = true;
            }
        }
        if (!changed)
            break;
    }

    free(indeg);
    free(state);
    free(is_back);

    if (g->reverse) {
        int max_l = 0;
        for (size_t i = 0; i < g->n_nodes; i++) {
            if (g->nodes[i].layer > max_l)
                max_l = g->nodes[i].layer;
        }
        for (size_t i = 0; i < g->n_nodes; i++)
            g->nodes[i].layer = max_l - g->nodes[i].layer;
    }
}

static void draw_box_row(struct tam_render_buf *b, const struct mnode *n, int row,
                         const struct tam_render_box *box)
{
    const char *tl = n->shape == SHAPE_ROUND ? box->r_tl : box->tl;
    const char *tr = n->shape == SHAPE_ROUND ? box->r_tr : box->tr;
    const char *bl = n->shape == SHAPE_ROUND ? box->r_bl : box->bl;
    const char *br = n->shape == SHAPE_ROUND ? box->r_br : box->br;
    int inner = n->box_w - 2;

    if (n->shape == SHAPE_DIAMOND) {
        if (row == 0 || row == 2) {
            tam__rbuf_putc(b, ' ');
            size_t rs = b->len;
            tam__rbuf_repeat(b, box->h, inner);
            tam__rbuf_push_span(b, rs, b->len, TAM_RENDER_STYLE_RULE);
            tam__rbuf_putc(b, ' ');
        } else {
            size_t ls = b->len;
            tam__rbuf_puts(b, "< ");
            tam__rbuf_push_span(b, ls, b->len, TAM_RENDER_STYLE_RULE);
            size_t ns = b->len;
            tam__rbuf_puts(b, n->label);
            tam__rbuf_push_span(b, ns, b->len, TAM_RENDER_STYLE_NODE);
            size_t rs = b->len;
            tam__rbuf_puts(b, " >");
            tam__rbuf_push_span(b, rs, b->len, TAM_RENDER_STYLE_RULE);
        }
        return;
    }

    if (row == 0) {
        size_t rs = b->len;
        tam__rbuf_puts(b, tl);
        tam__rbuf_repeat(b, box->h, inner);
        tam__rbuf_puts(b, tr);
        tam__rbuf_push_span(b, rs, b->len, TAM_RENDER_STYLE_RULE);
    } else if (row == 1) {
        size_t ls = b->len;
        tam__rbuf_puts(b, box->v);
        tam__rbuf_push_span(b, ls, b->len, TAM_RENDER_STYLE_RULE);
        tam__rbuf_putc(b, ' ');
        size_t ns = b->len;
        tam__rbuf_puts(b, n->label);
        tam__rbuf_push_span(b, ns, b->len, TAM_RENDER_STYLE_NODE);
        tam__rbuf_putc(b, ' ');
        size_t rs = b->len;
        tam__rbuf_puts(b, box->v);
        tam__rbuf_push_span(b, rs, b->len, TAM_RENDER_STYLE_RULE);
    } else {
        size_t rs = b->len;
        tam__rbuf_puts(b, bl);
        tam__rbuf_repeat(b, box->h, inner);
        tam__rbuf_puts(b, br);
        tam__rbuf_push_span(b, rs, b->len, TAM_RENDER_STYLE_RULE);
    }
}

static void render_td(struct tam_render_buf *b, struct mgraph *g, int width,
                      const struct tam_render_box *box)
{
    int max_layer = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        int lw = tam__render_str_width(g->nodes[i].label, strlen(g->nodes[i].label));
        g->nodes[i].box_w = lw + 4;
        if (g->nodes[i].layer > max_layer)
            max_layer = g->nodes[i].layer;
    }

    for (int l = 0; l <= max_layer; l++) {
        size_t count = 0;
        for (size_t i = 0; i < g->n_nodes; i++) {
            if (g->nodes[i].layer == l)
                count++;
        }
        if (count == 0)
            continue;

        // Pack nodes in this layer into rows fitting within width.
        size_t idx = 0;
        while (idx < g->n_nodes) {
            while (idx < g->n_nodes && g->nodes[idx].layer != l)
                idx++;
            if (idx >= g->n_nodes)
                break;

            size_t row_nodes[64];
            size_t rcount = 0;
            int used_w = 0;
            while (idx < g->n_nodes && rcount < 64) {
                if (g->nodes[idx].layer == l) {
                    int w = g->nodes[idx].box_w;
                    int add = rcount > 0 ? w + 2 : w;
                    if (rcount > 0 && used_w + add > width)
                        break;
                    row_nodes[rcount++] = idx;
                    used_w += add;
                }
                idx++;
            }

            for (int r = 0; r < 3; r++) {
                for (size_t k = 0; k < rcount; k++) {
                    if (k > 0)
                        tam__rbuf_puts(b, "  ");
                    draw_box_row(b, &g->nodes[row_nodes[k]], r, box);
                }
                tam__rbuf_putc(b, '\n');
            }
        }

        // Render outgoing edges from this layer.
        size_t out_from_layer = 0;
        for (size_t e = 0; e < g->n_edges; e++) {
            if (g->nodes[g->edges[e].from].layer == l)
                out_from_layer++;
        }
        for (size_t e = 0; e < g->n_edges; e++) {
            int u = g->edges[e].from;
            int v = g->edges[e].to;
            if (g->nodes[u].layer != l)
                continue;
            tam__rbuf_puts(b, "  ");
            size_t vs = b->len;
            tam__rbuf_puts(b, box->v);
            tam__rbuf_push_span(b, vs, b->len, TAM_RENDER_STYLE_RULE);
            if (g->edges[e].label) {
                tam__rbuf_putc(b, ' ');
                size_t es = b->len;
                tam__rbuf_printf(b, "(%s)", g->edges[e].label);
                tam__rbuf_push_span(b, es, b->len, TAM_RENDER_STYLE_EDGE);
            }
            if (g->nodes[v].layer != l + 1 || out_from_layer > 1) {
                tam__rbuf_putc(b, ' ');
                size_t as = b->len;
                tam__rbuf_puts(b, g->edges[e].arrow ? box->arrow_r : box->h);
                tam__rbuf_push_span(b, as, b->len, TAM_RENDER_STYLE_RULE);
                tam__rbuf_puts(b, " [");
                size_t ns = b->len;
                tam__rbuf_puts(b, g->nodes[v].label);
                tam__rbuf_push_span(b, ns, b->len, TAM_RENDER_STYLE_NODE);
                tam__rbuf_putc(b, ']');
            }
            tam__rbuf_putc(b, '\n');
        }
        if (l < max_layer) {
            tam__rbuf_puts(b, "  ");
            size_t as = b->len;
            tam__rbuf_puts(b, box->arrow_d);
            tam__rbuf_push_span(b, as, b->len, TAM_RENDER_STYLE_RULE);
            tam__rbuf_putc(b, '\n');
        }
    }
}

static bool try_render_lr(struct tam_render_buf *b, struct mgraph *g, int width,
                          const struct tam_render_box *box)
{
    int max_layer = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        int lw = tam__render_str_width(g->nodes[i].label, strlen(g->nodes[i].label));
        g->nodes[i].box_w = lw + 4;
        if (g->nodes[i].layer > max_layer)
            max_layer = g->nodes[i].layer;
    }

    int *layer_w = calloc((size_t)(max_layer + 1), sizeof(*layer_w));
    int *layer_count = calloc((size_t)(max_layer + 1), sizeof(*layer_count));
    if (!layer_w || !layer_count) {
        free(layer_w);
        free(layer_count);
        return false;
    }

    int max_rows = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        int l = g->nodes[i].layer;
        if (g->nodes[i].box_w > layer_w[l])
            layer_w[l] = g->nodes[i].box_w;
        layer_count[l]++;
        if (layer_count[l] > max_rows)
            max_rows = layer_count[l];
    }

    int total_w = 0;
    for (int l = 0; l <= max_layer; l++) {
        total_w += layer_w[l];
        if (l < max_layer)
            total_w += 5;
    }
    if (total_w > width) {
        free(layer_w);
        free(layer_count);
        return false;
    }

    for (int row_idx = 0; row_idx < max_rows; row_idx++) {
        for (int sub = 0; sub < 3; sub++) {
            for (int l = 0; l <= max_layer; l++) {
                int seen = 0;
                const struct mnode *found = NULL;
                for (size_t i = 0; i < g->n_nodes; i++) {
                    if (g->nodes[i].layer == l) {
                        if (seen == row_idx) {
                            found = &g->nodes[i];
                            break;
                        }
                        seen++;
                    }
                }
                if (found) {
                    draw_box_row(b, found, sub, box);
                    tam__rbuf_repeat(b, " ", layer_w[l] - found->box_w);
                } else {
                    tam__rbuf_repeat(b, " ", layer_w[l]);
                }
                if (l < max_layer) {
                    if (sub == 1 && found) {
                        tam__rbuf_puts(b, " ");
                        size_t as = b->len;
                        tam__rbuf_repeat(b, box->h, 2);
                        tam__rbuf_puts(b, box->arrow_r);
                        tam__rbuf_push_span(b, as, b->len, TAM_RENDER_STYLE_RULE);
                        tam__rbuf_puts(b, " ");
                    } else {
                        tam__rbuf_puts(b, "     ");
                    }
                }
            }
            tam__rbuf_putc(b, '\n');
        }
    }

    // List labeled edges below LR diagram for clarity.
    bool any_lbl = false;
    for (size_t e = 0; e < g->n_edges; e++) {
        if (g->edges[e].label) {
            if (!any_lbl) {
                tam__rbuf_putc(b, '\n');
                any_lbl = true;
            }
            tam__rbuf_puts(b, "  ");
            size_t fs = b->len;
            tam__rbuf_puts(b, g->nodes[g->edges[e].from].label);
            tam__rbuf_push_span(b, fs, b->len, TAM_RENDER_STYLE_NODE);
            tam__rbuf_putc(b, ' ');
            size_t rs = b->len;
            tam__rbuf_puts(b, box->h);
            tam__rbuf_push_span(b, rs, b->len, TAM_RENDER_STYLE_RULE);
            size_t es = b->len;
            tam__rbuf_printf(b, "(%s)", g->edges[e].label);
            tam__rbuf_push_span(b, es, b->len, TAM_RENDER_STYLE_EDGE);
            rs = b->len;
            tam__rbuf_puts(b, g->edges[e].arrow ? box->arrow_r : box->h);
            tam__rbuf_push_span(b, rs, b->len, TAM_RENDER_STYLE_RULE);
            tam__rbuf_putc(b, ' ');
            size_t ts = b->len;
            tam__rbuf_puts(b, g->nodes[g->edges[e].to].label);
            tam__rbuf_push_span(b, ts, b->len, TAM_RENDER_STYLE_NODE);
            tam__rbuf_putc(b, '\n');
        }
    }

    free(layer_w);
    free(layer_count);
    return true;
}

static const char *parse_state_ref(const char *p, const char *end,
                                   struct mgraph *g, bool is_target,
                                   int *node_out)
{
    p = skip_ws(p, end);
    if (p + 3 <= end && memcmp(p, "[*]", 3) == 0) {
        const char *id = is_target ? "__state_end__" : "__state_start__";
        *node_out = mgraph_find_or_add(g, id, "[*]", SHAPE_ROUND);
        return *node_out >= 0 ? p + 3 : NULL;
    }
    const char *id_start = p;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
        if (*p == '-' && p + 1 < end &&
            (p[1] == '-' || p[1] == '>' || p[1] == '.'))
            break;
        p++;
    }
    if (p == id_start)
        return NULL;
    size_t id_len = (size_t)(p - id_start);
    char *id = malloc(id_len + 1);
    if (!id)
        return NULL;
    memcpy(id, id_start, id_len);
    id[id_len] = '\0';
    *node_out = mgraph_find_or_add(g, id, NULL, SHAPE_ROUND);
    free(id);
    return *node_out >= 0 ? p : NULL;
}

static void parse_state_line(const char *p, const char *end,
                             struct mgraph *g, bool *in_note)
{
    if (*in_note) {
        if (strncasecmp(p, "end note", 8) == 0 &&
            (p + 8 == end || isspace((unsigned char)p[8])))
            *in_note = false;
        return;
    }

    if (strncasecmp(p, "note ", 5) == 0) {
        // Single-line note has ':'; multi-line note runs until 'end note'.
        if (!memchr(p, ':', (size_t)(end - p)))
            *in_note = true;
        return;
    }

    if (strncasecmp(p, "direction ", 10) == 0) {
        const char *d = skip_ws(p + 10, end);
        if (d + 2 <= end) {
            if (strncasecmp(d, "LR", 2) == 0) {
                g->lr = true;
                g->reverse = false;
            } else if (strncasecmp(d, "RL", 2) == 0) {
                g->lr = true;
                g->reverse = true;
            } else if (strncasecmp(d, "BT", 2) == 0) {
                g->lr = false;
                g->reverse = true;
            } else if (strncasecmp(d, "TB", 2) == 0 ||
                       strncasecmp(d, "TD", 2) == 0) {
                g->lr = false;
                g->reverse = false;
            }
        }
        return;
    }

    // state "Display Label" as StateId
    if (strncasecmp(p, "state ", 6) == 0) {
        const char *q = skip_ws(p + 6, end);
        if (q < end && *q == '"') {
            const char *ls = q + 1;
            const char *le = ls;
            while (le < end && *le != '"')
                le++;
            if (le < end) {
                const char *after = skip_ws(le + 1, end);
                if (after + 3 <= end && strncasecmp(after, "as", 2) == 0 &&
                    isspace((unsigned char)after[2])) {
                    const char *ids = skip_ws(after + 2, end);
                    const char *ide = ids;
                    while (ide < end &&
                           (isalnum((unsigned char)*ide) || *ide == '_' || *ide == '-'))
                        ide++;
                    if (ide > ids) {
                        size_t idl = (size_t)(ide - ids);
                        size_t ll = (size_t)(le - ls);
                        char *id = malloc(idl + 1);
                        char *lbl = malloc(ll + 1);
                        if (id && lbl) {
                            memcpy(id, ids, idl);
                            id[idl] = '\0';
                            memcpy(lbl, ls, ll);
                            lbl[ll] = '\0';
                            (void)mgraph_find_or_add(g, id, lbl, SHAPE_ROUND);
                        }
                        free(id);
                        free(lbl);
                    }
                }
            }
        }
        return;
    }

    if (strncasecmp(p, "classDef ", 9) == 0 ||
        strncasecmp(p, "class ", 6) == 0 ||
        strncasecmp(p, "style ", 6) == 0 ||
        (p < end && *p == '}'))
        return;

    // Transition: <src> --> <dst> [: <label>]
    int src_node = -1;
    const char *after_src = parse_state_ref(p, end, g, false, &src_node);
    if (!after_src)
        return;

    const char *op = skip_ws(after_src, end);
    if (op + 3 <= end && memcmp(op, "-->", 3) == 0) {
        int dst_node = -1;
        const char *after_dst = parse_state_ref(op + 3, end, g, true, &dst_node);
        if (!after_dst)
            return;
        const char *rest = skip_ws(after_dst, end);
        char *elbl = NULL;
        if (rest < end && *rest == ':') {
            const char *ls = skip_ws(rest + 1, end);
            const char *le = end;
            while (le > ls && isspace((unsigned char)le[-1]))
                le--;
            if (le > ls) {
                size_t ll = (size_t)(le - ls);
                elbl = malloc(ll + 1);
                if (elbl) {
                    memcpy(elbl, ls, ll);
                    elbl[ll] = '\0';
                }
            }
        }
        mgraph_add_edge(g, src_node, dst_node, elbl, false, false, true);
        free(elbl);
    }
}

void tam_render_mermaid(struct tam_render_buf *b, const char *src, size_t len,
                    int width, bool ascii)
{
    const struct tam_render_box *box = tam_render_box_chars(ascii);
    if (width < 20)
        width = 20;

    struct mgraph g = {0};
    bool header_ok = false;
    bool is_state = false;
    bool in_note = false;

    size_t pos = 0;
    while (pos < len) {
        const char *line = src + pos;
        size_t i = pos;
        while (i < len && src[i] != '\n')
            i++;
        size_t llen = i - pos;
        if (llen > 0 && line[llen - 1] == '\r')
            llen--;
        pos = (i < len) ? i + 1 : len;

        const char *p = skip_ws(line, line + llen);
        const char *end = line + llen;
        if (p == end || (p + 1 < end && p[0] == '%' && p[1] == '%'))
            continue;

        if (!header_ok) {
            if (strncasecmp(p, "flowchart", 9) == 0 &&
                (p + 9 == end || isspace((unsigned char)p[9]))) {
                p = skip_ws(p + 9, end);
                header_ok = true;
            } else if (strncasecmp(p, "graph", 5) == 0 &&
                       (p + 5 == end || isspace((unsigned char)p[5]))) {
                p = skip_ws(p + 5, end);
                header_ok = true;
            } else if (strncasecmp(p, "stateDiagram-v2", 15) == 0 &&
                       (p + 15 == end || isspace((unsigned char)p[15]))) {
                header_ok = true;
                is_state = true;
                continue;
            } else if (strncasecmp(p, "stateDiagram", 12) == 0 &&
                       (p + 12 == end || isspace((unsigned char)p[12]))) {
                header_ok = true;
                is_state = true;
                continue;
            } else {
                size_t ds = b->len;
                tam__rbuf_puts(b, "[tam-view: unsupported or invalid mermaid diagram]");
                tam__rbuf_push_span(b, ds, b->len, TAM_RENDER_STYLE_EDGE);
                tam__rbuf_putc(b, '\n');
                tam__rbuf_putn(b, src, len);
                if (len == 0 || src[len - 1] != '\n')
                    tam__rbuf_putc(b, '\n');
                return;
            }
            if (p + 2 <= end) {
                if (strncasecmp(p, "LR", 2) == 0)
                    g.lr = true;
                else if (strncasecmp(p, "RL", 2) == 0) {
                    g.lr = true;
                    g.reverse = true;
                } else if (strncasecmp(p, "BT", 2) == 0) {
                    g.reverse = true;
                }
            }
            continue;
        }

        if (is_state) {
            parse_state_line(p, end, &g, &in_note);
            continue;
        }

        if (strncasecmp(p, "subgraph", 8) == 0 ||
            strncasecmp(p, "end", 3) == 0 ||
            strncasecmp(p, "style ", 6) == 0 ||
            strncasecmp(p, "classDef ", 9) == 0 ||
            strncasecmp(p, "class ", 6) == 0 ||
            strncasecmp(p, "click ", 6) == 0 ||
            strncasecmp(p, "direction ", 10) == 0) {
            continue;
        }

        int cur = -1;
        const char *next_p = parse_node_ref(p, end, &g, &cur);
        if (!next_p)
            continue;
        p = next_p;

        for (;;) {
            char *elbl = NULL;
            bool dotted = false, thick = false, arrow = true;
            const char *after_edge = parse_edge_op(p, end, &elbl, &dotted,
                                                   &thick, &arrow);
            if (!after_edge)
                break;
            int dst = -1;
            const char *after_dst = parse_node_ref(after_edge, end, &g, &dst);
            if (!after_dst) {
                free(elbl);
                break;
            }
            mgraph_add_edge(&g, cur, dst, elbl, dotted, thick, arrow);
            free(elbl);
            cur = dst;
            p = after_dst;
        }
    }

    if (!header_ok || g.n_nodes == 0) {
        size_t ds = b->len;
        tam__rbuf_puts(b, "[tam-view: empty or invalid mermaid flowchart]");
        tam__rbuf_push_span(b, ds, b->len, TAM_RENDER_STYLE_EDGE);
        tam__rbuf_putc(b, '\n');
        tam__rbuf_putn(b, src, len);
        if (len == 0 || src[len - 1] != '\n')
            tam__rbuf_putc(b, '\n');
        mgraph_free(&g);
        return;
    }

    assign_layers(&g);
    if (!g.lr || !try_render_lr(b, &g, width, box))
        render_td(b, &g, width, box);

    mgraph_free(&g);
}
