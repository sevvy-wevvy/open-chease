#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "plat.h"

int proc_start(const char *path, int quiet, proc_t *p, int *to_fd, int *from_fd)
{
    int to[2], from[2];
    if (pipe(to)) return 0;
    if (pipe(from)) {
        close(to[0]), close(to[1]);
        return 0;
    }
    for (int k = 0; k < 2; k++) fcntl(to[k], F_SETFD, FD_CLOEXEC), fcntl(from[k], F_SETFD, FD_CLOEXEC); // or the other engines inherit them and we never see eof
    pid_t pid = fork();
    if (pid < 0) {
        close(to[0]), close(to[1]), close(from[0]), close(from[1]);
        return 0;
    }
    if (pid == 0) {
        setpgid(0, 0);
        signal(SIGPIPE, SIG_DFL);
        dup2(to[0], 0);
        dup2(from[1], 1);
        if (quiet) {
            int nul = open("/dev/null", O_WRONLY);
            if (nul >= 0) dup2(nul, 2);
        }
        char real[4096], dir[4096];
        if (realpath(path, real)) {
            snprintf(dir, sizeof(dir), "%s", real);
            char *slash = strrchr(dir, '/');
            if (slash && slash != dir) *slash = 0, (void)!chdir(dir);
            path = real;
        }
        execl(path, path, (char *)NULL);
        _exit(127);
    }
    setpgid(pid, pid);
    close(to[0]);
    close(from[1]);
    *p = pid, *to_fd = to[1], *from_fd = from[0];
    return 1;
}

void proc_kill(proc_t p)
{
    if (p > 0) kill(-(pid_t)p, SIGKILL), kill((pid_t)p, SIGKILL);
}

void proc_reap(proc_t p)
{
    if (p > 0) waitpid((pid_t)p, NULL, 0);
}

int proc_reap_code(proc_t p)
{
    int st;
    if (p <= 0 || waitpid((pid_t)p, &st, 0) != (pid_t)p) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int fd_wait(int fd, int ms)
{
    struct pollfd pf = {fd, POLLIN, 0};
    int r = poll(&pf, 1, ms);
    return r < 0 && errno == EINTR ? 0 : r;
}

static int split_cmd(char *s, char **argv, int max) // words, "quotes" and 'quotes' keep spaces
{
    int n = 0;
    while (*s && n < max - 1) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        char *w = s, *o = s;
        while (*s && *s != ' ' && *s != '\t') {
            if (*s == '"' || *s == '\'') {
                char q = *s++;
                while (*s && *s != q) *o++ = *s++;
                if (*s) s++;
            } else
                *o++ = *s++;
        }
        if (*s) s++;
        *o = 0;
        argv[n++] = w;
    }
    argv[n] = NULL;
    return n;
}

static int ends_in(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !strcmp(s + a - b, tail);
}

int proc_cmd(const char *cmd, const char *dir, int quiet, proc_t *p, int *to_fd, int *from_fd)
{
    char line[2048], first[4096], *argv[34], *av[35];
    snprintf(line, sizeof(line), "%s", cmd);
    int n = split_cmd(line, argv, 33);
    if (!n) return 0;
    int k = 0;
    if (!strchr(argv[0], '/')) { // a file sitting in the plugin folder beats one on PATH
        struct stat st;
        snprintf(first, sizeof(first), "%s/%s", dir, argv[0]);
        if (!stat(first, &st) && S_ISREG(st.st_mode)) {
            if (access(first, X_OK)) av[k++] = ends_in(first, ".py") ? "python3" : "sh"; // zips lose the x bit
            argv[0] = first;
        }
    }
    for (int i = 0; i <= n; i++) av[k + i] = argv[i];
    int to[2], from[2];
    if (pipe(to)) return 0;
    if (pipe(from)) {
        close(to[0]), close(to[1]);
        return 0;
    }
    for (int i = 0; i < 2; i++) fcntl(to[i], F_SETFD, FD_CLOEXEC), fcntl(from[i], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        close(to[0]), close(to[1]), close(from[0]), close(from[1]);
        return 0;
    }
    if (pid == 0) {
        setpgid(0, 0);
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGKILL); // we crashed, it goes too
#endif
        signal(SIGPIPE, SIG_DFL);
        dup2(to[0], 0);
        dup2(from[1], 1);
        if (quiet) {
            int nul = open("/dev/null", O_WRONLY);
            if (nul >= 0) dup2(nul, 2);
        }
        for (int fd = 3; fd < 1024; fd++) close(fd);
        if (chdir(dir)) _exit(127);
        execvp(av[0], av);
        _exit(127);
    }
    setpgid(pid, pid);
    close(to[0]);
    close(from[1]);
    fcntl(to[1], F_SETFL, O_NONBLOCK);
    fcntl(from[0], F_SETFL, O_NONBLOCK);
    *p = pid, *to_fd = to[1], *from_fd = from[0];
    return 1;
}

int fd_read_some(int fd, char *buf, int n)
{
    ssize_t r = read(fd, buf, (size_t)n);
    if (r > 0) return (int)r;
    return r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
}

int fd_write_some(int fd, const char *buf, int n)
{
    ssize_t r = write(fd, buf, (size_t)n);
    if (r >= 0) return (int)r;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
}

void fd_close(int fd)
{
    if (fd >= 0) close(fd);
}

int cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

void no_sigpipe(void) { signal(SIGPIPE, SIG_IGN); } // a dead engine would kill us on the next write otherwise

int is_exec(const char *path) { return *path && access(path, X_OK) == 0; }

int make_dir(const char *path) { return mkdir(path, 0700); }

int full_path(const char *in, char *out) { return realpath(in, out) != NULL; }

void exe_dir(char *out, size_t n)
{
    char exe[4096] = "";
#ifdef __APPLE__
    uint32_t sz = sizeof(exe);
    if (_NSGetExecutablePath(exe, &sz)) exe[0] = 0;
#else
    ssize_t l = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    exe[l > 0 ? l : 0] = 0;
#endif
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    snprintf(out, n, "%s", exe);
}

void default_data(char *out, size_t n)
{
#ifdef __APPLE__
    const char *mhome = getenv("HOME");
    if (mhome) {
        snprintf(out, n, "%s/Library/Application Support/open-chease", mhome);
        return;
    }
#endif
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdg && *xdg == '/') snprintf(out, n, "%s/open-chease", xdg);
    else if (home && *home) snprintf(out, n, "%s/.local/share/open-chease", home);
    else snprintf(out, n, "/tmp/open-chease-%d", (int)getuid());
}

int data_dir_ok(const char *dir)
{
    char up[1100];
    snprintf(up, sizeof(up), "%s", dir);
    for (char *p = up + 1; *p; p++)
        if (*p == '/') *p = 0, mkdir(up, 0755), *p = '/';
    mkdir(dir, 0700);
    struct stat st;
    if (lstat(dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) return 0; // don't run engines someone else could've dropped in
    if (st.st_mode & 022) chmod(dir, 0700);
    return 1;
}

int run_wait(char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGPIPE, SIG_DFL);
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) dup2(nul, 1), dup2(nul, 2);
        for (int fd = 3; fd < 1024; fd++) close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (pid < 0 || waitpid(pid, &st, 0) != pid) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int open_folder(const char *dir)
{
#ifdef __APPLE__
    char *argv[] = {"open", (char *)dir, NULL};
#else
    char *argv[] = {"xdg-open", (char *)dir, NULL};
#endif
    return run_wait(argv);
}

void relaunch_self(char **argv, const char *home)
{
    for (int fd = 3; fd < 1024; fd++) close(fd);
    char path[1100];
    if (home) {
        snprintf(path, sizeof(path), "%s/run.sh", home);
        if (access(path, X_OK) == 0) {
            argv[0] = path;
            execv(path, argv);
        }
    }
    char dir[4096];
    exe_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%.1000s/open-chease", dir);
    execv(path, argv);
    _exit(1);
}

void drop_console(void) {}

int spawn_detached(char *const argv[], int hidden)
{
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        signal(SIGPIPE, SIG_DFL);
        for (int fd = 3; fd < 1024; fd++) close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    return pid > 0 ? 0 : -1;
}

void quit_signals(void (*fn)(int))
{
    signal(SIGINT, fn);
    signal(SIGTERM, fn);
    signal(SIGHUP, fn);
}

static pid_t sh_job(const char *script, const char *const *env)
{
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0); // so job_kill gets curl too
        signal(SIGPIPE, SIG_DFL);
        for (int fd = 3; fd < 1024; fd++) close(fd); // dont hand the wayland socket and engine pipes to curl
        for (; *env; env += 2) setenv(env[0], env[1], 1);
        char full[8192];
        snprintf(full, sizeof(full), "trap 'exit 1' INT TERM HUP; %s", script);
        execl("/bin/sh", "sh", "-c", full, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) setpgid(pid, pid);
    return pid < 0 ? 0 : pid;
}

intptr_t dl_start(const DlJob *j)
{
    const char *env[] = {"U", j->url, "K", j->kind, "P", j->pick, "O", j->out, "D", j->data, "L", j->helper, NULL};
    if (!strcmp(j->kind, "lc0")) {
        if (strpbrk(j->helper, "'\\") || strpbrk(j->out, "'\\")) return 0;
        return sh_job("set -e; mkdir -p \"$O.files\"; cd \"$O.files\"; "
                      "if command -v curl >/dev/null; then curl -fsL --speed-time 60 --speed-limit 1000 \"$U\" -o net.tmp; else wget -q -T 60 -O net.tmp \"$U\"; fi; "
                      "mv net.tmp net.pb.gz; printf '#!/bin/sh\\nexec '\\''%s'\\'' --backend=cuda-auto --weights='\\''%s'\\'' \"$@\"\\n' "
                      "\"$L\" \"$O.files/net.pb.gz\" > \"$O.tmp\"; chmod +x \"$O.tmp\"; mv \"$O.tmp\" \"$O\"", env);
    }
    return sh_job("set -e; mkdir -p \"$D/engines\"; t=$(mktemp -d \"$D/dl.XXXXXX\"); trap 'rm -rf \"$t\"' EXIT; cd \"$t\"; "
                  "if command -v curl >/dev/null; then curl -fsL --speed-time 60 --speed-limit 1000 \"$U\" -o got; else wget -q -T 60 -O got \"$U\"; fi; "
                  "case \"$K\" in tgz) tar xzf got; rm got;; tar) tar xf got; rm got;; zip) unzip -q got 2>/dev/null || python3 -m zipfile -e got .; rm got;; esac; "
                  "if [ \"$K\" = bin ]; then chmod +x got; rm -rf \"$O.files\"; mv got \"$O\"; exit 0; fi; "
                  "b=$(find . -type f | while read -r f; do echo \"$(wc -c < \"$f\" | tr -d ' ') $f\"; done | sort -rn | while read -r n f; do "
                  "case \"$(head -c4 \"$f\" | od -An -tx1 | tr -d ' \\n')\" in (7f454c46|cffaedfe|cafebabe) : ;; (*) continue;; esac; "
                  "case \"$f\" in (*\"$P\") echo \"$f\"; break;; esac; done); " // bash 3.2 (what macs ship) chokes on a bare ) in there
                  "[ -n \"$b\" ]; chmod +x \"$b\"; rm -rf \"$O.files\" \"$O\"; mkdir \"$O.files\"; cp -R . \"$O.files/\"; "
                  "ln -s \"$O.files/${b#./}\" \"$O\"", env);
}

intptr_t lists_start(const char *data, const char *a, const char *b)
{
    const char *env[] = {"D", data, "A", a, "B", b, NULL};
    return sh_job("for n in $A $B; do u=https://raw.githubusercontent.com/sevvy-wevvy/open-chease/${OPEN_CHEASE_BRANCH:-main}/engines/$n; "
                  "(curl -fsL --max-time 15 \"$u\" -o \"$D/$n.new\" || wget -q -T 15 -O \"$D/$n.new\" \"$u\") "
                  "&& grep -q '|' \"$D/$n.new\" && mv \"$D/$n.new\" \"$D/$n\"; rm -f \"$D/$n.new\"; done", env);
}

intptr_t fetch_start(const char *url, const char *out)
{
    const char *env[] = {"U", url, "O", out, NULL};
    return sh_job("rm -f \"$O.new\"; if command -v curl >/dev/null; then curl -fsL --max-time 10 \"$U\" -o \"$O.new\"; "
                  "else wget -q -T 10 -O \"$O.new\" \"$U\"; fi && mv \"$O.new\" \"$O\" || { rm -f \"$O.new\"; exit 1; }", env);
}

intptr_t tgz_start(const char *url, const char *dir)
{
    const char *env[] = {"U", url, "O", dir, NULL};
    return sh_job("set -e; rm -rf \"$O\"; mkdir -p \"$O\"; cd \"$O\"; if command -v curl >/dev/null; then curl -fsL --max-time 60 \"$U\" -o got.tgz; "
                  "else wget -q -T 60 -O got.tgz \"$U\"; fi; tar xzf got.tgz; rm -f got.tgz", env);
}

void rm_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st)) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char sub[4200];
            snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
            rm_tree(sub);
        }
        if (d) closedir(d);
        rmdir(path);
    } else
        unlink(path);
}

int clip_get(char *out, int n)
{
    *out = 0;
#ifdef __APPLE__
    FILE *f = popen("pbpaste 2>/dev/null", "r");
#else
    FILE *f = popen("timeout 2 wl-paste -n 2>/dev/null || timeout 2 xclip -o -selection clipboard 2>/dev/null || timeout 2 xsel -ob 2>/dev/null", "r");
#endif
    if (!f) return 0;
    size_t got = fread(out, 1, (size_t)n - 1, f);
    pclose(f);
    out[got] = 0;
    return got > 0;
}

int on_path(const char *name, char *out, size_t n)
{
    const char *s = getenv("PATH");
    struct stat st;
    if (!s || !*name || strchr(name, '/')) return 0;
    while (*s) {
        size_t l = strcspn(s, ":");
        snprintf(out, n, "%.*s/%s", (int)(l ? l : 1), l ? s : ".", name);
        if (!stat(out, &st) && S_ISREG(st.st_mode) && !access(out, X_OK)) return 1;
        s += l;
        if (*s) s++;
    }
    return 0;
}

intptr_t cmd_start(const char *cmd, const char *dir, const char *log)
{
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif
        signal(SIGPIPE, SIG_DFL);
        int in = open("/dev/null", O_RDONLY), out = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (in >= 0) dup2(in, 0);
        if (out >= 0) dup2(out, 1), dup2(out, 2);
        for (int fd = 3; fd < 1024; fd++) close(fd);
        if (chdir(dir)) _exit(127);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) setpgid(pid, pid);
    return pid < 0 ? 0 : pid;
}

int one_instance(const char *data)
{
    char p[1100];
    snprintf(p, sizeof(p), "%.1000s/running.lock", data);
    int fd = open(p, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return 1;
    for (int i = 0; i < 30; i++) { // after an update the new one can start before the old one is gone
        if (!flock(fd, LOCK_EX | LOCK_NB)) return 1;
        usleep(100000);
    }
    close(fd);
    return 0;
}

intptr_t stage_start(const char *home, const char *branch)
{
    const char *env[] = {"H", home, "OPEN_CHEASE_BRANCH", branch, NULL};
    return sh_job("exec bash \"$H/get.sh\" --stage >/dev/null 2>&1", env);
}

int job_done(intptr_t h, int *ok)
{
    int st;
    if (h <= 0 || waitpid((pid_t)h, &st, WNOHANG) != (pid_t)h) return 0;
    *ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    return 1;
}

void job_kill(intptr_t h)
{
    if (h <= 0) return;
    kill(-(pid_t)h, SIGTERM);
    for (int i = 0; i < 20; i++) { // let the trap clean up first
        if (waitpid((pid_t)h, NULL, WNOHANG) == (pid_t)h) return;
        usleep(25000);
    }
    kill(-(pid_t)h, SIGKILL);
    waitpid((pid_t)h, NULL, 0);
}

void engine_path_for(const char *data, const char *slug, char *out, size_t n) { snprintf(out, n, "%s/engines/%s", data, slug); }

void engine_remove(const char *data, const char *path)
{
    size_t dl = strlen(data);
    if (!dl || strncmp(path, data, dl) || path[dl] != '/') return;
    unlink(path);
    char dir[1300];
    snprintf(dir, sizeof(dir), "%s.files", path);
    struct stat ds;
    if (!lstat(dir, &ds) && S_ISDIR(ds.st_mode)) {
        char *argv[] = {"rm", "-rf", dir, NULL};
        run_wait(argv);
    }
}

void engine_list(const char *dir, void (*add)(const char *label, const char *path))
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        char f[4096];
        snprintf(f, sizeof(f), "%s/%s", dir, e->d_name);
        struct stat st;
        if (e->d_name[0] != '.' && !stat(f, &st) && S_ISREG(st.st_mode) && access(f, X_OK) == 0) add(e->d_name, f);
    }
    closedir(d);
}

int ctl_open(const char *data)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/control", data);
    struct stat st;
    if (lstat(path, &st) && mkfifo(path, 0600)) return -1;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !S_ISFIFO(st.st_mode) || st.st_uid != getuid()) { // something else sitting there, leave it alone
        close(fd);
        return -1;
    }
    return fd;
}

int ctl_read(int fd, char *buf, int n)
{
    ssize_t r = fd >= 0 ? read(fd, buf, (size_t)n) : -1;
    return r > 0 ? (int)r : 0;
}

int ctl_send(const char *data, const char *msg)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/control", data);
    int fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC); // no reader = ENXIO instead of hanging
    if (fd < 0) return -1;
    struct stat st;
    size_t len = strlen(msg);
    int ok = !fstat(fd, &st) && S_ISFIFO(st.st_mode) && write(fd, msg, len) == (ssize_t)len;
    close(fd);
    return ok ? 0 : -1;
}

static int net_far(const unsigned char *a, int v6, int port, char *out, int n) // 1 = this address is off this pc
{
    if (v6 && !memcmp(a, "\0\0\0\0\0\0\0\0\0\0\377\377", 12)) a += 12, v6 = 0; // ::ffff:1.2.3.4
    if (!v6) {
        if (a[0] == 127 || !(a[0] | a[1] | a[2] | a[3])) return 0;
        snprintf(out, n, "%d.%d.%d.%d:%d", a[0], a[1], a[2], a[3], port);
        return 1;
    }
    static const unsigned char lo[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, zero[16] = {0};
    if (!memcmp(a, lo, 16) || !memcmp(a, zero, 16)) return 0;
    int l = snprintf(out, n, "[");
    for (int k = 0; k < 8 && l < n; k++) l += snprintf(out + l, n - l, "%s%x", k ? ":" : "", a[2 * k] << 8 | a[2 * k + 1]);
    if (l < n) snprintf(out + l, n - l, "]:%d", port);
    return 1;
}

#ifdef __APPLE__
#include <arpa/inet.h>
#include <libproc.h>
#include <sys/proc_info.h>

int proc_net(proc_t root, char *out, int n)
{
    pid_t pids[64];
    int np = 1;
    pids[0] = (pid_t)root;
    for (int i = 0; i < np && root > 0; i++) { // its kids, then theirs
        pid_t kid[64];
        memset(kid, 0, sizeof(kid));
        int got = proc_listchildpids(pids[i], kid, sizeof(kid));
        for (int k = 0; got > 0 && k < 64 && kid[k] > 0 && np < 64; k++) pids[np++] = kid[k];
    }
    for (int i = 0; i < np; i++) {
        struct proc_fdinfo fds[256];
        int sz = proc_pidinfo(pids[i], PROC_PIDLISTFDS, 0, fds, sizeof(fds));
        for (int k = 0; sz > 0 && k < sz / (int)PROC_PIDLISTFD_SIZE; k++) {
            if (fds[k].proc_fdtype != PROX_FDTYPE_SOCKET) continue;
            struct socket_fdinfo si;
            if (proc_pidfdinfo(pids[i], fds[k].proc_fd, PROC_PIDFDSOCKETINFO, &si, sizeof(si)) != sizeof(si)) continue;
            struct in_sockinfo *in = si.psi.soi_kind == SOCKINFO_TCP ? &si.psi.soi_proto.pri_tcp.tcpsi_ini
                                     : si.psi.soi_kind == SOCKINFO_IN ? &si.psi.soi_proto.pri_in
                                                                      : NULL;
            if (!in || !in->insi_fport) continue;
            int port = ntohs((unsigned short)in->insi_fport);
            if (in->insi_vflag & INI_IPV4) {
                if (net_far((const unsigned char *)&in->insi_faddr.ina_46.i46a_addr4, 0, port, out, n)) return 1;
            } else if (net_far((const unsigned char *)&in->insi_faddr.ina_6, 1, port, out, n))
                return 1;
        }
    }
    return 0;
}
#else
static int hexn(const char *s, int n, unsigned *v)
{
    *v = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i] | 32;
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) return 0;
        *v = *v << 4 | d;
    }
    return 1;
}

static int addr(const char *s, int v6, unsigned char *a, int *port) // kernel prints each 32 bit word in host order
{
    int words = v6 ? 4 : 1;
    for (int k = 0; k < words; k++) {
        unsigned w;
        if (!hexn(s + 8 * k, 8, &w)) return 0;
        uint32_t u = w;
        memcpy(a + 4 * k, &u, 4);
    }
    unsigned pt;
    if (s[8 * words] != ':' || !hexn(s + 8 * words + 1, 4, &pt)) return 0;
    *port = (int)pt;
    return 1;
}

int proc_net(proc_t root, char *out, int n)
{
    static int pid[4096], ppid[4096], grp[4096];
    int np = 0;
    DIR *d = opendir("/proc");
    struct dirent *e;
    while (d && (e = readdir(d)) && np < 4096) { // everyone and their parent, kids get picked out below
        int v = 0;
        const char *q = e->d_name;
        for (; *q >= '0' && *q <= '9' && v < 10000000; q++) v = v * 10 + *q - '0';
        if (*q || !v) continue;
        char path[64], buf[512];
        snprintf(path, sizeof(path), "/proc/%d/stat", v);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        ssize_t got = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (got <= 0) continue;
        buf[got] = 0;
        char *s = strrchr(buf, ')'); // the name can have spaces and brackets in it
        if (!s || s[1] != ' ' || !s[2] || s[3] != ' ') continue;
        s += 4;
        int a = 0, b = 0;
        for (; *s >= '0' && *s <= '9'; s++) a = a * 10 + *s - '0';
        if (*s++ != ' ') continue;
        for (; *s >= '0' && *s <= '9'; s++) b = b * 10 + *s - '0';
        pid[np] = v, ppid[np] = a, grp[np++] = b;
    }
    if (d) closedir(d);
    int mine[256], nm = 1;
    mine[0] = (int)root;
    for (int grew = 1; grew;) {
        grew = 0;
        for (int i = 0; i < np && nm < 256; i++) {
            int in = 0, par = 0;
            for (int k = 0; k < nm; k++) in |= mine[k] == pid[i], par |= mine[k] == ppid[i];
            if (!in && (par || grp[i] == (int)root)) mine[nm++] = pid[i], grew = 1; // plugins get their own group
        }
    }
    unsigned long ino[512];
    int ni = 0;
    for (int k = 0; k < nm && ni < 512; k++) {
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/fd", mine[k]);
        DIR *fdd = opendir(path);
        while (fdd && (e = readdir(fdd)) && ni < 512) {
            char lp[96], tgt[64];
            snprintf(lp, sizeof(lp), "%s/%.20s", path, e->d_name);
            ssize_t l = readlink(lp, tgt, sizeof(tgt) - 1);
            if (l < 9) continue;
            tgt[l] = 0;
            if (strncmp(tgt, "socket:[", 8)) continue;
            unsigned long v = 0;
            for (const char *q = tgt + 8; *q >= '0' && *q <= '9'; q++) v = v * 10 + *q - '0';
            ino[ni++] = v;
        }
        if (fdd) closedir(fdd);
    }
    if (!ni) return 0;
    static const char *tabs[] = {"/proc/net/tcp", "/proc/net/tcp6", "/proc/net/udp", "/proc/net/udp6"};
    for (int t = 0; t < 4; t++) {
        FILE *f = fopen(tabs[t], "r");
        if (!f) continue;
        char ln[512];
        int v6 = t & 1, udp = t >= 2, hit = 0;
        while (!hit && fgets(ln, sizeof(ln), f)) {
            char *w[12];
            int nw = 0;
            for (char *q = ln; *q && nw < 12;) { // sl local rem st queues tr retr uid timeout inode
                while (*q == ' ') q++;
                if (!*q || *q == '\n') break;
                w[nw++] = q;
                while (*q && *q != ' ' && *q != '\n') q++;
                if (*q) *q++ = 0;
            }
            if (nw < 10) continue;
            unsigned long v = 0;
            for (const char *q = w[9]; *q >= '0' && *q <= '9'; q++) v = v * 10 + *q - '0';
            int ours = 0;
            for (int k = 0; k < ni; k++) ours |= ino[k] == v;
            unsigned char la[16], ra[16];
            int lport, rport;
            if (!ours || !addr(w[1], v6, la, &lport) || !addr(w[2], v6, ra, &rport)) continue;
            if (rport && net_far(ra, v6, rport, out, n)) hit = 1;
            else if (!rport && lport && (udp || !strcmp(w[3], "0A")) && (!memcmp(la, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", v6 ? 16 : 4) ? snprintf(out, n, "any:%d", lport) > 0 : net_far(la, v6, lport, out, n))) { // open to the network, not just this pc
                char a[64];
                snprintf(a, sizeof(a), "%s", out);
                snprintf(out, n, "%s on %s", udp ? "udp" : "listening", a);
                hit = 1;
            }
        }
        fclose(f);
        if (hit) return 1;
    }
    return 0;
}
#endif
