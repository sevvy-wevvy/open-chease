#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app.h"

typedef void GDBusConnection;
typedef void GVariant;
typedef void GstElement;
typedef void GstSample;
typedef void GstBuffer;
typedef void GstCaps;
typedef void GstStructure;
typedef struct {
    uint32_t domain;
    int code;
    char *message;
} GError;
typedef struct {
    void *memory;
    int flags;
    uint8_t *data;
    size_t size;
    size_t maxsize;
    void *user_data[4];
    void *reserved[4];
} GstMapInfo;
typedef void (*SignalCb)(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *, void *);

static struct {
    GDBusConnection *(*bus_get_sync)(int, void *, GError **);
    GVariant *(*call_sync)(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *,
                           const void *, int, int, void *, GError **);
    GVariant *(*new_parsed)(const char *, ...);
    void (*variant_get)(GVariant *, const char *, ...);
    GVariant *(*child)(GVariant *, size_t);
    size_t (*n_children)(GVariant *);
    void (*unref)(GVariant *);
    const char *(*get_string)(GVariant *, size_t *);
    int (*lookup)(GVariant *, const char *, const char *, ...);
    unsigned (*subscribe)(GDBusConnection *, const char *, const char *, const char *, const char *, const char *, int,
                          SignalCb, void *, void *);
    int (*iteration)(void *, int);
    void (*error_free)(GError *);

    void (*gst_init)(int *, char ***);
    GstElement *(*parse_launch)(const char *, GError **);
    GstElement *(*bin_get_by_name)(GstElement *, const char *);
    int (*set_state)(GstElement *, int);
    GstSample *(*try_pull_sample)(GstElement *, uint64_t);
    GstBuffer *(*sample_get_buffer)(GstSample *);
    GstCaps *(*sample_get_caps)(GstSample *);
    GstStructure *(*caps_get_structure)(GstCaps *, unsigned);
    int (*structure_get_int)(GstStructure *, const char *, int *);
    int (*buffer_map)(GstBuffer *, GstMapInfo *, int);
    void (*buffer_unmap)(GstBuffer *, GstMapInfo *);
    void (*mini_unref)(void *);
} G;

typedef struct {
    char connector[64];
    char stream_path[256];
    unsigned node;
    int x, y, w, h;
    GstElement *pipe, *sink;
    uint8_t *buf;
    int fw, fh;
} Stream;

static GDBusConnection *bus;
static char session_path[256];
static Stream streams[MAX_OUTPUTS];
static int nstreams;

static int load(void)
{
    void *gio = dlopen("libgio-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    void *glib = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    void *gst = dlopen("libgstreamer-1.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    void *app = dlopen("libgstapp-1.0.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!gio || !glib) {
        fprintf(stderr, "gnome capture: cant find glib/gio\n");
        return 0;
    }
    if (!gst || !app) {
        fprintf(stderr, "gnome capture: cant find gstreamer, install libgstreamer-1.0 and libgstapp-1.0\n");
        return 0;
    }
#define L(lib, field, name)                                                   \
    do {                                                                      \
        *(void **)(&G.field) = dlsym(lib, name);                              \
        if (!G.field) {                                                       \
            fprintf(stderr, "gnome capture: missing %s\n", name);             \
            return 0;                                                         \
        }                                                                     \
    } while (0)
    L(gio, bus_get_sync, "g_bus_get_sync");
    L(gio, call_sync, "g_dbus_connection_call_sync");
    L(gio, subscribe, "g_dbus_connection_signal_subscribe");
    L(glib, new_parsed, "g_variant_new_parsed");
    L(glib, variant_get, "g_variant_get");
    L(glib, child, "g_variant_get_child_value");
    L(glib, n_children, "g_variant_n_children");
    L(glib, unref, "g_variant_unref");
    L(glib, get_string, "g_variant_get_string");
    L(glib, lookup, "g_variant_lookup");
    L(glib, iteration, "g_main_context_iteration");
    L(glib, error_free, "g_error_free");
    L(gst, gst_init, "gst_init");
    L(gst, parse_launch, "gst_parse_launch");
    L(gst, bin_get_by_name, "gst_bin_get_by_name");
    L(gst, set_state, "gst_element_set_state");
    L(gst, sample_get_buffer, "gst_sample_get_buffer");
    L(gst, sample_get_caps, "gst_sample_get_caps");
    L(gst, caps_get_structure, "gst_caps_get_structure");
    L(gst, structure_get_int, "gst_structure_get_int");
    L(gst, buffer_map, "gst_buffer_map");
    L(gst, buffer_unmap, "gst_buffer_unmap");
    L(gst, mini_unref, "gst_mini_object_unref");
    L(app, try_pull_sample, "gst_app_sink_try_pull_sample");
#undef L
    return 1;
}

static GVariant *call(const char *dest, const char *path, const char *iface, const char *method, GVariant *args)
{
    GError *err = NULL;
    GVariant *r = G.call_sync(bus, dest, path, iface, method, args, NULL, 0, 5000, NULL, &err);
    if (!r) {
        fprintf(stderr, "gnome capture: %s.%s failed: %s\n", iface, method, err ? err->message : "?");
        if (err) G.error_free(err);
    }
    return r;
}

static void on_stream_added(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                            const char *sig, GVariant *params, void *user)
{
    for (int i = 0; i < nstreams; i++)
        if (!strcmp(streams[i].stream_path, path)) G.variant_get(params, "(u)", &streams[i].node);
}

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

// skip the portal, no share-screen popup every launch
static int gnome_init(Backend *b)
{
    if (!load()) return 0;
    GError *err = NULL;
    bus = G.bus_get_sync(2, NULL, &err); // G_BUS_TYPE_SESSION
    if (!bus) return 0;
    GVariant *st = call("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
                        "org.gnome.Mutter.DisplayConfig", "GetCurrentState", NULL);
    if (!st) return 0;
    GVariant *mons = G.child(st, 1);
    size_t nm = G.n_children(mons);
    for (size_t i = 0; i < nm && nstreams < MAX_OUTPUTS; i++) {
        GVariant *m = G.child(mons, i);
        GVariant *spec = G.child(m, 0);
        GVariant *conn = G.child(spec, 0);
        snprintf(streams[nstreams].connector, 64, "%s", G.get_string(conn, NULL));
        G.unref(conn);
        G.unref(spec);
        GVariant *modes = G.child(m, 1);
        int current = 0;
        size_t nmodes = G.n_children(modes);
        for (size_t k = 0; k < nmodes; k++) {
            GVariant *mode = G.child(modes, k);
            GVariant *props = G.child(mode, 6);
            int is_cur = 0;
            if (G.lookup(props, "is-current", "b", &is_cur) && is_cur) current = 1;
            G.unref(props);
            G.unref(mode);
        }
        G.unref(modes);
        G.unref(m);
        if (current) nstreams++;
    }
    G.unref(mons);
    G.unref(st);
    if (!nstreams) return 0;

    GVariant *r = call("org.gnome.Mutter.ScreenCast", "/org/gnome/Mutter/ScreenCast", "org.gnome.Mutter.ScreenCast",
                       "CreateSession", G.new_parsed("(@a{sv} {},)"));
    if (!r) return 0;
    const char *sp;
    G.variant_get(r, "(&o)", &sp);
    snprintf(session_path, sizeof(session_path), "%s", sp);
    G.unref(r);
    for (int i = 0; i < nstreams; i++) {
        r = call("org.gnome.Mutter.ScreenCast", session_path, "org.gnome.Mutter.ScreenCast.Session", "RecordMonitor",
                 G.new_parsed("(%s, {'cursor-mode': <uint32 0>})", streams[i].connector));
        if (!r) return 0;
        G.variant_get(r, "(&o)", &sp);
        snprintf(streams[i].stream_path, sizeof(streams[i].stream_path), "%s", sp);
        G.unref(r);
        G.subscribe(bus, "org.gnome.Mutter.ScreenCast", "org.gnome.Mutter.ScreenCast.Stream", "PipeWireStreamAdded",
                    streams[i].stream_path, NULL, 0, on_stream_added, NULL, NULL);
    }
    r = call("org.gnome.Mutter.ScreenCast", session_path, "org.gnome.Mutter.ScreenCast.Session", "Start", NULL);
    if (!r) return 0;
    G.unref(r);
    long deadline = now_ms() + 5000;
    for (;;) {
        int missing = 0;
        for (int i = 0; i < nstreams; i++)
            if (!streams[i].node) missing = 1;
        if (!missing) break;
        if (now_ms() > deadline) {
            fprintf(stderr, "gnome capture: mutter never sent a pipewire stream\n");
            return 0;
        }
        if (!G.iteration(NULL, 0)) usleep(10000);
    }
    G.gst_init(NULL, NULL);
    for (int i = 0; i < nstreams; i++) {
        Stream *s = &streams[i];
        r = call("org.gnome.Mutter.ScreenCast", s->stream_path, "org.freedesktop.DBus.Properties", "Get",
                 G.new_parsed("('org.gnome.Mutter.ScreenCast.Stream', 'Parameters')"));
        if (r) {
            GVariant *v = G.child(r, 0);
            GVariant *dict = NULL;
            G.variant_get(v, "v", &dict);
            if (dict) {
                G.lookup(dict, "position", "(ii)", &s->x, &s->y);
                G.lookup(dict, "size", "(ii)", &s->w, &s->h);
                G.unref(dict);
            }
            G.unref(v);
            G.unref(r);
        }
        char desc[512];
        snprintf(desc, sizeof(desc),
                 "pipewiresrc path=%u always-copy=true ! videorate drop-only=true max-rate=2 ! videoconvert ! "
                 "video/x-raw,format=BGRx ! appsink name=sink max-buffers=1 drop=true sync=false",
                 s->node);
        GError *e = NULL;
        s->pipe = G.parse_launch(desc, &e);
        if (!s->pipe) {
            fprintf(stderr, "gnome capture: couldnt start gstreamer: %s\n", e ? e->message : "?");
            return 0;
        }
        s->sink = G.bin_get_by_name(s->pipe, "sink");
        G.set_state(s->pipe, 4); // GST_STATE_PLAYING
        snprintf(b->out[i].name, 64, "%.63s", s->connector);
    }
    b->nout = nstreams;
    for (int i = 0; i < nstreams; i++)
        b->out[i].logical = (Rect){streams[i].x, streams[i].y, streams[i].w, streams[i].h};
    return 1;
}

static int pull(Stream *s, uint64_t timeout_ns)
{
    GstSample *smp = G.try_pull_sample(s->sink, timeout_ns);
    if (!smp) return 0;
    GstCaps *caps = G.sample_get_caps(smp);
    GstStructure *st = caps ? G.caps_get_structure(caps, 0) : NULL;
    int w = 0, h = 0;
    if (st) {
        G.structure_get_int(st, "width", &w);
        G.structure_get_int(st, "height", &h);
    }
    GstBuffer *buf = G.sample_get_buffer(smp);
    GstMapInfo mi;
    if (w > 0 && h > 0 && buf && G.buffer_map(buf, &mi, 1)) { // GST_MAP_READ
        size_t need = (size_t)w * h * 4, stride = (size_t)w * 4;
        if (mi.size % (size_t)h == 0 && mi.size / (size_t)h > stride) stride = mi.size / (size_t)h; // padded rows from the gpu
        if (mi.size >= stride * (h - 1) + (size_t)w * 4) {
            if (w != s->fw || h != s->fh) {
                free(s->buf);
                s->buf = malloc(need);
                s->fw = w;
                s->fh = h;
            }
            if (s->buf)
                for (int y = 0; y < h; y++) memcpy(s->buf + (size_t)y * w * 4, (uint8_t *)mi.data + y * stride, (size_t)w * 4);
        }
        G.buffer_unmap(buf, &mi);
    }
    G.mini_unref(smp);
    return 1;
}

static int gnome_capture(Backend *b)
{
    G.iteration(NULL, 0);
    int got = 0;
    for (int i = 0; i < nstreams; i++) {
        Stream *s = &streams[i];
        uint64_t timeout = s->buf ? 0 : 2000000000ULL;
        while (pull(s, timeout)) timeout = 0;
        Output *o = &b->out[i];
        if (!s->buf) {
            o->have_frame = 0;
            continue;
        }
        if (s->w <= 0 || s->h <= 0) {
            s->w = s->fw;
            s->h = s->fh;
        }
        o->logical = (Rect){s->x, s->y, s->w, s->h};
        o->frame.px = s->buf;
        o->frame.w = s->fw;
        o->frame.h = s->fh;
        o->frame.stride = s->fw * 4;
        o->frame.bgr = 1;
        o->have_frame = 1;
        got++;
    }
    return got > 0;
}

static Backend gnome_backend = {.name = "mutter-screencast", .init = gnome_init, .capture = gnome_capture};
Backend *backend_gnome(void) { return &gnome_backend; }
