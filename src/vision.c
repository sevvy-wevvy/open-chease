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

static double score_board(const Frame *f, double bx, double by, double s, const uint8_t *light, const uint8_t *dark,
                          int (*masked)(void *, int, int), void *mctx, int *outside_penalty)
{
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
                const uint8_t *want = (((r + c) & 1) ? dark : light);
                int good = 0;
                for (int k = 0; k < 4; k++) {
                    double x = bx + (c + pts[k][0]) * s, y = by + (r + pts[k][1]) * s;
                    if (!sample_ok(f, x, y)) continue;
                    uint8_t px[3];
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

static int same_colors(const Chain *a, const Chain *b)
{
    return (cdiff(a->a, b->a) < 40 && cdiff(a->b, b->b) < 40) || (cdiff(a->a, b->b) < 40 && cdiff(a->b, b->a) < 40);
}

int vision_find_board(const Frame *f, Board *out, const Rect *ignore, int nignore, int (*masked)(void *, int, int),
                      void *mctx)
{
    Chain *chains = malloc(sizeof(Chain) * MAX_CHAINS);
    int nc = find_chains(f, chains, MAX_CHAINS, ignore, nignore);
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
                if (!same_colors(&chains[i], &chains[j])) continue;
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
                double sc = score_board(f, bx, by, s, light, dark, masked, mctx, &pen) - pen;
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
            double sc = score_board(f, cand.x, by, s, light, dark, masked, mctx, &pen) - pen;
            if (sc > lo_score) lo_score = sc, cand.y = by;
        }
        memcpy(cand.light, light, 3);
        memcpy(cand.dark, dark, 3);
        refine(f, &cand);
        refine(f, &cand);
        int pen = 0;
        double sc = score_board(f, cand.x, cand.y, cand.s, light, dark, masked, mctx, &pen) - pen;
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

int vision_verify(const Frame *f, Board *b, int (*masked)(void *, int, int), void *mctx)
{
    double sc = score_board(f, b->x, b->y, b->s, b->light, b->dark, masked, mctx, NULL);
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

static double piece_fill_lum(const Frame *f, double x0, double y0, double s, const uint8_t *bg, const uint8_t *base,
                             int (*masked)(void *, int, int), void *mctx)
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
                fg = cdiff(px, bg) > 60 && cdiff(px, base) > 30;
                if (masked && masked(mctx, x, y)) fg = 0;
            }
            m[(j + 1) * W + i + 1] = fg ? 1 : 0;
            int mx = px[0] > px[1] ? (px[0] > px[2] ? px[0] : px[2]) : (px[1] > px[2] ? px[1] : px[2]);
            int mn = px[0] < px[1] ? (px[0] < px[2] ? px[0] : px[2]) : (px[1] < px[2] ? px[1] : px[2]);
            int reddish = px[0] - px[1] > 15 && px[0] - px[2] > 15; // check glow makes the king look black
            lm[(j + 1) * W + i + 1] = (mx - mn > 30 || reddish) ? 128 : (uint8_t)lum(px);
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
    int occ[64];
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
                    rgb(f, (int)x, (int)y, cc[k]);
                    valid[k] = 1;
                }
#define SAME(a, b) (valid[a] && valid[b] && cdiff(cc[a], cc[b]) < 40)
#define DIFF(a, b) (valid[a] && valid[b] && cdiff(cc[a], cc[b]) >= 40)
                int hid = outside;
                if (SAME(0, 1) && SAME(2, 3) && DIFF(0, 2)) hid = 1;
                if (SAME(0, 2) && SAME(1, 3) && DIFF(0, 1)) hid = 1;
                if (SAME(0, 1) && SAME(0, 2) && SAME(0, 3) && cdiff(cc[0], base) > 170) hid = 1; // promotion picker
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
            out->highlighted[sq] = cdiff(bg, base) > 45;
            int fg = 0, valid = 0, covered = 0;
            uint8_t cell_lum[FEAT_N * FEAT_N];
            for (int gy = 0; gy < FEAT_N; gy++)
                for (int gx = 0; gx < FEAT_N; gx++) {
                    double x = x0 + (0.12 + 0.76 * (gx + 0.5) / FEAT_N) * s;
                    double y = y0 + (0.12 + 0.76 * (gy + 0.5) / FEAT_N) * s;
                    float v = 0;
                    if (sample_ok(f, x, y) && !(masked && masked(mctx, (int)x, (int)y))) {
                        uint8_t px[3];
                        rgb(f, (int)x, (int)y, px);
                        valid++;
                        if (cdiff(px, bg) > 60 && cdiff(px, b->light) > 40 && cdiff(px, b->dark) > 40) {
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
                double fl = piece_fill_lum(f, x0, y0, s, bg, base, masked, mctx);
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
        if (vals[n - 1] - vals[0] < 25) thr = vals[0] > 110 ? 0 : 255;
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
