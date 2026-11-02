#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "plugin.h"

#ifdef _WIN32
static const char *tool_cmds[PT_N][4] = {{"py", "python"}, {"node"}, {"gcc", "clang", "cc"}, {"g++", "clang++", "c++"}, {"rustc"}, {"dotnet"}};
#define CC_GET "https://www.msys2.org/"
#elif defined(__APPLE__)
static const char *tool_cmds[PT_N][4] = {{"python3"}, {"node"}, {"cc", "clang", "gcc"}, {"c++", "clang++", "g++"}, {"rustc"}, {"dotnet"}};
#define CC_GET "https://developer.apple.com/xcode/resources/"
#else
static const char *tool_cmds[PT_N][4] = {{"python3"}, {"node"}, {"cc", "gcc", "clang"}, {"c++", "g++", "clang++"}, {"rustc"}, {"dotnet"}};
#define CC_GET "https://gcc.gnu.org/install/binaries.html"
#endif
static const char *tool_names[PT_N] = {"python", "node", "gcc", "g++", "rust", "dotnet"};
static const char *tool_what[PT_N] = {"python", "node", "a C compiler", "a C++ compiler", "rust", "dotnet"};
static const char *tool_urls[PT_N] = {"https://www.python.org/downloads/", "https://nodejs.org/en/download", CC_GET, CC_GET,
                                      "https://rustup.rs/", "https://dotnet.microsoft.com/download"};

enum { TS_UNKNOWN, TS_CHECKING, TS_FOUND, TS_MISSING };

#define TS_NODE PT_N // extra slot: can this node run .ts at all, some are built without it

static struct {
    int state, cur, from;
    char ver[24], out[256], path[1024];
    int nout;
    proc_t pid;
    long t0;
} tools[PT_N + 1];

const char *plug_tool_name(int t) { return t >= 0 && t < PT_N ? tool_names[t] : ""; }
const char *plug_tool_url(int t) { return t >= 0 && t < PT_N ? tool_urls[t] : NULL; }

static void tool_next(int t) // tries the next name on the list, missing once theyre all gone
{
    if (t == TS_NODE) {
        char cmd[1100];
        int to;
        snprintf(cmd, sizeof(cmd), "\"%s\" -e \"console.log(process.features.typescript||0)\"", tools[PT_NODE].path);
        if (tools[t].cur++ || !proc_cmd(cmd, ".", 1, &tools[t].pid, &to, &tools[t].from)) {
            tools[t].state = TS_MISSING;
            return;
        }
        fd_close(to);
        tools[t].state = TS_CHECKING, tools[t].nout = 0, tools[t].t0 = -1;
        return;
    }
    while (tools[t].cur < 4 && tool_cmds[t][tools[t].cur]) {
        char path[1024], cmd[1100];
        int to;
        if (!on_path(tool_cmds[t][tools[t].cur++], path, sizeof(path)) || strchr(path, '"')) continue;
        snprintf(cmd, sizeof(cmd), "\"%s\" --version", path);
        if (!proc_cmd(cmd, ".", 1, &tools[t].pid, &to, &tools[t].from)) continue;
        memcpy(tools[t].path, path, sizeof(path));
        fd_close(to);
        tools[t].state = TS_CHECKING, tools[t].nout = 0, tools[t].t0 = -1;
        return;
    }
    tools[t].state = TS_MISSING;
}

static int find_ver(const char *s, char *out, int n) // first 1.2.3 looking thing
{
    for (; *s; s++) {
        if (*s < '0' || *s > '9') continue;
        const char *e = s;
        int dots = 0;
        while ((*e >= '0' && *e <= '9') || (*e == '.' && e[1] >= '0' && e[1] <= '9')) dots += *e++ == '.';
        if (dots && e - s < n) {
            memcpy(out, s, (size_t)(e - s)), out[e - s] = 0;
            return 1;
        }
        s = e - 1;
    }
    return 0;
}

static int ver_cmp(const char *a, const char *b)
{
    while (*a || *b) {
        long x = 0, y = 0;
        while (*a >= '0' && *a <= '9') x = x < 100000000 ? x * 10 + *a - '0' : x, a++;
        while (*b >= '0' && *b <= '9') y = y < 100000000 ? y * 10 + *b - '0' : y, b++;
        if (x != y) return x < y ? -1 : 1;
        while (*a && (*a < '0' || *a > '9')) a++;
        while (*b && (*b < '0' || *b > '9')) b++;
    }
    return 0;
}

static int poll_tools(long now)
{
    int dirty = 0;
    for (int t = 0; t <= PT_N; t++) {
        if (tools[t].state != TS_CHECKING) continue;
        if (tools[t].t0 < 0) tools[t].t0 = now;
        int got = 0, room;
        while ((room = (int)sizeof(tools[t].out) - 1 - tools[t].nout) > 0 && (got = fd_read_some(tools[t].from, tools[t].out + tools[t].nout, room)) > 0)
            tools[t].nout += got;
        if (got >= 0 && room > 0 && now - tools[t].t0 < 4000) continue;
        proc_kill(tools[t].pid); // already gone unless it hung, then it counts as a fail
        int code = proc_reap_code(tools[t].pid);
        fd_close(tools[t].from);
        tools[t].out[tools[t].nout] = 0;
        if (code || (t == PT_PYTHON && strncmp(tools[t].out, "Python 3", 8))) tool_next(t); // the windows store stub says nothing and fails
        else if (t == TS_NODE) tools[t].state = strstr(tools[t].out, "strip") || strstr(tools[t].out, "transform") ? TS_FOUND : TS_MISSING;
        else if (find_ver(tools[t].out, tools[t].ver, sizeof(tools[t].ver))) tools[t].state = TS_FOUND;
        else tool_next(t); // no version out of it, a stub or something broken
        dirty = 1;
    }
    return dirty;
}

static int is(const char *w, const char *list)
{
    size_t l = strlen(w);
    for (const char *s = list; *s;) {
        size_t k = strcspn(s, " ");
        if (k == l && !strncmp(s, w, l)) return 1;
        s += k;
        while (*s == ' ') s++;
    }
    return 0;
}

static int tool_of(const char *w) // a name from lang or needs, -2 = open-chease itself
{
    if (is(w, "python python3 py")) return PT_PYTHON;
    if (is(w, "node nodejs javascript typescript js ts")) return PT_NODE;
    if (is(w, "c cc gcc clang tcc")) return PT_CC;
    if (is(w, "cpp c++ g++ clang++")) return PT_CXX;
    if (is(w, "rust rustc cargo")) return PT_RUST;
    if (is(w, "csharp c# dotnet .net")) return PT_DOTNET;
    if (is(w, "open-chease")) return -2;
    return -1;
}

static void first_word(const char *s, char *out, int n)
{
    while (*s == ' ' || *s == '"') s++;
    int l = 0;
    for (; *s && *s != ' ' && *s != '"' && l < n - 1; s++) {
        if (*s == '/' || *s == '\\') l = 0;
        else out[l++] = *s >= 'A' && *s <= 'Z' ? *s + 32 : *s;
    }
    out[l] = 0;
    if (l > 4 && !strcmp(out + l - 4, ".exe")) out[l - 4] = 0;
}

static int ends(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !strcmp(s + a - b, tail);
}

static int is_ts(const Plugin *p)
{
    char w[64];
    first_word(p->run, w, sizeof(w));
    return !strcmp(p->lang, "typescript") || ends(w, ".ts") || (!strcmp(w, "node") && strstr(p->run, ".ts"));
}

int plug_lang(const Plugin *p)
{
    char w[64];
    if (*p->lang) return strcmp(p->lang, "other") ? tool_of(p->lang) : -1;
    first_word(p->build, w, sizeof(w)); // no lang key, guess it from build and run
    int t = tool_of(w);
    if (t >= 0 && t != PT_NODE && t != PT_PYTHON) return t;
    first_word(p->run, w, sizeof(w));
    if (!strncmp(w, "python", 6) || !strcmp(w, "py") || ends(w, ".py")) return PT_PYTHON;
    if (!strcmp(w, "node") || ends(w, ".js") || ends(w, ".mjs") || ends(w, ".cjs") || ends(w, ".ts")) return PT_NODE;
    if (!strcmp(w, "dotnet")) return PT_DOTNET;
    return -1;
}

int plug_os_ok(const Plugin *p)
{
    if (!*p->platforms) return 1;
    char w[16];
    for (const char *s = p->platforms; *s;) {
        int l = 0;
        while (*s == ',' || *s == ' ') s++;
        for (; *s && *s != ',' && *s != ' '; s++)
            if (l < 15) w[l++] = *s >= 'A' && *s <= 'Z' ? *s + 32 : *s;
        w[l] = 0;
        if (!strcmp(w, PL_OS) || (!strcmp(PL_OS, "windows") && is(w, "win win32 win64")) ||
            (!strcmp(PL_OS, "mac") && is(w, "macos osx darwin")))
            return 1;
    }
    return 0;
}

static int check(Plugin *p, char *why, int n, int *bad) // see plug_ready, bad = the tool to blame
{
    *bad = -1;
    if (why) *why = 0;
    if (!plug_os_ok(p)) {
        if (why) {
            char list[80];
            size_t l = 0;
            for (const char *s = p->platforms; *s && l < sizeof(list) - 3; s++) // "linux,mac" -> "linux, mac"
                if (*s == ',') list[l++] = ',', list[l++] = ' ';
                else if (*s != ' ') list[l++] = *s;
            list[l] = 0;
            snprintf(why, n, "only for %s", list);
        }
        return -1;
    }
    int need[PT_N] = {0};
    char want[PT_N][24] = {{0}};
    int t = plug_lang(p);
    if (t >= 0) need[t] = 1;
    if (is_ts(p)) need[PT_NODE] = 1, snprintf(want[PT_NODE], 24, "22.18"); // older node cant run .ts
    char proto[24] = "";
    for (const char *s = p->needs; *s;) { // "python 3.8, open-chease 1"
        char name[32], v[24];
        int l = 0, m = 0;
        while (*s == ' ' || *s == ',') s++;
        for (; *s && *s != ' ' && *s != ','; s++)
            if (l < 31) name[l++] = *s >= 'A' && *s <= 'Z' ? *s + 32 : *s;
        name[l] = 0;
        while (*s == ' ') s++;
        for (; *s && *s != ' ' && *s != ','; s++)
            if (m < 23) v[m++] = *s;
        v[m] = 0;
        int k = tool_of(name);
        if (k == -2) snprintf(proto, sizeof(proto), "%s", v);
        else if (k >= 0) need[k] = 1, snprintf(want[k], 24, "%s", v);
    }
    if (*proto && ver_cmp(PL_PROTO_S, proto) < 0) {
        if (why) snprintf(why, n, "needs a newer open-chease (plugin version %s)", proto);
        return -1;
    }
    int waiting = -1;
    for (t = 0; t < PT_N; t++) {
        if (!need[t]) continue;
        if (tools[t].state == TS_UNKNOWN) tools[t].cur = 0, tool_next(t);
        if (tools[t].state == TS_CHECKING) {
            if (waiting < 0) waiting = t;
            continue;
        }
        if (tools[t].state == TS_MISSING) {
            if (why) snprintf(why, n, "needs %s, couldnt find it", tool_what[t]);
            *bad = t;
            return -1;
        }
        if (*want[t] && ver_cmp(tools[t].ver, want[t]) < 0) {
            if (why) snprintf(why, n, "needs %s %s, you have %s", tool_names[t], want[t], tools[t].ver);
            *bad = t;
            return -1;
        }
    }
    if (waiting >= 0) {
        if (why) snprintf(why, n, "checking for %s...", tool_what[waiting]);
        return 0;
    }
    if (is_ts(p)) {
        if (tools[TS_NODE].state == TS_UNKNOWN) tools[TS_NODE].cur = 0, tool_next(TS_NODE);
        if (tools[TS_NODE].state == TS_CHECKING) {
            if (why) snprintf(why, n, "checking if node can run .ts...");
            return 0;
        }
        if (tools[TS_NODE].state == TS_MISSING) {
            if (why) snprintf(why, n, "your node cant run .ts files, get node 22.18+ from nodejs.org");
            *bad = PT_NODE;
            return -1;
        }
    }
    return 1;
}

int plug_ready(Plugin *p, char *why, int n)
{
    int bad;
    return check(p, why, n, &bad);
}

int plug_missing(const Plugin *p)
{
    int bad;
    check((Plugin *)p, NULL, 0, &bad);
    return bad;
}

static uint64_t fnv(uint64_t h, const void *d, size_t n)
{
    const unsigned char *b = d;
    while (n--) h = (h ^ *b++) * 1099511628211ull;
    return h;
}

static int src_file(const char *n)
{
    static const char *ext[] = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx", ".inl", ".rs", ".cs", ".csproj",
                                ".fsproj", ".sln", ".props", ".toml", ".ts", ".js", ".mjs", ".cjs", ".go", ".zig"};
    for (size_t k = 0; k < sizeof(ext) / sizeof(ext[0]); k++)
        if (ends(n, ext[k])) return 1;
    return is(n, "plugin.txt Makefile makefile GNUmakefile CMakeLists.txt");
}

static uint64_t hash_dir(const char *dir, const char *rel, int depth, long *budget) // order doesnt matter, the files get added up
{
    uint64_t sum = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (*e->d_name == '.') continue;
        char path[1400], r[400];
        struct stat st;
        snprintf(path, sizeof(path), "%.1100s/%.250s", dir, e->d_name);
        snprintf(r, sizeof(r), "%.140s/%.250s", rel, e->d_name);
        if (stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 6 && !is(e->d_name, "out obj bin target node_modules")) sum += hash_dir(path, r, depth + 1, budget);
            continue;
        }
        if (!S_ISREG(st.st_mode) || !src_file(e->d_name)) continue;
        uint64_t h = fnv(14695981039346656037ull, r, strlen(r));
        long long sz = st.st_size;
        h = fnv(h, &sz, sizeof(sz));
        FILE *f = *budget > 0 ? fopen(path, "rb") : NULL;
        char buf[8192];
        size_t got;
        while (f && *budget > 0 && (got = fread(buf, 1, sizeof(buf), f)) > 0) h = fnv(h, buf, got), *budget -= (long)got;
        if (f) fclose(f);
        h ^= h >> 31, h *= 0x9E3779B97F4A7C15ull;
        sum += h;
    }
    if (d) closedir(d);
    return sum;
}

static void hex(uint64_t v, char *out)
{
    for (int i = 15; i >= 0; i--, v >>= 4) out[i] = "0123456789abcdef"[v & 15];
    out[16] = 0;
}

static int built_already(Plugin *p)
{
    long budget = 64L << 20;
    char path[1100], had[40] = "", now[17];
    p->srchash = hash_dir(p->dir, "", 0, &budget) + fnv(14695981039346656037ull, p->build, strlen(p->build));
    hex(p->srchash, now);
    snprintf(path, sizeof(path), "%s/.built", p->dir);
    FILE *f = fopen(path, "r");
    if (f) {
        if (!fgets(had, sizeof(had), f)) *had = 0;
        fclose(f);
    }
    had[strcspn(had, "\r\n")] = 0;
    return !strcmp(had, now);
}

static void build_start(Plugin *p)
{
    char log[1100], path[1100];
    snprintf(log, sizeof(log), "%s/build.log", p->dir);
    snprintf(path, sizeof(path), "%s/.built", p->dir);
    remove(path);
    p->bjob = cmd_start(p->build, p->dir, log);
    p->bstart = -1;
    if (p->bjob) snprintf(p->status, sizeof(p->status), "building it...");
    else p->bfail = 1, snprintf(p->status, sizeof(p->status), "couldnt start the build");
}

static void log_tail(Plugin *p)
{
    char path[1100], buf[4097];
    snprintf(path, sizeof(path), "%s/build.log", p->dir);
    memset(p->blog, 0, sizeof(p->blog));
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, sz > 4096 ? sz - 4096 : 0, SEEK_SET);
    size_t n = fread(buf, 1, 4096, f);
    fclose(f);
    buf[n] = 0;
    int got = 0;
    char *e = buf + n;
    while (got < 3 && e > buf) { // last 3 lines with something on them, walking back so each goes in at the top
        while (e > buf && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        char *s = e;
        while (s > buf && s[-1] != '\n') s--;
        if (s == e) break;
        for (char *q = s; *q; q++)
            if ((unsigned char)*q < 32 || *q == 127) *q = ' ';
        for (int k = got; k > 0; k--) memcpy(p->blog[k], p->blog[k - 1], sizeof(p->blog[0]));
        snprintf(p->blog[0], sizeof(p->blog[0]), "%.89s", s);
        got++;
        e = s;
    }
}

static int building(void)
{
    for (int i = 0; i < nplugs; i++)
        if (plugs[i].bjob) return 1;
    return 0;
}

void plug_start(Plugin *p)
{
    if (p->alive || p->bjob) return;
    p->want = p->bfail = 0;
    int r = plug_ready(p, p->status, sizeof(p->status));
    if (r == 0) p->want = 1; // plug_run_poll tries again once its looked
    if (r <= 0) return;
    int build = *p->build && !built_already(p);
    if (build && building()) { // one at a time, plug_built tells the files apart by what showed up since plug_cleared
        p->want = 1;
        snprintf(p->status, sizeof(p->status), "waiting for another build...");
        return;
    }
    if (build ? !plug_cleared(p) : (plug_recheck(p), !p->trusted)) {
        snprintf(p->status, sizeof(p->status), "waiting for your ok");
        return;
    }
    if (build) build_start(p);
    else plug_launch(p);
}

void plug_run_stop(Plugin *p)
{
    if (!p->bjob) return;
    job_kill(p->bjob);
    p->bjob = 0;
    plug_built(p); // even half a build made files, they dont need a new ok
}

int plug_run_poll(long now)
{
    int dirty = poll_tools(now);
    for (int i = 0; i < nplugs; i++) {
        Plugin *p = &plugs[i];
        if (p->want && p->on && !p->alive && !p->bjob) {
            char was[80];
            memcpy(was, p->status, sizeof(was));
            plug_start(p);
            dirty |= strcmp(was, p->status) != 0;
        }
        if (!p->bjob) continue;
        int ok;
        if (p->bstart < 0) p->bstart = now;
        if (job_done(p->bjob, &ok)) {
            p->bjob = 0;
            dirty = 1;
            plug_built(p);
            if (!ok) {
                p->bfail = 1;
                log_tail(p);
                snprintf(p->status, sizeof(p->status), "build failed, the end of build.log:");
                continue;
            }
            char path[1100], h[17];
            snprintf(path, sizeof(path), "%s/.built", p->dir);
            hex(p->srchash, h);
            FILE *f = fopen(path, "w");
            if (f) fprintf(f, "%s\n", h), fclose(f);
            if (p->on) plug_launch(p);
        } else if (now - p->bstart > 600000) {
            plug_run_stop(p);
            p->bfail = 1, p->blog[0][0] = 0;
            snprintf(p->status, sizeof(p->status), "build took over 10 min, stopped it");
            dirty = 1;
        }
    }
    return dirty;
}

int plug_busy(void)
{
    for (int t = 0; t <= PT_N; t++)
        if (tools[t].state == TS_CHECKING) return 1;
    for (int i = 0; i < nplugs; i++)
        if (plugs[i].bjob || plugs[i].want) return 1;
    return 0;
}

void plug_tools_recheck(void)
{
    for (int t = 0; t <= PT_N; t++) {
        if (tools[t].state == TS_CHECKING) proc_kill(tools[t].pid), proc_reap(tools[t].pid), fd_close(tools[t].from);
        tools[t].state = TS_UNKNOWN;
    }
    for (int i = 0; i < nplugs; i++) {
        Plugin *p = &plugs[i];
        p->bfail = 0;
        memset(p->blog, 0, sizeof(p->blog));
        if (p->on && !p->alive && !p->bjob) plug_start(p);
    }
}
