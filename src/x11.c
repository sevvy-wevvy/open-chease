#include <dlfcn.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shapeconst.h>

#include "app.h"

static struct {
    Display *(*OpenDisplay)(const char *);
    int (*DefaultScreen_)(Display *);
    Window (*RootWindow_)(Display *, int);
    int (*DisplayWidth_)(Display *, int);
    int (*DisplayHeight_)(Display *, int);
    Status (*MatchVisualInfo)(Display *, int, int, int, XVisualInfo *);
    Colormap (*CreateColormap)(Display *, Window, Visual *, int);
    Window (*CreateWindow)(Display *, Window, int, int, unsigned, unsigned, unsigned, int, unsigned, Visual *,
                           unsigned long, XSetWindowAttributes *);
    int (*MapRaised)(Display *, Window);
    int (*RaiseWindow)(Display *, Window);
    int (*SelectInput)(Display *, Window, long);
    XImage *(*CreateImage)(Display *, Visual *, unsigned, int, int, char *, unsigned, unsigned, int, int);
    int (*PutImage)(Display *, Drawable, GC, XImage *, int, int, int, int, unsigned, unsigned);
    GC (*CreateGC)(Display *, Drawable, unsigned long, XGCValues *);
    int (*Flush)(Display *);
    int (*Pending)(Display *);
    int (*NextEvent)(Display *, XEvent *);
    int (*ConnectionNumber_)(Display *);
    Atom (*InternAtom)(Display *, const char *, Bool);
    int (*ChangeProperty)(Display *, Window, Atom, Atom, int, int, const unsigned char *, int);
    int (*StoreName)(Display *, Window, const char *);
    XserverRegion (*FixesCreateRegion)(Display *, XRectangle *, int);
    void (*FixesSetWindowShapeRegion)(Display *, Window, int, int, int, XserverRegion);
    void (*FixesDestroyRegion)(Display *, XserverRegion);
    Bool (*FixesQueryExtension)(Display *, int *, int *);
    XImage *(*GetImage)(Display *, Drawable, int, int, unsigned, unsigned, unsigned long, int);
    Window (*GetSelectionOwner)(Display *, Atom);
    int (*MoveResizeWindow)(Display *, Window, int, int, unsigned, unsigned);
    int (*UnmapWindow)(Display *, Window);
    int (*GrabKeyboard)(Display *, Window, Bool, int, int, Time);
    int (*UngrabKeyboard)(Display *, Time);
    int (*LookupString)(XKeyEvent *, char *, int, KeySym *, void *);
    void *(*SetErrorHandler)(int (*)(Display *, XErrorEvent *));
    Status (*GetGeometry)(Display *, Drawable, Window *, int *, int *, unsigned *, unsigned *, unsigned *, unsigned *);
} X;

typedef struct {
    Display *dpy;
    Window win;
    GC gc;
    XImage *img;
    Visual *visual;
    int w, h;
    Canvas canvas;
    double scale;
    int composited, mapped;
    int bx, by, bw, bh;
    Backend *cap;
    int gen;
    long checked;
} XState;

static XState xs;

static int load_x(void)
{
    static int loaded;
    if (loaded) return loaded > 0;
    loaded = -1;
    void *x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
    void *xf = dlopen("libXfixes.so.3", RTLD_NOW);
    if (!x11 || !xf) return 0;
#define L(lib, field, name)                                  \
    do {                                                     \
        *(void **)(&X.field) = dlsym(lib, name);             \
        if (!X.field) return 0;                              \
    } while (0)
    L(x11, OpenDisplay, "XOpenDisplay");
    L(x11, DefaultScreen_, "XDefaultScreen");
    L(x11, RootWindow_, "XRootWindow");
    L(x11, DisplayWidth_, "XDisplayWidth");
    L(x11, DisplayHeight_, "XDisplayHeight");
    L(x11, MatchVisualInfo, "XMatchVisualInfo");
    L(x11, CreateColormap, "XCreateColormap");
    L(x11, CreateWindow, "XCreateWindow");
    L(x11, MapRaised, "XMapRaised");
    L(x11, RaiseWindow, "XRaiseWindow");
    L(x11, SelectInput, "XSelectInput");
    L(x11, CreateImage, "XCreateImage");
    L(x11, PutImage, "XPutImage");
    L(x11, CreateGC, "XCreateGC");
    L(x11, Flush, "XFlush");
    L(x11, Pending, "XPending");
    L(x11, NextEvent, "XNextEvent");
    L(x11, ConnectionNumber_, "XConnectionNumber");
    L(x11, InternAtom, "XInternAtom");
    L(x11, ChangeProperty, "XChangeProperty");
    L(x11, StoreName, "XStoreName");
    L(xf, FixesCreateRegion, "XFixesCreateRegion");
    L(xf, FixesSetWindowShapeRegion, "XFixesSetWindowShapeRegion");
    L(xf, FixesDestroyRegion, "XFixesDestroyRegion");
    L(xf, FixesQueryExtension, "XFixesQueryExtension");
    L(x11, GetImage, "XGetImage");
    L(x11, GetSelectionOwner, "XGetSelectionOwner");
    L(x11, MoveResizeWindow, "XMoveResizeWindow");
    L(x11, UnmapWindow, "XUnmapWindow");
    L(x11, GrabKeyboard, "XGrabKeyboard");
    L(x11, UngrabKeyboard, "XUngrabKeyboard");
    L(x11, LookupString, "XLookupString");
    L(x11, SetErrorHandler, "XSetErrorHandler");
    L(x11, GetGeometry, "XGetGeometry");
#undef L
    loaded = 1;
    return 1;
}

static void make_img(void);

static int x_init(Overlay *ov, Backend *cap)
{
    if (!load_x()) return 0;
    xs.dpy = X.OpenDisplay(NULL);
    if (!xs.dpy) return 0;
    int ev, er;
    if (!X.FixesQueryExtension(xs.dpy, &ev, &er)) return 0;
    int scr = X.DefaultScreen_(xs.dpy);
    char cm[32];
    // whoever owns _NET_WM_CM_Sn is the compositor
    snprintf(cm, sizeof(cm), "_NET_WM_CM_S%d", scr);
    xs.composited = X.GetSelectionOwner(xs.dpy, X.InternAtom(xs.dpy, cm, False)) != 0;
    if (!xs.composited)
        fprintf(stderr, "heads up, theres no compositor running so the overlay will look a bit rough. "
                        "on xfce turn it on in Settings > Window Manager Tweaks > Compositor\n");
    Window root = X.RootWindow_(xs.dpy, scr);
    xs.w = X.DisplayWidth_(xs.dpy, scr);
    xs.h = X.DisplayHeight_(xs.dpy, scr);
    XVisualInfo vi;
    if (!X.MatchVisualInfo(xs.dpy, scr, 32, TrueColor, &vi)) return 0; // argb visual, the alpha only works with a compositor
    xs.visual = vi.visual;
    XSetWindowAttributes a;
    memset(&a, 0, sizeof(a));
    a.override_redirect = True; // wm leaves it alone, no frame no focus
    a.colormap = X.CreateColormap(xs.dpy, root, vi.visual, AllocNone);
    a.background_pixel = 0;
    a.border_pixel = 0;
    xs.win = X.CreateWindow(xs.dpy, root, 0, 0, 1, 1, 0, 32, InputOutput, vi.visual, // real size comes on commit
                            CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &a);
    X.StoreName(xs.dpy, xs.win, "open-chease");
    Atom type = X.InternAtom(xs.dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom notif = X.InternAtom(xs.dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    X.ChangeProperty(xs.dpy, xs.win, type, 4, 32, PropModeReplace, (unsigned char *)&notif, 1); // 4 = XA_ATOM, the 6 below is XA_CARDINAL
    long keep = 2; // 2 = never unredirect us, xfwm did and the screen went black
    X.ChangeProperty(xs.dpy, xs.win, X.InternAtom(xs.dpy, "_NET_WM_BYPASS_COMPOSITOR", False), 6, 32, PropModeReplace,
                     (unsigned char *)&keep, 1);
    XserverRegion empty = X.FixesCreateRegion(xs.dpy, NULL, 0);
    X.FixesSetWindowShapeRegion(xs.dpy, xs.win, ShapeInput, 0, 0, empty);
    X.FixesDestroyRegion(xs.dpy, empty);
    X.SelectInput(xs.dpy, xs.win, ButtonPressMask | ButtonReleaseMask | ExposureMask | KeyPressMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask);
    xs.gc = X.CreateGC(xs.dpy, xs.win, 0, NULL);
    xs.cap = cap;
    xs.gen = cap->out_gen;
    make_img();
    X.Flush(xs.dpy);
    ov->ncanvas = 1;
    return 1;
}

static void make_img(void)
{
    if (xs.img) xs.img->f.destroy_image(xs.img); // frees data too
    char *data = calloc((size_t)xs.w * xs.h, 4);
    xs.img = X.CreateImage(xs.dpy, xs.visual, 32, ZPixmap, 0, data, (unsigned)xs.w, (unsigned)xs.h, 32, xs.w * 4);
    Backend *cap = xs.cap;
    double layout_w = 0, layout_h = 0;
    for (int i = 0; i < cap->nout; i++) {
        Rect r = cap->out[i].logical;
        if (r.x + r.w > layout_w) layout_w = r.x + r.w;
        if (r.y + r.h > layout_h) layout_h = r.y + r.h;
    }
    xs.scale = layout_w > 0 ? xs.w / layout_w : 1; // grab rects can be logical px (xwayland), X is real px
    xs.canvas.px = (uint32_t *)data;
    xs.canvas.w = xs.w;
    xs.canvas.h = xs.h;
    xs.canvas.ox = 0;
    xs.canvas.oy = 0;
    xs.canvas.scale = xs.scale;
    xs.canvas.cx0 = xs.canvas.cy0 = xs.canvas.dx0 = xs.canvas.dy0 = 0;
    xs.canvas.cx1 = xs.canvas.dx1 = xs.w;
    xs.canvas.cy1 = xs.canvas.dy1 = xs.h;
}

static long now_ms(void);

static Canvas *x_canvas(Overlay *ov, int i)
{
    if (i) return NULL;
    long t = now_ms();
    if (xs.gen != xs.cap->out_gen || t - xs.checked > 2000) { // no randr here, just poll the root size
        xs.checked = t;
        Window r;
        int gx, gy;
        unsigned gw, gh, gb, gd;
        int scr = X.DefaultScreen_(xs.dpy);
        if (X.GetGeometry(xs.dpy, X.RootWindow_(xs.dpy, scr), &r, &gx, &gy, &gw, &gh, &gb, &gd) && gw && gh &&
            ((int)gw != xs.w || (int)gh != xs.h || xs.gen != xs.cap->out_gen)) {
            xs.w = (int)gw;
            xs.h = (int)gh;
            xs.gen = xs.cap->out_gen;
            make_img();
        }
    }
    return &xs.canvas;
}

static void set_bounding_shape(void)
{
    if (xs.composited) return; // no compositor = no alpha, so cut the window down to the solid px
    int cap = 4096, n = 0;
    XRectangle *r = malloc(sizeof(XRectangle) * cap);
    for (int y = 0; y < xs.bh; y++) {
        const uint32_t *row = xs.canvas.px + (size_t)(xs.by + y) * xs.w + xs.bx;
        int x = 0;
        while (x < xs.bw) {
            while (x < xs.bw && (row[x] >> 24) < 110) x++;
            int start = x;
            while (x < xs.bw && (row[x] >> 24) >= 110) x++;
            if (x > start) {
                if (n == cap) {
                    cap *= 2;
                    r = realloc(r, sizeof(XRectangle) * cap);
                }
                r[n++] = (XRectangle){(short)start, (short)y, (unsigned short)(x - start), 1};
            }
        }
    }
    XserverRegion reg = X.FixesCreateRegion(xs.dpy, r, n);
    X.FixesSetWindowShapeRegion(xs.dpy, xs.win, ShapeBounding, 0, 0, reg);
    X.FixesDestroyRegion(xs.dpy, reg);
    free(r);
}

static void x_commit(Overlay *ov, const Rect *hits, int nhits)
{
    int x0 = xs.w, y0 = xs.h, x1 = -1, y1 = -1;
    const Canvas *cv = &xs.canvas;
    int sx0 = cv->dx0 < 0 ? 0 : cv->dx0, sx1 = cv->dx1 > xs.w ? xs.w : cv->dx1;
    int sy0 = cv->dy0 < 0 ? 0 : cv->dy0, sy1 = cv->dy1 > xs.h ? xs.h : cv->dy1;
    for (int y = sy0; y < sy1; y++) {
        const uint32_t *row = xs.canvas.px + (size_t)y * xs.w;
        int first = -1, last = -1;
        for (int x = sx0; x < sx1; x++)
            if (row[x] >> 24) {
                if (first < 0) first = x;
                last = x;
            }
        if (first < 0) continue;
        if (first < x0) x0 = first;
        if (last > x1) x1 = last;
        if (y < y0) y0 = y;
        y1 = y;
    }
    if (x1 < 0) { // nothing drawn
        if (xs.mapped) {
            X.UnmapWindow(xs.dpy, xs.win);
            xs.mapped = 0;
        }
        X.Flush(xs.dpy);
        return;
    }
    xs.bx = x0;
    xs.by = y0;
    xs.bw = x1 - x0 + 1;
    xs.bh = y1 - y0 + 1;
    // xfwm: screen sized window = fullscreen, 1px short dodges that
    if (xs.bw >= xs.w && xs.bh >= xs.h) xs.bh--;
    X.MoveResizeWindow(xs.dpy, xs.win, xs.bx, xs.by, (unsigned)xs.bw, (unsigned)xs.bh);
    XRectangle rects[128];
    int n = 0;
    for (int i = 0; i < nhits && n < 128; i++) {
        rects[n].x = (short)(hits[i].x * xs.scale - xs.bx);
        rects[n].y = (short)(hits[i].y * xs.scale - xs.by);
        rects[n].width = (unsigned short)(hits[i].w * xs.scale + 1); // +1 so rounding cant shave a button edge
        rects[n].height = (unsigned short)(hits[i].h * xs.scale + 1);
        n++;
    }
    XserverRegion region = X.FixesCreateRegion(xs.dpy, n ? rects : NULL, n);
    X.FixesSetWindowShapeRegion(xs.dpy, xs.win, ShapeInput, 0, 0, region);
    X.FixesDestroyRegion(xs.dpy, region);
    set_bounding_shape();
    if (!xs.mapped) {
        X.MapRaised(xs.dpy, xs.win);
        xs.mapped = 1;
    }
    X.PutImage(xs.dpy, xs.win, xs.gc, xs.img, xs.bx, xs.by, 0, 0, (unsigned)xs.bw, (unsigned)xs.bh);
    X.RaiseWindow(xs.dpy, xs.win); // windows raised since then would cover us
    X.Flush(xs.dpy);
}

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int ptr_in, ptr_down;
static double ptr_x, ptr_y;

static int x_held(Overlay *ov, double *x, double *y)
{
    double cx, cy;
    if (ptr_down) ov->wait_events(ov, 0, &cx, &cy); // pulls in the motion and the release
    *x = ptr_x, *y = ptr_y;
    return ptr_down;
}

static int x_pointer(Overlay *ov, double *x, double *y)
{
    if (!ptr_in) return 0;
    *x = ptr_x, *y = ptr_y;
    return 1;
}

static int x_wait(Overlay *ov, int timeout_ms, double *cx, double *cy)
{
    long deadline = now_ms() + timeout_ms;
    for (;;) {
        while (X.Pending(xs.dpy)) {
            XEvent e;
            X.NextEvent(xs.dpy, &e);
            if (e.type == MotionNotify || e.type == EnterNotify) {
                int mx = e.type == MotionNotify ? e.xmotion.x : e.xcrossing.x, my = e.type == MotionNotify ? e.xmotion.y : e.xcrossing.y;
                ptr_in = 1, ptr_x = (mx + xs.bx) / xs.scale, ptr_y = (my + xs.by) / xs.scale;
                continue;
            }
            if (e.type == LeaveNotify) {
                ptr_in = 0;
                continue;
            }
            if (e.type == ButtonRelease) {
                if (e.xbutton.button == 1) ptr_down = 0;
                continue;
            }
            if (e.type == ButtonPress) {
                if (e.xbutton.button == 1) ptr_down = 1;
                *cx = (e.xbutton.x + xs.bx) / xs.scale;
                *cy = (e.xbutton.y + xs.by) / xs.scale;
                if (e.xbutton.button == 4 || e.xbutton.button == 5) { // 4/5 = wheel
                    ov->key = e.xbutton.button == 5 ? 1 : -1;
                    return 3;
                }
                if (e.xbutton.button != 1) continue;
                return 1;
            }
            if (e.type == KeyPress) {
                char buf[8] = {0};
                KeySym sym = 0;
                int n = X.LookupString(&e.xkey, buf, sizeof(buf), &sym, NULL);
                if (n == 1) {
                    ov->key = (unsigned char)buf[0];
                    return 2;
                }
            }
            if (e.type == Expose && xs.mapped)
                X.PutImage(xs.dpy, xs.win, xs.gc, xs.img, xs.bx, xs.by, 0, 0, (unsigned)xs.bw, (unsigned)xs.bh);
        }
        long left = deadline - now_ms();
        if (left <= 0) return 0;
        struct pollfd pfd = {X.ConnectionNumber_(xs.dpy), POLLIN, 0};
        poll(&pfd, 1, (int)left);
    }
}

static void x_keyboard(Overlay *ov, int on)
{
    if (!xs.dpy) return;
    if (on && X.GrabKeyboard(xs.dpy, xs.win, False, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
        fprintf(stderr, "couldnt grab the keyboard, use the keys on screen to search\n");
    if (!on) X.UngrabKeyboard(xs.dpy, CurrentTime);
    X.Flush(xs.dpy);
}

static Display *cap_dpy;
static XImage *cap_img;
static int cap_w, cap_h;

static int x_err;
static int on_xerr(Display *d, XErrorEvent *e) { return x_err = 1, 0; } // default handler exit()s, e.g. on a resolution change
static uint8_t *conv;

static int xcap_init(Backend *b)
{
    if (!load_x()) return 0;
    cap_dpy = X.OpenDisplay(NULL);
    if (!cap_dpy) return 0;
    X.SetErrorHandler(on_xerr);
    int scr = X.DefaultScreen_(cap_dpy);
    cap_w = X.DisplayWidth_(cap_dpy, scr);
    cap_h = X.DisplayHeight_(cap_dpy, scr);
    b->nout = 1;
    snprintf(b->out[0].name, sizeof(b->out[0].name), "X11 screen");
    b->out[0].logical = (Rect){0, 0, cap_w, cap_h};
    return 1;
}

static int xcap_capture(Backend *b)
{
    if (cap_img) {
        cap_img->f.destroy_image(cap_img);
        cap_img = NULL;
    }
    Window root = X.RootWindow_(cap_dpy, X.DefaultScreen_(cap_dpy)), r;
    int gx, gy;
    unsigned gw, gh, gb, gd;
    if (X.GetGeometry(cap_dpy, root, &r, &gx, &gy, &gw, &gh, &gb, &gd) && gw && gh && ((int)gw != cap_w || (int)gh != cap_h)) {
        cap_w = (int)gw, cap_h = (int)gh;
        b->out[0].logical = (Rect){0, 0, cap_w, cap_h};
        b->out_gen++;
    }
    x_err = 0;
    cap_img = X.GetImage(cap_dpy, root, 0, 0, (unsigned)cap_w, (unsigned)cap_h, ~0UL, ZPixmap);
    Output *o = &b->out[0];
    o->have_frame = 0;
    if (x_err || !cap_img || cap_img->bits_per_pixel != 32) return 0; // 16 bit screens, not bothering
    o->frame.px = (uint8_t *)cap_img->data;
    o->frame.w = cap_img->width;
    o->frame.h = cap_img->height;
    o->frame.stride = cap_img->bytes_per_line;
    o->frame.bgr = cap_img->red_mask == 0xff0000; // B G R X in memory
    if (cap_img->red_mask == 0x3ff00000 || cap_img->red_mask == 0x3ff) { // depth 30, squash to 8 bits a channel
        int rb = cap_img->red_mask == 0x3ff00000, w = cap_img->width, h = cap_img->height;
        conv = realloc(conv, (size_t)w * h * 4);
        if (!conv) return 0;
        for (int y = 0; y < h; y++) {
            const uint32_t *src = (const uint32_t *)(cap_img->data + (size_t)y * cap_img->bytes_per_line);
            uint8_t *dst = conv + (size_t)y * w * 4;
            for (int i = 0; i < w; i++) {
                uint32_t v = src[i];
                uint8_t hi = (uint8_t)(v >> 22), mid = (uint8_t)(v >> 12), lo = (uint8_t)(v >> 2);
                dst[i * 4] = rb ? lo : hi, dst[i * 4 + 1] = mid, dst[i * 4 + 2] = rb ? hi : lo, dst[i * 4 + 3] = 255;
            }
        }
        o->frame.px = conv;
        o->frame.stride = w * 4;
        o->frame.bgr = 1;
    }
    o->have_frame = 1;
    return 1;
}

static Backend x_backend = {.name = "x11-getimage", .init = xcap_init, .capture = xcap_capture};
Backend *backend_x11(void) { return &x_backend; }

static Overlay x_overlay = {"x11", x_init, x_canvas, 0, x_commit, x_wait, NULL, x_keyboard, 0, x_pointer, x_held};
Overlay *overlay_x11(void) { return &x_overlay; }
