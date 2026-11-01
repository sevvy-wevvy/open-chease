#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
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

int fd_wait(int fd, int ms)
{
    struct pollfd pf = {fd, POLLIN, 0};
    int r = poll(&pf, 1, ms);
    return r < 0 && errno == EINTR ? 0 : r;
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

int spawn_detached(char *const argv[])
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
