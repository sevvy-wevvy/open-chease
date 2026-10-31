#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

static inline const uint8_t *pix(const Frame *f, int x, int y) { return f->px + (size_t)y * f->stride + (size_t)x * 4; }

static inline void rgb(const Frame *f, int x, int y, uint8_t out[3])
{
    const uint8_t *p = pix(f, x, y);
    if (f->bgr) {
        out[0] = p[2];
        out[1] = p[1];
        out[2] = p[0];
    } else {
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
    }
}

static inline int cdiff(const uint8_t *a, const uint8_t *b)
{
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]);
}

static inline int lum(const uint8_t *c) { return (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000; }

typedef struct {
    int start, len;
    uint8_t c[3];
} Run;

typedef struct {
    int y, x0, n;
    double pitch;
    uint8_t a[3], b[3];
} Chain;

#define MAX_CHAINS 4096
#define TOL_RUN 30
#define MIN_SQ 18 // smaller than this isnt a real board

static int sample_ok(const Frame *f, double x, double y)
{
    return x >= 0 && y >= 0 && x < f->w && y < f->h;
}

static void patch(const Frame *f, double cx, double cy, int r, uint8_t out[3])
{
    int sum[3] = {0, 0, 0}, n = 0, st = r > 3 ? r / 2 : 1;
    for (int y = (int)cy - r; y <= (int)cy + r; y += st)
        for (int x = (int)cx - r; x <= (int)cx + r; x += st) {
            if (!sample_ok(f, x, y)) continue;
            uint8_t px[3];
            rgb(f, x, y, px);
            for (int k = 0; k < 3; k++) sum[k] += px[k];
            n++;
        }
    for (int k = 0; k < 3; k++) out[k] = (uint8_t)(n ? sum[k] / n : 0);
}

static double score_board(const Frame *f, double bx, double by, double s, const uint8_t *light, const uint8_t *dark,
                          int (*masked)(void *, int, int), void *mctx, int *outside_penalty, int soft)
{
    int pr = (int)fmax(1, s * 0.035), sep = cdiff(light, dark);
    static const double pts[5][2] = {{0.17, 0.17}, {0.83, 0.17}, {0.17, 0.83}, {0.83, 0.83}, {0.5, 0.15}};
    int ok = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            const uint8_t *want = ((r + c) & 1) ? dark : light;
            const uint8_t *other = ((r + c) & 1) ? light : dark;
            int good = 0, bad = 0, valid = 0;
            for (int k = 0; k < 5; k++) {
                double x = bx + (c + pts[k][0]) * s, y = by + (r + pts[k][1]) * s;
                if (!sample_ok(f, x, y)) {
                    bad++;
                    continue;
                }
                if (masked && masked(mctx, (int)x, (int)y)) continue;
                valid++;
                uint8_t px[3];
                if (soft) {
                    patch(f, x, y, pr, px);
                    int dw = cdiff(px, want), dot = cdiff(px, other);
                    if (dw < dot && dw < sep * 0.6)
                        good++;
                    else if (dot < dw && dot < sep * 0.6)
                        bad++;
                    continue;
                }
                rgb(f, (int)x, (int)y, px);
                if (cdiff(px, want) < 42)
                    good++;
                else if (cdiff(px, other) < 42)
                    bad++;
            }
            if (!valid && !bad)
                ok++;
            else if (good >= 2 && good > bad)
                ok++;
        }
    // checkers keep going past the edge? then we're a row off
    if (outside_penalty) {
        int pen = 0;
        for (int side = 0; side < 4; side++) {
            int hits = 0;
            for (int i = 0; i < 8; i++) {
                int r, c;
                if (side == 0) r = -1, c = i;
                else if (side == 1) r = 8, c = i;
                else if (side == 2) r = i, c = -1;
                else r = i, c = 8;
                const uint8_t *want = (((r + c) & 1) ? dark : light), *other = (((r + c) & 1) ? light : dark);
                int good = 0;
                for (int k = 0; k < 4; k++) {
                    double x = bx + (c + pts[k][0]) * s, y = by + (r + pts[k][1]) * s;
                    if (!sample_ok(f, x, y)) continue;
                    uint8_t px[3];
                    if (soft) {
                        patch(f, x, y, pr, px);
                        if (cdiff(px, want) < cdiff(px, other) * 0.6 && cdiff(px, want) < sep * 0.4) good++;
                        continue;
                    }
                    rgb(f, (int)x, (int)y, px);
                    if (cdiff(px, want) < 30) good++;
                }
                if (good >= 3) hits++;
            }
            if (hits >= 5) pen += hits;
        }
        *outside_penalty = pen;
    }
    return ok;
}

static int is_flip(const uint8_t *a, const uint8_t *b, const Board *bd)
{
    return (cdiff(a, bd->light) < 40 && cdiff(b, bd->dark) < 40) || (cdiff(a, bd->dark) < 40 && cdiff(b, bd->light) < 40);
}

static double edge_strength_v(const Frame *f, const Board *bd, int x, int y0, int y1)
{
    int o = (int)(bd->s * 0.03); // zoomed boards smear the edge a few px
    if (x - 1 - o < 0 || x + o >= f->w) return 0;
    if (y0 < 0) y0 = 0;
    if (y1 > f->h) y1 = f->h;
    double sum = 0;
    for (int y = y0; y < y1; y++) {
        uint8_t a[3], b[3], a2[3];
        rgb(f, x - 1 - o, y, a);
        rgb(f, x + o, y, b);
        if (is_flip(a, b, bd)) sum += 1;
        else if (x - 2 - o >= 0) {
            rgb(f, x - 2 - o, y, a2);
            if (is_flip(a2, b, bd)) sum += 0.5;
        }
    }
    return sum;
}

static double edge_strength_h(const Frame *f, const Board *bd, int y, int x0, int x1)
{
    int o = (int)(bd->s * 0.03);
    if (y - 1 - o < 0 || y + o >= f->h) return 0;
    if (x0 < 0) x0 = 0;
    if (x1 > f->w) x1 = f->w;
    double sum = 0;
    for (int x = x0; x < x1; x++) {
        uint8_t a[3], b[3], a2[3];
        rgb(f, x, y - 1 - o, a);
        rgb(f, x, y + o, b);
        if (is_flip(a, b, bd)) sum += 1;
        else if (y - 2 - o >= 0) {
            rgb(f, x, y - 2 - o, a2);
            if (is_flip(a2, b, bd)) sum += 0.5;
        }
    }
    return sum;
}

static int line_fit(const double *ks, const double *vs, const double *str, double s, double *slope, double *icpt)
{
    double mx = s * 0.6;
    for (int i = 0; i < 7; i++)
        if (str[i] > mx) mx = str[i];
    double n = 0, sk = 0, skk = 0, sv = 0, skv = 0;
    for (int i = 0; i < 7; i++) {
        if (str[i] < mx * 0.25) continue;
        n++, sk += ks[i], skk += ks[i] * ks[i], sv += vs[i], skv += ks[i] * vs[i];
    }
    double den = n * skk - sk * sk;
    if (n < 3 || den <= 0) return 0;
    *slope = (n * skv - sk * sv) / den;
    *icpt = (sv - *slope * sk) / n;
    return 1;
}

static double plateau_mid(const double *es, int win, double *best)
{
    int bi = 0;
    for (int i = 1; i <= 2 * win; i++)
        if (es[i] > es[bi]) bi = i;
    *best = es[bi];
    if (es[bi] <= 0) return 0;
    int lo = bi, hi = bi;
    while (lo > 0 && es[lo - 1] >= es[bi] * 0.95) lo--;
    while (hi < 2 * win && es[hi + 1] >= es[bi] * 0.95) hi++;
    return (lo + hi) / 2.0 - win;
}

static void refine(const Frame *f, Board *b)
{
    double s = b->s;
    int win = (int)(s * 0.45) + 2; // the first guess can be 20+ px off on big boards
    if (win > 500) win = 500;
    double es[1001];
    double ks[7], xs[7], ys[7], sx_[7], sy_[7];
    for (int k = 1; k <= 7; k++) {
        double ex = b->x + k * s, ey = b->y + k * s, best, besty;
        for (int d = -win; d <= win; d++) es[d + win] = edge_strength_v(f, b, (int)(ex + d), (int)b->y, (int)(b->y + 8 * s));
        double bx = ex + plateau_mid(es, win, &best);
        for (int d = -win; d <= win; d++) es[d + win] = edge_strength_h(f, b, (int)(ey + d), (int)b->x, (int)(b->x + 8 * s));
        double by = ey + plateau_mid(es, win, &besty);
        ks[k - 1] = k, xs[k - 1] = bx, ys[k - 1] = by, sx_[k - 1] = best, sy_[k - 1] = besty;
    }
    double slope_x, icpt_x, slope_y, icpt_y;
    int okx = line_fit(ks, xs, sx_, s, &slope_x, &icpt_x), oky = line_fit(ks, ys, sy_, s, &slope_y, &icpt_y);
    if (!okx && !oky) return;
    double ns = okx && oky ? (slope_x + slope_y) / 2 : okx ? slope_x : slope_y;
    if (fabs(ns - s) >= s * 0.08) return;
    b->s = ns;
    if (okx) b->x = icpt_x;
    if (oky) b->y = icpt_y;
}

static int in_ignore(const Rect *ig, int n, double x, double y)
{
    for (int i = 0; i < n; i++)
        if (x >= ig[i].x && x < ig[i].x + ig[i].w && y >= ig[i].y && y < ig[i].y + ig[i].h) return 1;
    return 0;
}

static int find_chains(const Frame *f, Chain *chains, int maxc, const Rect *ignore, int nignore)
{
    int nchains = 0;
    int step = f->h > 1500 ? 4 : 3;
    Run *runs = malloc(sizeof(Run) * (f->w + 1));
    for (int y = 0; y < f->h && nchains < maxc; y += step) {
        int nr = 0;
        uint8_t ref[3];
        rgb(f, 0, y, ref);
        int start = 0;
        for (int x = 1; x <= f->w; x++) {
            uint8_t c[3];
            int end = x == f->w;
            if (!end) rgb(f, x, y, c);
            if (end || cdiff(c, ref) > TOL_RUN) {
                runs[nr].start = start;
                runs[nr].len = x - start;
                memcpy(runs[nr].c, ref, 3);
                nr++;
                if (!end) {
                    memcpy(ref, c, 3);
                    start = x;
                }
            }
        }
        int idx[4096];
        int ns = 0;
        for (int i = 0; i < nr && ns < 4096; i++)
            if (runs[i].len >= MIN_SQ) idx[ns++] = i;
        for (int i = 0; i < ns; i++) {
            Run *r0 = &runs[idx[i]];
            int n = 1, last = i;
            double L = r0->len;
            for (int j = i + 1; j < ns; j++) {
                Run *prev = &runs[idx[last]], *cur = &runs[idx[j]];
                if (cur->start - (prev->start + prev->len) > 3 + L * 0.04) break;
                double tol = L * 0.08 + 3;
                if (fabs(cur->len - L) > tol) break;
                if (cdiff(cur->c, prev->c) < 45) break;
                if (n >= 2 && cdiff(cur->c, runs[idx[last - 1]].c) > 40) break;
                n++;
                last = j;
                if (n > 8) break;
            }
            if (n >= 3) {
                Run *rl = &runs[idx[last]];
                double pitch = (double)(rl->start - r0->start) / (n - 1);
                if (pitch >= MIN_SQ && !in_ignore(ignore, nignore, r0->start, y)) {
                    Chain *ch = &chains[nchains++];
                    ch->y = y;
                    ch->x0 = r0->start;
                    ch->n = n;
                    ch->pitch = pitch;
                    memcpy(ch->a, r0->c, 3);
                    memcpy(ch->b, runs[idx[i + 1]].c, 3);
                    if (nchains >= maxc) break;
                }
                i = last - 1;
            }
        }
    }
    free(runs);
    return nchains;
}

static int find_chains_soft(const Frame *f, Chain *chains, int maxc, const Rect *ignore, int nignore)
{
    int nchains = 0, W = f->w, step = f->h > 1500 ? 6 : 4;
    int *pre = malloc(sizeof(int) * 3 * (W + 1)), *sm = malloc(sizeof(int) * 3 * W), *ed = malloc(sizeof(int) * W);
    int *cut = malloc(sizeof(int) * (W + 2));
    Run *runs = malloc(sizeof(Run) * (W + 1));
    for (int y = 2; y < f->h - 2 && nchains < maxc; y += step) {
        for (int k = 0; k < 3; k++) pre[k] = 0;
        for (int x = 0; x < W; x++) {
            int acc[3] = {0, 0, 0};
            for (int dy = -2; dy <= 2; dy++) {
                uint8_t px[3];
                rgb(f, x, y + dy, px);
                for (int k = 0; k < 3; k++) acc[k] += px[k];
            }
            for (int k = 0; k < 3; k++) pre[(x + 1) * 3 + k] = pre[x * 3 + k] + acc[k];
        }
        for (int x = 0; x < W; x++) {
            int a = x - 2 < 0 ? 0 : x - 2, b = x + 3 > W ? W : x + 3;
            for (int k = 0; k < 3; k++) sm[x * 3 + k] = (pre[b * 3 + k] - pre[a * 3 + k]) / (5 * (b - a));
        }
        for (int x = 0; x < W; x++) {
            int a = x - 3 < 0 ? 0 : x - 3, b = x + 3 >= W ? W - 1 : x + 3;
            ed[x] = abs(sm[b * 3] - sm[a * 3]) + abs(sm[b * 3 + 1] - sm[a * 3 + 1]) + abs(sm[b * 3 + 2] - sm[a * 3 + 2]);
        }
        int nc = 0;
        cut[nc++] = 0;
        for (int x = 3; x < W - 3; x++) {
            if (ed[x] < 60) continue;
            int peak = 1;
            for (int d = -3; d <= 3 && peak; d++)
                if (d && (ed[x + d] > ed[x] || (ed[x + d] == ed[x] && d < 0))) peak = 0;
            if (peak && x - cut[nc - 1] >= 4) cut[nc++] = x;
        }
        cut[nc++] = W;
        int nr = 0;
        for (int i = 0; i + 1 < nc; i++) {
            int a = cut[i], b = cut[i + 1], len = b - a;
            runs[nr].start = a;
            runs[nr].len = len;
            int m0 = a + len / 5, m1 = b - len / 5, acc[3] = {0, 0, 0}, n = 0;
            for (int x = m0; x < m1; x++, n++)
                for (int k = 0; k < 3; k++) acc[k] += sm[x * 3 + k];
            for (int k = 0; k < 3; k++) runs[nr].c[k] = (uint8_t)(n ? acc[k] / n : sm[a * 3 + k]);
            nr++;
        }
        for (int i = 0; i < nr; i++) {
            Run *r0 = &runs[i];
            if (r0->len < MIN_SQ) continue;
            int n = 1, last = i;
            double L = r0->len;
            for (int j = i + 1; j < nr; j++) {
                Run *prev = &runs[last], *cur = &runs[j];
                if (fabs(cur->len - L) > L * 0.1 + 4) break;
                int c1 = cdiff(cur->c, prev->c);
                if (c1 < 45) break;
                if (n >= 2 && cdiff(cur->c, runs[last - 1].c) > fmax(40, c1 * 0.45)) break;
                n++;
                last = j;
                if (n > 8) break;
            }
            if (n >= 3) {
                double pitch = (double)(runs[last].start - r0->start) / (n - 1);
                if (pitch >= MIN_SQ && !in_ignore(ignore, nignore, r0->start, y)) {
                    Chain *ch = &chains[nchains++];
                    ch->y = y;
                    ch->x0 = r0->start;
                    ch->n = n;
                    ch->pitch = pitch;
                    memcpy(ch->a, r0->c, 3);
                    memcpy(ch->b, runs[i + 1].c, 3);
                    if (nchains >= maxc) break;
                }
                i = last - 1;
            }
        }
    }
    free(pre);
    free(sm);
    free(ed);
    free(cut);
    free(runs);
    return nchains;
}

static int same_colors(const Chain *a, const Chain *b, int tol)
{
    return (cdiff(a->a, b->a) < tol && cdiff(a->b, b->b) < tol) || (cdiff(a->a, b->b) < tol && cdiff(a->b, b->a) < tol);
}

static int find_board(const Frame *f, Board *out, const Rect *ignore, int nignore, int (*masked)(void *, int, int),
                      void *mctx, int soft)
{
    Chain *chains = malloc(sizeof(Chain) * MAX_CHAINS);
    int nc = soft ? find_chains_soft(f, chains, MAX_CHAINS, ignore, nignore) : find_chains(f, chains, MAX_CHAINS, ignore, nignore);
    int used[64] = {0};
    Board best = {0};
    double best_score = 0;
    // ads and thumbnails make fake chains
    for (int attempt = 0; attempt < 6; attempt++) {
        int bi = -1;
        double bw = 0;
        for (int i = 0; i < nc; i++) {
            if (chains[i].n == 0) continue;
            int skip = 0;
            for (int u = 0; u < attempt; u++)
                if (used[u] == i) skip = 1;
            if (skip) continue;
            double w = chains[i].n * 4;
            int support = 0;
            for (int j = 0; j < nc; j++) {
                if (fabs(chains[j].pitch - chains[i].pitch) > chains[i].pitch * 0.05) continue;
                if (abs(chains[j].y - chains[i].y) > chains[i].pitch * 8) continue;
                if (!same_colors(&chains[i], &chains[j], soft ? 70 : 40)) continue;
                double dist = abs(chains[j].x0 - chains[i].x0);
                double ph = dist - floor(dist / chains[i].pitch) * chains[i].pitch;
                if (ph > 4 && ph < chains[i].pitch - 4) continue;
                support += chains[j].n;
            }
            w += support;
            if (w > bw) bw = w, bi = i;
        }
        if (bi < 0) break;
        used[attempt] = bi;
        Chain *ch = &chains[bi];
        double s = ch->pitch;
        uint8_t light[3], dark[3];
        if (lum(ch->a) >= lum(ch->b)) {
            memcpy(light, ch->a, 3);
            memcpy(dark, ch->b, 3);
        } else {
            memcpy(light, ch->b, 3);
            memcpy(dark, ch->a, 3);
        }
        if (cdiff(light, dark) < 45) continue;
        double lo_score = 0;
        Board cand = {0};
        int coarse = (int)fmax(1, s / 8);
        for (int i = 0; i < 8; i++) {
            double bx = ch->x0 - i * s;
            if (bx < -s * 0.5 || bx + 8 * s > f->w + s * 0.5) continue;
            for (double by = ch->y - 8 * s + 1; by <= ch->y; by += coarse) {
                if (by < -s * 0.5 || by + 8 * s > f->h + s * 0.5) continue;
                int pen = 0;
                double sc = score_board(f, bx, by, s, light, dark, masked, mctx, &pen, soft) - pen;
                if (sc > lo_score) {
                    lo_score = sc;
                    cand.x = bx;
                    cand.y = by;
                    cand.s = s;
                }
            }
        }
        if (lo_score < 44) continue;
        double by0 = cand.y;
        for (double by = by0 - coarse; by <= by0 + coarse; by += 1) {
            int pen = 0;
            double sc = score_board(f, cand.x, by, s, light, dark, masked, mctx, &pen, soft) - pen;
            if (sc > lo_score) lo_score = sc, cand.y = by;
        }
        memcpy(cand.light, light, 3);
        memcpy(cand.dark, dark, 3);
        cand.soft = soft;
        refine(f, &cand);
        refine(f, &cand);
        int pen = 0;
        double sc = score_board(f, cand.x, cand.y, cand.s, light, dark, masked, mctx, &pen, soft) - pen;
        if (sc > best_score) {
            best_score = sc;
            best = cand;
        }
        if (best_score >= 60) break;
    }
    free(chains);
    if (best_score >= 52 && best.s * 8 >= 150) { // 52/64, room for highlights + check glow
        best.found = 1;
        best.score = (int)best_score;
        *out = best;
        return 1;
    }
    out->score = (int)best_score;
    return 0;
}

static int textured(const Frame *f, const Board *b)
{
    static const double pts[4][2] = {{0.12, 0.12}, {0.88, 0.12}, {0.12, 0.88}, {0.88, 0.88}};
    int h[766] = {0}, n = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            for (int k = 0; k < 4; k++) {
                double x = b->x + (c + pts[k][0]) * b->s, y = b->y + (r + pts[k][1]) * b->s;
                if (!sample_ok(f, x, y)) continue;
                uint8_t px[3];
                rgb(f, (int)x, (int)y, px);
                h[cdiff(px, ((r + c) & 1) ? b->dark : b->light)]++;
                n++;
            }
    int acc = 0, v = 0;
    while (v < 765 && (acc += h[v]) <= n / 2) v++;
    return v > 10;
}

int vision_find_board(const Frame *f, Board *out, const Rect *ignore, int nignore, int (*masked)(void *, int, int),
                      void *mctx)
{
    static int misses;
    if (find_board(f, out, ignore, nignore, masked, mctx, 0)) {
        out->soft = textured(f, out);
        misses = 0;
        return 1;
    }
    int sc = out->score;
    if (misses++ % 3 == 0 && find_board(f, out, ignore, nignore, masked, mctx, 1)) {
        misses = 0;
        return 1;
    }
    if (sc > out->score) out->score = sc;
    return 0;
}

int vision_verify(const Frame *f, Board *b, int (*masked)(void *, int, int), void *mctx)
{
    double sc = score_board(f, b->x, b->y, b->s, b->light, b->dark, masked, mctx, NULL, b->soft);
    b->score = (int)sc;
    if (sc >= 46) return 1;
    static const double pts[4][2] = {{0.12, 0.12}, {0.88, 0.12}, {0.12, 0.88}, {0.88, 0.88}};
    double sum[2][3] = {{0}}, cnt[2] = {0};
    uint8_t smp[64][3];
    int have[64] = {0};
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int votes = 0;
            uint8_t cols[4][3];
            for (int k = 0; k < 4; k++) {
                double x = b->x + (c + pts[k][0]) * b->s, y = b->y + (r + pts[k][1]) * b->s;
                if (!sample_ok(f, x, y) || (masked && masked(mctx, (int)x, (int)y))) continue;
                rgb(f, (int)x, (int)y, cols[votes]);
                votes++;
            }
            if (votes < 2) continue;
            int bi = 0, bn = -1;
            for (int a = 0; a < votes; a++) {
                int m = 0;
                for (int k = 0; k < votes; k++) m += cdiff(cols[a], cols[k]) < 30;
                if (m > bn) bn = m, bi = a;
            }
            memcpy(smp[r * 8 + c], cols[bi], 3);
            have[r * 8 + c] = 1;
            int par = (r + c) & 1;
            for (int k = 0; k < 3; k++) sum[par][k] += cols[bi][k];
            cnt[par]++;
        }
    if (cnt[0] < 20 || cnt[1] < 20) return 0;
    uint8_t m0[3], m1[3];
    for (int k = 0; k < 3; k++) {
        m0[k] = (uint8_t)(sum[0][k] / cnt[0]);
        m1[k] = (uint8_t)(sum[1][k] / cnt[1]);
    }
    if (cdiff(m0, m1) < 30) return 0;
    int ok = 0;
    for (int i = 0; i < 64; i++) {
        if (!have[i]) {
            ok++;
            continue;
        }
        int par = ((i >> 3) + (i & 7)) & 1;
        const uint8_t *own = par ? m1 : m0, *oth = par ? m0 : m1;
        if (cdiff(smp[i], own) < cdiff(smp[i], oth)) ok++;
    }
    return ok >= 52;
}

static int soft_colors;

static double piece_fill_lum(const Frame *f, double x0, double y0, double s, const uint8_t *bg, const uint8_t *base,
                             int (*masked)(void *, int, int), void *mctx, int thr)
{
    int step = s > 90 ? 2 : 1;
    int a = (int)(x0 + s * 0.05), b = (int)(y0 + s * 0.05);
    int n = (int)(s * 0.9 / step);
    if (n < 8) return -1;
    if (n > 128) n = 128;
    static uint8_t m[130 * 130];
    static uint8_t lm[130 * 130];
    static int stack[130 * 130];
    int W = n + 2;
    memset(m, 0, (size_t)W * W);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            int x = a + i * step, y = b + j * step;
            uint8_t px[3] = {0, 0, 0};
            int fg = 0;
            if (sample_ok(f, x, y)) {
                rgb(f, x, y, px);
                fg = cdiff(px, bg) > thr && cdiff(px, base) > thr / 2;
                if (masked && masked(mctx, x, y)) fg = 0;
            }
            m[(j + 1) * W + i + 1] = fg ? 1 : 0;
            int mx = px[0] > px[1] ? (px[0] > px[2] ? px[0] : px[2]) : (px[1] > px[2] ? px[1] : px[2]);
            int mn = px[0] < px[1] ? (px[0] < px[2] ? px[0] : px[2]) : (px[1] < px[2] ? px[1] : px[2]);
            int reddish = px[0] - px[1] > 15 && px[0] - px[2] > 15; // check glow makes the king look black
            int glow = reddish && px[1] < px[0] * 0.55;
            lm[(j + 1) * W + i + 1] = (soft_colors ? glow : (mx - mn > 30 || reddish)) ? 128 : (uint8_t)lum(px);
        }
    int sp = 0;
    stack[sp++] = 0;
    m[0] = 2;
    while (sp) {
        int k = stack[--sp];
        int kx = k % W, ky = k / W;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = kx + dx[d], ny = ky + dy[d];
            if (nx < 0 || ny < 0 || nx >= W || ny >= W) continue;
            int q = ny * W + nx;
            if (m[q] == 0) {
                m[q] = 2;
                stack[sp++] = q;
            }
        }
    }
    long cnt = 0, bright = 0, dark = 0;
    for (int j = 1; j <= n; j++)
        for (int i = 1; i <= n; i++) {
            int q = j * W + i;
            if (m[q] == 2) continue;
            cnt++;
            if (lm[q] > 170)
                bright++;
            else if (lm[q] < 80)
                dark++;
        }
    if (cnt < n * n / 40 || bright + dark < cnt / 8) return -1;
    return 255.0 * bright / (bright + dark);
}

static int display_to_sq(int r, int c, int white_bottom)
{
    return white_bottom ? (7 - r) * 8 + c : r * 8 + (7 - c);
}

void vision_read(const Frame *f, const Board *b, int white_bottom, float color_thr, BoardRead *out,
                 int (*masked)(void *, int, int), void *mctx)
{
    memset(out, 0, sizeof(*out));
    double s = b->s;
    float lums[64];
    int occ[64], board_nz = 0;
    if (b->soft) {
        int per[64], np = 0, pr = (int)fmax(1, s * 0.025);
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                uint8_t smp[16][3], med[3];
                int dev[16], nd = 0;
                for (int k = 0; k < 16; k++) {
                    double t = 0.14 + 0.72 * (k % 8) / 7.0, x = b->x + (c + (k < 8 ? t : 0.14)) * s, y = b->y + (r + (k < 8 ? 0.14 : t)) * s;
                    if (!sample_ok(f, x, y)) continue;
                    patch(f, x, y, pr, smp[nd++]);
                }
                if (nd < 8) continue;
                for (int ch = 0; ch < 3; ch++) {
                    int h[256] = {0}, acc = 0, v = 0;
                    for (int k = 0; k < nd; k++) h[smp[k][ch]]++;
                    while (v < 255 && (acc += h[v]) <= nd / 2) v++;
                    med[ch] = (uint8_t)v;
                }
                for (int k = 0; k < nd; k++) dev[k] = cdiff(smp[k], med);
                for (int a2 = 1; a2 < nd; a2++)
                    for (int b2 = a2; b2 > 0 && dev[b2 - 1] > dev[b2]; b2--) {
                        int t = dev[b2];
                        dev[b2] = dev[b2 - 1];
                        dev[b2 - 1] = t;
                    }
                per[np++] = dev[nd / 2];
            }
        for (int a2 = 1; a2 < np; a2++)
            for (int b2 = a2; b2 > 0 && per[b2 - 1] > per[b2]; b2--) {
                int t = per[b2];
                per[b2] = per[b2 - 1];
                per[b2 - 1] = t;
            }
        if (np) board_nz = per[np / 2];
    }
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int sq = display_to_sq(r, c, white_bottom);
            double x0 = b->x + c * s, y0 = b->y + r * s;
            uint8_t ring[64][3];
            int nring = 0;
            for (int k = 0; k < 16 && nring < 64; k++) {
                double t = 0.14 + 0.72 * k / 15.0;
                double pts[4][2] = {{t, 0.14}, {t, 0.86}, {0.14, t}, {0.86, t}};
                for (int q = 0; q < 4; q++) {
                    double x = x0 + pts[q][0] * s, y = y0 + pts[q][1] * s;
                    if (!sample_ok(f, x, y)) continue;
                    if (masked && masked(mctx, (int)x, (int)y)) continue;
                    if (b->soft)
                        patch(f, x, y, (int)fmax(1, s * 0.025), ring[nring]);
                    else
                        rgb(f, (int)x, (int)y, ring[nring]);
                    nring++;
                }
            }
            const uint8_t *base = ((r + c) & 1) ? b->dark : b->light;
            {
                uint8_t cc[4][3];
                int valid[4] = {0, 0, 0, 0}, outside = 0;
                static const double cp[4][2] = {{0.05, 0.05}, {0.95, 0.05}, {0.05, 0.95}, {0.95, 0.95}};
                for (int k = 0; k < 4; k++) {
                    double x = x0 + cp[k][0] * s, y = y0 + cp[k][1] * s;
                    if (!sample_ok(f, x, y)) {
                        outside = 1;
                        continue;
                    }
                    if (masked && masked(mctx, (int)x, (int)y)) continue;
                    if (b->soft && ((c == 0 && k == 0) || (r == 7 && k == 3))) continue;
                    if (b->soft)
                        patch(f, x, y, (int)fmax(1, s * 0.02), cc[k]);
                    else
                        rgb(f, (int)x, (int)y, cc[k]);
                    valid[k] = 1;
                }
                int ct = b->soft ? (int)fmax(70, cdiff(b->light, b->dark) * 0.45) : 40;
#define SAME(a, b) (valid[a] && valid[b] && cdiff(cc[a], cc[b]) < ct)
#define DIFF(a, b) (valid[a] && valid[b] && cdiff(cc[a], cc[b]) >= ct)
                int hid = outside;
                if (b->soft) {
                    int sep = cdiff(b->light, b->dark), off[4];
                    for (int k = 0; k < 4; k++) {
                        off[k] = 0;
                        if (!valid[k]) continue;
                        int dl = cdiff(cc[k], b->light), dd = cdiff(cc[k], b->dark);
                        off[k] = (dl < dd ? dl : dd) > fmax(sep * 0.6, 110);
                    }
                    if ((off[0] && off[1] && !off[2] && !off[3] && SAME(0, 1)) || (off[2] && off[3] && !off[0] && !off[1] && SAME(2, 3)) ||
                        (off[0] && off[2] && !off[1] && !off[3] && SAME(0, 2)) || (off[1] && off[3] && !off[0] && !off[2] && SAME(1, 3)))
                        hid = 1;
                } else {
                    if (SAME(0, 1) && SAME(2, 3) && DIFF(0, 2)) hid = 1;
                    if (SAME(0, 2) && SAME(1, 3) && DIFF(0, 1)) hid = 1;
                }
                if (SAME(0, 1) && SAME(0, 2) && SAME(0, 3) && cdiff(cc[0], base) > (b->soft ? fmax(170, cdiff(b->light, b->dark) * 0.75) : 170)) hid = 1; // promotion picker
#undef SAME
#undef DIFF
                out->hidden[sq] = (uint8_t)hid;
            }
            uint8_t bg[3];
            memcpy(bg, base, 3);
            int bestn = 0;
            for (int i = 0; i < nring; i++) {
                int n = 0;
                for (int j = 0; j < nring; j++)
                    if (cdiff(ring[i], ring[j]) < 30) n++;
                if (n > bestn) bestn = n, memcpy(bg, ring[i], 3);
            }
            int nbase = 0;
            for (int i = 0; i < nring; i++) nbase += cdiff(ring[i], base) < 30;
            if (bestn < nring / 4 || nbase * 8 >= nring) memcpy(bg, base, 3);
            int fthr = 60, sthr = 40;
            if (b->soft && nring) {
                for (int k = 0; k < 3; k++) {
                    int h[256] = {0}, acc = 0, v = 0;
                    for (int i = 0; i < nring; i++) h[ring[i][k]]++;
                    while (v < 255 && (acc += h[v]) <= nring / 2) v++;
                    bg[k] = (uint8_t)v;
                }
                int dev[64], nd = 0;
                for (int i = 0; i < nring; i++) dev[nd++] = cdiff(ring[i], bg);
                for (int a2 = 1; a2 < nd; a2++)
                    for (int b2 = a2; b2 > 0 && dev[b2 - 1] > dev[b2]; b2--) {
                        int t = dev[b2];
                        dev[b2] = dev[b2 - 1];
                        dev[b2 - 1] = t;
                    }
                int nz = dev[nd * 2 / 5] > board_nz ? dev[nd * 2 / 5] : board_nz;
                fthr = (int)fmax(60, nz * 2.2);
                sthr = (int)fmax(40, nz * 1.6);
            }
            out->highlighted[sq] = cdiff(bg, base) > (b->soft ? 70 : 45);
            int fg = 0, valid = 0, covered = 0;
            uint8_t cell_lum[FEAT_N * FEAT_N];
            for (int gy = 0; gy < FEAT_N; gy++)
                for (int gx = 0; gx < FEAT_N; gx++) {
                    double x = x0 + (0.12 + 0.76 * (gx + 0.5) / FEAT_N) * s;
                    double y = y0 + (0.12 + 0.76 * (gy + 0.5) / FEAT_N) * s;
                    float v = 0;
                    if (sample_ok(f, x, y) && !(masked && masked(mctx, (int)x, (int)y))) {
                        uint8_t px[3];
                        if (b->soft)
                            patch(f, x, y, (int)fmax(1, s * 0.012), px);
                        else
                            rgb(f, (int)x, (int)y, px);
                        valid++;
                        if (cdiff(px, bg) > fthr && cdiff(px, b->light) > sthr && cdiff(px, b->dark) > sthr) {
                            fg++;
                            v = 1;
                            cell_lum[gy * FEAT_N + gx] = (uint8_t)lum(px);
                        }
                    } else {
                        v = 0.5f;
                        covered++;
                    }
                    int lft = gx < 6, rgt = gx >= FEAT_N - 6, top = gy < 7, bot = gy >= FEAT_N - 7;
                    if ((c == 0 && lft && top) || (c == 7 && rgt && top) || (r == 7 && bot && (lft || rgt))) v = 0.5f; // coordinate letters live here
                    out->mask[sq][gy * FEAT_N + gx] = v;
                }
            out->occ_frac[sq] = valid ? (float)fg / valid : 0;
            if (covered * 4 > FEAT_N * FEAT_N) out->hidden[sq] = 1;
            int hist[256] = {0}, nh = 0;
            const float *mk = out->mask[sq];
            for (int gy = 1; gy < FEAT_N - 1; gy++)
                for (int gx = 1; gx < FEAT_N - 1; gx++) {
                    int i = gy * FEAT_N + gx;
                    if (mk[i] == 1 && mk[i - 1] == 1 && mk[i + 1] == 1 && mk[i - FEAT_N] == 1 && mk[i + FEAT_N] == 1) {
                        hist[cell_lum[i]]++;
                        nh++;
                    }
                }
            if (nh < 4)
                for (int i = 0; i < FEAT_N * FEAT_N; i++)
                    if (mk[i] == 1) hist[cell_lum[i]]++, nh++;
            long lsum = 0;
            for (int v = 0; v < 256; v++) lsum += (long)v * hist[v];
            out->lum[sq] = nh ? (float)lsum / nh : 0;
            if (out->occ_frac[sq] > 0.075f) {
                double fl = piece_fill_lum(f, x0, y0, s, bg, base, masked, mctx, fthr);
                if (fl >= 0) out->lum[sq] = (float)fl;
            }
            occ[sq] = out->occ_frac[sq] > 0.075f;
            if (occ[sq]) {
                int x0b = FEAT_N, x1b = -1, y0b = FEAT_N, y1b = -1;
                for (int gy = 0; gy < FEAT_N; gy++)
                    for (int gx = 0; gx < FEAT_N; gx++)
                        if (out->mask[sq][gy * FEAT_N + gx] == 1) {
                            if (gx < x0b) x0b = gx;
                            if (gx > x1b) x1b = gx;
                            if (gy < y0b) y0b = gy;
                            if (gy > y1b) y1b = gy;
                        }
                int bw = x1b - x0b + 1, bh = y1b - y0b + 1;
                double cx = (x0b + x1b) / 2.0, cy = (y0b + y1b) / 2.0;
                if (bw <= 12 && bh <= 12 && fabs(cx - 11.5) <= 3 && fabs(cy - 11.5) <= 3) {
                    occ[sq] = 0;
                    out->occ_frac[sq] = 0;
                }
            }
            lums[sq] = out->lum[sq];
        }
    double vals[64];
    int n = 0;
    for (int i = 0; i < 64; i++)
        if (occ[i] && !out->hidden[i]) vals[n++] = lums[i];
    double thr = 90;
    for (int a = 1; a < n; a++)
        for (int b2 = a; b2 > 0 && vals[b2 - 1] > vals[b2]; b2--) {
            double t = vals[b2];
            vals[b2] = vals[b2 - 1];
            vals[b2 - 1] = t;
        }
    if (n >= 2) {
        double best = -1;
        for (int k = 1; k < n; k++) {
            double gap = vals[k] - vals[k - 1];
            double lo_n = k, hi_n = n - k;
            double bal = sqrt((lo_n < hi_n ? lo_n : hi_n) / (lo_n > hi_n ? lo_n : hi_n));
            double mid = (vals[k] + vals[k - 1]) / 2;
            double pull = 1.0 / (1.0 + fabs(mid - 100) / 80);
            double score = gap * bal * pull;
            if (score > best) best = score, thr = mid;
        }
        if (vals[n - 1] - vals[0] < 25) {
            if (!soft_colors && n >= 4 && color_thr <= 0) {
                soft_colors = 1;
                vision_read(f, b, white_bottom, color_thr, out, masked, mctx);
                soft_colors = 0;
                return;
            }
            thr = vals[0] > 110 ? 0 : 255;
        }
    }
    if (thr > 0 && thr < 255) {
        if (thr < 45) thr = 45;
        if (thr > 160) thr = 160;
    }
    if (color_thr > 0) thr = color_thr;
    for (int i = 0; i < 64; i++) out->obs[i] = occ[i] ? (lums[i] > thr ? OBS_WHITE : OBS_BLACK) : OBS_EMPTY;
    for (int i = 0; i < 64; i++)
        if (out->hidden[i]) {
            out->obs[i] = OBS_UNKNOWN;
            out->nhidden++;
        }
}
