#include <pthread.h>
#include <string.h>

#include "app.h"

int qs_caps(Pos *p, int alpha, int beta, int d)
{
    int stand = evaluate(p);
    if (d == 0 || stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    Move ms[256];
    int n = pos_gen_legal(p, ms);
    for (int i = 0; i < n && alpha < beta; i++) {
        if (p->sq[M_TO(ms[i])] == EMPTY && M_FLAG(ms[i]) != FL_EP) continue;
        Undo u;
        if (!pos_make(p, ms[i], &u)) continue;
        int sc = -qs_caps(p, -beta, -alpha, d - 1);
        pos_unmake(p, &u);
        if (sc > alpha) alpha = sc;
    }
    return alpha;
}

int quick_search(Pos *p, int depth, int alpha, int beta) // own stack, doesnt touch the shared tables
{
    if (!depth) return qs_caps(p, alpha, beta, 4); // 4 captures deep is plenty for these picks
    Move ms[256];
    int n = pos_gen_legal(p, ms);
    if (!n) return pos_in_check(p, p->side) ? -30000 : 0;
    for (int i = 0; i < n && alpha < beta; i++) {
        Undo u;
        if (!pos_make(p, ms[i], &u)) continue;
        int sc = -quick_search(p, depth - 1, -beta, -alpha);
        pos_unmake(p, &u);
        if (sc > alpha) alpha = sc;
    }
    return alpha;
}

static const int see_val[] = {100, 300, 300, 500, 900, 2000};

static int swap_on(Pos *p, int sq) // what the side to move gets out of recapturing on sq, it can always stop
{
    Move ms[256];
    int n = pos_gen_legal(p, ms), best = 0;
    for (int i = 0; i < n; i++) {
        if ((int)M_TO(ms[i]) != sq || M_PROMO(ms[i])) continue;
        int v = see_val[p->sq[sq] % 6];
        Undo u;
        if (!pos_make(p, ms[i], &u)) continue;
        v -= swap_on(p, sq);
        pos_unmake(p, &u);
        if (v > best) best = v;
    }
    return best;
}

int see_gain(Pos *p, Move m)
{
    int vic = p->sq[M_TO(m)];
    if (M_FLAG(m) == FL_EP) vic = PAWN;
    Undo u;
    if (M_PROMO(m) || !pos_make(p, m, &u)) return 0;
    int g = (vic == EMPTY ? 0 : see_val[vic % 6]) - swap_on(p, M_TO(m));
    pos_unmake(p, &u);
    return g;
}

static void lost_on_line(const Pos *start, Move first, const Move *pv, int npv, int me, int *lost)
{
    static const int val[] = {1, 3, 3, 5, 9, 0};
    Pos t = *start;
    int down[6] = {0};
    for (int k = -1; k < npv; k++) {
        Move m = k < 0 ? first : pv[k], ms[256];
        int n = pos_gen_legal(&t, ms), ok = 0;
        for (int i = 0; i < n; i++) ok |= ms[i] == m;
        if (!ok) break;
        int vic = M_FLAG(m) == FL_EP ? (t.side ^ 1) * 6 + PAWN : t.sq[M_TO(m)];
        if (vic != EMPTY) down[vic % 6 == BISHOP ? KNIGHT : vic % 6] += vic / 6 == me ? 1 : -1;
        if (M_PROMO(m)) down[PAWN] += t.side == me ? -1 : 1, down[M_PROMO(m) == BISHOP ? KNIGHT : M_PROMO(m)] += t.side == me ? -1 : 1;
        Undo u;
        pos_make(&t, m, &u);
    }
    int net = 0;
    for (int ty = 0; ty < 5; ty++) net += down[ty] * val[ty];
    *lost = -1;
    if (net < 2) return; // the line got cut short or its all positional
    for (int ty = QUEEN; ty >= PAWN; ty--)
        if (down[ty] > 0) {
            *lost = ty;
            return;
        }
}

// captures that win stuff on a captures only count, and checks that dont drop anything, then a real search on each
int trap_scan(Pos *p, Traps *out, int ms, atomic_int *stop)
{
    static EngineTable tab;
    memset(out, 0, sizeof(*out));
    out->key = p->key;
    if (!tab.e && !engine_table_init(&tab, 8)) return 0;
    int me = p->side, base = evaluate(p);
    Move all[256], cand[6];
    int n = pos_gen_legal(p, all), nc = 0, key[6], look[6];
    for (int i = 0; i < n; i++) {
        Move m = all[i];
        int cap = p->sq[M_TO(m)] != EMPTY || M_FLAG(m) == FL_EP;
        Undo u;
        if (!pos_make(p, m, &u)) continue;
        int chk = pos_in_check(p, p->side), sc = -qs_caps(p, -99999, 99999, 4);
        pos_unmake(p, &u);
        int see = cap ? see_gain(p, m) : 0;
        if (see >= 80) sc = sc > base + 60 ? sc : base + 60;
        if (!(cap ? sc >= base + 60 : chk && sc >= base - 30)) continue;
        int k = sc + (cap ? 1000 : 0), at = nc;
        if (nc == 6) {
            if (k <= key[5]) continue;
            at = 5;
        } else {
            nc++;
        }
        while (at > 0 && key[at - 1] < k) cand[at] = cand[at - 1], key[at] = key[at - 1], look[at] = look[at - 1], at--;
        cand[at] = m, key[at] = k, look[at] = sc;
    }
    if (!nc) return 1;
    SearchLimits lim = {0};
    lim.stop = stop;
    lim.movetime_ms = ms;
    lim.max_depth = ms >= 1000 ? 20 : 10;
    SearchInfo fi;
    Pos t = *p;
    Move best_mv = engine_search_in(&t, &lim, &fi, &tab);
    if (stop && atomic_load(stop)) return 0;
    int best = fi.score, sc[6];
    Move reply[8 * 6];
    int nrep[6];
    for (int i = 0; i < nc; i++) {
        sc[i] = -MATE, nrep[i] = 0;
        if (cand[i] == best_mv) continue;
        Undo u;
        t = *p;
        pos_make(&t, cand[i], &u);
        Move ls[256];
        if (!pos_gen_legal(&t, ls)) continue; // mate or stalemate, nothing to warn about
        engine_search_in(&t, &lim, &fi, &tab);
        if (stop && atomic_load(stop)) return 0;
        sc[i] = -fi.score;
        nrep[i] = fi.pvlen < 8 ? fi.pvlen : 8;
        memcpy(reply + 8 * i, fi.pv, sizeof(Move) * nrep[i]);
        if (sc[i] > best) best = sc[i];
    }
    if (best < -MATE_BOUND) return 1; // getting mated anyway
    for (int i = 0; i < nc && out->n < 4; i++) {
        if (cand[i] == best_mv || sc[i] == -MATE) continue;
        int loss = best - sc[i];
        if (loss < 180 || look[i] - sc[i] < 180 || sc[i] > 150) continue; // still fine after it, or it never looked good
        int k = out->n++;
        out->mv[k] = cand[i];
        out->loss[k] = loss;
        out->mate[k] = sc[i] < -MATE_BOUND ? (MATE + sc[i] + 1) / 2 : 0;
        out->reply[k] = nrep[i] ? reply[8 * i] : 0;
        lost_on_line(p, cand[i], reply + 8 * i, nrep[i], me, &out->lost[k]);
    }
    return 1;
}

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static pthread_t thr;
static int started, have_want;
static Pos want;
static Traps res;
static atomic_int stop_flag;

static void *trap_thread(void *arg)
{
    for (;;) {
        pthread_mutex_lock(&mu);
        while (!have_want) pthread_cond_wait(&cv, &mu);
        Pos p = want;
        have_want = 0;
        atomic_store(&stop_flag, 0);
        pthread_mutex_unlock(&mu);
        for (int pass = 0; pass < 2; pass++) { // a quick look first, then a deeper one, some traps only show up past depth 10
            Traps t;
            if (!trap_scan(&p, &t, pass ? 1000 : 250, &stop_flag)) break;
            t.done = 1;
            pthread_mutex_lock(&mu);
            if (want.key == t.key) res = t;
            pthread_mutex_unlock(&mu);
        }
    }
    return NULL;
}

void trap_ask(const Pos *p)
{
    pthread_mutex_lock(&mu);
    if (!p) {
        want.key = 0, res.key = 0;
        atomic_store(&stop_flag, 1);
    } else if (p->key != want.key) {
        want = *p, have_want = 1;
        res.key = 0;
        atomic_store(&stop_flag, 1);
        if (!started) started = pthread_create(&thr, NULL, trap_thread, NULL) == 0;
        pthread_cond_signal(&cv);
    }
    pthread_mutex_unlock(&mu);
}

int trap_get(u64 key, Traps *t)
{
    pthread_mutex_lock(&mu);
    int ok = res.key == key && res.done;
    if (ok) *t = res;
    pthread_mutex_unlock(&mu);
    return ok;
}
