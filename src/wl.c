#define _GNU_SOURCE
#include <errno.h>
#include <dlfcn.h>
#include <poll.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "app.h"
#include "proto/wlr-layer-shell-unstable-v1.h"
#include "proto/wlr-screencopy-unstable-v1.h"
#include "proto/xdg-output-unstable-v1.h"

typedef struct {
    struct wl_buffer *buf;
    void *data;
    size_t size;
    int w, h, stride, busy;
    uint32_t format;
} ShmBuf;

typedef struct {
    struct wl_output *output;
    uint32_t global_name;
    struct zxdg_output_v1 *xdg;
    int lx, ly, lw, lh, have_logical;
    char name[64];

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer;
    int cw, ch, configured;
    ShmBuf draw[2];
    int pend[2][4], pend_all[2];
    Canvas canvas;
    uint32_t *canvas_px;

    struct zwlr_screencopy_frame_v1 *frame;
    ShmBuf cap;
    uint32_t cap_format;
    int have_format; // cap_format alone cant tell, ARGB8888 is 0
    int cap_w, cap_h, cap_stride, cap_state, y_invert, warned;
    uint32_t offered[8];
    int noffered;
    uint8_t *flip, *conv;
} WlOut;

static struct wl_display *dpy;
static struct wl_registry *reg;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct zwlr_screencopy_manager_v1 *screencopy;
static uint32_t screencopy_version;
static struct zxdg_output_manager_v1 *xdg_output_mgr;
static struct wl_seat *seat;
static struct wl_pointer *pointer;
static struct wl_keyboard *keyboard;
static int key_q[32], nkey_q, scroll_acc, scrolled;
static double scroll_lx, scroll_ly;
static WlOut outs[MAX_OUTPUTS];
static int nouts;
static struct wl_surface *ptr_surface;
static double ptr_x, ptr_y;
static int clicked;
static double click_lx, click_ly;
static int inited;

static void buf_release(void *data, struct wl_buffer *buf)
{
    ((ShmBuf *)data)->busy = 0;
}
static const struct wl_buffer_listener buf_listener = {buf_release};

static int make_shm(ShmBuf *b, int w, int h, int stride, uint32_t format)
{
    size_t size = (size_t)stride * h;
    int fd = memfd_create("open-chease", MFD_CLOEXEC);
    if (fd < 0) return 0;
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return 0;
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return 0;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    b->buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);
    b->data = data;
    b->size = size;
    b->w = w;
    b->h = h;
    b->stride = stride;
    b->format = format;
    b->busy = 0;
    wl_buffer_add_listener(b->buf, &buf_listener, b);
    return 1;
}

static void free_shm(ShmBuf *b)
{
    if (b->buf) wl_buffer_destroy(b->buf);
    if (b->data) munmap(b->data, b->size);
    memset(b, 0, sizeof(*b));
}

static void xo_pos(void *d, struct zxdg_output_v1 *x, int32_t px, int32_t py)
{
    WlOut *o = d;
    o->lx = px;
    o->ly = py;
}
static void xo_size(void *d, struct zxdg_output_v1 *x, int32_t w, int32_t h)
{
    WlOut *o = d;
    o->lw = w;
    o->lh = h;
    o->have_logical = 1;
}
static void xo_done(void *d, struct zxdg_output_v1 *x) {}
static void xo_name(void *d, struct zxdg_output_v1 *x, const char *n)
{
    snprintf(((WlOut *)d)->name, 64, "%s", n);
}
static void xo_desc(void *d, struct zxdg_output_v1 *x, const char *n) {}
static const struct zxdg_output_v1_listener xo_listener = {xo_pos, xo_size, xo_done, xo_name, xo_desc};

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    ptr_surface = s;
    ptr_x = wl_fixed_to_double(x);
    ptr_y = wl_fixed_to_double(y);
}
static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
    ptr_surface = NULL;
}
static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
    ptr_x = wl_fixed_to_double(x);
    ptr_y = wl_fixed_to_double(y);
}
static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
    if (state != WL_POINTER_BUTTON_STATE_PRESSED || !ptr_surface) return;
    for (int i = 0; i < nouts; i++)
        if (outs[i].surface == ptr_surface) {
            clicked = 1;
            click_lx = outs[i].lx + ptr_x;
            click_ly = outs[i].ly + ptr_y;
        }
}
static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
    if (a != WL_POINTER_AXIS_VERTICAL_SCROLL || !ptr_surface) return;
    scroll_acc += v; // wl_fixed, keep the fractions so slow touchpad scrolls still add up
    for (int i = 0; i < nouts; i++)
        if (outs[i].surface == ptr_surface) scroll_lx = outs[i].lx + ptr_x, scroll_ly = outs[i].ly + ptr_y;
    if (scroll_acc >= 10 * 256 || scroll_acc <= -10 * 256) scrolled = scroll_acc > 0 ? 1 : -1, scroll_acc = 0;
}
static void ptr_frame(void *d, struct wl_pointer *p) {}
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s) {}
static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {}
static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}
static void ptr_axis_v120(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}
static void ptr_axis_rel(void *d, struct wl_pointer *p, uint32_t a, uint32_t v) {}
static const struct wl_pointer_listener ptr_listener = {ptr_enter, ptr_leave, ptr_motion, ptr_button, ptr_axis,
                                                        ptr_frame, ptr_axis_source, ptr_axis_stop, ptr_axis_discrete,
                                                        ptr_axis_v120, ptr_axis_rel};

static struct {
    void *(*context_new)(int);
    void *(*keymap_new_from_string)(void *, const char *, int, int);
    void *(*state_new)(void *);
    int (*key_get_utf8)(void *, uint32_t, char *, size_t);
    int (*update_mask)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    void (*state_unref)(void *);
    void (*keymap_unref)(void *);
    void *ctx, *keymap, *state;
    int tried;
} xkb;

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size)
{
    if (!xkb.tried) {
        xkb.tried = 1;
        void *h = dlopen("libxkbcommon.so.0", RTLD_NOW);
        if (h) {
            xkb.context_new = (void *(*)(int))dlsym(h, "xkb_context_new");
            xkb.keymap_new_from_string = (void *(*)(void *, const char *, int, int))dlsym(h, "xkb_keymap_new_from_string");
            xkb.state_new = (void *(*)(void *))dlsym(h, "xkb_state_new");
            xkb.key_get_utf8 = (int (*)(void *, uint32_t, char *, size_t))dlsym(h, "xkb_state_key_get_utf8");
            xkb.update_mask = (int (*)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))dlsym(h, "xkb_state_update_mask");
            xkb.state_unref = (void (*)(void *))dlsym(h, "xkb_state_unref");
            xkb.keymap_unref = (void (*)(void *))dlsym(h, "xkb_keymap_unref");
            if (xkb.context_new && xkb.keymap_new_from_string && xkb.state_new && xkb.key_get_utf8 && xkb.update_mask)
                xkb.ctx = xkb.context_new(0);
        }
    }
    if (xkb.ctx && fmt == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            void *km = xkb.keymap_new_from_string(xkb.ctx, map, 1, 0);
            munmap(map, size);
            if (km) {
                if (xkb.state) xkb.state_unref(xkb.state);
                if (xkb.keymap) xkb.keymap_unref(xkb.keymap);
                xkb.keymap = km;
                xkb.state = xkb.state_new(km);
            }
        }
    }
    close(fd);
}
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s, struct wl_array *keys) {}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s) {}
static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state)
{
    static const char map[] = // no libxkbcommon fallback, plain us evdev
        "\0\x1b" "1234567890-=\b\0" "qwertyuiop[]\r\0" "asdfghjkl;'`\0\\" "zxcvbnm,./\0*\0 ";
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || nkey_q >= 32) return;
    if (xkb.state) {
        char buf[8] = {0};
        if (xkb.key_get_utf8(xkb.state, key + 8, buf, sizeof(buf)) == 1) key_q[nkey_q++] = (unsigned char)buf[0];
        return;
    }
    if (key >= sizeof(map) - 1 || !map[key]) return;
    key_q[nkey_q++] = (unsigned char)map[key];
}
static void kb_mods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t g)
{
    if (xkb.state) xkb.update_mask(xkb.state, dep, lat, lock, 0, 0, g);
}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener kb_listener = {kb_keymap, kb_enter, kb_leave, kb_key, kb_mods, kb_repeat};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
        keyboard = wl_seat_get_keyboard(s);
        wl_keyboard_add_listener(keyboard, &kb_listener, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
        pointer = wl_seat_get_pointer(s);
        wl_pointer_add_listener(pointer, &ptr_listener, NULL);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer) {
        if (wl_pointer_get_version(pointer) >= 3)
            wl_pointer_release(pointer);
        else
            wl_pointer_destroy(pointer);
        pointer = NULL;
        ptr_surface = NULL;
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {}
static const struct wl_seat_listener seat_listener = {seat_caps, seat_name};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    if (!strcmp(iface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (!strcmp(iface, wl_shm_interface.name)) {
        shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    } else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
        layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, version < 4 ? version : 4);
    } else if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name)) {
        screencopy_version = version < 3 ? version : 3;
        screencopy = wl_registry_bind(r, name, &zwlr_screencopy_manager_v1_interface, screencopy_version);
    } else if (!strcmp(iface, zxdg_output_manager_v1_interface.name)) {
        xdg_output_mgr = wl_registry_bind(r, name, &zxdg_output_manager_v1_interface, version < 3 ? version : 3);
    } else if (!strcmp(iface, wl_seat_interface.name) && !seat) {
        seat = wl_registry_bind(r, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    } else if (!strcmp(iface, wl_output_interface.name) && nouts < MAX_OUTPUTS) {
        WlOut *o = &outs[nouts++];
        memset(o, 0, sizeof(*o));
        o->global_name = name;
        o->output = wl_registry_bind(r, name, &wl_output_interface, version < 3 ? version : 3);
        snprintf(o->name, sizeof(o->name), "output-%u", name);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = {reg_global, reg_remove};

int backend_wl_init_shared(void)
{
    if (inited) return inited > 0;
    inited = -1;
    dpy = wl_display_connect(NULL);
    if (!dpy) return 0;
    reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!compositor || !shm || !nouts) return 0;
    if (xdg_output_mgr)
        for (int i = 0; i < nouts; i++) {
            outs[i].xdg = zxdg_output_manager_v1_get_xdg_output(xdg_output_mgr, outs[i].output);
            zxdg_output_v1_add_listener(outs[i].xdg, &xo_listener, &outs[i]);
        }
    wl_display_roundtrip(dpy);
    wl_display_roundtrip(dpy);
    inited = 1;
    return 1;
}

static int is_10bit(uint32_t f)
{
    return f == WL_SHM_FORMAT_XRGB2101010 || f == WL_SHM_FORMAT_ARGB2101010 || f == WL_SHM_FORMAT_XBGR2101010 ||
           f == WL_SHM_FORMAT_ABGR2101010;
}

static void fr_buffer(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride)
{
    WlOut *o = d;
    if (o->noffered < 8) o->offered[o->noffered++] = fmt;
    if (o->have_format) return;
    if (fmt != WL_SHM_FORMAT_XRGB8888 && fmt != WL_SHM_FORMAT_ARGB8888 && fmt != WL_SHM_FORMAT_XBGR8888 &&
        fmt != WL_SHM_FORMAT_ABGR8888 && !is_10bit(fmt))
        return;
    o->cap_format = fmt;
    o->have_format = 1;
    o->cap_w = (int)w;
    o->cap_h = (int)h;
    o->cap_stride = (int)stride;
    if (screencopy_version < 3) {
        if (o->cap.w != (int)w || o->cap.h != (int)h || o->cap.format != fmt || o->cap.stride != (int)stride) {
            free_shm(&o->cap);
            if (!make_shm(&o->cap, (int)w, (int)h, (int)stride, fmt)) {
                o->cap_state = -1;
                return;
            }
        }
        zwlr_screencopy_frame_v1_copy(f, o->cap.buf);
    }
}
static void fr_flags(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t flags)
{
    ((WlOut *)d)->y_invert = flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;
}
static void fr_ready(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t a, uint32_t b, uint32_t c)
{
    WlOut *o = d;
    o->cap_state = 1;
    zwlr_screencopy_frame_v1_destroy(f);
    o->frame = NULL;
}
static void fr_failed(void *d, struct zwlr_screencopy_frame_v1 *f)
{
    WlOut *o = d;
    o->cap_state = -1;
    zwlr_screencopy_frame_v1_destroy(f);
    o->frame = NULL;
    if (!o->warned++) fprintf(stderr, "the compositor wont let me grab %s\n", o->name);
}
static void fr_damage(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {}
static void fr_dmabuf(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t fmt, uint32_t w, uint32_t h) {}
static void fr_buffer_done(void *d, struct zwlr_screencopy_frame_v1 *f)
{
    WlOut *o = d;
    if (!o->have_format) {
        o->cap_state = -1;
        if (!o->warned++) {
            fprintf(stderr, "%s only offers screen formats i cant read:", o->name);
            for (int i = 0; i < o->noffered; i++) {
                uint32_t f = o->offered[i];
                if (f <= 1) fprintf(stderr, " %s", f ? "XRGB8888" : "ARGB8888");
                else fprintf(stderr, " %.4s", (char *)&f);
            }
            fprintf(stderr, "\n");
        }
        return;
    }
    if (o->cap.w != o->cap_w || o->cap.h != o->cap_h || o->cap.format != o->cap_format || o->cap.stride != o->cap_stride) {
        free_shm(&o->cap);
        if (!make_shm(&o->cap, o->cap_w, o->cap_h, o->cap_stride, o->cap_format)) {
            o->cap_state = -1;
            return;
        }
    }
    zwlr_screencopy_frame_v1_copy(f, o->cap.buf);
}
static const struct zwlr_screencopy_frame_v1_listener fr_listener = {fr_buffer, fr_flags, fr_ready, fr_failed,
                                                                     fr_damage, fr_dmabuf, fr_buffer_done};

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int dispatch_for(int timeout_ms)
{
    while (wl_display_prepare_read(dpy) != 0) wl_display_dispatch_pending(dpy);
    wl_display_flush(dpy);
    struct pollfd pfd = {wl_display_get_fd(dpy), POLLIN, 0};
    int r = poll(&pfd, 1, timeout_ms);
    if (r > 0) {
        if (wl_display_read_events(dpy) < 0) return -1;
    } else {
        wl_display_cancel_read(dpy);
    }
    if (wl_display_dispatch_pending(dpy) < 0) return -1;
    return r;
}

static int wl_cap_init(Backend *b)
{
    if (!backend_wl_init_shared() || !screencopy) return 0;
    b->nout = nouts;
    for (int i = 0; i < nouts; i++) snprintf(b->out[i].name, 64, "%s", outs[i].name);
    return 1;
}

static int wl_capture(Backend *b)
{
    for (int i = 0; i < nouts; i++) {
        WlOut *o = &outs[i];
        o->cap_state = 0;
        o->cap_format = 0;
        o->have_format = 0;
        o->noffered = 0;
        o->y_invert = 0;
        o->frame = zwlr_screencopy_manager_v1_capture_output(screencopy, 0, o->output);
        zwlr_screencopy_frame_v1_add_listener(o->frame, &fr_listener, o);
    }
    long deadline = now_ms() + 600;
    for (;;) {
        int pending = 0;
        for (int i = 0; i < nouts; i++)
            if (outs[i].cap_state == 0) pending = 1;
        if (!pending) break;
        long left = deadline - now_ms();
        if (left <= 0) {
            for (int i = 0; i < nouts; i++)
                if (outs[i].cap_state == 0 && !outs[i].warned++) fprintf(stderr, "%s never sent a frame\n", outs[i].name);
            break;
        }
        if (dispatch_for((int)left) < 0) return 0;
    }
    int got = 0;
    for (int i = 0; i < nouts; i++) {
        WlOut *o = &outs[i];
        Output *bo = &b->out[i];
        if (o->frame) {
            zwlr_screencopy_frame_v1_destroy(o->frame);
            o->frame = NULL;
        }
        bo->logical = (Rect){o->lx, o->ly, o->have_logical ? o->lw : o->cap_w, o->have_logical ? o->lh : o->cap_h};
        if (o->cap_state != 1) {
            bo->have_frame = 0;
            continue;
        }
        uint8_t *px = o->cap.data;
        if (o->y_invert) {
            size_t sz = (size_t)o->cap_stride * o->cap_h;
            o->flip = realloc(o->flip, sz);
            for (int y = 0; y < o->cap_h; y++)
                memcpy(o->flip + (size_t)y * o->cap_stride, px + (size_t)(o->cap_h - 1 - y) * o->cap_stride,
                       (size_t)o->cap_stride);
            px = o->flip;
        }
        int stride = o->cap_stride, bgr = o->cap_format == WL_SHM_FORMAT_XRGB8888 || o->cap_format == WL_SHM_FORMAT_ARGB8888;
        if (is_10bit(o->cap_format)) { // 10 bit laptop panels
            int rb = o->cap_format == WL_SHM_FORMAT_XRGB2101010 || o->cap_format == WL_SHM_FORMAT_ARGB2101010;
            o->conv = realloc(o->conv, (size_t)o->cap_w * o->cap_h * 4);
            for (int y = 0; y < o->cap_h; y++) {
                const uint32_t *src = (const uint32_t *)(px + (size_t)y * o->cap_stride);
                uint8_t *dst = o->conv + (size_t)y * o->cap_w * 4;
                for (int x = 0; x < o->cap_w; x++) {
                    uint32_t v = src[x];
                    uint8_t hi = (uint8_t)(v >> 22), mid = (uint8_t)(v >> 12), lo = (uint8_t)(v >> 2);
                    dst[x * 4 + 0] = rb ? lo : hi;
                    dst[x * 4 + 1] = mid;
                    dst[x * 4 + 2] = rb ? hi : lo;
                    dst[x * 4 + 3] = 255;
                }
            }
            px = o->conv;
            stride = o->cap_w * 4;
            bgr = 1;
        }
        bo->frame.px = px;
        bo->frame.w = o->cap_w;
        bo->frame.h = o->cap_h;
        bo->frame.stride = stride;
        bo->frame.bgr = bgr;
        bo->have_frame = 1;
        got++;
    }
    return got > 0;
}

static Backend wl_backend = {.name = "wlr-screencopy", .init = wl_cap_init, .capture = wl_capture};
Backend *backend_wl(void) { return &wl_backend; }

static void ls_configure(void *d, struct zwlr_layer_surface_v1 *l, uint32_t serial, uint32_t w, uint32_t h)
{
    WlOut *o = d;
    zwlr_layer_surface_v1_ack_configure(l, serial);
    if ((int)w != o->cw || (int)h != o->ch) {
        o->cw = (int)w;
        o->ch = (int)h;
        free_shm(&o->draw[0]);
        free_shm(&o->draw[1]);
        memset(&o->canvas, 0, sizeof(o->canvas));
        free(o->canvas_px);
        o->canvas_px = calloc((size_t)w * h, 4);
    }
    o->configured = 1;
}
static void ls_closed(void *d, struct zwlr_layer_surface_v1 *l)
{
    ((WlOut *)d)->configured = 0;
}
static const struct zwlr_layer_surface_v1_listener ls_listener = {ls_configure, ls_closed};

static int wl_ov_init(Overlay *ov, Backend *cap)
{
    if (!backend_wl_init_shared() || !layer_shell) return 0;
    for (int i = 0; i < nouts; i++) {
        WlOut *o = &outs[i];
        o->surface = wl_compositor_create_surface(compositor);
        o->layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, o->surface, o->output,
                                                         ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "open-chease");
        zwlr_layer_surface_v1_add_listener(o->layer, &ls_listener, o);
        zwlr_layer_surface_v1_set_anchor(o->layer, 15); // all 4 edges
        zwlr_layer_surface_v1_set_exclusive_zone(o->layer, -1); // or waybar pushes it around
        zwlr_layer_surface_v1_set_keyboard_interactivity(o->layer, 0);
        zwlr_layer_surface_v1_set_size(o->layer, 0, 0);
        struct wl_region *empty = wl_compositor_create_region(compositor);
        wl_surface_set_input_region(o->surface, empty);
        wl_region_destroy(empty);
        wl_surface_commit(o->surface);
    }
    wl_display_roundtrip(dpy);
    wl_display_roundtrip(dpy);
    ov->ncanvas = nouts;
    return 1;
}

static Canvas *wl_canvas(Overlay *ov, int i)
{
    WlOut *o = &outs[i];
    if (!o->configured || !o->canvas_px) return NULL;
    o->canvas.px = o->canvas_px;
    o->canvas.w = o->cw;
    o->canvas.h = o->ch;
    o->canvas.ox = o->lx;
    o->canvas.oy = o->ly;
    o->canvas.scale = 1;
    return &o->canvas;
}

static void wl_commit(Overlay *ov, const Rect *hits, int nhits)
{
    for (int i = 0; i < nouts; i++) {
        WlOut *o = &outs[i];
        if (!o->configured || !o->canvas_px) continue;
        ShmBuf *b = NULL;
        int x0, y0, x1, y1, dmg = canvas_damage(&o->canvas, &x0, &y0, &x1, &y1), bi = 0;
        for (int k = 0; k < 2 && dmg; k++) {
            int *q = o->pend[k];
            if (q[2] <= q[0]) q[0] = x0, q[1] = y0, q[2] = x1, q[3] = y1;
            if (x0 < q[0]) q[0] = x0;
            if (y0 < q[1]) q[1] = y0;
            if (x1 > q[2]) q[2] = x1;
            if (y1 > q[3]) q[3] = y1;
        }
        for (int k = 0; k < 2; k++) {
            if (!o->draw[k].buf && make_shm(&o->draw[k], o->cw, o->ch, o->cw * 4, WL_SHM_FORMAT_ARGB8888)) o->pend_all[k] = 1;
            if (o->draw[k].buf && !o->draw[k].busy) {
                b = &o->draw[k];
                bi = k;
                break;
            }
        }
        if (!b) continue;
        int *q = o->pend[bi], full = o->pend_all[bi];
        if (full)
            memcpy(b->data, o->canvas_px, (size_t)o->cw * o->ch * 4);
        else
            for (int y = q[1]; y < q[3]; y++)
                memcpy((uint32_t *)b->data + (size_t)y * o->cw + q[0], o->canvas_px + (size_t)y * o->cw + q[0], (size_t)(q[2] - q[0]) * 4);
        q[0] = q[1] = q[2] = q[3] = 0;
        o->pend_all[bi] = 0;
        b->busy = 1;
        struct wl_region *region = wl_compositor_create_region(compositor);
        for (int h = 0; h < nhits; h++)
            wl_region_add(region, (int)(hits[h].x - o->lx), (int)(hits[h].y - o->ly), (int)hits[h].w, (int)hits[h].h);
        wl_surface_set_input_region(o->surface, region);
        wl_region_destroy(region);
        wl_surface_attach(o->surface, b->buf, 0, 0);
        if (full)
            wl_surface_damage(o->surface, 0, 0, o->cw, o->ch);
        else if (dmg)
            wl_surface_damage(o->surface, x0, y0, x1 - x0, y1 - y0);
        wl_surface_commit(o->surface);
    }
    wl_display_flush(dpy);
}

static int wl_wait(Overlay *ov, int timeout_ms, double *cx, double *cy)
{
    long deadline = now_ms() + timeout_ms;
    for (;;) {
        if (clicked) {
            clicked = 0;
            *cx = click_lx;
            *cy = click_ly;
            return 1;
        }
        if (nkey_q) {
            ov->key = key_q[0];
            memmove(key_q, key_q + 1, sizeof(int) * (size_t)--nkey_q);
            return 2;
        }
        if (scrolled) {
            ov->key = scrolled;
            scrolled = 0;
            *cx = scroll_lx;
            *cy = scroll_ly;
            return 3;
        }
        long left = deadline - now_ms();
        if (left <= 0) return 0;
        if (dispatch_for((int)left) < 0) return -1;
    }
}

static void wl_keyboard_mode(Overlay *ov, int on)
{
    for (int i = 0; i < nouts; i++) {
        if (!outs[i].layer) continue;
        zwlr_layer_surface_v1_set_keyboard_interactivity(outs[i].layer, on ? 1 : 0);
        wl_surface_commit(outs[i].surface);
    }
    nkey_q = 0;
    wl_display_flush(dpy);
}

static Overlay wl_overlay = {"layer-shell", wl_ov_init, wl_canvas, 0, wl_commit, wl_wait, NULL, wl_keyboard_mode, 0};
Overlay *overlay_wl(void) { return &wl_overlay; }
