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

static inline void plot(Canvas *c, int x, int y, uint32_t argb, double cover)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h || cover <= 0) return;
    if (cover > 1) cover = 1;
    blend(&c->px[y * c->w + x], premul(argb, cover));
}

void canvas_clear(Canvas *c) { memset(c->px, 0, (size_t)c->w * c->h * 4); }

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
    for (int y = iy0; y < iy1; y++) {
        double cy = fmin(y + 1, y1) - fmax(y, y0);
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
    for (int y = ya; y < yb; y++) {
        for (int x = xa; x < xb; x++) {
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
