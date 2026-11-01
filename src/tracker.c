#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "templates_data.h"

#define TPL_MAGIC 0x4f435433 // OCT3, old ones were learned before fill_holes

static void load_builtin(Templates *t, int set, int k)
{
    static float filled[BUILTIN_SETS][BUILTIN_SCALES][2][6][FEAT_N * FEAT_N];
    static uint8_t done[BUILTIN_SETS][BUILTIN_SCALES];
    if (!done[set][k]) {
        for (int c = 0; c < 2; c++)
            for (int p = 0; p < 6; p++) {
                float *m = filled[set][k][c][p];
                for (int i = 0; i < FEAT_N * FEAT_N; i++) m[i] = (builtin_templates[set][k][c][p][i >> 3] >> (i & 7)) & 1;
                vision_fill_holes(m);
            }
        done[set][k] = 1;
    }
    memcpy(t->t, filled[set][k], sizeof(t->t));
}

void templates_builtin(Templates *t)
{
    memset(t, 0, sizeof(*t));
    load_builtin(t, 0, 0);
    t->have = 1;
}

int templates_load(Templates *t, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned magic = 0;
    Templates tmp;
    int ok = fread(&magic, 4, 1, f) == 1 && magic == TPL_MAGIC && fread(tmp.t, sizeof(tmp.t), 1, f) == 1 &&
             fread(&tmp.color_thr, sizeof(float), 1, f) == 1 && fgetc(f) == EOF;
    fclose(f);
    if (ok) ok = tmp.color_thr >= 0 && tmp.color_thr <= 255;
    for (int i = 0; ok && i < 2 * 6 * FEAT_N * FEAT_N; i++) {
        float v = (&tmp.t[0][0][0])[i];
        ok = v == -1 || (v >= 0 && v <= 1); // nan fails this too
    }
    if (!ok) return 0;
    memcpy(t->t, tmp.t, sizeof(tmp.t));
    t->color_thr = tmp.color_thr;
    t->have = 1;
    t->learned = 1;
    return 1;
}

void templates_save(const Templates *t, const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    unsigned magic = TPL_MAGIC;
    int ok = fwrite(&magic, 4, 1, f) == 1 && fwrite(t->t, sizeof(t->t), 1, f) == 1 &&
             fwrite(&t->color_thr, sizeof(float), 1, f) == 1;
    if (fclose(f) == 0 && ok) replace_file(tmp, path); // no half written file if it dies mid save
}

static int piece_color(int pc) { return pc / 6; }
static int piece_type(int pc) { return pc % 6; }

void templates_learn(Templates *t, const BoardRead *r, const Pos *p)
{
    static float acc[2][6][FEAT_N * FEAT_N];
    static int cnt[2][6], seen[2][6][FEAT_N * FEAT_N];
    memset(acc, 0, sizeof(acc));
    memset(cnt, 0, sizeof(cnt));
    memset(seen, 0, sizeof(seen));
    for (int sq = 0; sq < 64; sq++) {
        int pc = p->sq[sq];
        if (pc == EMPTY) continue;
        int c = piece_color(pc), ty = piece_type(pc);
        for (int i = 0; i < FEAT_N * FEAT_N; i++)
            if (r->mask[sq][i] != 0.5f) acc[c][ty][i] += r->mask[sq][i], seen[c][ty][i]++;
        cnt[c][ty]++;
    }
    float max_black = 0, min_white = 255;
    for (int sq = 0; sq < 64; sq++) {
        int pc = p->sq[sq];
        if (pc == EMPTY) continue;
        if (piece_color(pc) == WHITE && r->lum[sq] < min_white) min_white = r->lum[sq];
        if (piece_color(pc) == BLACK && r->lum[sq] > max_black) max_black = r->lum[sq];
    }
    if (min_white > max_black + 4) t->color_thr = (min_white + max_black) / 2;
    for (int c = 0; c < 2; c++)
        for (int ty = 0; ty < 6; ty++)
            if (cnt[c][ty])
                for (int i = 0; i < FEAT_N * FEAT_N; i++)
                    t->t[c][ty][i] = seen[c][ty][i] ? acc[c][ty][i] / seen[c][ty][i] : -1; // -1 = never seen
    t->have = 1;
    t->learned = 1;
}

static float similarity(const float *a, const float *b)
{
    float mn = 0, mx = 0;
    for (int i = 0; i < FEAT_N * FEAT_N; i++) {
        if (a[i] == 0.5f || b[i] < 0) continue; // coordinate labels and our overlay
        float x = a[i], y = b[i];
        mn += x < y ? x : y;
        mx += x > y ? x : y;
    }
    return mx > 0 ? mn / mx : 0;
}

static void type_scores(const Templates *t, const BoardRead *r, int sq, float *out)
{
    int c = r->obs[sq] == OBS_WHITE ? WHITE : BLACK;
    for (int ty = 0; ty < 6; ty++) {
        float a = similarity(r->mask[sq], t->t[c][ty]);
        float b = similarity(r->mask[sq], t->t[c ^ 1][ty]);
        out[ty] = a > b * 0.97f ? a : b * 0.97f; // other colors shape still counts, a bit less
    }
}

// how sure the shapes are about pc on sq: its score minus the best other type, under 0 means it looks like something else
float tracker_confidence(const Templates *t, const BoardRead *r, int sq, int pc)
{
    float sc[6], other = 0;
    type_scores(t, r, sq, sc);
    for (int ty = 0; ty < 6; ty++)
        if (ty != piece_type(pc) && sc[ty] > other) other = sc[ty];
    return sc[piece_type(pc)] - other;
}

// one square fixed by hand, nudge that pieces shape towards it
void templates_learn_one(Templates *t, const BoardRead *r, int sq, int pc)
{
    float *d = t->t[piece_color(pc)][piece_type(pc)];
    for (int i = 0; i < FEAT_N * FEAT_N; i++) {
        float v = r->mask[sq][i];
        if (v == 0.5f) continue;
        d[i] = d[i] < 0 ? v : d[i] * 0.4f + v * 0.6f;
    }
    t->have = 1;
    t->learned = 1;
}

int classify_piece(const Templates *t, const BoardRead *r, int sq)
{
    float sc[6];
    type_scores(t, r, sq, sc);
    int best = 0;
    for (int ty = 1; ty < 6; ty++)
        if (sc[ty] > sc[best]) best = ty;
    return best;
}

static void expected_obs(const Pos *p, uint8_t *out)
{
    for (int i = 0; i < 64; i++) {
        int pc = p->sq[i];
        out[i] = pc == EMPTY ? OBS_EMPTY : (piece_color(pc) == WHITE ? OBS_WHITE : OBS_BLACK);
    }
}

void tracker_start(Tracker *t, int user_color)
{
    memset(t, 0, sizeof(*t));
    t->user_color = user_color;
    t->white_bottom = user_color == WHITE;
    for (int k = 0; k < 4; k++) t->promo_ticks[k] = 10; // 10 = slot free
    pos_startpos(&t->pos);
    t->sure = ~0ULL;
    t->lock_side = -1;
    snprintf(t->status, sizeof(t->status), "tracking");
}

void tracker_lock(Tracker *t, int sq, int pc)
{
    t->locked |= 1ULL << sq;
    t->lockpc[sq] = (signed char)pc;
}

static void apply_locks(Tracker *t, const BoardRead *r)
{
    int touched = 0;
    for (int sq = 0; sq < 64; sq++) {
        if (!(t->locked >> sq & 1)) continue;
        int pc = t->lockpc[sq];
        if (pc == EMPTY || r->obs[sq] != OBS_EMPTY) {
            if (pc != EMPTY && pc % 6 == KING) // one king each, the locked one wins
                for (int k = 0; k < 64; k++)
                    if (k != sq && t->pos.sq[k] == pc && !(t->locked >> k & 1)) pos_put(&t->pos, k, EMPTY);
            pos_put(&t->pos, sq, pc);
            touched = 1;
        } else {
            t->locked &= ~(1ULL << sq);
        }
    }
    if (t->lock_side >= 0) t->pos.side = t->lock_side, touched = 1;
    if (touched) pos_finish_setup(&t->pos);
    if (t->lock_side >= 0 && pos_in_check(&t->pos, t->pos.side ^ 1)) { // that turn cant be right, the one who just moved is in check
        t->pos.side ^= 1, t->lock_side = -1;
        pos_finish_setup(&t->pos);
    }
}

static void drop_moved_locks(Tracker *t)
{
    for (int sq = 0; sq < 64; sq++)
        if ((t->locked >> sq & 1) && t->pos.sq[sq] != t->lockpc[sq]) t->locked &= ~(1ULL << sq);
    t->lock_side = -1;
}

int orientation_score(const BoardRead *r)
{
    int sc = 0;
    for (int sq = 0; sq < 64; sq++) {
        int low = (sq >> 3) < 4;
        if (r->obs[sq] == OBS_WHITE) sc += low ? 1 : -1;
        if (r->obs[sq] == OBS_BLACK) sc += low ? -1 : 1;
    }
    return sc;
}

static void diff2(const uint8_t *exp, const uint8_t *obs, int *occ_d, int *col_d)
{
    int o = 0, c = 0;
    for (int i = 0; i < 64; i++) {
        if (obs[i] == OBS_UNKNOWN) continue;
        if ((exp[i] == OBS_EMPTY) != (obs[i] == OBS_EMPTY))
            o++;
        else if (exp[i] != obs[i])
            c++;
    }
    *occ_d = o;
    *col_d = c;
}

static int promo_type(const Templates *tpl, const BoardRead *r, int sq, float margin, const Pos *p, int skip)
{
    if (r->obs[sq] == OBS_EMPTY || r->obs[sq] == OBS_UNKNOWN) return QUEEN; // still under the popup
    int c = r->obs[sq] == OBS_WHITE ? WHITE : BLACK, kinds = 0;
    float ref[6] = {-1, -1, -1, -1, -1, -1};
    // real pieces on this board beat the templates, same piece set
    for (int s2 = 0; s2 < 64; s2++) {
        int pc = p->sq[s2];
        if (s2 == sq || s2 == skip || pc == EMPTY) continue;
        int ty = piece_type(pc), pcol = piece_color(pc);
        if (ty < KNIGHT || ty > QUEEN || r->obs[s2] != (pcol == WHITE ? OBS_WHITE : OBS_BLACK)) continue;
        float v = similarity(r->mask[sq], r->mask[s2]) * (pcol == c ? 1 : 0.97f);
        if (ref[ty] < 0) kinds++;
        if (v > ref[ty]) ref[ty] = v;
    }
    if (kinds >= 2) {
        float best = -1, next = -1;
        int bt = QUEEN;
        for (int ty = KNIGHT; ty <= QUEEN; ty++)
            if (ref[ty] > best) next = best, best = ref[ty], bt = ty;
            else if (ref[ty] > next) next = ref[ty];
        if (bt != QUEEN && ref[QUEEN] >= 0 && ref[QUEEN] + 0.07f >= best) return QUEEN; // lean queen, almost everyone promotes to one
        return best >= 0.75f && best - next >= 0.08f ? bt : QUEEN;
    }
    if (!tpl->have) return QUEEN;
    float sc[6];
    type_scores(tpl, r, sq, sc);
    int best = QUEEN;
    for (int ty = KNIGHT; ty <= ROOK; ty++)
        if (sc[ty] > sc[best] + margin) best = ty;
    return best;
}

static int same_squares(Move a, Move b) { return M_FROM(a) == M_FROM(b) && M_TO(a) == M_TO(b); }

typedef struct {
    Move seq[3];
    int n, c, count;
    u64 key;
} Match;

static void consider(Match *best, const Move *path, int n, int c, u64 key)
{
    if (c < best->c) {
        best->c = c;
        best->n = n;
        best->key = key;
        best->count = 0;
        memcpy(best->seq, path, sizeof(Move) * n);
        return;
    }
    if (c > best->c || key == best->key) return; // same cost but other squares = cant tell which
    for (int i = 0; i < n; i++)
        if (!same_squares(path[i], best->seq[i])) {
            best->count++;
            return;
        }
}

static void dfs(Pos *p, const BoardRead *r, const uint8_t *base, int depth, int maxd, Move *path, Match *best)
{
    Move moves[256];
    int n = pos_gen_legal(p, moves);
    uint8_t exp[64];
    for (int i = 0; i < n; i++) {
        Undo u;
        pos_make(p, moves[i], &u);
        path[depth] = moves[i];
        if (depth + 1 == maxd) {
            expected_obs(p, exp);
            int o, c;
            diff2(exp, r->obs, &o, &c);
            // probably the piece being dragged
            int floating = 0;
            if (o == 1) {
                int want = p->side == WHITE ? OBS_WHITE : OBS_BLACK;
                for (int q = 0; q < 64; q++)
                    if (exp[q] == OBS_EMPTY && base[q] == OBS_EMPTY && r->obs[q] == want) floating = 1;
            }
            if (!o || floating) {
                int strict = 0;
                for (int q = 0; q < 64; q++)
                    if (exp[q] != base[q] && r->obs[q] != OBS_UNKNOWN && exp[q] != r->obs[q]) strict++;
                if (!strict) consider(best, path, maxd, c + (floating ? 10 : 0), p->key); // a clean match always beats a dragged one
            }
        } else {
            dfs(p, r, base, depth + 1, maxd, path, best);
        }
        pos_unmake(p, &u);
    }
}

static int find_line(Pos *start, const BoardRead *r, int maxd, Match *out)
{
    for (int d = 1; d <= maxd; d++) {
        Match best = {.c = 99};
        Move path[3];
        Pos p = *start;
        uint8_t base[64];
        expected_obs(start, base);
        dfs(&p, r, base, 0, d, path, &best);
        int ok = best.c >= 10 ? best.c - 10 <= 1 : best.c <= (d == 1 ? 2 : 1); // a square or two can read the wrong color
        if (best.c < 99 && !best.count && ok) {
            *out = best;
            return 1;
        }
        if (best.c < 99 && best.count) return 0; // two lines fit, wait for a better frame
    }
    return 0;
}

static void push_prev(Tracker *t)
{
    memmove(&t->prev[1], &t->prev[0], sizeof(Pos) * 2);
    memmove(&t->prev_sure[1], &t->prev_sure[0], sizeof(u64) * 2);
    t->prev[0] = t->pos;
    t->prev_sure[0] = t->sure;
    if (t->nprev < 3) t->nprev++;
}

static void play_line(Tracker *t, Pos from, u64 sure, const Match *m, const BoardRead *r, Templates *tpl)
{
    push_prev(t);
    t->pos = from;
    t->sure = sure;
    for (int i = 0; i < m->n; i++) {
        Move mv = m->seq[i];
        for (int k = 0; k < 4; k++)
            if ((int)M_FROM(mv) == t->promo_sq[k] || (int)M_TO(mv) == t->promo_sq[k]) t->promo_ticks[k] = 10;
        if (M_PROMO(mv)) {
            int ty = i == m->n - 1 ? promo_type(tpl, r, M_TO(mv), 0.12f, &t->pos, M_FROM(mv)) : QUEEN;
            mv = MK_MOVE(M_FROM(mv), M_TO(mv), ty, M_FLAG(mv));
            int slot = 0;
            for (int k = 0; k < 4; k++)
                if (t->promo_ticks[k] >= 10) {
                    slot = k;
                    break;
                }
            t->promo_sq[slot] = M_TO(mv);
            t->promo_color[slot] = t->pos.side;
            t->promo_ticks[slot] = 0;
            memset(t->promo_votes[slot], 0, sizeof(t->promo_votes[slot]));
            if (i == m->n - 1 && r->obs[M_TO(mv)] != OBS_UNKNOWN && r->obs[M_TO(mv)] != OBS_EMPTY && !r->hidden[M_TO(mv)]) {
                t->promo_votes[slot][promo_type(tpl, r, M_TO(mv), 0.02f, &t->pos, M_FROM(mv))]++;
                t->promo_ticks[slot] = 1;
            }
        }
        int f = M_FROM(mv), to = M_TO(mv);
        u64 was = t->sure >> f & 1;
        t->sure &= ~(1ULL << f | 1ULL << to);
        if (was && !M_PROMO(mv)) t->sure |= 1ULL << to;
        if (M_FLAG(mv) == FL_CASTLE) {
            int rf = to > f ? f + 3 : f - 4, rt = to > f ? f + 1 : f - 1;
            if (t->sure >> rf & 1) t->sure = (t->sure & ~(1ULL << rf)) | 1ULL << rt;
        }
        Undo u;
        pos_make(&t->pos, mv, &u);
        t->last_move = mv;
    }
    drop_moved_locks(t);
}

static int can_reach(const Pos *p, int ty, int color, int from, int to)
{
    int ff = from & 7, fr = from >> 3, tf = to & 7, tr = to >> 3;
    int df = tf - ff, dr = tr - fr, adf = df < 0 ? -df : df, adr = dr < 0 ? -dr : dr;
    int target = p->sq[to];
    if (ty == PAWN) {
        int dir = color == WHITE ? 1 : -1;
        if (tr == 0 || tr == 7) return 0;
        if (adf == 1 && dr == dir) return target != EMPTY;
        if (df == 0 && dr == dir) return target == EMPTY;
        if (df == 0 && dr == 2 * dir && fr == (color == WHITE ? 1 : 6))
            return target == EMPTY && p->sq[from + 8 * dir] == EMPTY;
        return 0;
    }
    if (ty == KNIGHT) return (adf == 1 && adr == 2) || (adf == 2 && adr == 1);
    int straight = df == 0 || dr == 0, diag = adf == adr;
    if (ty == ROOK && !straight) return 0;
    if (ty == BISHOP && !diag) return 0;
    if (ty == QUEEN && !straight && !diag) return 0;
    int sf = (df > 0) - (df < 0), sr = (dr > 0) - (dr < 0);
    int f = ff + sf, rr = fr + sr;
    while (f != tf || rr != tr) {
        if (p->sq[rr * 8 + f] != EMPTY) return 0;
        f += sf;
        rr += sr;
    }
    return 1;
}

static int heal_types(Tracker *t, const BoardRead *r, Templates *tpl)
{
    Pos *p = &t->pos;
    int us = p->side, want = us == WHITE ? OBS_WHITE : OBS_BLACK;
    int from = -1, to = -1, nf = 0, nt = 0;
    for (int sq = 0; sq < 64; sq++) {
        int pc = p->sq[sq], o = r->obs[sq];
        if (o == OBS_UNKNOWN) continue;
        if (pc != EMPTY && pc / 6 == us && o == OBS_EMPTY) from = sq, nf++;
        if (o == want && (pc == EMPTY || pc / 6 != us)) to = sq, nt++;
        if (pc == EMPTY && o != OBS_EMPTY && o != want) return 0;
        if (pc != EMPTY && pc / 6 != us && o == OBS_EMPTY) return 0;
    }
    if (nf != 1 || nt != 1 || ((t->locked | t->sure) >> from & 1)) return 0;
    int old = p->sq[from] % 6;
    if (old == KING || (p->sq[to] != EMPTY && p->sq[to] % 6 == KING)) return 0;
    float sc[6];
    type_scores(tpl, r, to, sc);
    int best = -1;
    for (int ty = PAWN; ty <= QUEEN; ty++) {
        if (ty == old || !can_reach(p, ty, us, from, to)) continue;
        if (best < 0 || sc[ty] > sc[best]) best = ty;
    }
    // the move only works as some other piece, so we had its type wrong
    if (best < 0 || sc[best] < sc[old] + 0.03f) return 0;
    Pos fixed = *p;
    pos_put(&fixed, from, us * 6 + best);
    Match m;
    if (!find_line(&fixed, r, 1, &m)) return 0;
    play_line(t, fixed, t->sure, &m, r, tpl);
    return 1;
}

static int apply_matching(Tracker *t, const BoardRead *r, Templates *tpl)
{
    Match m;
    int changed = 0;
    uint8_t cur[64];
    expected_obs(&t->pos, cur);
    for (int i = 0; i < 64; i++)
        if (r->obs[i] != OBS_UNKNOWN && (cur[i] == OBS_EMPTY) != (r->obs[i] == OBS_EMPTY)) changed++;
    int nw = 0, nb = 0;
    for (int i = 0; i < 64; i++) {
        nw += r->obs[i] == OBS_WHITE;
        nb += r->obs[i] == OBS_BLACK;
    }
    int extra = nw > __builtin_popcountll(t->pos.occ[WHITE]) || nb > __builtin_popcountll(t->pos.occ[BLACK]);
    int depth = changed >= 3 && !extra ? 3 : 2; // missed a frame or two, look deeper
    if (find_line(&t->pos, r, depth, &m)) {
        play_line(t, t->pos, t->sure, &m, r, tpl);
        return 1;
    }
    if (t->side_uncertain) { // after a re-read the turn is a guess
        Pos flipped = t->pos;
        flipped.side ^= 1;
        pos_finish_setup(&flipped);
        if (!pos_in_check(&flipped, flipped.side ^ 1) && find_line(&flipped, r, 2, &m)) {
            play_line(t, flipped, t->sure, &m, r, tpl);
            return 1;
        }
    }
    if (heal_types(t, r, tpl)) return 1;
    for (int k = 0; k < t->nprev && !extra; k++) { // plain takeback
        uint8_t e2[64];
        int od2, cd2;
        expected_obs(&t->prev[k], e2);
        diff2(e2, r->obs, &od2, &cd2);
        if (od2 == 0 && cd2 <= 1) {
            t->pos = t->prev[k];
            t->sure = t->prev_sure[k];
            t->last_move = 0;
            memmove(&t->prev[0], &t->prev[k + 1], sizeof(Pos) * (t->nprev - k - 1));
            memmove(&t->prev_sure[0], &t->prev_sure[k + 1], sizeof(u64) * (t->nprev - k - 1));
            t->nprev -= k + 1;
            return 1;
        }
    }
    for (int k = 0; k < t->nprev && !extra; k++) { // takeback, or we followed a wrong move
        Pos back = t->prev[k];
        if (find_line(&back, r, 2, &m) && m.c == 0) {
            Pos keep[3];
            u64 keep_sure[3];
            memcpy(keep, t->prev, sizeof(keep));
            memcpy(keep_sure, t->prev_sure, sizeof(keep_sure));
            int nk = t->nprev - k - 1;
            play_line(t, back, keep_sure[k], &m, r, tpl);
            memcpy(&t->prev[1], &keep[k + 1], sizeof(Pos) * (nk > 2 ? 2 : nk));
            memcpy(&t->prev_sure[1], &keep_sure[k + 1], sizeof(u64) * (nk > 2 ? 2 : nk));
            t->nprev = 1 + (nk > 2 ? 2 : nk);
            return 1;
        }
    }
    return 0;
}

static void verify_promo(Tracker *t, const BoardRead *r, const Templates *tpl)
{
    for (int k = 0; k < 4; k++) {
        if (t->promo_ticks[k] >= 10) continue;
        int sq = t->promo_sq[k];
        if (t->locked >> sq & 1) {
            t->promo_ticks[k] = 10;
            continue;
        }
        int pc = t->pos.sq[sq];
        if (pc == EMPTY || pc % 6 == PAWN || pc % 6 == KING || pc / 6 != t->promo_color[k]) {
            t->promo_ticks[k] = 10;
            continue;
        }
        if (r->obs[sq] == OBS_UNKNOWN || r->obs[sq] == OBS_EMPTY || r->hidden[sq]) continue; // picker still open
        t->promo_ticks[k]++;
        int ty = promo_type(tpl, r, sq, 0.02f, &t->pos, -1);
        t->promo_votes[k][ty]++;
        int best = QUEEN;
        for (int q = KNIGHT; q <= QUEEN; q++)
            if (t->promo_votes[k][q] > t->promo_votes[k][best]) best = q;
        // promo popup hides the square at first
        if (t->promo_votes[k][best] >= 2 && best != pc % 6 && t->promo_votes[k][best] * 2 > t->promo_ticks[k]) {
            pos_put(&t->pos, sq, (pc / 6) * 6 + best);
            t->promo_ticks[k] = 10;
        }
    }
}

// our own overlay reads as unknown, and the last games check ring sits right on the start squares
// one wrong color is still the start, 32 pieces on the start squares cant be anything else
static int is_start_obs(const uint8_t *obs, int rotated)
{
    int seen = 0, flips = 0;
    for (int i = 0; i < 64; i++) {
        int want = i < 16 ? OBS_WHITE : (i >= 48 ? OBS_BLACK : OBS_EMPTY), got = obs[rotated ? 63 - i : i];
        if (got == OBS_UNKNOWN) continue;
        if ((got == OBS_EMPTY) != (want == OBS_EMPTY)) return 0;
        if (got != want && ++flips > 1) return 0;
        seen++;
    }
    return seen >= 52;
}

static int is_startpos(const Pos *p)
{
    Pos s;
    pos_startpos(&s);
    return !memcmp(p->sq, s.sq, sizeof(s.sq));
}

static float board_fit(const Templates *t, const BoardRead *r)
{
    float total = 0;
    for (int sq = 0; sq < 64; sq++) {
        if (r->obs[sq] == OBS_EMPTY) continue;
        float sc[6], best = 0;
        type_scores(t, r, sq, sc);
        for (int ty = 0; ty < 6; ty++)
            if (sc[ty] > best) best = sc[ty];
        total += best;
    }
    return total;
}

static int read_position(Tracker *t, const BoardRead *r, Templates *tpl, Pos *outp);

void tracker_resync(Tracker *t, const BoardRead *r, Templates *tpl)
{
    Pos np;
    if (is_start_obs(r->obs, 0))
        pos_startpos(&np); // the shapes can be way off on a new piece set, the start is the start
    else if (!read_position(t, r, tpl, &np))
        return;
    t->pos = np;
    t->sure = is_start_obs(r->obs, 0) ? ~0ULL : 0;
    apply_locks(t, r);
    t->synced = 1;
    t->last_move = 0;
    t->side_uncertain = t->lock_side < 0;
    t->nprev = 0;
    for (int k = 0; k < 4; k++) t->promo_ticks[k] = 10;
}

static const Templates *fit_builtin(Templates *tpl, const BoardRead *r)
{
    static Templates tmp, pick;
    float best = tpl->learned ? board_fit(tpl, r) * 1.03f : -1; // learned set wins close calls
    int bs = -1, bk = 0;
    for (int set = 0; set < BUILTIN_SETS; set++)
        for (int k = 0; k < BUILTIN_SCALES; k++) {
            load_builtin(&tmp, set, k);
            float f = board_fit(&tmp, r);
            if (f > best) best = f, bs = set, bk = k;
        }
    if (bs < 0) return tpl;
    if (!tpl->learned) {
        load_builtin(tpl, bs, bk);
        tpl->have = 1;
        return tpl;
    }
    pick = *tpl;
    load_builtin(&pick, bs, bk);
    pick.learned = 0;
    return &pick;
}

static const int type_cap[6] = {8, 2, 2, 2, 1, 1};

static int read_position(Tracker *t, const BoardRead *r, Templates *tpl, Pos *outp)
{
    if (r->nhidden) return 0;
    const Templates *use = fit_builtin(tpl, r);
    float sc[64][6];
    int types[64];
    for (int sq = 0; sq < 64; sq++) {
        types[sq] = -1;
        if (r->obs[sq] != OBS_WHITE && r->obs[sq] != OBS_BLACK) continue;
        type_scores(use, r, sq, sc[sq]);
        int rank = sq >> 3;
        if (rank == 0 || rank == 7) sc[sq][PAWN] = -1;
        sc[sq][KING] = -1;
    }
    for (int c = 0; c < 2; c++) {
        int want = c == WHITE ? OBS_WHITE : OBS_BLACK, ksq = -1;
        float kbest = -2;
        for (int sq = 0; sq < 64; sq++) {
            if (r->obs[sq] != want) continue;
            float ks[6];
            type_scores(use, r, sq, ks);
            float alt = 0;
            for (int ty = 0; ty < 5; ty++)
                if (ty != PAWN && ks[ty] > alt) alt = ks[ty];
            float km = 2 * ks[KING] - alt;
            if (km > kbest) kbest = km, ksq = sq;
        }
        if (ksq < 0) return 0;
        types[ksq] = KING;
        int cap[6];
        memcpy(cap, type_cap, sizeof(cap));
        for (int sq = 0; sq < 64; sq++) {
            if (r->obs[sq] != want || sq == ksq) continue;
            int b = PAWN;
            for (int ty = 0; ty < 5; ty++)
                if (sc[sq][ty] > sc[sq][b]) b = ty;
            types[sq] = b;
        }
        for (int iter = 0; iter < 64; iter++) {
            int cnt[6] = {0};
            for (int sq = 0; sq < 64; sq++)
                if (r->obs[sq] == want && types[sq] >= 0) cnt[types[sq]]++;
            int mv_sq = -1, mv_to = -1;
            float loss = 1e9f;
            for (int sq = 0; sq < 64; sq++) {
                if (r->obs[sq] != want || types[sq] < 0 || types[sq] == KING) continue;
                int over = types[sq];
                if (over == PAWN ? cnt[PAWN] <= 8 : cnt[over] <= cap[over]) continue;
                for (int ty = 0; ty < 5; ty++) {
                    if (ty == over || sc[sq][ty] < 0) continue;
                    if (ty == PAWN ? cnt[PAWN] >= 8 : cnt[ty] >= cap[ty]) continue;
                    float l = sc[sq][over] - sc[sq][ty];
                    if (over == QUEEN && l > 0.15f) continue; // two queens happens, dont force one away
                    if (l < loss) loss = l, mv_sq = sq, mv_to = ty;
                }
            }
            if (mv_sq < 0) break;
            types[mv_sq] = mv_to;
        }
    }
    Pos np;
    pos_from_fen(&np, "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    pos_put(&np, 4, EMPTY);
    pos_put(&np, 60, EMPTY);
    int side = t->user_color;
    int hl_from_color = -1, nhl = 0;
    for (int sq = 0; sq < 64; sq++)
        if (r->highlighted[sq]) {
            nhl++;
            if (r->obs[sq] == OBS_WHITE || r->obs[sq] == OBS_BLACK) hl_from_color = r->obs[sq] == OBS_WHITE ? WHITE : BLACK;
        }
    if (nhl >= 1 && nhl <= 3 && hl_from_color >= 0) side = hl_from_color ^ 1; // the highlighted piece just moved
    for (int sq = 0; sq < 64; sq++)
        if (types[sq] >= 0) pos_put(&np, sq, (r->obs[sq] == OBS_WHITE ? 0 : 6) + types[sq]);
    np.side = side;
    np.castle = 15; // cant see castling rights from pixels
    np.ep = -1;
    np.half = 0;
    np.fullmove = 20; // just not the opening
    pos_finish_setup(&np);
    if (!np.bb[5] || !np.bb[11]) return 0;
    if (pos_in_check(&np, np.side ^ 1)) {
        np.side ^= 1;
        pos_finish_setup(&np);
    }
    *outp = np;
    return 1;
}

static int piece_diff(const Pos *a, const Pos *b)
{
    int d = 0;
    for (int i = 0; i < 64; i++) d += a->sq[i] != b->sq[i];
    return d;
}

void tracker_update(Tracker *t, const BoardRead *r, Templates *tpl)
{
    uint8_t exp[64];
    expected_obs(&t->pos, exp);
    int od, cd;
    diff2(exp, r->obs, &od, &cd);
    int start = is_start_obs(r->obs, 0) ? 1 : is_start_obs(r->obs, 1) ? 2 : 0;
    if (od == 0 && cd <= 1 && !(start == 1 && !is_startpos(&t->pos))) { // colors can match the start with the wrong pieces after a bad read
        t->pending_count = 0;
        t->mismatch_ticks = 0;
        t->implausible_ticks = 0;
        verify_promo(t, r, tpl);
        snprintf(t->status, sizeof(t->status), "in sync");
        return;
    }
    if (start) {
        int uc = t->user_color, wb = t->white_bottom;
        int rotated = start == 2;
        tracker_start(t, rotated ? uc ^ 1 : uc);
        t->white_bottom = rotated ? !wb : wb;
        t->flip_pending = rotated;
        snprintf(t->status, sizeof(t->status), "new game");
        return;
    }
    int nw = 0, nb = 0;
    for (int i = 0; i < 64; i++) {
        nw += r->obs[i] == OBS_WHITE;
        nb += r->obs[i] == OBS_BLACK;
    }
    int pw = __builtin_popcountll(t->pos.occ[WHITE]), pb = __builtin_popcountll(t->pos.occ[BLACK]);
    int implausible = nw > pw + 1 || nb > pb + 1 || (nw > pw && nb > pb); // pieces dont appear out of nowhere, its mid animation or a drag
    // has to hold still for one read first
    if (memcmp(t->pending, r->obs, 64)) {
        memcpy(t->pending, r->obs, 64);
        t->pending_count = 1;
        t->mismatch_ticks++;
        snprintf(t->status, sizeof(t->status), "board changing");
        return;
    }
    t->pending_count++;
    if (!implausible || t->side_uncertain) {
        if (apply_matching(t, r, tpl)) {
            t->pending_count = 0;
            t->mismatch_ticks = 0;
            t->implausible_ticks = 0;
            t->side_uncertain = 0;
            snprintf(t->status, sizeof(t->status), "in sync");
            return;
        }
    }
    t->mismatch_ticks++;
    if (implausible) {
        t->implausible_ticks++;
        if (t->implausible_ticks < 30) {
            snprintf(t->status, sizeof(t->status), "waiting");
            return;
        }
    }
    if (r->nhidden) {
        snprintf(t->status, sizeof(t->status), "board partly hidden");
        return;
    }
    if (t->mismatch_ticks < 6) {
        snprintf(t->status, sizeof(t->status), "reading board");
        return;
    }
    Pos np;
    int extra = nw > pw || nb > pb;
    if (read_position(t, r, tpl, &np) &&
        ((od >= 3 && !extra && piece_diff(&np, &t->pos) >= 3) || t->mismatch_ticks >= 30 ||
         (!extra && t->pending_count >= 4 && t->mismatch_ticks >= 12))) { // still for 2s and nothing fits
        tracker_resync(t, r, tpl);
        t->mismatch_ticks = 0;
        t->implausible_ticks = 0;
        snprintf(t->status, sizeof(t->status), "re-read board");
    } else {
        snprintf(t->status, sizeof(t->status), "waiting");
    }
}
