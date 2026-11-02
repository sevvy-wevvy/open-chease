#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "plugin.h"

Plugin plugs[PL_MAX];
int nplugs;
void (*plug_note)(const Plugin *p, const char *msg);
int (*plug_grab)(double x, double y, double w, double h, const char *path, int *pw, int *ph);

static int loud;

typedef struct {
    char key[72];
    int v;
} Saved;
static Saved saved[400]; // plugin.<name> and plugin.<name>.<key> from the settings file, kept even for plugins that arent there
static int nsaved;

static Saved *saved_get(const char *key, int make)
{
    for (int i = 0; i < nsaved; i++)
        if (!strcmp(saved[i].key, key)) return &saved[i];
    if (!make || nsaved == (int)(sizeof(saved) / sizeof(saved[0])) || strlen(key) >= sizeof(saved[0].key)) return NULL;
    snprintf(saved[nsaved].key, sizeof(saved[0].key), "%s", key);
    saved[nsaved].v = 0;
    return &saved[nsaved++];
}

static const char *num(const char *s, double *out) // "-12.5", no strtod
{
    while (*s == ' ') s++;
    int neg = *s == '-';
    if (neg || *s == '+') s++;
    if (!((*s >= '0' && *s <= '9') || (*s == '.' && s[1] >= '0' && s[1] <= '9'))) return NULL;
    double v = 0, sc = 0.1;
    for (; *s >= '0' && *s <= '9'; s++)
        if (v < 1e9) v = v * 10 + *s - '0';
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++, sc /= 10) v += (*s - '0') * sc;
    *out = neg ? -v : v;
    return s;
}

static const char *inum(const char *s, int *out)
{
    double d;
    s = num(s, &d);
    if (s) *out = (int)(d < 0 ? d - 0.5 : d + 0.5);
    return s;
}

void plug_cfg_line(const char *key, const char *val)
{
    int v;
    if (!inum(val, &v)) return;
    Saved *s = saved_get(key, 1);
    if (s) s->v = v;
}

void plug_cfg_save(FILE *f)
{
    for (int i = 0; i < nsaved; i++) fprintf(f, "%s %d\n", saved[i].key, saved[i].v);
}

int plug_square(const char *s)
{
    if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
    if (s[2] && s[2] != ' ' && s[2] != '\r' && s[2] != '\n') return -1;
    return (s[1] - '1') * 8 + s[0] - 'a';
}

static void tidy(char *s) // no control chars on screen
{
    for (; *s; s++)
        if ((unsigned char)*s < 32 || *s == 127) *s = ' ';
}

static int ok_name(const char *n)
{
    if (!*n || strlen(n) > 31 || *n == '.') return 0;
    for (; *n; n++)
        if (!((*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z') || (*n >= '0' && *n <= '9') || *n == '-' || *n == '_' || *n == '.')) return 0;
    return 1;
}

static void read_manifest(Plugin *p, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char ln[600];
    while (fgets(ln, sizeof(ln), f)) {
        ln[strcspn(ln, "\r\n")] = 0;
        char *eq = strchr(ln, '=');
        if (*ln == '#' || !eq) continue;
        char *k = ln, *v = eq + 1, *e = eq;
        *eq = 0;
        while (*k == ' ') k++;
        while (e > k && e[-1] == ' ') *--e = 0;
        while (*v == ' ') v++;
        tidy(v);
        if (!strcmp(k, "run")) snprintf(p->run, sizeof(p->run), "%s", v);
        else if (!strcmp(k, "name")) snprintf(p->title, sizeof(p->title), "%s", v);
        else if (!strcmp(k, "description")) snprintf(p->desc, sizeof(p->desc), "%s", v);
        else if (!strcmp(k, "version")) snprintf(p->ver, sizeof(p->ver), "%s", v);
    }
    fclose(f);
}

void plug_scan(const char *data, int verbose)
{
    loud = verbose;
    char base[1100];
    snprintf(base, sizeof(base), "%s/plugins", data);
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && nplugs < PL_MAX) {
        if (!ok_name(e->d_name)) continue;
        Plugin *p = &plugs[nplugs];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%.31s", e->d_name);
        snprintf(p->dir, sizeof(p->dir), "%.980s/%.31s", base, e->d_name);
        char mf[1200];
        snprintf(mf, sizeof(mf), "%s/plugin.txt", p->dir);
        read_manifest(p, mf);
        if (!*p->run) continue;
        if (!*p->title) memcpy(p->title, p->name, sizeof(p->name));
        snprintf(p->status, sizeof(p->status), "off");
        p->pid = 0, p->to = p->from = -1;
        char k[80];
        snprintf(k, sizeof(k), "plugin.%s", p->name);
        Saved *s = saved_get(k, 0);
        p->on = s && s->v;
        nplugs++;
    }
    closedir(d);
    for (int i = 0; i < nplugs; i++) // the folder order is random, the settings tab shouldnt be
        for (int j = i + 1; j < nplugs; j++)
            if (strcmp(plugs[j].name, plugs[i].name) < 0) {
                Plugin t = plugs[i];
                plugs[i] = plugs[j], plugs[j] = t;
            }
}

static void say(Plugin *p, const char *msg)
{
    snprintf(p->said, sizeof(p->said), "%s", msg);
}

void plug_start(Plugin *p)
{
    if (p->alive) return;
    p->nset = p->nitem = p->nnext = p->nin = p->nout = p->junk = p->tick_ms = 0;
    p->panel.nline = p->npanel.nline = 0;
    p->panel.title[0] = p->npanel.title[0] = p->tab[0] = 0;
    p->said[0] = 0;
    if (!proc_cmd(p->run, p->dir, !loud, &p->pid, &p->to, &p->from)) {
        snprintf(p->status, sizeof(p->status), "couldnt start it, check run in plugin.txt");
        return;
    }
    p->alive = p->fresh = 1;
    p->started = -1;
    snprintf(p->status, sizeof(p->status), "running");
    plug_send(p, "hello 1 %s", p->name);
}

void plug_stop(Plugin *p, const char *why)
{
    if (!p->alive) return;
    fd_close(p->to);
    fd_close(p->from);
    p->to = p->from = -1;
    proc_kill(p->pid);
    proc_reap(p->pid);
    p->pid = 0;
    p->alive = 0;
    p->nitem = p->nnext = 0;
    p->panel.nline = 0, p->panel.title[0] = 0;
    snprintf(p->status, sizeof(p->status), "%s", why);
    if (loud) fprintf(stderr, "plugin %s: %s\n", p->name, why);
}

static void vsend(Plugin *p, const char *fmt, va_list ap)
{
    if (!p->alive) return;
    char ln[1200];
    int n = vsnprintf(ln, sizeof(ln) - 1, fmt, ap);
    if (n < 0) return;
    if (n > (int)sizeof(ln) - 2) n = sizeof(ln) - 2;
    ln[n++] = '\n';
    if (p->nout + n > (int)sizeof(p->out)) { // its not reading, dont let it pile up forever
        plug_stop(p, "stopped reading, turned it off");
        return;
    }
    memcpy(p->out + p->nout, ln, n);
    p->nout += n;
}

void plug_send(Plugin *p, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsend(p, fmt, ap);
    va_end(ap);
}

void plug_send_all(const char *fmt, ...)
{
    for (int i = 0; i < nplugs; i++) {
        va_list ap;
        va_start(ap, fmt);
        vsend(&plugs[i], fmt, ap);
        va_end(ap);
    }
}

int plug_running(void)
{
    for (int i = 0; i < nplugs; i++)
        if (plugs[i].alive) return 1;
    return 0;
}

void plug_quit(void)
{
    for (int i = 0; i < nplugs; i++) plug_stop(&plugs[i], "off");
}

void plug_change(Plugin *p, int k, int v)
{
    PlugSet *s = &p->set[k];
    if (s->kind == PS_BUTTON) {
        plug_send(p, "click %s", s->key);
        return;
    }
    if (v < s->lo) v = s->lo;
    if (v > s->hi) v = s->hi;
    s->v = v;
    char key[80];
    snprintf(key, sizeof(key), "plugin.%s.%s", p->name, s->key);
    Saved *sv = saved_get(key, 1);
    if (sv) sv->v = v;
    plug_send(p, "set %s %d", s->key, v);
}

static char *word(char **s) // next space separated word, NULL at the end
{
    char *w = *s;
    while (*w == ' ') w++;
    if (!*w) return NULL;
    char *e = w;
    while (*e && *e != ' ') e++;
    if (*e) *e++ = 0;
    *s = e;
    return w;
}

static int peek(const char *s, char *out, int n) // copies the next word without eating it, its length
{
    while (*s == ' ') s++;
    int l = 0;
    while (s[l] && s[l] != ' ' && l < n - 1) out[l] = s[l], l++;
    out[l] = 0;
    return s[l] && s[l] != ' ' ? 0 : l;
}

static int color(const char *s, uint32_t *out) // #rgb #rrggbb #aarrggbb
{
    if (*s != '#') return 0;
    s++;
    uint32_t v = 0;
    int n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        int d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
        if (d < 0 || n >= 8) return 0;
        v = v * 16 + d;
    }
    if (n == 3) v = ((v >> 8) * 17) << 16 | ((v >> 4 & 15) * 17) << 8 | (v & 15) * 17;
    if (n == 3 || n == 6) v |= 0xFF000000u;
    else if (n != 8) return 0;
    *out = v;
    return 1;
}

static int pos(char **s, PlugItem *it) // a square name or x y in board px
{
    char w[8];
    if (peek(*s, w, sizeof(w)) == 2 && plug_square(w) >= 0) {
        it->sq = plug_square(w), it->x = it->y = 0;
        word(s);
        return 1;
    }
    const char *q = num(*s, &it->x);
    if (!q || !(q = num(q, &it->y))) return 0;
    it->sq = -1;
    *s = (char *)q;
    return 1;
}

static void cut(char *dst, size_t n, const char *src)
{
    while (*src == ' ') src++;
    snprintf(dst, n, "%s", src);
    size_t l = strlen(dst);
    while (l && dst[l - 1] == ' ') dst[--l] = 0;
}

static void label_tip(PlugSet *st, char *rest)
{
    char *bar = strchr(rest, '|');
    if (bar) *bar = 0, cut(st->tip, sizeof(st->tip), bar + 1);
    else st->tip[0] = 0;
    cut(st->label, sizeof(st->label), rest);
    if (!*st->label) snprintf(st->label, sizeof(st->label), "%s", st->key);
}

static void declare(Plugin *p, char *s)
{
    char *kind = word(&s), *key = word(&s);
    if (!kind || !key || strlen(key) >= sizeof(p->set[0].key) || !ok_name(key)) return;
    int k = 0;
    while (k < p->nset && strcmp(p->set[k].key, key)) k++;
    if (k == PL_SETS) return;
    PlugSet st = {0};
    snprintf(st.key, sizeof(st.key), "%s", key);
    const char *q = s;
    if (!strcmp(kind, "bool")) {
        st.kind = PS_BOOL, st.hi = 1;
        if (!(q = inum(q, &st.def))) return;
    } else if (!strcmp(kind, "slider")) {
        st.kind = PS_SLIDER;
        if (!(q = inum(q, &st.def)) || !(q = inum(q, &st.lo)) || !(q = inum(q, &st.hi)) || st.hi <= st.lo || st.hi - st.lo > 100000) return;
    } else if (!strcmp(kind, "choice")) {
        st.kind = PS_CHOICE;
        if (!(q = inum(q, &st.def))) return;
        char *r = (char *)q, *list = word(&r);
        if (!list) return;
        for (char *c = list; *c && st.nch < PL_CHOICES;) {
            char *comma = strchr(c, ',');
            if (comma) *comma = 0;
            snprintf(st.ch[st.nch], sizeof(st.ch[0]), "%s", c);
            for (char *u = st.ch[st.nch]; *u; u++)
                if (*u == '_') *u = ' ';
            if (*st.ch[st.nch]) st.nch++;
            if (!comma) break;
            c = comma + 1;
        }
        if (!st.nch) return;
        st.hi = st.nch - 1;
        q = r;
    } else if (!strcmp(kind, "button")) {
        st.kind = PS_BUTTON;
    } else
        return;
    label_tip(&st, (char *)q);
    if (st.def < st.lo) st.def = st.lo;
    if (st.def > st.hi) st.def = st.hi;
    char sk[80];
    snprintf(sk, sizeof(sk), "plugin.%s.%s", p->name, key);
    Saved *sv = saved_get(sk, 0);
    st.v = sv ? sv->v : k < p->nset ? p->set[k].v : st.def;
    if (st.v < st.lo || st.v > st.hi) st.v = st.def;
    p->set[k] = st;
    if (k == p->nset) p->nset++;
    if (st.kind != PS_BUTTON) plug_send(p, "set %s %d", st.key, st.v);
}

static int draw_cmd(Plugin *p, char *s)
{
    static const char *kinds[] = {"text", "big", "rect", "box", "arrow", "square"};
    char *w = word(&s);
    if (!w || p->nnext >= PL_ITEMS) return 0;
    int kind = -1;
    for (int i = 0; i < 6; i++)
        if (!strcmp(w, kinds[i])) kind = i;
    if (kind < 0) return 0;
    PlugItem it = {.kind = kind, .sq = -1, .sq2 = -1};
    if (kind == PD_ARROW || kind == PD_SQUARE) {
        char *a = word(&s), *b = kind == PD_ARROW ? word(&s) : a;
        if (!a || !b || (it.sq = plug_square(a)) < 0 || (it.sq2 = plug_square(b)) < 0) return 0;
    } else if (!pos(&s, &it))
        return 0;
    if (kind == PD_RECT || kind == PD_BOX) {
        const char *q = num(s, &it.w);
        if (!q || !(q = num(q, &it.h)) || it.w <= 0 || it.h <= 0) return 0;
        s = (char *)q;
        if (it.w > 4000) it.w = 4000;
        if (it.h > 4000) it.h = 4000;
    }
    char *c = word(&s);
    if (!c || !color(c, &it.col)) return 0;
    if (kind == PD_TEXT || kind == PD_BIG) {
        cut(it.text, sizeof(it.text), s);
        if (!*it.text) return 0;
    }
    if (it.x < -4000 || it.x > 4000 || it.y < -4000 || it.y > 4000) return 0;
    p->next[p->nnext++] = it;
    return 1;
}

static void frame_cmd(Plugin *p, char *s, long now)
{
    double v[4];
    const char *q = s;
    for (int i = 0; i < 4; i++)
        if (!(q = num(q, &v[i]))) {
            plug_send(p, "frame none bad numbers");
            return;
        }
    if (v[2] < 1 || v[3] < 1) {
        plug_send(p, "frame none too small");
        return;
    }
    if (p->last_frame && now - p->last_frame < 100) {
        plug_send(p, "frame none busy");
        return;
    }
    p->last_frame = now ? now : 1;
    char path[1100];
    snprintf(path, sizeof(path), "%s/frame.ppm", p->dir);
    int w = 0, h = 0;
    if (!plug_grab || !plug_grab(v[0], v[1], v[2] > 1024 ? 1024 : v[2], v[3] > 1024 ? 1024 : v[3], path, &w, &h))
        plug_send(p, "frame none no board");
    else
        plug_send(p, "frame %d %d %s", w, h, path);
}

static int handle(Plugin *p, char *ln, long now) // 1 = something on screen changed
{
    tidy(ln);
    char *s = ln, *cmd = word(&s);
    if (!cmd) return 0;
    while (*s == ' ') s++;
    if (!strcmp(cmd, "draw")) return draw_cmd(p, s), 0;
    if (!strcmp(cmd, "show")) {
        memcpy(p->item, p->next, sizeof(PlugItem) * p->nnext);
        p->nitem = p->nnext, p->nnext = 0;
        p->panel = p->npanel;
        p->npanel.nline = 0, p->npanel.title[0] = 0;
        return 1;
    }
    if (!strcmp(cmd, "clear")) {
        int had = p->nitem || p->panel.nline || *p->panel.title;
        p->nitem = p->nnext = 0;
        p->panel.nline = p->npanel.nline = 0;
        p->panel.title[0] = p->npanel.title[0] = 0;
        return had;
    }
    if (!strcmp(cmd, "panel")) {
        cut(p->npanel.title, sizeof(p->npanel.title), s);
        return 0;
    }
    if (!strcmp(cmd, "line")) {
        PlugPanel *pn = &p->npanel;
        if (pn->nline >= PL_LINES) return 0;
        uint32_t col = 0;
        char c[12];
        if (peek(s, c, sizeof(c)) && color(c, &col)) word(&s);
        else col = 0;
        cut(pn->line[pn->nline], sizeof(pn->line[0]), s);
        pn->col[pn->nline++] = col;
        return 0;
    }
    if (!strcmp(cmd, "setting")) return declare(p, s), 1;
    if (!strcmp(cmd, "tab")) {
        cut(p->tab, sizeof(p->tab), s);
        return 1;
    }
    if (!strcmp(cmd, "note")) {
        if (*s && plug_note) plug_note(p, s);
        return 1;
    }
    if (!strcmp(cmd, "log")) {
        say(p, s);
        if (loud) fprintf(stderr, "plugin %s: %s\n", p->name, s);
        return 0;
    }
    if (!strcmp(cmd, "frame")) return frame_cmd(p, s, now), 0;
    if (!strcmp(cmd, "tick")) {
        int ms = 0;
        if (inum(s, &ms)) p->tick_ms = ms <= 0 ? 0 : ms < 100 ? 100 : ms;
        return 0;
    }
    return 0; // anything else gets ignored so newer plugins still run on older versions
}

static int flush(Plugin *p, long now)
{
    if (!p->nout) return 1;
    int put = fd_write_some(p->to, p->out, p->nout);
    if (put < 0) {
        plug_stop(p, now - p->started < 3000 ? "quit right away, check it runs" : "it quit");
        return 0;
    }
    memmove(p->out, p->out + put, p->nout - put);
    p->nout -= put;
    return 1;
}

int plug_poll(long now)
{
    int dirty = 0;
    for (int i = 0; i < nplugs; i++) {
        Plugin *p = &plugs[i];
        if (!p->alive) continue;
        if (p->started < 0) p->started = now;
        if (p->tick_ms && now - p->last_tick >= p->tick_ms) p->last_tick = now, plug_send(p, "tick %ld", now);
        if (!p->alive) continue;
        if (!flush(p, now)) {
            dirty = 1;
            continue;
        }
        int lines = 0, bytes = 0, gone = 0;
        while (lines < 300 && bytes < 65536) { // a chatty one gets a slice, the rest waits for the next round
            int room = (int)sizeof(p->in) - p->nin;
            int got = fd_read_some(p->from, p->in + p->nin, room > 4096 ? 4096 : room);
            if (got < 0) gone = 1;
            if (got <= 0) break;
            bytes += got;
            p->nin += got;
            char *start = p->in, *end = p->in + p->nin, *nl;
            while ((nl = memchr(start, '\n', end - start))) {
                *nl = 0;
                if (nl > start && nl[-1] == '\r') nl[-1] = 0;
                if (!p->junk) dirty |= handle(p, start, now);
                p->junk = 0;
                start = nl + 1;
                lines++;
                if (!p->alive) break;
            }
            if (!p->alive) break;
            p->nin = (int)(end - start);
            memmove(p->in, start, p->nin);
            if (p->nin == (int)sizeof(p->in)) p->nin = 0, p->junk = 1; // a line this long is garbage, skip to the next newline
        }
        if (p->alive && !flush(p, now)) dirty = 1;
        if (gone && p->alive) {
            plug_stop(p, now - p->started < 3000 ? "quit right away, check it runs" : "it quit");
            dirty = 1;
        }
    }
    return dirty;
}
