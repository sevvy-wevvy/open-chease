#include <math.h>
#include <string.h>

#include "app.h"
#include "font_data.h"

static inline uint32_t premul(uint32_t argb, double cover)
{
    double a = ((argb >> 24) & 255) / 255.0 * cover;
    if (a <= 0) return 0;
    uint32_t A = (uint32_t)(a * 255 + 0.5);
    uint32_t R = (uint32_t)(((argb >> 16) & 255) * a + 0.5);
    uint32_t G = (uint32_t)(((argb >> 8) & 255) * a + 0.5);
    uint32_t B = (uint32_t)((argb & 255) * a + 0.5);
    return A << 24 | R << 16 | G << 8 | B;
}

static inline void blend(uint32_t *dst, uint32_t src)
{
    uint32_t sa = src >> 24;
    if (!sa) return;
    if (sa == 255) {
        *dst = src;
        return;
    }
    uint32_t d = *dst, inv = 255 - sa;
    uint32_t a = sa + (((d >> 24) & 255) * inv + 127) / 255;
    uint32_t r = ((src >> 16) & 255) + (((d >> 16) & 255) * inv + 127) / 255;
    uint32_t g = ((src >> 8) & 255) + (((d >> 8) & 255) * inv + 127) / 255;
    uint32_t b = (src & 255) + ((d & 255) * inv + 127) / 255;
    *dst = a << 24 | r << 16 | g << 8 | b;
}

static inline void mark(Canvas *c, int x0, int y0, int x1, int y1)
{
    if (c->dx1 <= c->dx0) {
        c->dx0 = x0, c->dy0 = y0, c->dx1 = x1, c->dy1 = y1;
        return;
    }
    if (x0 < c->dx0) c->dx0 = x0;
    if (y0 < c->dy0) c->dy0 = y0;
    if (x1 > c->dx1) c->dx1 = x1;
    if (y1 > c->dy1) c->dy1 = y1;
}

static inline void plot(Canvas *c, int x, int y, uint32_t argb, double cover)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h || cover <= 0) return;
    if (cover > 1) cover = 1;
    mark(c, x, y, x + 1, y + 1);
    blend(&c->px[y * c->w + x], premul(argb, cover));
}

static void fill_span(Canvas *c, int y, int xa, int xb, uint32_t src)
{
    if (xb <= xa) return;
    mark(c, xa, y, xb, y + 1);
    uint32_t *p = c->px + (size_t)y * c->w;
    for (int x = xa; x < xb; x++) blend(&p[x], src);
}

void canvas_clear(Canvas *c)
{
    c->cx0 = c->dx0, c->cy0 = c->dy0, c->cx1 = c->dx1, c->cy1 = c->dy1;
    if (c->dx1 > c->dx0) {
        int x0 = c->dx0 < 0 ? 0 : c->dx0, x1 = c->dx1 > c->w ? c->w : c->dx1;
        int y0 = c->dy0 < 0 ? 0 : c->dy0, y1 = c->dy1 > c->h ? c->h : c->dy1;
        for (int y = y0; y < y1 && x1 > x0; y++) memset(c->px + (size_t)y * c->w + x0, 0, (size_t)(x1 - x0) * 4);
    }
    c->dx0 = c->dy0 = c->dx1 = c->dy1 = 0;
}

int canvas_damage(const Canvas *c, int *x0, int *y0, int *x1, int *y1)
{
    int a = c->dx1 > c->dx0, b = c->cx1 > c->cx0;
    if (!a && !b) return 0;
    *x0 = a ? c->dx0 : c->cx0, *y0 = a ? c->dy0 : c->cy0, *x1 = a ? c->dx1 : c->cx1, *y1 = a ? c->dy1 : c->cy1;
    if (a && b) {
        if (c->cx0 < *x0) *x0 = c->cx0;
        if (c->cy0 < *y0) *y0 = c->cy0;
        if (c->cx1 > *x1) *x1 = c->cx1;
        if (c->cy1 > *y1) *y1 = c->cy1;
    }
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > c->w) *x1 = c->w;
    if (*y1 > c->h) *y1 = c->h;
    return *x1 > *x0 && *y1 > *y0;
}

void canvas_blit(Canvas *dst, const Canvas *src, double alpha, double dx, double dy, double clip_h)
{
    int a = (int)(alpha * 256 + 0.5);
    if (a <= 0) return;
    if (a > 256) a = 256;
    int ox = (int)lround((src->ox + dx - dst->ox) * dst->scale), oy = (int)lround((src->oy + dy - dst->oy) * dst->scale);
    int h = src->h;
    if (clip_h >= 0 && clip_h * src->scale < h) h = (int)(clip_h * src->scale);
    if (src->dx1 <= src->dx0) return;
    mark(dst, ox + src->dx0, oy + src->dy0, ox + src->dx1, oy + (src->dy1 < h ? src->dy1 : h));
    if (dst->dx0 < 0) dst->dx0 = 0;
    if (dst->dy0 < 0) dst->dy0 = 0;
    for (int y = 0; y < h; y++) {
        int ty = oy + y;
        if (ty < 0 || ty >= dst->h) continue;
        const uint32_t *row = src->px + (size_t)y * src->w;
        uint32_t *out = dst->px + (size_t)ty * dst->w;
        for (int x = 0; x < src->w; x++) {
            uint32_t v = row[x];
            int tx = ox + x;
            if (!v || tx < 0 || tx >= dst->w) continue;
            if (a < 256)
                v = (((v >> 24) & 255) * a >> 8) << 24 | (((v >> 16) & 255) * a >> 8) << 16 | (((v >> 8) & 255) * a >> 8) << 8 |
                    ((v & 255) * a >> 8);
            blend(&out[tx], v);
        }
    }
}

static void to_px(const Canvas *c, double lx, double ly, double *px, double *py)
{
    *px = (lx - c->ox) * c->scale;
    *py = (ly - c->oy) * c->scale;
}

void draw_rect(Canvas *c, Rect r, uint32_t argb)
{
    double x0, y0, x1, y1;
    to_px(c, r.x, r.y, &x0, &y0);
    to_px(c, r.x + r.w, r.y + r.h, &x1, &y1);
    int ix0 = (int)floor(x0), iy0 = (int)floor(y0), ix1 = (int)ceil(x1), iy1 = (int)ceil(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > c->w) ix1 = c->w;
    if (iy1 > c->h) iy1 = c->h;
    uint32_t solid = premul(argb, 1);
    for (int y = iy0; y < iy1; y++) {
        double cy = fmin(y + 1, y1) - fmax(y, y0);
        int fa = (int)ceil(x0), fb = (int)floor(x1);
        if (fa < ix0) fa = ix0;
        if (fb > ix1) fb = ix1;
        if (cy >= 1 && fb > fa) {
            for (int x = ix0; x < fa; x++) plot(c, x, y, argb, fmin(x + 1, x1) - fmax(x, x0));
            fill_span(c, y, fa, fb, solid);
            for (int x = fb; x < ix1; x++) plot(c, x, y, argb, fmin(x + 1, x1) - fmax(x, x0));
            continue;
        }
        for (int x = ix0; x < ix1; x++) {
            double cx = fmin(x + 1, x1) - fmax(x, x0);
            plot(c, x, y, argb, cx * cy);
        }
    }
}

void draw_frame(Canvas *c, Rect r, double w, uint32_t argb)
{
    draw_rect(c, (Rect){r.x, r.y, r.w, w}, argb);
    draw_rect(c, (Rect){r.x, r.y + r.h - w, r.w, w}, argb);
    draw_rect(c, (Rect){r.x, r.y + w, w, r.h - 2 * w}, argb);
    draw_rect(c, (Rect){r.x + r.w - w, r.y + w, w, r.h - 2 * w}, argb);
}

void draw_round_rect(Canvas *c, Rect r, double rad, uint32_t argb)
{
    double x0, y0, x1, y1;
    to_px(c, r.x, r.y, &x0, &y0);
    to_px(c, r.x + r.w, r.y + r.h, &x1, &y1);
    rad *= c->scale;
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)) return;
    int ya = (int)fmax(0, floor(y0)), yb = (int)fmin(c->h, ceil(y1)), xa = (int)fmax(0, floor(x0)), xb = (int)fmin(c->w, ceil(x1));
    uint32_t solid = premul(argb, 1);
    for (int y = ya; y < yb; y++) {
        int inner = y >= y0 + rad && y + 1 <= y1 - rad;
        int fa = (int)ceil(inner ? x0 : x0 + rad), fb = (int)floor(inner ? x1 : x1 - rad);
        if (y < y0 || y + 1 > y1) fa = fb = 0;
        if (fa < xa) fa = xa;
        if (fb > xb) fb = xb;
        for (int x = xa; x < xb; x++) {
            if (x == fa && fb > fa) {
                fill_span(c, y, fa, fb, solid);
                x = fb - 1;
                continue;
            }
            double cov = 0; // 3x3 samples per pixel
            for (int sy = 0; sy < 3; sy++)
                for (int sx = 0; sx < 3; sx++) {
                    double px = x + (sx + 0.5) / 3, py = y + (sy + 0.5) / 3;
                    if (px < x0 || px > x1 || py < y0 || py > y1) continue;
                    double qx = px < x0 + rad ? x0 + rad : (px > x1 - rad ? x1 - rad : px);
                    double qy = py < y0 + rad ? y0 + rad : (py > y1 - rad ? y1 - rad : py);
                    double dx = px - qx, dy = py - qy;
                    if (dx * dx + dy * dy <= rad * rad) cov += 1.0 / 9;
                }
            plot(c, x, y, argb, cov);
        }
    }
}

static double seg_dist(double px, double py, double x0, double y0, double x1, double y1)
{
    double dx = x1 - x0, dy = y1 - y0;
    double l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    double ex = x0 + t * dx - px, ey = y0 + t * dy - py;
    return sqrt(ex * ex + ey * ey);
}

void draw_line(Canvas *c, double lx0, double ly0, double lx1, double ly1, double w, uint32_t argb)
{
    double x0, y0, x1, y1;
    to_px(c, lx0, ly0, &x0, &y0);
    to_px(c, lx1, ly1, &x1, &y1);
    double hw = w * c->scale / 2;
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1) || !isfinite(hw)) return;
    int bx0 = (int)fmax(0, floor(fmin(x0, x1) - hw - 1)), bx1 = (int)fmin(c->w, ceil(fmax(x0, x1) + hw + 1));
    int by0 = (int)fmax(0, floor(fmin(y0, y1) - hw - 1)), by1 = (int)fmin(c->h, ceil(fmax(y0, y1) + hw + 1));
    for (int y = by0; y < by1; y++)
        for (int x = bx0; x < bx1; x++) {
            double d = seg_dist(x + 0.5, y + 0.5, x0, y0, x1, y1);
            double cov = hw + 0.5 - d;
            plot(c, x, y, argb, cov);
        }
}

static double edge(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

void draw_triangle(Canvas *c, double ax, double ay, double bx, double by, double cx, double cy, uint32_t argb)
{
    to_px(c, ax, ay, &ax, &ay);
    to_px(c, bx, by, &bx, &by);
    to_px(c, cx, cy, &cx, &cy);
    if (edge(ax, ay, bx, by, cx, cy) < 0) {
        double tx = bx, ty = by;
        bx = cx;
        by = cy;
        cx = tx;
        cy = ty;
    }
    if (!isfinite(ax + ay + bx + by + cx + cy)) return;
    int x0 = (int)fmax(0, floor(fmin(ax, fmin(bx, cx)))), x1 = (int)fmin(c->w, ceil(fmax(ax, fmax(bx, cx))));
    int y0 = (int)fmax(0, floor(fmin(ay, fmin(by, cy)))), y1 = (int)fmin(c->h, ceil(fmax(ay, fmax(by, cy))));
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int in = 0;
            for (int s = 0; s < 4; s++) {
                double px = x + (s & 1 ? 0.75 : 0.25), py = y + (s & 2 ? 0.75 : 0.25);
                if (edge(ax, ay, bx, by, px, py) >= 0 && edge(bx, by, cx, cy, px, py) >= 0 &&
                    edge(cx, cy, ax, ay, px, py) >= 0)
                    in++;
            }
            plot(c, x, y, argb, in / 4.0);
        }
}

static const FontFace *face(int big) { return big ? &font_20 : &font_14; }

static int text_zoom = 1;
void set_text_zoom(int z) { text_zoom = z < 1 ? 1 : z; }

double text_width(const char *s, int big)
{
    const FontFace *f = face(big);
    double w = 0;
    for (; *s; s++) {
        int ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) ch = '?';
        w += f->glyphs[ch - 32].adv;
    }
    return w * text_zoom;
}

double text_lw(const Canvas *c, const char *s, int big) { return text_width(s, big) * (c->scale >= 1.75 ? 2 : 1) / c->scale; }

double draw_text(Canvas *c, double lx, double ly, const char *s, int big, uint32_t argb)
{
    const FontFace *f = face(big);
    double x, y;
    to_px(c, lx, ly, &x, &y);
    int sc = (c->scale >= 1.75 ? 2 : 1) * text_zoom; // hidpi
    double start = lx;
    for (; *s; s++) {
        int ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) ch = '?';
        const Glyph *g = &f->glyphs[ch - 32];
        for (int gy = 0; gy < g->h; gy++)
            for (int gx = 0; gx < g->w; gx++) {
                uint8_t a = f->bits[g->off + gy * g->w + gx];
                if (!a) continue;
                for (int dy = 0; dy < sc; dy++)
                    for (int dx = 0; dx < sc; dx++)
                        plot(c, (int)x + (g->x + gx) * sc + dx, (int)y + (g->y + gy) * sc + dy, argb, a / 255.0);
            }
        x += g->adv * sc;
        lx += g->adv * sc / c->scale;
    }
    return lx - start;
}

int canvas_alpha_at(Canvas *c, double lx, double ly)
{
    double x, y;
    to_px(c, lx, ly, &x, &y);
    int ix = (int)x, iy = (int)y;
    if (ix < 0 || iy < 0 || ix >= c->w || iy >= c->h) return -1;
    return (int)(c->px[iy * c->w + ix] >> 24);
}
