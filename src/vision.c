#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

static inline const uint8_t *pix(const Frame *f, int x, int y) { return f->px + (size_t)y * f->stride + (size_t)x * 4; }
static inline double len2(double x, double y) { return sqrt(x * x + y * y); } // hypot wants a newer glibc

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
    static const double pts[5][2] = {{0.17, 0.17}, {0.83, 0.17}, {0.17, 0.83}, {0.83, 0.83}, {0.5, 0.15}}; // near the corners, pieces mostly sit in the middle
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
            if (!valid && !bad) // all under our overlay, dont punish it
                ok++;
            else if (good >= 2 && good > bad) // a piece or highlight can eat a couple
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

static int flip_tol = 40;

static int is_flip(const uint8_t *a, const uint8_t *b, const Board *bd)
{
    int t = flip_tol;
    return (cdiff(a, bd->light) < t && cdiff(b, bd->dark) < t) || (cdiff(a, bd->dark) < t && cdiff(b, bd->light) < t);
}

// squares lighter on top than on the bottom? then the colors we got from one scan row only match half of each square
static int shaded(const Frame *f, const Board *b)
{
    int n = 0, hits = 0, pr = (int)fmax(1, b->s * 0.01);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            uint8_t e[2][2][3];
            int ok = 1;
            for (int v = 0; v < 2 && ok; v++)
                for (int k = 0; k < 2 && ok; k++) {
                    double x = b->x + (c + (k ? 0.92 : 0.08)) * b->s, y = b->y + (r + (v ? 0.8 : 0.25)) * b->s;
                    if (!sample_ok(f, x - pr, y - pr) || !sample_ok(f, x + pr, y + pr)) ok = 0;
                    else patch(f, x, y, pr, e[v][k]);
                }
            if (!ok || cdiff(e[0][0], e[0][1]) > 20 || cdiff(e[1][0], e[1][1]) > 20) continue;
            n++;
            hits += cdiff(e[0][0], e[1][0]) > 30 && lum(e[0][0]) > lum(e[1][0]);
        }
    return n >= 24 && hits * 4 >= n * 3;
}

// 2 per clean edge pixel, 1 for a blurry one. dir 0 = vertical line at x=pos, 1 = horizontal at y=pos, from a0 to a1
static int edge_count(const Frame *f, const Board *bd, int o, int dir, int pos, int a0, int a1)
{
    int n = 0;
    if (dir == 0) {
        if (pos - 1 - o < 0 || pos + o >= f->w) return 0;
        if (a0 < 0) a0 = 0;
        if (a1 > f->h) a1 = f->h;
        for (int y = a0; y < a1; y++) {
            uint8_t a[3], b[3], a2[3];
            rgb(f, pos - 1 - o, y, a);
            rgb(f, pos + o, y, b);
            if (is_flip(a, b, bd)) n += 2;
            else if (pos - 2 - o >= 0) {
                rgb(f, pos - 2 - o, y, a2);
                if (is_flip(a2, b, bd)) n++; // blurry edge, one more px out
            }
        }
        return n;
    }
    if (pos - 1 - o < 0 || pos + o >= f->h) return 0;
    if (a0 < 0) a0 = 0;
    if (a1 > f->w) a1 = f->w;
    for (int x = a0; x < a1; x++) {
        uint8_t a[3], b[3], a2[3];
        rgb(f, x, pos - 1 - o, a);
        rgb(f, x, pos + o, b);
        if (is_flip(a, b, bd)) n += 2;
        else if (pos - 2 - o >= 0) {
            rgb(f, x, pos - 2 - o, a2);
            if (is_flip(a2, b, bd)) n++;
        }
    }
    return n;
}

static int gpu_checked[2]; // first gpu answer of each kind gets checked against ours

static void edge_counts(const Frame *f, const Board *bd, const int *jobs, int n, int *out)
{
    int o = (int)(bd->s * 0.03); // zoomed boards smear the edge a few px
    if (gpu_edges(f, jobs, n, o, bd->light, bd->dark, out)) {
        if (gpu_checked[0]) return;
        gpu_checked[0] = 1;
        for (int i = 0; i < n; i++)
            if (out[i] != edge_count(f, bd, o, jobs[i * 4], jobs[i * 4 + 1], jobs[i * 4 + 2], jobs[i * 4 + 3])) {
                gpu_broken("edges");
                break;
            }
    }
    for (int i = 0; i < n; i++) out[i] = edge_count(f, bd, o, jobs[i * 4], jobs[i * 4 + 1], jobs[i * 4 + 2], jobs[i * 4 + 3]);
}

static int line_fit(const double *ks, const double *vs, const double *str, double s, double *slope, double *icpt)
{
    double mx = s * 0.6;
    for (int i = 0; i < 7; i++)
        if (str[i] > mx) mx = str[i];
    double n = 0, sk = 0, skk = 0, sv = 0, skv = 0;
    for (int i = 0; i < 7; i++) {
        if (str[i] < mx * 0.25) continue; // lines mostly behind pieces dont get a vote
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
    // smeared edges give a wide flat peak, take the middle
    while (lo > 0 && es[lo - 1] >= es[bi] * 0.95) lo--;
    while (hi < 2 * win && es[hi + 1] >= es[bi] * 0.95) hi++;
    return (lo + hi) / 2.0 - win;
}

static void refine(const Frame *f, Board *b)
{
    double s = b->s;
    int win = (int)(s * 0.45) + 2; // the first guess can be 20+ px off on big boards
    if (win > 500) win = 500;
    int nd = 2 * win + 1, nj = 7 * 2 * nd;
    int *jobs = malloc(sizeof(int) * 4 * nj), *cnt = malloc(sizeof(int) * nj);
    for (int k = 1; k <= 7; k++) {
        double ex = b->x + k * s, ey = b->y + k * s;
        for (int d = -win; d <= win; d++) {
            int *jv = jobs + ((k - 1) * 2 * nd + d + win) * 4, *jh = jv + nd * 4;
            jv[0] = 0, jv[1] = (int)(ex + d), jv[2] = (int)b->y, jv[3] = (int)(b->y + 8 * s);
            jh[0] = 1, jh[1] = (int)(ey + d), jh[2] = (int)b->x, jh[3] = (int)(b->x + 8 * s);
        }
    }
    edge_counts(f, b, jobs, nj, cnt);
    double es[1001];
    double ks[7], xs[7], ys[7], sx_[7], sy_[7];
    for (int k = 1; k <= 7; k++) {
        double ex = b->x + k * s, ey = b->y + k * s, best, besty;
        const int *c = cnt + (k - 1) * 2 * nd;
        for (int i = 0; i < nd; i++) es[i] = c[i] / 2.0;
        double bx = ex + plateau_mid(es, win, &best);
        for (int i = 0; i < nd; i++) es[i] = c[nd + i] / 2.0;
        double by = ey + plateau_mid(es, win, &besty);
        ks[k - 1] = k, xs[k - 1] = bx, ys[k - 1] = by, sx_[k - 1] = best, sy_[k - 1] = besty;
    }
    free(jobs);
    free(cnt);
    double slope_x, icpt_x, slope_y, icpt_y;
    int okx = line_fit(ks, xs, sx_, s, &slope_x, &icpt_x), oky = line_fit(ks, ys, sy_, s, &slope_y, &icpt_y);
    if (!okx && !oky) return;
    double ns = okx && oky ? (slope_x + slope_y) / 2 : okx ? slope_x : slope_y;
    if (fabs(ns - s) >= s * 0.08) return; // 8% off is a bad fit not a better one
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
                if (cur->start - (prev->start + prev->len) > 3 + L * 0.04) break; // antialiased seam between squares
                double tol = L * 0.08 + 3;
                if (fabs(cur->len - L) > tol) break;
                if (cdiff(cur->c, prev->c) < 45) break;
                if (n >= 2 && cdiff(cur->c, runs[idx[last - 1]].c) > 40) break; // a b a b, every second one has to match
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

// one blurred row and how hard it changes at every x
static void soft_row(const Frame *f, int y, int *pre, int *sm, int *ed)
{
    int W = f->w;
    for (int k = 0; k < 3; k++) pre[k] = 0;
    for (int x = 0; x < W; x++) {
        int acc[3] = {0, 0, 0};
        for (int dy = -2; dy <= 2; dy++) { // 5 rows blurred so wood grain doesnt split a square
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
}

static int find_chains_soft(const Frame *f, Chain *chains, int maxc, const Rect *ignore, int nignore)
{
    int nchains = 0, W = f->w, step = f->h > 1500 ? 6 : 4;
    int *pre = malloc(sizeof(int) * 3 * (W + 1)), *sm = malloc(sizeof(int) * 3 * W), *ed = malloc(sizeof(int) * W);
    int *cut = malloc(sizeof(int) * (W + 2));
    Run *runs = malloc(sizeof(Run) * (W + 1));
    int nrows = f->h > 4 ? (f->h - 5) / step + 1 : 0;
    uint8_t *gsm = nrows ? malloc((size_t)nrows * W * 4) : NULL;
    uint16_t *ged = nrows ? malloc((size_t)nrows * W * 2) : NULL;
    int gpu = gsm && ged && gpu_blur_rows(f, 2, step, nrows, gsm, ged);
    if (gpu && !gpu_checked[1]) {
        gpu_checked[1] = 1;
        for (int i = 0; i < nrows && gpu; i++) {
            soft_row(f, 2 + i * step, pre, sm, ed);
            for (int x = 0; x < W && gpu; x++)
                if (ed[x] != ged[(size_t)i * W + x] || sm[x * 3] != gsm[((size_t)i * W + x) * 4] || sm[x * 3 + 1] != gsm[((size_t)i * W + x) * 4 + 1] ||
                    sm[x * 3 + 2] != gsm[((size_t)i * W + x) * 4 + 2])
                    gpu = 0;
        }
        if (!gpu) gpu_broken("blur");
    }
    for (int y = 2, row = 0; y < f->h - 2 && nchains < maxc; y += step, row++) {
        if (gpu) {
            const uint8_t *g = gsm + (size_t)row * W * 4;
            const uint16_t *e = ged + (size_t)row * W;
            for (int x = 0; x < W; x++) sm[x * 3] = g[x * 4], sm[x * 3 + 1] = g[x * 4 + 1], sm[x * 3 + 2] = g[x * 4 + 2], ed[x] = e[x];
        } else
            soft_row(f, y, pre, sm, ed);
        int nc = 0;
        cut[nc++] = 0;
        for (int x = 3; x < W - 3; x++) {
            if (ed[x] < 60) continue;
            int peak = 1;
            for (int d = -3; d <= 3 && peak; d++)
                // only the local max, grain makes lots of small bumps
                if (d && (ed[x + d] > ed[x] || (ed[x + d] == ed[x] && d < 0))) peak = 0;
            if (peak && x - cut[nc - 1] >= 4) cut[nc++] = x;
        }
        cut[nc++] = W;
        int nr = 0;
        for (int i = 0; i + 1 < nc; i++) {
            int a = cut[i], b = cut[i + 1], len = b - a;
            runs[nr].start = a;
            runs[nr].len = len;
            int m0 = a + len / 5, m1 = b - len / 5, acc[3] = {0, 0, 0}, n = 0; // middle of the run, the ends are blurry
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
    free(gsm);
    free(ged);
    free(cut);
    free(runs);
    return nchains;
}

static int same_colors(const Chain *a, const Chain *b, int tol)
{
    return (cdiff(a->a, b->a) < tol && cdiff(a->b, b->b) < tol) || (cdiff(a->a, b->b) < tol && cdiff(a->b, b->a) < tol);
}

int vision_strict = 2, vision_lite;

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
                if (ph > 4 && ph < chains[i].pitch - 4) continue; // same grid, just offset by whole squares
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
        int coarse = (int)fmax(1, s / 8); // coarse first, then 1px around the best
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
        if (shaded(f, &cand)) flip_tol = (int)fmax(40, cdiff(light, dark) * 0.45);
        refine(f, &cand);
        refine(f, &cand);
        flip_tol = 40;
        int pen = 0;
        double sc = score_board(f, cand.x, cand.y, cand.s, light, dark, masked, mctx, &pen, soft) - pen;
        if (sc > best_score) {
            best_score = sc;
            best = cand;
        }
        if (best_score >= 60) break; // good enough, dont bother with the other chains
    }
    free(chains);
    if (best_score >= 44 + vision_strict * 4 && best.s * 8 >= 150) { // 52/64 by default, room for highlights + check glow
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
    return v > 10; // median off by more than 10 = wood/marble, read it soft
}

void hm_map(const double *h, double x, double y, double *ox, double *oy)
{
    double w = h[6] * x + h[7] * y + h[8];
    if (fabs(w) < 1e-12) w = 1e-12;
    *ox = (h[0] * x + h[1] * y + h[2]) / w;
    *oy = (h[3] * x + h[4] * y + h[5]) / w;
}

void hm_mul(const double *a, const double *b, double *out)
{
    double t[9];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) t[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
    memcpy(out, t, sizeof(t));
}

int hm_inv(const double *m, double *out)
{
    double t[9] = {m[4] * m[8] - m[5] * m[7], m[2] * m[7] - m[1] * m[8], m[1] * m[5] - m[2] * m[4],
                   m[5] * m[6] - m[3] * m[8], m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
                   m[3] * m[7] - m[4] * m[6], m[1] * m[6] - m[0] * m[7], m[0] * m[4] - m[1] * m[3]};
    double det = m[0] * t[0] + m[1] * t[3] + m[2] * t[6];
    if (fabs(det) < 1e-15) return 0;
    for (int i = 0; i < 9; i++) out[i] = t[i] / det;
    return 1;
}

static int solve(double *a, double *b, int n) // gaussian elimination, answer ends up in b
{
    for (int c = 0; c < n; c++) {
        int p = c;
        for (int r = c + 1; r < n; r++)
            if (fabs(a[r * n + c]) > fabs(a[p * n + c])) p = r;
        if (fabs(a[p * n + c]) < 1e-12) return 0;
        if (p != c) {
            for (int k = 0; k < n; k++) {
                double t = a[c * n + k];
                a[c * n + k] = a[p * n + k];
                a[p * n + k] = t;
            }
            double t = b[c];
            b[c] = b[p];
            b[p] = t;
        }
        for (int r = 0; r < n; r++) {
            if (r == c) continue;
            double q = a[r * n + c] / a[c * n + c];
            if (q == 0) continue;
            for (int k = c; k < n; k++) a[r * n + k] -= q * a[c * n + k];
            b[r] -= q * b[c];
        }
    }
    for (int c = 0; c < n; c++) b[c] /= a[c * n + c];
    return 1;
}

// least squares homography from grid spots (gx gy) to image points (px py)
static int fit_h(const double *g, const double *p, int n, int full, double *h)
{
    double mx = 0, my = 0, sc = 0;
    for (int i = 0; i < n; i++) mx += p[i * 2], my += p[i * 2 + 1];
    mx /= n, my /= n;
    for (int i = 0; i < n; i++) sc += len2(p[i * 2] - mx, p[i * 2 + 1] - my);
    sc = sc > 0 ? sc / n : 1;
    int k = full ? 8 : 6;
    double a[64] = {0}, b[8] = {0};
    for (int i = 0; i < n; i++) {
        double u = g[i * 2], v = g[i * 2 + 1], x = (p[i * 2] - mx) / sc, y = (p[i * 2 + 1] - my) / sc;
        double rx[8] = {u, v, 1, 0, 0, 0, -u * x, -v * x}, ry[8] = {0, 0, 0, u, v, 1, -u * y, -v * y};
        for (int r = 0; r < k; r++) {
            for (int c = 0; c < k; c++) a[r * k + c] += rx[r] * rx[c] + ry[r] * ry[c];
            b[r] += rx[r] * x + ry[r] * y;
        }
    }
    if (!solve(a, b, k)) return 0;
    double hn[9] = {b[0], b[1], b[2], b[3], b[4], b[5], full ? b[6] : 0, full ? b[7] : 0, 1};
    double t[9] = {sc, 0, mx, 0, sc, my, 0, 0, 1};
    hm_mul(t, hn, h);
    return 1;
}

typedef struct {
    float x, y, r;
} Xc;

static int xc_cmp(const void *a, const void *b)
{
    float d = ((const Xc *)b)->r - ((const Xc *)a)->r;
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

// saddle points where 4 squares meet, ring of 16 samples like the chess corner detector
static int find_xcorners(const uint8_t *g, int w, int h, Xc *out, int maxn)
{
    enum { R = 4 };
    int off[16];
    for (int k = 0; k < 16; k++) off[k] = (int)lround(R * sin(k * M_PI / 8)) * w + (int)lround(R * cos(k * M_PI / 8));
    float *resp = calloc((size_t)w * h, sizeof(float));
    for (int y = R; y < h - R; y++)
        for (int x = R; x < w - R; x++) {
            const uint8_t *p = g + (size_t)y * w + x;
            int I[16], sr = 0, dr = 0, sum = 0;
            for (int k = 0; k < 16; k++) I[k] = p[off[k]], sum += I[k];
            for (int n = 0; n < 4; n++) sr += abs(I[n] + I[n + 8] - I[n + 4] - I[n + 12]);
            if (sr < 90) continue;
            for (int n = 0; n < 8; n++) dr += abs(I[n] - I[n + 8]);
            int loc = p[0] + p[-1] + p[1] + p[-w] + p[w];
            int r = sr - dr - abs(sum * 5 - loc * 16) / 5;
            if (r > 90) resp[(size_t)y * w + x] = (float)r;
        }
    int n = 0, cap = maxn * 4;
    Xc *all = malloc(sizeof(Xc) * cap);
    for (int y = R + 2; y < h - R - 2 && n < cap; y++)
        for (int x = R + 2; x < w - R - 2 && n < cap; x++) {
            float v = resp[(size_t)y * w + x];
            if (v <= 0) continue;
            int peak = 1;
            for (int dy = -2; dy <= 2 && peak; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    float q = resp[(size_t)(y + dy) * w + x + dx];
                    if ((dy || dx) && (q > v || (q == v && (dy < 0 || (dy == 0 && dx < 0))))) {
                        peak = 0;
                        break;
                    }
                }
            if (!peak) continue;
            const float *c = resp + (size_t)y * w + x;
            double ax = c[-1] - 2 * v + c[1], ay = c[-w] - 2 * v + c[w]; // parabola through the peak
            double ox = ax < 0 ? 0.5 * (c[-1] - c[1]) / ax : 0, oy = ay < 0 ? 0.5 * (c[-w] - c[w]) / ay : 0;
            if (fabs(ox) > 1) ox = 0;
            if (fabs(oy) > 1) oy = 0;
            all[n++] = (Xc){(float)(x + ox), (float)(y + oy), v};
        }
    free(resp);
    qsort(all, n, sizeof(Xc), xc_cmp);
    if (n > maxn) n = maxn;
    memcpy(out, all, sizeof(Xc) * n);
    free(all);
    return n;
}

typedef struct {
    const Xc *pt;
    int n, cell, cw, ch;
    int *head, *next, *stamp, cur;
} XcIndex;

static int xc_near(const XcIndex *ix, double x, double y, double rad)
{
    int c0 = (int)((x - rad) / ix->cell), c1 = (int)((x + rad) / ix->cell), r0 = (int)((y - rad) / ix->cell), r1 = (int)((y + rad) / ix->cell);
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= ix->cw) c1 = ix->cw - 1;
    if (r1 >= ix->ch) r1 = ix->ch - 1;
    int best = -1;
    double bd = rad * rad;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            for (int i = ix->head[r * ix->cw + c]; i >= 0; i = ix->next[i]) {
                if (ix->stamp[i] == ix->cur) continue;
                double dx = ix->pt[i].x - x, dy = ix->pt[i].y - y, d = dx * dx + dy * dy;
                if (d < bd) bd = d, best = i;
            }
    return best;
}

#define GS 17 // grid spots -8..8 round the seed

typedef struct {
    int at[GS][GS];
    int n, i0, i1, j0, j1;
    double h[9];
} XGrid;

static int grid_fit(const XcIndex *ix, XGrid *gr)
{
    double g[2 * GS * GS], p[2 * GS * GS];
    int n = 0, ci[GS] = {0}, cj[GS] = {0}, ni = 0, nj = 0;
    for (int i = 0; i < GS; i++)
        for (int j = 0; j < GS; j++) {
            int k = gr->at[i][j];
            if (k < 0) continue;
            g[n * 2] = i - 8, g[n * 2 + 1] = j - 8, p[n * 2] = ix->pt[k].x, p[n * 2 + 1] = ix->pt[k].y;
            n++;
            ci[i] = cj[j] = 1;
        }
    for (int i = 0; i < GS; i++) ni += ci[i], nj += cj[i];
    return n >= 3 && fit_h(g, p, n, n >= 8 && ni >= 3 && nj >= 3, gr->h);
}

static void grid_span(XGrid *gr)
{
    gr->n = 0, gr->i0 = gr->j0 = GS, gr->i1 = gr->j1 = -1;
    for (int i = 0; i < GS; i++)
        for (int j = 0; j < GS; j++)
            if (gr->at[i][j] >= 0) {
                gr->n++;
                if (i < gr->i0) gr->i0 = i;
                if (i > gr->i1) gr->i1 = i;
                if (j < gr->j0) gr->j0 = j;
                if (j > gr->j1) gr->j1 = j;
            }
}

static double grid_step(const double *h, int i, int j)
{
    double x, y, a, b, c, d;
    hm_map(h, i - 8, j - 8, &x, &y);
    hm_map(h, i - 7, j - 8, &a, &b);
    hm_map(h, i - 8, j - 7, &c, &d);
    return fmin(len2(a - x, b - y), len2(c - x, d - y));
}

static int grow(XcIndex *ix, int s, int u, int v, XGrid *gr)
{
    for (int i = 0; i < GS; i++)
        for (int j = 0; j < GS; j++) gr->at[i][j] = -1;
    ix->cur++;
    const Xc *P = ix->pt;
    gr->at[8][8] = s, gr->at[9][8] = u, gr->at[8][9] = v;
    ix->stamp[s] = ix->stamp[u] = ix->stamp[v] = ix->cur;
    for (int it = 0; it < 24; it++) {
        if (!grid_fit(ix, gr)) return 0;
        grid_span(gr);
        int added = 0;
        for (int i = 0; i < GS; i++)
            for (int j = 0; j < GS; j++) {
                if (gr->at[i][j] >= 0) continue;
                if (!((i > 0 && gr->at[i - 1][j] >= 0) || (i < GS - 1 && gr->at[i + 1][j] >= 0) || (j > 0 && gr->at[i][j - 1] >= 0) ||
                      (j < GS - 1 && gr->at[i][j + 1] >= 0)))
                    continue;
                if ((i < gr->i0 ? gr->i1 - i : i - gr->i0) > 8 || (j < gr->j0 ? gr->j1 - j : j - gr->j0) > 8) continue; // 9 lines tops
                double x, y, st = grid_step(gr->h, i, j);
                if (st < 4) continue;
                hm_map(gr->h, i - 8, j - 8, &x, &y);
                int k = xc_near(ix, x, y, fmax(1.5, st * 0.22));
                if (k < 0) continue;
                gr->at[i][j] = k;
                ix->stamp[k] = ix->cur;
                added++;
            }
        if (!added) break;
    }
    // piece corners sneak in near the right spot, throw out the worst one till the fit is tight
    for (int it = 0; it < 20; it++) {
        if (!grid_fit(ix, gr)) return 0;
        int wi = -1, wj = -1;
        double worst = 0.07;
        for (int i = 0; i < GS; i++)
            for (int j = 0; j < GS; j++) {
                int k = gr->at[i][j];
                if (k < 0) continue;
                double x, y;
                hm_map(gr->h, i - 8, j - 8, &x, &y);
                double e = len2(x - P[k].x, y - P[k].y) / grid_step(gr->h, i, j);
                if (e > worst) worst = e, wi = i, wj = j;
            }
        if (wi < 0) break;
        gr->at[wi][wj] = -1;
    }
    grid_span(gr);
    if (gr->n < 8) return 0;
    return gr->n >= 12 && gr->i1 - gr->i0 >= 3 && gr->j1 - gr->j0 >= 3;
}

static float gain_at(const float *g, double u, double v)
{
    double a = (u - 4) / 4, b = (v - 4) / 4;
    return (float)(g[0] + g[1] * a + g[2] * b + g[3] * a * b);
}

static void med3(uint8_t (*c)[3], int n, uint8_t *out)
{
    for (int k = 0; k < 3; k++) {
        int h[256] = {0}, acc = 0, v = 0;
        for (int i = 0; i < n; i++) h[c[i][k]]++;
        while (v < 255 && (acc += h[v]) <= n / 2) v++;
        out[k] = (uint8_t)v;
    }
}

static void unlit(const uint8_t *px, float g, uint8_t *out)
{
    for (int k = 0; k < 3; k++) {
        double v = px[k] / g;
        out[k] = (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v);
    }
}

static double local_sq(const double *hm, double u, double v)
{
    double x, y, a, b, c, d;
    hm_map(hm, u, v, &x, &y);
    hm_map(hm, u + 1, v, &a, &b);
    hm_map(hm, u, v + 1, &c, &d);
    return fmin(len2(a - x, b - y), len2(c - x, d - y));
}

// checker score through the homography, works out the colors and the lighting on the way
static double score_persp(const Frame *f, const double *hm, uint8_t *light, uint8_t *dark, float *gain, int (*masked)(void *, int, int),
                          void *mctx)
{
    static const double pts[5][2] = {{0.17, 0.17}, {0.83, 0.17}, {0.17, 0.83}, {0.83, 0.83}, {0.5, 0.15}};
    uint8_t smp[64][5][3], sq[64][3];
    int sv[64][5], have[64], outside = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int i = r * 8 + c, nv = 0;
            uint8_t cols[5][3];
            int pr = (int)fmax(1, local_sq(hm, c, r) * 0.03);
            for (int k = 0; k < 5; k++) {
                double x, y;
                hm_map(hm, c + pts[k][0], r + pts[k][1], &x, &y);
                sv[i][k] = 0;
                if (!sample_ok(f, x, y)) {
                    outside++;
                    continue;
                }
                if (masked && masked(mctx, (int)x, (int)y)) continue;
                patch(f, x, y, pr, smp[i][k]);
                sv[i][k] = 1;
                memcpy(cols[nv++], smp[i][k], 3);
            }
            have[i] = nv >= 3;
            if (have[i]) med3(cols, nv, sq[i]);
        }
    if (outside > 16) return 0;
    float g[4] = {1, 0, 0, 0};
    uint8_t lt[3] = {0}, dk[3] = {0};
    for (int pass = 0; pass < 3; pass++) {
        uint8_t grp[2][64][3];
        int ng[2] = {0, 0};
        for (int i = 0; i < 64; i++)
            if (have[i]) {
                int par = ((i >> 3) + (i & 7)) & 1;
                unlit(sq[i], gain_at(g, (i & 7) + 0.5, (i >> 3) + 0.5), grp[par][ng[par]++]);
            }
        if (ng[0] < 16 || ng[1] < 16) return 0;
        med3(grp[0], ng[0], lt);
        med3(grp[1], ng[1], dk);
        if (pass == 2) break;
        // lighting as a smooth ramp over the board, highlights and pieces fall out as outliers
        double ll[2] = {lum(lt) + 1, lum(dk) + 1};
        for (int rej = 0; rej < 2; rej++) {
            double a[16] = {0}, b[4] = {0};
            int n = 0;
            for (int i = 0; i < 64; i++) {
                if (!have[i]) continue;
                double u = (i & 7) + 0.5, v = (i >> 3) + 0.5, ratio = (lum(sq[i]) + 1) / ll[((i >> 3) + (i & 7)) & 1];
                if (rej && fabs(ratio - gain_at(g, u, v)) > 0.12) continue;
                double x[4] = {1, (u - 4) / 4, (v - 4) / 4, (u - 4) * (v - 4) / 16};
                for (int p = 0; p < 4; p++) {
                    for (int q = 0; q < 4; q++) a[p * 4 + q] += x[p] * x[q];
                    b[p] += x[p] * ratio;
                }
                n++;
            }
            if (n < 20 || !solve(a, b, 4)) return 0;
            for (int p = 0; p < 4; p++) g[p] = (float)b[p];
        }
        for (int k = 0; k < 4; k++) {
            float c = gain_at(g, k & 1 ? 8 : 0, k & 2 ? 8 : 0);
            if (c < 0.35f || c > 2.5f) return 0;
        }
    }
    if (lum(lt) < lum(dk)) return 0; // top left is always a light square
    int sep = cdiff(lt, dk);
    if (sep < 45) return 0;
    int ok = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int i = r * 8 + c, good = 0, bad = 0, valid = 0;
            const uint8_t *want = ((r + c) & 1) ? dk : lt, *other = ((r + c) & 1) ? lt : dk;
            for (int k = 0; k < 5; k++) {
                if (!sv[i][k]) continue;
                valid++;
                uint8_t px[3];
                unlit(smp[i][k], gain_at(g, c + pts[k][0], r + pts[k][1]), px);
                int dw = cdiff(px, want), dot = cdiff(px, other);
                if (dw < dot && dw < sep * 0.6) good++;
                else if (dot < dw && dot < sep * 0.6) bad++;
            }
            if (!valid) ok += 1;
            else if (good >= 2 && good > bad) ok++;
        }
    int pen = 0;
    for (int side = 0; side < 4; side++) {
        int hits = 0;
        for (int i = 0; i < 8; i++) {
            int r, c;
            if (side == 0) r = -1, c = i;
            else if (side == 1) r = 8, c = i;
            else if (side == 2) r = i, c = -1;
            else r = i, c = 8;
            const uint8_t *want = ((r + c) & 1) ? dk : lt, *other = ((r + c) & 1) ? lt : dk;
            int good = 0, pr = (int)fmax(1, local_sq(hm, c, r) * 0.03);
            for (int k = 0; k < 4; k++) {
                double x, y;
                hm_map(hm, c + pts[k][0], r + pts[k][1], &x, &y);
                if (!sample_ok(f, x, y)) continue;
                uint8_t px[3], raw[3];
                patch(f, x, y, pr, raw);
                float gg = gain_at(g, c + pts[k][0], r + pts[k][1]);
                unlit(raw, gg < 0.35f ? 0.35f : gg, px);
                if (cdiff(px, want) < cdiff(px, other) * 0.6 && cdiff(px, want) < sep * 0.4) good++;
            }
            if (good >= 3) hits++;
        }
        if (hits >= 5) pen += hits;
    }
    memcpy(light, lt, 3);
    memcpy(dark, dk, 3);
    memcpy(gain, g, sizeof(g));
    return ok - pen;
}

static int convex_ok(const double *q) // 4 corners in order, all turning the same way
{
    int sgn = 0;
    for (int k = 0; k < 4; k++) {
        const double *a = q + k * 2, *b = q + ((k + 1) & 3) * 2, *c = q + ((k + 2) & 3) * 2;
        double z = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
        int s = z > 0 ? 1 : z < 0 ? -1 : 0;
        if (!s || (sgn && s != sgn)) return 0;
        sgn = s;
    }
    return 1;
}

static double try_grid(const Frame *f, const XGrid *gr, int d, Board *out, int (*masked)(void *, int, int), void *mctx)
{
    // full res: downsampled px x covers d*x .. d*x+d
    double D[9] = {d, 0, (d - 1) * 0.5, 0, d, (d - 1) * 0.5, 0, 0, 1}, hg[9];
    hm_mul(D, gr->h, hg);
    double ci = (gr->i0 + gr->i1) / 2.0 - 8, cj = (gr->j0 + gr->j1) / 2.0 - 8, x0, y0, xi, yi, xj, yj;
    hm_map(hg, ci, cj, &x0, &y0);
    hm_map(hg, ci + 1, cj, &xi, &yi);
    hm_map(hg, ci, cj + 1, &xj, &yj);
    double di[2] = {xi - x0, yi - y0}, dj[2] = {xj - x0, yj - y0};
    int swap = fabs(dj[0]) > fabs(di[0]); // which grid axis runs across the screen
    int su = (swap ? dj[0] : di[0]) > 0 ? 1 : -1, sv = (swap ? di[1] : dj[1]) > 0 ? 1 : -1;
    double best = 0;
    for (int ei = gr->i1 - 8; ei <= gr->i0; ei++)
        for (int ej = gr->j1 - 8; ej <= gr->j0; ej++) {
            // board u v to grid spots, grid i is u or v and may run backwards
            double a = ei - 8, b = ej - 8, S[9] = {0, 0, 0, 0, 0, 0, 0, 0, 1};
            int iu = !swap, ju = swap; // grid i fed by u?
            double si = iu ? su : sv, sj = ju ? su : sv;
            S[iu ? 0 : 1] = si, S[2] = si > 0 ? a : a + 8;
            S[3 + (ju ? 0 : 1)] = sj, S[5] = sj > 0 ? b : b + 8;
            double hm[9];
            hm_mul(hg, S, hm);
            double q[8];
            hm_map(hm, 0, 0, &q[0], &q[1]);
            hm_map(hm, 8, 0, &q[2], &q[3]);
            hm_map(hm, 8, 8, &q[4], &q[5]);
            hm_map(hm, 0, 8, &q[6], &q[7]);
            if (!convex_ok(q)) continue;
            double bx0 = q[0], bx1 = q[0], by0 = q[1], by1 = q[1];
            for (int k = 1; k < 4; k++) {
                bx0 = fmin(bx0, q[k * 2]), bx1 = fmax(bx1, q[k * 2]);
                by0 = fmin(by0, q[k * 2 + 1]), by1 = fmax(by1, q[k * 2 + 1]);
            }
            double ms = 1e9;
            for (int k = 0; k < 4; k++) ms = fmin(ms, local_sq(hm, k & 1 ? 7 : 0, k & 2 ? 7 : 0));
            if (bx1 - bx0 < 150 || by1 - by0 < 60 || ms < 8) continue;
            if (bx0 < -ms || by0 < -ms || bx1 > f->w + ms || by1 > f->h + ms) continue;
            uint8_t lt[3] = {0}, dk[3] = {0};
            float g[4] = {1, 0, 0, 0};
            double sc = score_persp(f, hm, lt, dk, g, masked, mctx);
            if (sc <= best) continue;
            best = sc;
            memset(out, 0, sizeof(*out));
            memcpy(out->hm, hm, sizeof(hm));
            memcpy(out->light, lt, 3);
            memcpy(out->dark, dk, 3);
            memcpy(out->gain, g, sizeof(g));
            out->persp = 1;
            out->s = fmax(bx1 - bx0, by1 - by0) / 8;
            out->x = (bx0 + bx1) / 2 - out->s * 4;
            out->y = (by0 + by1) / 2 - out->s * 4;
            out->score = (int)sc;
        }
    return best;
}

static int find_persp(const Frame *f, Board *out, const Rect *ignore, int nignore, int (*masked)(void *, int, int), void *mctx)
{
    int d = (f->w + 1999) / 2000, gw = f->w / d, gh = f->h / d;
    if (gw < 64 || gh < 64) return 0;
    uint8_t *g = malloc((size_t)gw * gh);
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            int acc = 0;
            for (int dy = 0; dy < d; dy++)
                for (int dx = 0; dx < d; dx++) {
                    uint8_t px[3];
                    rgb(f, x * d + dx, y * d + dy, px);
                    acc += lum(px);
                }
            g[(size_t)y * gw + x] = (uint8_t)(acc / (d * d));
        }
    enum { MAXP = 6000 };
    Xc *pt = malloc(sizeof(Xc) * MAXP);
    int n = find_xcorners(g, gw, gh, pt, MAXP);
    free(g);
    int m = 0;
    for (int i = 0; i < n; i++)
        if (!in_ignore(ignore, nignore, pt[i].x * d, pt[i].y * d)) pt[m++] = pt[i];
    n = m;
    XcIndex ix = {pt, n, 16, gw / 16 + 1, gh / 16 + 1, NULL, NULL, NULL, 0};
    ix.head = malloc(sizeof(int) * ix.cw * ix.ch);
    ix.next = malloc(sizeof(int) * (n + 1));
    ix.stamp = calloc(n + 1, sizeof(int));
    int *tried = calloc(n + 1, sizeof(int));
    for (int i = 0; i < ix.cw * ix.ch; i++) ix.head[i] = -1;
    for (int i = n - 1; i >= 0; i--) {
        int c = (int)(pt[i].y / 16) * ix.cw + (int)(pt[i].x / 16);
        ix.next[i] = ix.head[c], ix.head[c] = i;
    }
    Board best = {0};
    double best_sc = 0;
    XGrid gr;
    int seeds = 0;
    for (int s = 0; s < n && seeds < 80 && best_sc < 60; s++) {
        if (tried[s]) continue;
        seeds++;
        int nb[8], nn = 0;
        double nd[8], maxd = gh / 6.0;
        ix.cur++;
        ix.stamp[s] = ix.cur;
        for (int k = 0; k < 8; k++) { // 8 nearest
            int q = xc_near(&ix, pt[s].x, pt[s].y, maxd);
            if (q < 0) break;
            ix.stamp[q] = ix.cur;
            double dd = len2(pt[q].x - pt[s].x, pt[q].y - pt[s].y);
            if (dd < 5) {
                k--;
                continue;
            }
            nb[nn] = q, nd[nn++] = dd;
        }
        for (int a = 1; a < nn; a++)
            for (int b = a; b > 0 && nd[b - 1] > nd[b]; b--) {
                double t = nd[b];
                nd[b] = nd[b - 1], nd[b - 1] = t;
                int ti = nb[b];
                nb[b] = nb[b - 1], nb[b - 1] = ti;
            }
        for (int a = 0; a < nn && a < 4 && best_sc < 60; a++) {
            double ux = pt[nb[a]].x - pt[s].x, uy = pt[nb[a]].y - pt[s].y;
            int v = -1;
            for (int b = 0; b < nn && v < 0; b++) {
                if (b == a) continue;
                double vx = pt[nb[b]].x - pt[s].x, vy = pt[nb[b]].y - pt[s].y;
                double cs = (ux * vx + uy * vy) / (nd[a] * nd[b]), ratio = nd[b] / nd[a];
                if (fabs(cs) < 0.6 && ratio > 0.5 && ratio < 2) v = nb[b];
            }
            if (v < 0 || !grow(&ix, s, nb[a], v, &gr)) continue;
            for (int i = 0; i < GS; i++)
                for (int j = 0; j < GS; j++)
                    if (gr.at[i][j] >= 0) tried[gr.at[i][j]] = 1;
            Board cand;
            double sc = try_grid(f, &gr, d, &cand, masked, mctx);
            if (sc > best_sc) best_sc = sc, best = cand;
        }
    }
    free(pt);
    free(ix.head);
    free(ix.next);
    free(ix.stamp);
    free(tried);
    if (best_sc >= 44 + vision_strict * 4) {
        best.found = 1;
        *out = best;
        return 1;
    }
    if (best_sc > out->score) out->score = (int)best_sc;
    return 0;
}

// the tilted board drawn flat, so the normal reader can have it
static uint8_t *flat_px;
static size_t flat_cap;

typedef struct {
    int (*masked)(void *, int, int);
    void *mctx;
    const Board *b;
    int ws;
} FlatMask;

static int flat_masked(void *ctx, int x, int y)
{
    FlatMask *m = ctx;
    double fx, fy;
    hm_map(m->b->hm, (x + 0.5) / m->ws, (y + 0.5) / m->ws, &fx, &fy);
    return m->masked(m->mctx, (int)fx, (int)fy);
}

static int flat_size(const Board *b)
{
    double q[8], a = 0;
    hm_map(b->hm, 0, 0, &q[0], &q[1]);
    hm_map(b->hm, 8, 0, &q[2], &q[3]);
    hm_map(b->hm, 8, 8, &q[4], &q[5]);
    hm_map(b->hm, 0, 8, &q[6], &q[7]);
    for (int k = 0; k < 4; k++) a += q[k * 2] * q[((k + 1) & 3) * 2 + 1] - q[((k + 1) & 3) * 2] * q[k * 2 + 1];
    int ws = (int)lround(sqrt(fabs(a) / 2) / 8);
    return ws < 32 ? 32 : ws > 80 ? 80 : ws;
}

static void flatten(const Frame *f, const Board *b, Frame *wf, Board *fb)
{
    int ws = flat_size(b), n = ws * 8;
    if (flat_cap < (size_t)n * n * 4) {
        free(flat_px);
        flat_cap = (size_t)n * n * 4;
        flat_px = malloc(flat_cap);
    }
    for (int Y = 0; Y < n; Y++)
        for (int X = 0; X < n; X++) {
            double u = (X + 0.5) / ws, v = (Y + 0.5) / ws, fx, fy;
            hm_map(b->hm, u, v, &fx, &fy);
            fx -= 0.5, fy -= 0.5;
            if (fx < 0) fx = 0;
            if (fy < 0) fy = 0;
            if (fx > f->w - 1.001) fx = f->w - 1.001;
            if (fy > f->h - 1.001) fy = f->h - 1.001;
            int ix = (int)fx, iy = (int)fy;
            double ax = fx - ix, ay = fy - iy;
            uint8_t p00[3], p10[3], p01[3], p11[3];
            rgb(f, ix, iy, p00);
            rgb(f, ix + 1 < f->w ? ix + 1 : ix, iy, p10);
            rgb(f, ix, iy + 1 < f->h ? iy + 1 : iy, p01);
            rgb(f, ix + 1 < f->w ? ix + 1 : ix, iy + 1 < f->h ? iy + 1 : iy, p11);
            float g = gain_at(b->gain, u, v);
            if (g < 0.35f) g = 0.35f;
            uint8_t *o = flat_px + ((size_t)Y * n + X) * 4;
            for (int k = 0; k < 3; k++) {
                double c = ((p00[k] * (1 - ax) + p10[k] * ax) * (1 - ay) + (p01[k] * (1 - ax) + p11[k] * ax) * ay) / g;
                o[k] = (uint8_t)(c > 255 ? 255 : c + 0.5);
            }
            o[3] = 255;
        }
    *wf = (Frame){flat_px, n, n, n * 4, 0};
    *fb = *b;
    fb->persp = 0, fb->x = fb->y = 0, fb->s = ws;
}

// a weak flat hit can be a tilted board the flat finder squeezed into a square
static void really_flat(const Frame *f, Board *out, const Rect *ignore, int nignore, int (*masked)(void *, int, int), void *mctx)
{
    Board pb = {0};
    if (!find_persp(f, &pb, ignore, nignore, masked, mctx)) return;
    double off = 0;
    for (int k = 0; k < 4; k++) {
        double x, y, u = k & 1 ? 8 : 0, v = k & 2 ? 8 : 0;
        hm_map(pb.hm, u, v, &x, &y);
        off = fmax(off, len2(x - (out->x + u * out->s), y - (out->y + v * out->s)));
    }
    if (off > out->s * 0.25) *out = pb, out->soft = 1;
}

int vision_find_board(const Frame *f, Board *out, const Rect *ignore, int nignore, int (*masked)(void *, int, int),
                      void *mctx)
{
    static int misses;
    gpu_frame();
    if (find_board(f, out, ignore, nignore, masked, mctx, 0)) {
        out->soft = textured(f, out);
        misses = 0;
        if (out->score < 62) really_flat(f, out, ignore, nignore, masked, mctx);
        return 1;
    }
    int sc = out->score, m = misses++;
    if (!vision_lite && m % 3 == 0 && find_board(f, out, ignore, nignore, masked, mctx, 1)) { // blurred pass is slow, only every 3rd miss
        misses = 0;
        really_flat(f, out, ignore, nignore, masked, mctx);
        return 1;
    }
    if (sc > out->score) out->score = sc;
    if (m % 3 == 1 && find_persp(f, out, ignore, nignore, masked, mctx)) { // tilted, like a 3d game looking at it from the side
        out->soft = 1; // resampled and never quite pixel exact, read it like a textured board
        misses = 0;
        return 1;
    }
    return 0;
}

typedef struct {
    int (*fn)(void *, int, int);
    void *ctx;
    int ox, oy;
} Shift;

static int shifted(void *p, int x, int y)
{
    Shift *s = p;
    return s->fn(s->ctx, x + s->ox, y + s->oy);
}

static int find_near(const Frame *f, const Board *old, Board *out, int x0, int y0, int x1, int y1, int (*masked)(void *, int, int),
                     void *mctx)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > f->w) x1 = f->w;
    if (y1 > f->h) y1 = f->h;
    if (x1 - x0 < 150 || y1 - y0 < 150) return 0;
    Frame c = *f;
    c.px = (uint8_t *)pix(f, x0, y0);
    c.w = x1 - x0, c.h = y1 - y0;
    Shift sh = {masked, mctx, x0, y0};
    Board b = {0};
    int hit = find_board(&c, &b, NULL, 0, masked ? shifted : NULL, &sh, 0);
    if (!hit && old->soft && !vision_lite) hit = find_board(&c, &b, NULL, 0, masked ? shifted : NULL, &sh, 1);
    if (!hit) return 0;
    b.x += x0, b.y += y0;
    if (cdiff(b.light, old->light) >= 40 || cdiff(b.dark, old->dark) >= 40) return 0; // some other board
    if (b.s < old->s * 0.6 || b.s > old->s * 1.6) return 0;
    if (!b.soft) b.soft = textured(f, &b);
    b.out = old->out;
    *out = b;
    return 1;
}

// board moved or got zoomed, look around the old spot first then the whole frame
int vision_relocate(const Frame *f, Board *b, int full, int (*masked)(void *, int, int), void *mctx)
{
    Board nb;
    gpu_frame();
    int m = (int)(b->s * 4), x = (int)b->x, y = (int)b->y, w = (int)(b->s * 8);
    int hit = find_near(f, b, &nb, x - m, y - m, x + w + m, y + w + m, masked, mctx);
    if (!hit && full) hit = find_near(f, b, &nb, 0, 0, f->w, f->h, masked, mctx);
    if (!hit || (fabs(nb.x - b->x) < 2 && fabs(nb.y - b->y) < 2 && fabs(nb.s - b->s) < 1)) return 0; // same spot, just covered
    *b = nb;
    return 1;
}

int vision_verify(const Frame *f, Board *b, int (*masked)(void *, int, int), void *mctx)
{
    if (b->persp) {
        Frame wf;
        Board fb;
        flatten(f, b, &wf, &fb);
        FlatMask fm = {masked, mctx, b, (int)fb.s};
        int ok = vision_verify(&wf, &fb, masked ? flat_masked : NULL, &fm);
        b->score = fb.score;
        return ok;
    }
    int pen = 0;
    double sc = score_board(f, b->x, b->y, b->s, b->light, b->dark, masked, mctx, &pen, b->soft) - pen; // squares past the edge = it slid by whole squares
    b->score = (int)sc;
    if (sc >= 46) return 1;
    // square colors changed? still a board if the checker pattern holds
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
                if (m > bn) bn = m, bi = a; // most common of the 4, a piece edge can hit one
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

#define BG_ROWS 32

// boards that shade each square top to bottom (roblox), take the color row by row off both side edges
static int square_rows(const Frame *f, double x0, double y0, double s, const uint8_t *bg, const uint8_t *base,
                       const uint8_t *other, int (*masked)(void *, int, int), void *mctx, uint8_t rows[BG_ROWS][3])
{
    int ok[BG_ROWS], nok = 0, far = 0, pr = (int)fmax(0, s * 0.012);
    for (int j = 0; j < BG_ROWS; j++) {
        double y = y0 + (j + 0.5) / BG_ROWS * s;
        uint8_t e[2][3];
        ok[j] = 0;
        int nv = 0;
        for (int k = 0; k < 2; k++) {
            double x = x0 + (k ? 0.95 : 0.05) * s;
            if (!sample_ok(f, x - pr, y - pr) || !sample_ok(f, x + pr, y + pr) || (masked && masked(mctx, (int)x, (int)y))) break;
            patch(f, x, y, pr, e[k]);
            nv++;
        }
        if (nv < 2 || cdiff(e[0], e[1]) > 24) continue; // a letter or a piece on one side
        for (int k = 0; k < 3; k++) rows[j][k] = (uint8_t)((e[0][k] + e[1][k]) / 2);
        if (cdiff(rows[j], base) >= cdiff(rows[j], other) || cdiff(rows[j], bg) > 120) continue;
        ok[j] = 1;
        nok++;
        far += cdiff(rows[j], bg) > 30;
    }
    if (nok < BG_ROWS / 2 || !far) return 0; // flat square, the ring color does fine
    for (int j = 0; j < BG_ROWS; j++) {
        if (ok[j]) continue;
        int a = j - 1, b = j + 1;
        while (a >= 0 && !ok[a]) a--;
        while (b < BG_ROWS && !ok[b]) b++;
        int src = a < 0 ? b : b >= BG_ROWS ? a : (j - a <= b - j ? a : b);
        memcpy(rows[j], rows[src], 3);
    }
    return 1;
}

static inline const uint8_t *row_bg(const uint8_t (*rows)[3], const uint8_t *bg, double y0, double s, double y)
{
    if (!rows) return bg;
    int j = (int)((y - y0) / s * BG_ROWS);
    return rows[j < 0 ? 0 : j >= BG_ROWS ? BG_ROWS - 1 : j];
}

static double piece_fill_lum(const Frame *f, double x0, double y0, double s, const uint8_t *bg, const uint8_t (*rows)[3],
                             const uint8_t *base, int (*masked)(void *, int, int), void *mctx, int thr, int *colored)
{
    int step = s > 90 ? 2 : 1;
    int a = (int)(x0 + s * 0.05), b = (int)(y0 + s * 0.05);
    int n = (int)(s * 0.9 / step);
    if (n < 8) return -1;
    if (n > 128) n = 128;
    static uint8_t m[130 * 130];
    static uint8_t lm[130 * 130], col[130 * 130];
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
                fg = cdiff(px, row_bg(rows, bg, y0, s, y)) > thr && cdiff(px, base) > thr / 2;
                if (masked && masked(mctx, x, y)) fg = 0;
            }
            m[(j + 1) * W + i + 1] = fg ? 1 : 0;
            int mx = px[0] > px[1] ? (px[0] > px[2] ? px[0] : px[2]) : (px[1] > px[2] ? px[1] : px[2]);
            int mn = px[0] < px[1] ? (px[0] < px[2] ? px[0] : px[2]) : (px[1] < px[2] ? px[1] : px[2]);
            int reddish = px[0] - px[1] > 15 && px[0] - px[2] > 15; // check glow makes the king look black
            int glow = reddish && px[1] < px[0] * 0.55;
            col[(j + 1) * W + i + 1] = (mx - mn > 30 || reddish) && !glow;
            lm[(j + 1) * W + i + 1] = (soft_colors ? glow : (mx - mn > 30 || reddish)) ? 128 : (uint8_t)lum(px); // 128 = no vote either way
        }
    // flood the bg in from the border, whats left is the piece plus its insides
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
    long cnt = 0, bright = 0, dark = 0, ncol = 0;
    for (int j = 1; j <= n; j++)
        for (int i = 1; i <= n; i++) {
            int q = j * W + i;
            if (m[q] == 2) continue;
            cnt++;
            ncol += col[q];
            if (lm[q] > 170)
                bright++;
            else if (lm[q] < 80)
                dark++;
        }
    if (colored) *colored = cnt >= n * n / 40 && ncol * 2 > cnt;
    if (cnt < n * n / 40 || bright + dark < cnt / 8) return -1;
    double ratio = 255.0 * bright / (bright + dark);
    // some sets have a grey fill with a light glow round it, peel the edge off and the middle is the real color
    int peel = (int)(n * 0.07);
    for (int it = 0; it < peel; it++) {
        for (int j = 1; j <= n; j++)
            for (int i = 1; i <= n; i++) {
                int q = j * W + i;
                if (m[q] != 2 && (m[q - 1] == 2 || m[q + 1] == 2 || m[q - W] == 2 || m[q + W] == 2)) m[q] = 3;
            }
        for (int q = 0; q < W * W; q++)
            if (m[q] == 3) m[q] = 2;
    }
    int h[256] = {0}, nh = 0;
    for (int j = 1; j <= n; j++)
        for (int i = 1; i <= n; i++) {
            int q = j * W + i;
            if (m[q] != 2 && lm[q] != 128) h[lm[q]]++, nh++;
        }
    if (nh < cnt / 6) return ratio;
    int v = 0, acc = 0;
    while (v < 255 && (acc += h[v]) <= nh / 2) v++;
    return v > 80 && v < 170 ? v : ratio; // only steps in where the old count is blind
}

static int display_to_sq(int r, int c, int white_bottom)
{
    return white_bottom ? (7 - r) * 8 + c : r * 8 + (7 - c);
}

// white pieces on light squares only show their outline, on dark ones theyre solid
void vision_fill_holes(float *m)
{
    uint8_t wall[FEAT_N * FEAT_N] = {0}, out[FEAT_N * FEAT_N] = {0};
    int stack[FEAT_N * FEAT_N], n = 0;
    for (int y = 0; y < FEAT_N; y++) // grow it by 2 so little gaps dont leak
        for (int x = 0; x < FEAT_N; x++)
            for (int dy = -2; dy <= 2 && !wall[y * FEAT_N + x]; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < FEAT_N && yy < FEAT_N && m[yy * FEAT_N + xx] == 1) {
                        wall[y * FEAT_N + x] = 1;
                        break;
                    }
                }
    for (int i = 0; i < FEAT_N; i++) {
        int edge[4] = {i, (FEAT_N - 1) * FEAT_N + i, i * FEAT_N, i * FEAT_N + FEAT_N - 1};
        for (int k = 0; k < 4; k++)
            if (!wall[edge[k]] && !out[edge[k]]) out[edge[k]] = 1, stack[n++] = edge[k];
    }
    while (n) {
        int i = stack[--n], x = i % FEAT_N, y = i / FEAT_N;
        int nb[4] = {x > 0 ? i - 1 : -1, x < FEAT_N - 1 ? i + 1 : -1, y > 0 ? i - FEAT_N : -1, y < FEAT_N - 1 ? i + FEAT_N : -1};
        for (int k = 0; k < 4; k++)
            if (nb[k] >= 0 && !wall[nb[k]] && !out[nb[k]]) out[nb[k]] = 1, stack[n++] = nb[k];
    }
    for (int y = 0; y < FEAT_N; y++)
        for (int x = 0; x < FEAT_N; x++) {
            int i = y * FEAT_N + x, near = 0;
            if (m[i] != 0) continue;
            for (int dy = -3; dy <= 3 && !near; dy++)
                for (int dx = -3; dx <= 3; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= FEAT_N || yy >= FEAT_N || out[yy * FEAT_N + xx]) {
                        near = 1;
                        break;
                    }
                }
            if (!near) m[i] = 1;
        }
}

// last read of every square, a square whose pixels didnt change gets its old result back
typedef struct {
    uint64_t h;
    uint8_t hidden, foreign, highlighted, occ, colored;
    float occ_frac, lum, mask[FEAT_N * FEAT_N];
} SqMemo;

typedef struct {
    double x, y, s;
    uint8_t light[3], dark[3];
    int soft, nz, w, h, bgr, masked, ok;
    SqMemo sq[64];
} Memo;

static Memo memo[2]; // [soft_colors]

int vision_nomemo;

static uint64_t sq_print(const Frame *f, double x0, double y0, double s, int (*masked)(void *, int, int), void *mctx)
{
    uint64_t h = 1469598103934665603ULL;
    int a = (int)x0, b = (int)y0, e = (int)(x0 + s) + 1, g = (int)(y0 + s) + 1;
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (e > f->w) e = f->w;
    if (g > f->h) g = f->h;
    for (int y = b; y < g; y += 3) {
        const uint8_t *p = pix(f, a, y);
        for (int x = a; x < e; x += 3, p += 12) {
            uint64_t v = p[0] | (uint64_t)p[1] << 8 | (uint64_t)p[2] << 16;
            if (masked && masked(mctx, x, y)) v |= 1ULL << 24;
            h = (h ^ v) * 1099511628211ULL;
        }
    }
    return h;
}

void vision_read(const Frame *f, const Board *b, int white_bottom, float color_thr, BoardRead *out,
                 int (*masked)(void *, int, int), void *mctx)
{
    if (b->persp) {
        Frame wf;
        Board fb;
        flatten(f, b, &wf, &fb);
        FlatMask fm = {masked, mctx, b, (int)fb.s};
        vision_read(&wf, &fb, white_bottom, color_thr, out, masked ? flat_masked : NULL, &fm);
        double k = b->s / fb.s; // popup back into the square round the board, thats what main draws in
        out->popup = (Rect){b->x + out->popup.x * k, b->y + out->popup.y * k, out->popup.w * k, out->popup.h * k};
        return;
    }
    memset(out, 0, sizeof(*out));
    double s = b->s;
    float lums[64];
    int occ[64], board_nz = 0, ncolored = 0;
    // how noisy the texture is over the whole board, the thresholds below scale off it
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
    Memo *mm = &memo[soft_colors];
    int use = !vision_nomemo;
    if (use && !(mm->ok && mm->x == b->x && mm->y == b->y && mm->s == s && !memcmp(mm->light, b->light, 3) && !memcmp(mm->dark, b->dark, 3) &&
                 mm->soft == b->soft && mm->nz == board_nz && mm->w == f->w && mm->h == f->h && mm->bgr == f->bgr && mm->masked == !!masked)) {
        mm->x = b->x, mm->y = b->y, mm->s = s, mm->soft = b->soft, mm->nz = board_nz, mm->w = f->w, mm->h = f->h, mm->bgr = f->bgr;
        mm->masked = !!masked, mm->ok = 1;
        memcpy(mm->light, b->light, 3);
        memcpy(mm->dark, b->dark, 3);
        for (int i = 0; i < 64; i++) mm->sq[i].h = 0;
    }
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int sq = display_to_sq(r, c, white_bottom);
            double x0 = b->x + c * s, y0 = b->y + r * s;
            SqMemo *me = &mm->sq[r * 8 + c];
            uint64_t hh = use ? sq_print(f, x0, y0, s, masked, mctx) | 1 : 0;
            if (use && me->h == hh) {
                out->hidden[sq] = me->hidden, out->foreign[r * 8 + c] = me->foreign, out->highlighted[sq] = me->highlighted;
                out->occ_frac[sq] = me->occ_frac, out->lum[sq] = me->lum;
                memcpy(out->mask[sq], me->mask, sizeof(me->mask));
                occ[sq] = me->occ;
                ncolored += me->colored;
                lums[sq] = me->lum;
                continue;
            }
            int sqcol = 0;
            uint8_t ring[64][3];
            double ringy[64];
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
                    ringy[nring++] = y;
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
                    if (b->soft && ((c == 0 && k == 0) || (r == 7 && k == 3))) continue; // coordinate labels
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
                    // a straight edge cuts the square, something is on top of the board
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
                int nfar = 0, far_t = b->soft ? (int)fmax(110, cdiff(b->light, b->dark) * 0.6) : 60;
                for (int k = 0; k < 4; k++) nfar += valid[k] && cdiff(cc[k], b->light) > far_t && cdiff(cc[k], b->dark) > far_t;
                // something of theirs on top, not ours. popups have pictures and text in them so 2 odd corners is enough
                out->foreign[r * 8 + c] = (uint8_t)(!outside && (hid || nfar >= 2));
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
            if (bestn < nring / 4 || nbase * 8 >= nring) memcpy(bg, base, 3); // only trust the ring color if the theme color is basically gone (last move tint)
            int fthr = 60, sthr = 40;
            if (b->soft && nring)
                for (int k = 0; k < 3; k++) {
                    int h[256] = {0}, acc = 0, v = 0;
                    for (int i = 0; i < nring; i++) h[ring[i][k]]++;
                    while (v < 255 && (acc += h[v]) <= nring / 2) v++;
                    bg[k] = (uint8_t)v;
                }
            uint8_t rowc[BG_ROWS][3];
            const uint8_t(*rows)[3] = square_rows(f, x0, y0, s, bg, base, ((r + c) & 1) ? b->light : b->dark, masked, mctx, rowc) ? (const uint8_t(*)[3])rowc : NULL;
            if (b->soft && nring) {
                int dev[64], nd = 0;
                for (int i = 0; i < nring; i++) dev[nd++] = cdiff(ring[i], row_bg(rows, bg, y0, s, ringy[i]));
                for (int a2 = 1; a2 < nd; a2++)
                    for (int b2 = a2; b2 > 0 && dev[b2 - 1] > dev[b2]; b2--) {
                        int t = dev[b2];
                        dev[b2] = dev[b2 - 1];
                        dev[b2 - 1] = t;
                    }
                int nz = dev[nd * 2 / 5] > board_nz ? dev[nd * 2 / 5] : board_nz;
                fthr = (int)fmax(60, nz * 2.2); // noisier wood needs a bigger gap
                sthr = (int)fmax(40, nz * 1.6);
            }
            out->highlighted[sq] = cdiff(bg, base) > (b->soft ? 70 : 45); // tracker guesses whose turn it is from these
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
                        if (cdiff(px, row_bg(rows, bg, y0, s, y)) > fthr && cdiff(px, b->light) > sthr && cdiff(px, b->dark) > sthr) {
                            fg++;
                            v = 1;
                            cell_lum[gy * FEAT_N + gx] = (uint8_t)lum(px);
                        }
                    } else {
                        v = 0.5f; // unknown, similarity() skips it
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
                    // inner cells only, outlines are dark on both colors
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
                int colored = 0;
                double fl = piece_fill_lum(f, x0, y0, s, bg, rows, base, masked, mctx, fthr, &colored);
                sqcol = colored && !out->hidden[sq];
                ncolored += sqcol;
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
                if (bw <= 12 && bh <= 12 && fabs(cx - 11.5) <= 3 && fabs(cy - 11.5) <= 3) { // legal move dot, not a piece
                    occ[sq] = 0;
                    out->occ_frac[sq] = 0;
                }
            }
            if (occ[sq]) vision_fill_holes(out->mask[sq]);
            lums[sq] = out->lum[sq];
            if (use) {
                me->h = hh, me->hidden = out->hidden[sq], me->foreign = out->foreign[r * 8 + c], me->highlighted = out->highlighted[sq];
                me->occ_frac = out->occ_frac[sq], me->lum = out->lum[sq], me->occ = (uint8_t)occ[sq], me->colored = (uint8_t)sqcol;
                memcpy(me->mask, out->mask[sq], sizeof(me->mask));
            }
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
        int flat = vals[n - 1] - vals[0] < 25;
        // one blob of grey, or most pieces filled with color? recolored pieces, retry without dropping the colored pixels
        if (((flat && color_thr <= 0) || ncolored * 3 >= n * 2) && !soft_colors && n >= 4) {
            soft_colors = 1;
            vision_read(f, b, white_bottom, color_thr, out, masked, mctx);
            soft_colors = 0;
            return;
        }
        if (flat) thr = vals[0] > 110 ? 0 : 255;
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
    // biggest patch of covered squares is a popup like "you won", we stop drawing there
    uint8_t seen[64] = {0};
    int best = 0;
    for (int i = 0; i < 64; i++) {
        if (!out->foreign[i] || seen[i]) continue;
        int st[64], n = 0, cnt = 0, r0 = 8, r1 = -1, c0 = 8, c1 = -1;
        st[n++] = i, seen[i] = 1;
        while (n) {
            int q = st[--n], r = q / 8, c = q % 8;
            cnt++;
            if (r < r0) r0 = r;
            if (r > r1) r1 = r;
            if (c < c0) c0 = c;
            if (c > c1) c1 = c;
            int nb[4] = {c > 0 ? q - 1 : -1, c < 7 ? q + 1 : -1, r > 0 ? q - 8 : -1, r < 7 ? q + 8 : -1};
            for (int k = 0; k < 4; k++)
                if (nb[k] >= 0 && out->foreign[nb[k]] && !seen[nb[k]]) seen[nb[k]] = 1, st[n++] = nb[k];
        }
        int block = 0; // a 2x2 of covered squares, highlights and check glows never make one
        for (int rr = r0; rr < r1 && !block; rr++)
            for (int cc2 = c0; cc2 < c1 && !block; cc2++) {
                int q = rr * 8 + cc2;
                block = seen[q] && out->foreign[q] && out->foreign[q + 1] && out->foreign[q + 8] && out->foreign[q + 9];
            }
        if (block && cnt >= 4 && cnt > best) {
            best = cnt;
            out->has_popup = 1;
            out->popup = (Rect){b->x + c0 * s, b->y + r0 * s, (c1 - c0 + 1) * s, (r1 - r0 + 1) * s};
        }
    }
}
