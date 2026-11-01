#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

static const char *dir;
static Canvas canvas;
static int frame_no;
static uint8_t *buf; // bgrx like a real grab
static int bw, bh;

int replay_frame(void) { return frame_no; } // already bumped by rp_capture

static int load(int n)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%06d.ppm", dir, n);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int w = 0, h = 0, mx = 0, vals[3], nv = 0, c;
    if (fgetc(f) != 'P' || fgetc(f) != '6') {
        fclose(f);
        return 0;
    }
    while (nv < 3 && (c = fgetc(f)) != EOF) { // three numbers, # comments allowed (gimp writes them)
        if (c == '#')
            while ((c = fgetc(f)) != EOF && c != '\n') {}
        else if (c >= '0' && c <= '9') {
            int v = c - '0';
            while ((c = fgetc(f)) >= '0' && c <= '9' && v < 100000) v = v * 10 + c - '0';
            vals[nv++] = v;
        }
    }
    if (nv == 3) w = vals[0], h = vals[1], mx = vals[2];
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || mx != 255) { // 8 bit only
        fclose(f);
        return 0;
    }
    uint8_t *rgb = malloc((size_t)w * h * 3);
    if (!rgb) {
        fclose(f);
        return 0;
    }
    size_t got = fread(rgb, 3, (size_t)w * h, f);
    fclose(f);
    if (got != (size_t)w * h) {
        free(rgb);
        return 0;
    }
    if (w != bw || h != bh) {
        free(buf);
        buf = malloc((size_t)w * h * 4);
        if (!buf) {
            free(rgb);
            bw = bh = 0;
            return 0;
        }
        bw = w;
        bh = h;
    }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        buf[i * 4] = rgb[i * 3 + 2];
        buf[i * 4 + 1] = rgb[i * 3 + 1];
        buf[i * 4 + 2] = rgb[i * 3];
        buf[i * 4 + 3] = 255;
    }
    free(rgb);
    return 1;
}

static int rp_init(Backend *b)
{
    if (!load(0)) return 0;
    b->nout = 1;
    snprintf(b->out[0].name, sizeof(b->out[0].name), "replay");
    b->out[0].logical = (Rect){0, 0, bw, bh}; // always 1x
    return 1;
}

static int rp_capture(Backend *b)
{
    if (!load(frame_no)) return 0;
    if (getenv("OPEN_CHEASE_SEE_OVERLAY") && canvas.px && canvas.w == bw && canvas.h == bh) // a real screen grab sees our overlay too
        for (int i = 0; i < bw * bh; i++) {
            uint32_t o = canvas.px[i], a = o >> 24;
            for (int k = 0; k < 3; k++) buf[i * 4 + k] = (uint8_t)(((o >> (8 * k)) & 255) + buf[i * 4 + k] * (255 - a) / 255);
        }
    Output *o = &b->out[0];
    o->frame.px = buf;
    o->frame.w = bw;
    o->frame.h = bh;
    o->frame.stride = bw * 4;
    o->frame.bgr = 1;
    o->have_frame = 1;
    frame_no++;
    return 1;
}

static Backend rp_backend = {.name = "replay", .init = rp_init, .capture = rp_capture};

Backend *backend_replay(const char *d)
{
    dir = d;
    return &rp_backend;
}

static int ro_init(Overlay *ov, Backend *cap)
{
    canvas.w = (int)cap->out[0].logical.w;
    canvas.h = (int)cap->out[0].logical.h;
    canvas.px = calloc((size_t)canvas.w * canvas.h, 4);
    canvas.scale = 1;
    ov->ncanvas = 1;
    return 1;
}

static Canvas *ro_canvas(Overlay *ov, int i) { return i == 0 ? &canvas : NULL; }
static void ro_commit(Overlay *ov, const Rect *hits, int nhits)
{
    const char *shots = getenv("OPEN_CHEASE_SHOTS"); // frame + overlay like the user would see it
    if (!shots || !buf || canvas.w != bw || canvas.h != bh) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%06d.ppm", shots, frame_no - 1);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", canvas.w, canvas.h);
    for (int i = 0; i < canvas.w * canvas.h; i++) {
        uint32_t o = canvas.px[i], a = o >> 24;
        uint8_t px[3];
        for (int k = 0; k < 3; k++) {
            int under = buf[i * 4 + 2 - k], over = (o >> (16 - 8 * k)) & 255; // buf is bgr, ppm wants rgb
            px[k] = (uint8_t)(over + under * (255 - a) / 255);
        }
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static int ro_wait(Overlay *ov, int timeout_ms, double *cx, double *cy)
{
    static int kframe = -1;
    static const char *kp;
    const char *keys = getenv("OPEN_CHEASE_KEYS"); // "frame:text", ^ = backspace, ~ = escape
    if (keys && kframe != frame_no - 1) {
        kframe = frame_no - 1;
        kp = NULL;
        int n = 0;
        const char *c = keys;
        while (*c >= '0' && *c <= '9') n = n * 10 + *c++ - '0';
        if (*c == ':' && n == kframe) kp = c + 1;
    }
    if (kp && *kp) { // one char per call, like real key events
        char ch = *kp++;
        ov->key = ch == '^' ? 8 : ch == '~' ? 27 : ch;
        return 2;
    }
    static int last = -1;
    const char *clicks = getenv("OPEN_CHEASE_CLICKS"); // "frame:x,y frame:x,y"
    if (!clicks || frame_no - 1 == last) return 0;
    last = frame_no - 1;
    for (const char *p = clicks; p && *p;) {
        int n = 0, x = 0, y = 0;
        if (sscanf(p, "%d:%d,%d", &n, &x, &y) == 3 && n == frame_no - 1) {
            *cx = x, *cy = y;
            return 1;
        }
        p = strchr(p, ' ');
        if (p) p++;
    }
    return 0;
}

static int num(const char **p)
{
    int v = 0;
    while (**p >= '0' && **p <= '9' && v < 100000) v = v * 10 + *(*p)++ - '0';
    return v;
}

static int ro_pointer(Overlay *ov, double *x, double *y)
{
    const char *p = getenv("OPEN_CHEASE_HOVER"); // "frame:x,y ...", the mouse sits there from that frame on
    int got = 0;
    while (p && *p) {
        int n = num(&p);
        if (*p++ != ':') break;
        int hx = num(&p);
        if (*p++ != ',') break;
        int hy = num(&p);
        if (n <= frame_no - 1) *x = hx, *y = hy, got = 1;
        while (*p == ' ') p++;
    }
    return got;
}

static int ro_held(Overlay *ov, double *x, double *y)
{
    const char *h = getenv("OPEN_CHEASE_HELD"); // test hook, "a-b" holds the button down from frame a to b
    if (!h) return 0;
    int a = num(&h), b = *h == '-' ? (h++, num(&h)) : a;
    return frame_no - 1 >= a && frame_no - 1 <= b && ro_pointer(ov, x, y);
}

static Overlay ro_overlay = {"replay", ro_init, ro_canvas, 0, ro_commit, ro_wait, NULL, NULL, 0, ro_pointer, ro_held};
Overlay *overlay_replay(void) { return &ro_overlay; }
