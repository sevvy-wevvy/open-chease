#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "app.h"
#include "plat.h"

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int think_ms = 8000;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static Analysis cur;
static Pos want_pos;
static int have_want, quit_flag, external, builtin_started, watch_started;
static pthread_cond_t wcv = PTHREAD_COND_INITIALIZER, rcv = PTHREAD_COND_INITIALIZER;
static char want_path[1024]; // empty = no outside engine
static unsigned gen, result_gen; // gen goes up on every switch, older threads check it and back off
static int result_ok;
static int opt_threads, opt_hash = 128, opt_lines = 1, opts_dirty, bad_hits;
static u64 bad_key;
static atomic_int stop_flag;
static pthread_t thr;

static char id_name[48];
static void uci_send(const char *s);
static FILE *uci_in;
static int uci_out_fd = -1;
static proc_t uci_pid;
static int uci_searching, uci_stop_sent;
static Pos search_pos;

static int elo, sent_elo = -1;

void analyzer_set_think_ms(int ms) { think_ms = ms; }

void analyzer_set_elo(int e)
{
    pthread_mutex_lock(&mu);
    if (e != elo) {
        elo = e;
        cur.key = 0; // next set_position redoes it at the new level
        if (external && uci_searching && !uci_stop_sent) {
            uci_stop_sent = 1;
            uci_send("stop\n");
        }
    }
    pthread_mutex_unlock(&mu);
}

static int builtin_depth(void) // rough, no elo calibration in mine
{
    if (!elo) return 0;
    return elo < 1500 ? 1 : elo < 1800 ? 2 : elo < 2100 ? 3 : 5;
}

static void fill_from_info(const SearchInfo *in, u64 key)
{
    pthread_mutex_lock(&mu);
    if (cur.key == key && in->pvlen > 0) {
        cur.best = in->pv[0];
        cur.depth = in->depth;
        cur.mate = 0;
        cur.score = in->score;
        if (in->score > MATE_BOUND) cur.mate = (MATE - in->score + 1) / 2; // plies to moves
        if (in->score < -MATE_BOUND) cur.mate = -(MATE + in->score) / 2;
        cur.pvlen = in->pvlen < 8 ? in->pvlen : 8;
        for (int i = 0; i < cur.pvlen; i++) cur.pv[i] = in->pv[i];
    }
    pthread_mutex_unlock(&mu);
}

static void builtin_cb(const SearchInfo *in, void *user) { fill_from_info(in, *(u64 *)user); }

static void *builtin_thread(void *arg)
{
    for (;;) {
        pthread_mutex_lock(&mu);
        while ((!have_want || external) && !quit_flag) pthread_cond_wait(&cv, &mu); // have_want belongs to the uci engine while its running
        if (quit_flag) {
            pthread_mutex_unlock(&mu);
            break;
        }
        Pos p = want_pos;
        have_want = 0;
        atomic_store(&stop_flag, 0);
        pthread_mutex_unlock(&mu);
        u64 key = p.key;
        SearchLimits lim = {0};
        lim.stop = &stop_flag;
        lim.movetime_ms = think_ms;
        lim.max_depth = builtin_depth();
        lim.cb = builtin_cb;
        lim.user = &key;
        engine_search(&p, &lim, NULL);
    }
    return NULL;
}

static void uci_send(const char *s)
{
    if (uci_out_fd < 0) return;
    size_t n = strlen(s);
    while (n) {
        ssize_t w = write(uci_out_fd, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

static void send_fd(int fd, const char *s)
{
    size_t n = strlen(s);
    while (fd >= 0 && n) {
        ssize_t w = write(fd, s, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return;
        s += w, n -= (size_t)w;
    }
}

static void send_opts_fd(int fd);
static void send_opts(void) { send_opts_fd(uci_out_fd); }

static void send_opts_fd(int fd)
{
    long cpus = cpu_count();
    int threads = opt_threads ? opt_threads : (cpus > 2 ? (int)(cpus / 2) : 1); // leave half for the game
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "setoption name Threads value %d\nsetoption name Hash value %d\nsetoption name MultiPV value %d\n",
             threads, opt_hash, opt_lines);
    send_fd(fd, cmd);
    opts_dirty = 0;
}

void uci_fen(const Pos *p, char *fen)
{
    pos_to_fen(p, fen);
    char *sp = strchr(fen, ' ');
    char *cs = sp ? strchr(sp + 1, ' ') : NULL;
    // castling rights are a guess, keep them only if king + rook are home
    if (cs) {
        char cr[8] = "", ok[5] = "", *o = ok;
        sscanf(cs + 1, "%7s", cr);
        int wk = p->sq[4] == KING, bk = p->sq[60] == 6 + KING;
        if (wk && p->sq[7] == ROOK && strchr(cr, 'K')) *o++ = 'K';
        if (wk && p->sq[0] == ROOK && strchr(cr, 'Q')) *o++ = 'Q';
        if (bk && p->sq[63] == 6 + ROOK && strchr(cr, 'k')) *o++ = 'k';
        if (bk && p->sq[56] == 6 + ROOK && strchr(cr, 'q')) *o++ = 'q';
        if (o == ok) *o++ = '-';
        *o = 0;
        char rest[64], *end = strchr(cs + 1, ' ');
        snprintf(rest, sizeof(rest), "%s", end ? end : "");
        snprintf(cs + 1, 128 - (size_t)(cs + 1 - fen), "%s%s", ok, rest);
    }
}

static void uci_go(const Pos *p)
{
    char fen[128], cmd[256];
    if (opts_dirty) send_opts();
    if (elo != sent_elo) {
        char opt[160];
        if (elo)
            snprintf(opt, sizeof(opt), "setoption name UCI_LimitStrength value true\nsetoption name UCI_Elo value %d\n", elo);
        else
            snprintf(opt, sizeof(opt), "setoption name UCI_LimitStrength value false\n");
        uci_send(opt);
        sent_elo = elo;
    }
    uci_fen(p, fen);
    snprintf(cmd, sizeof(cmd), "position fen %s\ngo movetime %d\n", fen, elo && think_ms > 1500 ? 1500 : think_ms); // a weak pick doesnt need 8s
    search_pos = *p;
    uci_searching = 1;
    uci_stop_sent = 0;
    uci_send(cmd);
}

static void parse_info(char *line)
{
    char *mp = strstr(line, " multipv ");
    int k = mp ? atoi(mp + 9) : 1;
    if (k < 1 || k > 3) return; // best + 2 alts is all we draw
    char *pv = strstr(line, " pv ");
    if (!pv) return;
    int depth = 0, score = 0, mate = 0, have_score = 0;
    char *d = strstr(line, " depth ");
    if (d) depth = atoi(d + 7);
    char *sc = strstr(line, " score cp ");
    if (sc) score = atoi(sc + 10), have_score = 1;
    char *sm = strstr(line, " score mate ");
    if (sm) mate = atoi(sm + 12), have_score = 1;
    // bound scores jump around on re-search
    if (!have_score || strstr(line, "lowerbound") || strstr(line, "upperbound")) return;
    Pos p = search_pos;
    char *sv;
    Move moves[8];
    int n = 0;
    char *tok = strtok_r(pv + 4, " \r\n", &sv);
    while (tok && n < 8) {
        Move m = move_from_uci(&p, tok);
        if (!m) break;
        moves[n++] = m;
        Undo u;
        pos_make(&p, m, &u);
        tok = strtok_r(NULL, " \r\n", &sv);
    }
    if (!n) return;
    pthread_mutex_lock(&mu);
    if (cur.key == search_pos.key && k > 1) {
        cur.alt[k - 2] = moves[0];
        cur.alt_score[k - 2] = mate ? (mate > 0 ? MATE - 2 * mate : -MATE - 2 * mate) : score;
        cur.alt_mate[k - 2] = mate;
    } else if (cur.key == search_pos.key) {
        if (!elo) cur.best = moves[0]; // with a cap the weaker pick only shows up in bestmove
        cur.depth = depth;
        cur.score = mate ? (mate > 0 ? MATE - 2 * mate : -MATE - 2 * mate) : score;
        cur.mate = mate;
        cur.pvlen = n;
        memcpy(cur.pv, moves, sizeof(Move) * n);
    }
    pthread_mutex_unlock(&mu);
}

static void *uci_thread(void *arg)
{
    char line[8192];
    while (fgets(line, sizeof(line), uci_in)) {
        if (!strncmp(line, "bestmove", 8)) {
            pthread_mutex_lock(&mu);
            // after a stop its the bestmove for the old position, toss it
            if (elo && uci_searching && !uci_stop_sent && cur.key == search_pos.key) {
                char *sv2, *tok = strtok_r(line + 8, " \r\n", &sv2);
                Move m = tok ? move_from_uci(&search_pos, tok) : 0;
                if (m) cur.best = m;
            }
            uci_searching = 0;
            uci_stop_sent = 0;
            if (have_want) { // board changed mid search, engine is free now so go again
                have_want = 0;
                Pos p = want_pos;
                uci_go(&p);
            }
            pthread_mutex_unlock(&mu);
        } else if (!strncmp(line, "info", 4)) {
            pthread_mutex_lock(&mu);
            int stale = !uci_searching || uci_stop_sent;
            pthread_mutex_unlock(&mu);
            if (!stale) parse_info(line);
        }
    }
    return NULL;
}

// stockfish segfaults on a missing king etc
int pos_ok(const Pos *p)
{
    int kings[2] = {0, 0};
    for (int sq = 0; sq < 64; sq++) {
        int pc = p->sq[sq];
        if (pc == EMPTY) continue;
        if (pc % 6 == KING) kings[pc / 6]++;
        if (pc % 6 == PAWN && (sq < 8 || sq >= 56)) return 0;
    }
    if (kings[0] != 1 || kings[1] != 1) return 0;
    return !pos_in_check(p, p->side ^ 1); // side that just moved cant be in check
}

static void grab_id(const char *buf)
{
    const char *p = strstr(buf, "id name ");
    if (!p) return;
    p += 8;
    size_t n = strcspn(p, "\r\n");
    const char *by = strstr(p, " by ");
    if (by && (size_t)(by - p) < n) n = (size_t)(by - p);
    if (n > sizeof(id_name) - 1) n = sizeof(id_name) - 1;
    if (n) memcpy(id_name, p, n), id_name[n] = 0;
}

static int read_until(int fd, const char *token, int ms) // raw reads, a hung engine can't block us
{
    char buf[4096];
    size_t n = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if ((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000 > ms * 3) return 0; // one that never stops talking but never says uciok
        if (fd_wait(fd, ms) <= 0) return 0;
        if (n > sizeof(buf) - 256) { // keep a tail incase the token got split between reads
            memmove(buf, buf + n - 64, 64);
            n = 64;
        }
        ssize_t r = read(fd, buf + n, sizeof(buf) - 1 - n);
        if (r <= 0) return 0;
        n += (size_t)r;
        buf[n] = 0;
        grab_id(buf);
        if (strstr(buf, token)) return 1;
    }
}

static int start_uci(const char *path, proc_t *pidp, int *outp, FILE **inp)
{
    proc_t pid;
    int to_child, from_child;
    if (!proc_start(path, 0, &pid, &to_child, &from_child)) return 0;
    no_sigpipe();
    id_name[0] = 0;
    send_fd(to_child, "uci\n");
    int ok = read_until(from_child, "uciok", 5000);
    if (ok) {
        pthread_mutex_lock(&mu);
        send_opts_fd(to_child);
        pthread_mutex_unlock(&mu);
        send_fd(to_child, "isready\n");
        ok = read_until(from_child, "readyok", 10000); // net loading
    }
    FILE *in = ok ? fdopen(from_child, "r") : NULL;
    if (!in) {
        proc_kill(pid);
        proc_reap(pid);
        close(from_child), close(to_child);
        return 0;
    }
    *pidp = pid, *outp = to_child, *inp = in;
    return 1;
}

static void name_from_path(const char *path)
{
    if (*id_name) {
        snprintf(cur.engine, sizeof(cur.engine), "%s", id_name);
        snprintf(cur.engine_base, sizeof(cur.engine_base), "%s", id_name);
        return;
    }
    const char *base = strrchr(path, '/'), *bs = strrchr(path, '\\');
    if (bs > base) base = bs;
    snprintf(cur.engine, sizeof(cur.engine), "%.47s", base ? base + 1 : path);
    char *dash = strchr(cur.engine, '-');
    if (dash && !strncmp(cur.engine, "stockfish", 9)) *dash = 0; // stockfish-ubuntu-x86-64-avx2 is way too long
    snprintf(cur.engine_base, sizeof(cur.engine_base), "%s", cur.engine);
}

static void use_builtin(void)
{
    external = 0;
    snprintf(cur.engine, sizeof(cur.engine), "open-chease");
    snprintf(cur.engine_base, sizeof(cur.engine_base), "open-chease");
    if (!builtin_started) {
        builtin_started = 1;
        engine_tt_resize(64);
        pthread_create(&thr, NULL, builtin_thread, NULL);
    }
}

static void *uci_watch(void *arg) // owns the outside engine: start it, read it, clean up, restart if it died on us
{
    for (;;) {
        pthread_mutex_lock(&mu);
        while (!quit_flag && !*want_path) pthread_cond_wait(&wcv, &mu);
        if (quit_flag) {
            pthread_mutex_unlock(&mu);
            break;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s", want_path);
        unsigned g = gen;
        pthread_mutex_unlock(&mu);

        proc_t pid = 0;
        int ofd = -1;
        FILE *in = NULL;
        int ok = start_uci(path, &pid, &ofd, &in);
        pthread_mutex_lock(&mu);
        if (g != gen) { // got switched while it was starting
            pthread_mutex_unlock(&mu);
            if (ok) proc_kill(pid), proc_reap(pid), fclose(in), close(ofd);
            continue;
        }
        if (!ok) {
            want_path[0] = 0;
            use_builtin();
            cur.key = 0;
            result_gen = g, result_ok = 0;
            pthread_cond_broadcast(&rcv);
            pthread_mutex_unlock(&mu);
            continue;
        }
        uci_pid = pid, uci_out_fd = ofd, uci_in = in;
        external = 1;
        uci_searching = uci_stop_sent = 0;
        sent_elo = -1;
        name_from_path(path);
        cur.key = 0;
        result_gen = g, result_ok = 1;
        pthread_cond_broadcast(&rcv);
        pthread_mutex_unlock(&mu);

        uci_thread(NULL);

        pthread_mutex_lock(&mu);
        int died = g == gen && !quit_flag; // a switch kills it on purpose, thats not a crash
        uci_pid = 0, uci_out_fd = -1, uci_in = NULL;
        uci_searching = uci_stop_sent = 0;
        if (died) {
            if (search_pos.key == bad_key) bad_hits++;
            else bad_key = search_pos.key, bad_hits = 1;
            snprintf(cur.engine, sizeof(cur.engine), "engine died");
        }
        Pos last = search_pos;
        pthread_mutex_unlock(&mu);
        fclose(in);
        close(ofd);
        proc_kill(pid); // it may still be alive if only the pipe broke
        proc_reap(pid);
        if (died) {
            char fen[128];
            pos_to_fen(&last, fen);
            fprintf(stderr, "engine died (last position %s), restarting it\n", fen);
            usleep(500000); // dont spin if it dies right on startup
        }
    }
    return NULL;
}

int analyzer_start(const char *engine_path)
{
    engine_init();
    return analyzer_switch(engine_path);
}

int analyzer_switch(const char *engine_path)
{
    pthread_mutex_lock(&mu);
    cur.key = 0;
    cur.best = 0;
    cur.alt[0] = cur.alt[1] = 0;
    atomic_store(&stop_flag, 1);
    gen++;
    unsigned g = gen;
    proc_t old = uci_pid;
    if (!engine_path || !*engine_path || !is_exec(engine_path)) {
        want_path[0] = 0;
        use_builtin();
        pthread_mutex_unlock(&mu);
        proc_kill(old);
        return 0;
    }
    snprintf(want_path, sizeof(want_path), "%s", engine_path);
    snprintf(cur.engine, sizeof(cur.engine), "starting...");
    if (!watch_started) {
        watch_started = 1;
        pthread_t w;
        pthread_create(&w, NULL, uci_watch, NULL);
    }
    pthread_cond_signal(&wcv);
    pthread_mutex_unlock(&mu);
    proc_kill(old); // uci_watch gets eof and picks up the new path
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += 20; // 5s uciok + 10s readyok and some slack

    pthread_mutex_lock(&mu);
    while (result_gen != g && gen == g)
        if (pthread_cond_timedwait(&rcv, &mu, &until)) break;
    int ok = result_gen == g && result_ok;
    pthread_mutex_unlock(&mu);
    return ok;
}

void analyzer_set_options(int threads, int hash_mb, int lines)
{
    pthread_mutex_lock(&mu);
    if (threads != opt_threads || hash_mb != opt_hash || lines != opt_lines) {
        opt_threads = threads, opt_hash = hash_mb, opt_lines = lines;
        opts_dirty = 1;
        cur.key = 0;
        cur.alt[0] = cur.alt[1] = 0;
        if (external && uci_searching && !uci_stop_sent) {
            uci_stop_sent = 1;
            uci_send("stop\n");
        }
    }
    pthread_mutex_unlock(&mu);
}

void analyzer_set_position(const Pos *p)
{
    pthread_mutex_lock(&mu);
    if (cur.key == p->key && cur.key) {
        pthread_mutex_unlock(&mu);
        return;
    }
    cur.key = p->key;
    cur.best = 0;
    cur.depth = 0;
    cur.pvlen = 0;
    cur.mate = 0;
    cur.score = 0;
    cur.alt[0] = cur.alt[1] = 0;
    if (!pos_ok(p) || (p->key == bad_key && bad_hits >= 2)) { // that one keeps killing it
        pthread_mutex_unlock(&mu);
        return;
    }
    if (external) {
        if (uci_searching) {
            want_pos = *p;
            have_want = 1;
            if (!uci_stop_sent) {
                uci_stop_sent = 1;
                uci_send("stop\n");
            }
        } else {
            have_want = 0;
            uci_go(p);
        }
    } else {
        want_pos = *p;
        have_want = 1;
        atomic_store(&stop_flag, 1);
        pthread_cond_signal(&cv);
    }
    pthread_mutex_unlock(&mu);
}

void analyzer_stop(void)
{
    pthread_mutex_lock(&mu);
    cur.key = 0;
    cur.best = 0;
    if (external) {
        if (uci_searching && !uci_stop_sent) {
            uci_stop_sent = 1;
            uci_send("stop\n");
        }
        have_want = 0;
    } else {
        atomic_store(&stop_flag, 1);
    }
    pthread_mutex_unlock(&mu);
}

int analyzer_get(Analysis *a)
{
    pthread_mutex_lock(&mu);
    *a = cur;
    pthread_mutex_unlock(&mu);
    return a->best != 0;
}
