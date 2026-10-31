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
} GstMapInfo; // has to match the real one byte for byte
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
    void (*obj_unref)(void *);
    void (*unsubscribe)(GDBusConnection *, unsigned);
} G;

typedef struct {
    char connector[64];
    char stream_path[256];
    unsigned node;
    int x, y, w, h;
    GstElement *pipe, *sink;
    uint8_t *buf;
    int fw, fh;
    unsigned sub;
} Stream;

static GDBusConnection *bus;
static char session_path[256];
static Stream streams[MAX_OUTPUTS];
static int nstreams, changed, loaded;
static long changed_at;
static unsigned session_sub;

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
    L(gst, obj_unref, "gst_object_unref");
    L(gio, unsubscribe, "g_dbus_connection_signal_unsubscribe");
    L(app, try_pull_sample, "gst_app_sink_try_pull_sample");
#undef L
    return 1;
}

static GVariant *call(const char *dest, const char *path, const char *iface, const char *method, GVariant *args)
{
    GError *err = NULL;
    GVariant *r = G.call_sync(bus, dest, path, iface, method, args, NULL, 0, 5000, NULL, &err); // 0 = no flags, 5s timeout
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
        if (!strcmp(streams[i].stream_path, path)) G.variant_get(params, "(u)", &streams[i].node); // the pipewire node id
}

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void on_change(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                      const char *sig, GVariant *params, void *user)
{
    changed = 1;
    changed_at = now_ms();
}

static void stop_streams(void)
{
    for (int i = 0; i < nstreams; i++) {
        Stream *s = &streams[i];
        if (s->sub) G.unsubscribe(bus, s->sub);
        if (s->pipe) {
            G.set_state(s->pipe, 1); // GST_STATE_NULL, before the unref
            if (s->sink) G.obj_unref(s->sink);
            G.obj_unref(s->pipe);
        }
        free(s->buf);
    }
    if (session_sub) G.unsubscribe(bus, session_sub); // before Stop, or its Closed signal kicks off another restart
    session_sub = 0;
    if (*session_path) {
        GError *err = NULL;
        GVariant *r = G.call_sync(bus, "org.gnome.Mutter.ScreenCast", session_path, "org.gnome.Mutter.ScreenCast.Session",
                                  "Stop", NULL, NULL, 0, 2000, NULL, &err);
        if (r) G.unref(r);
        if (err) G.error_free(err);
    }
    *session_path = 0;
    memset(streams, 0, sizeof(streams));
    nstreams = 0;
}

static int start_streams(Backend *b)
{
    GVariant *st = call("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
                        "org.gnome.Mutter.DisplayConfig", "GetCurrentState", NULL);
    if (!st) return 0;
    GVariant *mons = G.child(st, 1); // (serial, monitors, logical monitors, props)
    size_t nm = G.n_children(mons);
    for (size_t i = 0; i < nm && nstreams < MAX_OUTPUTS; i++) {
        GVariant *m = G.child(mons, i);
        GVariant *spec = G.child(m, 0); // connector vendor product serial
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
        if (current) nstreams++; // no current mode = plugged in but off
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
                 G.new_parsed("(%s, {'cursor-mode': <uint32 0>})", streams[i].connector)); // 0 = hidden
        if (!r) return 0;
        G.variant_get(r, "(&o)", &sp);
        snprintf(streams[i].stream_path, sizeof(streams[i].stream_path), "%s", sp);
        G.unref(r);
        // before Start or the node id can go by before we listen
        streams[i].sub = G.subscribe(bus, "org.gnome.Mutter.ScreenCast", "org.gnome.Mutter.ScreenCast.Stream",
                                     "PipeWireStreamAdded", streams[i].stream_path, NULL, 0, on_stream_added, NULL, NULL);
    }
    session_sub = G.subscribe(bus, "org.gnome.Mutter.ScreenCast", "org.gnome.Mutter.ScreenCast.Session", "Closed",
                              session_path, NULL, 0, on_change, NULL, NULL);
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
        if (!G.iteration(NULL, 0)) usleep(10000); // dbus signals only run from the main context
    }
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
        // 2 fps max and only the newest buffer, the board doesnt move that fast
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

// skip the portal, no share-screen popup every launch
static int gnome_init(Backend *b)
{
    if (!loaded) {
        if (!load()) return 0;
        GError *err = NULL;
        bus = G.bus_get_sync(2, NULL, &err); // G_BUS_TYPE_SESSION
        if (!bus) return 0;
        G.gst_init(NULL, NULL);
        G.subscribe(bus, "org.gnome.Mutter.DisplayConfig", "org.gnome.Mutter.DisplayConfig", "MonitorsChanged",
                    "/org/gnome/Mutter/DisplayConfig", NULL, 0, on_change, NULL, NULL);
        loaded = 1;
    }
    return start_streams(b);
}

static void restart(Backend *b)
{
    changed = 0;
    fprintf(stderr, "screens changed, restarting the screen grab\n");
    stop_streams();
    for (int i = 0; i < MAX_OUTPUTS; i++) b->out[i].have_frame = 0;
    b->nout = 0;
    int ok = 0;
    for (int tries = 0; tries < 3 && !(ok = start_streams(b)); tries++) {
        stop_streams();
        usleep(300000); // mutter can still be mid reconfigure
    }
    if (!ok) changed = 1, changed_at = now_ms() + 4000; // so the next try is ~4.7s out
    b->out_gen++;
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
    while (G.iteration(NULL, 0)) {} // run whatever dbus signals came in
    static int ticks, at = -1;
    if (at < 0) {
        const char *e = getenv("OPEN_CHEASE_GNOME_RESTART_AT"); // test hook, fakes a monitor change after n grabs
        at = 0;
        while (e && *e >= '0' && *e <= '9') at = at * 10 + *e++ - '0';
    }
    if (at && ++ticks == at) changed = 1;
    if (changed && now_ms() - changed_at > 700) restart(b); // monitor changes come in bursts, let it settle
    int got = 0;
    for (int i = 0; i < nstreams; i++) {
        Stream *s = &streams[i];
        uint64_t timeout = s->buf ? 0 : 2000000000ULL; // first frame can take a bit, after that never block
        while (pull(s, timeout)) timeout = 0;
        Output *o = &b->out[i];
        if (!s->buf) {
            o->have_frame = 0;
            continue;
        }
        if (s->w <= 0 || s->h <= 0) { // Parameters had no size
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
