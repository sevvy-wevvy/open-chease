#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app.h"

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    char path[1024], running[1024], failed[1024];
    int elo, ms, started;
    Pos want;
    int have_want;
    u64 asked; // so a late answer for an old position gets dropped
    Opinion res;
    pid_t pid;
    int out, in, has_elo;
    char buf[8192];
    size_t nbuf;
    char name[48];
} Slot;

static Slot slots[2] = {{.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER},
                        {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER}};

static void put(Slot *s, const char *str)
{
    size_t n = strlen(str);
    while (s->out >= 0 && n) {
        ssize_t w = write(s->out, str, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return;
        str += w, n -= (size_t)w;
    }
}

static int num(const char *p)
{
    int neg = *p == '-', v = 0;
    if (neg) p++;
    while (*p >= '0' && *p <= '9') v = v * 10 + *p++ - '0';
    return neg ? -v : v;
}

static int get_line(Slot *s, char *line, size_t n, int ms)
{
    for (;;) {
        char *nl = memchr(s->buf, '\n', s->nbuf);
        if (nl) {
            size_t len = (size_t)(nl - s->buf);
            size_t c = len < n - 1 ? len : n - 1;
            memcpy(line, s->buf, c);
            line[c] = 0;
            memmove(s->buf, nl + 1, s->nbuf - len - 1);
            s->nbuf -= len + 1;
            return 1;
        }
        if (s->nbuf >= sizeof(s->buf) - 1) s->nbuf = 0; // 8k with no newline, just throw it out
        struct pollfd p = {s->in, POLLIN, 0};
        int r = poll(&p, 1, ms);
        if (r == 0) return 0;
        if (r < 0) return errno == EINTR ? 0 : -1;
        ssize_t got = read(s->in, s->buf + s->nbuf, sizeof(s->buf) - 1 - s->nbuf);
        if (got <= 0) return -1;
        s->nbuf += (size_t)got;
    }
}

static void stop_engine(Slot *s)
{
    if (s->pid > 0) {
        put(s, "quit\n");
        kill(-s->pid, SIGKILL); // whole group, some engines are shell wrappers
        waitpid(s->pid, NULL, 0);
    }
    if (s->out >= 0) close(s->out);
    if (s->in >= 0) close(s->in);
    s->pid = 0, s->out = s->in = -1, s->nbuf = 0;
    s->running[0] = 0;
}

static int wait_for(Slot *s, const char *token, int ms)
{
    char line[1024];
    for (int spent = 0; spent < ms;) {
        int r = get_line(s, line, sizeof(line), 200);
        if (r < 0) return 0;
        if (!r) {
            spent += 200;
            continue;
        }
        if (!strncmp(line, "id name ", 8)) {
            char *by = strstr(line + 8, " by ");
            if (by) *by = 0;
            size_t l = strlen(line + 8);
            if (l > sizeof(s->name) - 1) l = sizeof(s->name) - 1;
            memcpy(s->name, line + 8, l);
            s->name[l] = 0;
        }
        if (strstr(line, "option name UCI_Elo ")) s->has_elo = 1; // no UCI_Elo = depth cap later
        if (!strncmp(line, token, strlen(token))) return 1;
    }
    return 0;
}

static int start_engine(Slot *s, const char *path)
{
    int to[2], from[2];
    if (pipe(to)) return 0;
    if (pipe(from)) {
        close(to[0]), close(to[1]);
        return 0;
    }
    for (int k = 0; k < 2; k++) fcntl(to[k], F_SETFD, FD_CLOEXEC), fcntl(from[k], F_SETFD, FD_CLOEXEC); // main engine forks too, it mustnt hold our pipes open
    pid_t pid = fork();
    if (pid < 0) {
        close(to[0]), close(to[1]), close(from[0]), close(from[1]);
        return 0;
    }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(to[0], 0);
        dup2(from[1], 1);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, 2); // keep its spam out of our terminal
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
    signal(SIGPIPE, SIG_IGN);
    s->pid = pid, s->out = to[1], s->in = from[0], s->nbuf = 0, s->has_elo = 0, s->name[0] = 0;
    put(s, "uci\n");
    if (!wait_for(s, "uciok", 5000)) {
        stop_engine(s);
        return 0;
    }
    put(s, "setoption name Threads value 1\nsetoption name Hash value 32\nisready\n"); // side job, the main engine gets the cpu
    if (!wait_for(s, "readyok", 30000)) { // long, nobody is waiting on this thread
        stop_engine(s);
        return 0;
    }
    if (!*s->name) {
        const char *b = strrchr(path, '/');
        const char *nm = b ? b + 1 : path;
        size_t l = strlen(nm);
        if (l > 47) l = 47;
        memcpy(s->name, nm, l);
        s->name[l] = 0;
    }
    strcpy(s->running, path);
    return 1;
}

static void *slot_thread(void *arg)
{
    Slot *s = arg;
    int sent_elo = -1;
    pthread_mutex_lock(&s->mu);
    for (;;) {
        if (!*s->path && s->pid > 0) {
            pthread_mutex_unlock(&s->mu);
            stop_engine(s);
            pthread_mutex_lock(&s->mu);
            continue;
        }
        // a failed path sits here till second_set clears it
        if (!s->have_want || !*s->path || !strcmp(s->path, s->failed)) {
            pthread_cond_wait(&s->cv, &s->mu);
            continue;
        }
        if (strcmp(s->path, s->running)) {
            char path[1024];
            strcpy(path, s->path);
            pthread_mutex_unlock(&s->mu);
            stop_engine(s);
            int ok = start_engine(s, path);
            pthread_mutex_lock(&s->mu);
            sent_elo = -1;
            if (!ok) {
                strcpy(s->failed, path);
                s->res.failed = 1;
                continue;
            }
            memcpy(s->res.name, s->name, sizeof(s->name));
            s->res.failed = 0;
            if (strcmp(s->path, path)) continue; // switched again while it was starting
        }
        Pos p = s->want;
        s->have_want = 0;
        int elo = s->elo, ms = s->ms;
        pthread_mutex_unlock(&s->mu);

        char fen[128], cmd[320];
        uci_fen(&p, fen);
        if (elo != sent_elo && s->has_elo) {
            if (elo)
                snprintf(cmd, sizeof(cmd), "setoption name UCI_LimitStrength value true\nsetoption name UCI_Elo value %d\n", elo);
            else
                snprintf(cmd, sizeof(cmd), "setoption name UCI_LimitStrength value false\n");
            put(s, cmd);
            sent_elo = elo;
        }
        // cant cap it with UCI_Elo so cap the depth, same table as the builtin
        int depth = elo && !s->has_elo ? (elo < 1500 ? 1 : elo < 1800 ? 2 : elo < 2100 ? 3 : 5) : 0;
        if (depth)
            snprintf(cmd, sizeof(cmd), "position fen %s\ngo depth %d\n", fen, depth);
        else
            snprintf(cmd, sizeof(cmd), "position fen %s\ngo movetime %d\n", fen, ms);
        put(s, cmd);
        Opinion o = {0};
        o.key = p.key;
        int stopped = 0, dead = 0, waited = 0, limit = (depth ? 20000 : ms) + 3000; // depth runs have no clock
        char line[4096];
        for (;;) {
            int r = get_line(s, line, sizeof(line), 100);
            if (r < 0) {
                dead = 1;
                break;
            }
            if (!r) {
                waited += 100;
                if (waited > limit) {
                    if (stopped) {
                        dead = 1;
                        break;
                    }
                    put(s, "stop\n"), stopped = 1, waited = limit - 2500; // 2.5s to answer the stop or its hung
                    continue;
                }
                pthread_mutex_lock(&s->mu);
                int moved_on = s->have_want || strcmp(s->path, s->running);
                pthread_mutex_unlock(&s->mu);
                if (moved_on && !stopped) put(s, "stop\n"), stopped = 1; // nobody wants this answer anymore
                continue;
            }
            if (!strncmp(line, "info", 4) && !strstr(line, "bound")) { // lower/upperbound scores are junk
                char *mp = strstr(line, " multipv ");
                if (mp && num(mp + 9) != 1) continue;
                char *d = strstr(line, " depth "), *cp = strstr(line, " score cp "), *mt = strstr(line, " score mate ");
                if (d) o.depth = num(d + 7);
                if (cp) o.score = num(cp + 10), o.mate = 0;
                if (mt) {
                    o.mate = num(mt + 12);
                    o.score = o.mate > 0 ? MATE - 2 * o.mate : -MATE - 2 * o.mate;
                }
            } else if (!strncmp(line, "bestmove ", 9)) {
                char mv[8] = "";
                snprintf(mv, sizeof(mv), "%.7s", line + 9);
                mv[strcspn(mv, " \r")] = 0;
                Pos t = p;
                o.best = move_from_uci(&t, mv);
                break;
            }
        }
        pthread_mutex_lock(&s->mu);
        if (dead) {
            stop_engine(s);
            continue;
        }
        if (!stopped && o.best && o.key == s->asked) { // a stopped search is half done, dont show it
            memcpy(o.name, s->name, sizeof(o.name));
            o.done = 1;
            s->res = o;
        }
    }
    return NULL;
}

void second_set(int k, const char *path, int elo, int ms)
{
    Slot *s = &slots[k];
    pthread_mutex_lock(&s->mu);
    if (strcmp(s->path, path) || elo != s->elo) s->res.key = 0, s->res.done = 0, s->asked = 0;
    s->failed[0] = 0;
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->elo = elo;
    s->ms = ms;
    if (!s->started && *path) {
        s->started = 1;
        s->out = s->in = -1;
        pthread_t t;
        pthread_create(&t, NULL, slot_thread, s);
        pthread_detach(t);
    }
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mu);
}

void second_ask(int k, const Pos *p)
{
    Slot *s = &slots[k];
    pthread_mutex_lock(&s->mu);
    if (*s->path && p->key != s->asked && pos_ok(p)) {
        s->asked = p->key;
        s->want = *p;
        s->have_want = 1;
        pthread_cond_signal(&s->cv);
    }
    pthread_mutex_unlock(&s->mu);
}

int second_get(int k, Opinion *o)
{
    Slot *s = &slots[k];
    pthread_mutex_lock(&s->mu);
    *o = s->res;
    o->failed = *s->path && !strcmp(s->path, s->failed);
    int ok = *s->path && s->res.done;
    pthread_mutex_unlock(&s->mu);
    return ok;
}

void second_quit(void)
{
    for (int k = 0; k < 2; k++)
        if (slots[k].pid > 0) kill(-slots[k].pid, SIGKILL); // no waitpid, were exiting anyway

}
