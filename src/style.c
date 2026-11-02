#include <math.h>
#include <stdlib.h>

#include "app.h"

static const int margin[] = {0, 35, 35, 40, 60, 35}; // how far under the best line each style may go, centipawns

static int king_dist(int a, int b)
{
    int f = abs((a & 7) - (b & 7)), r = abs((a >> 3) - (b >> 3));
    return f > r ? f : r;
}

// how much a normal player is expected to drop answering m. replies that look good on a captures only count
// get picked way more, so a bait they cant resist scores high
static int bait(const Pos *p, Move m)
{
    static u64 bk[8];
    static Move bm[8];
    static int bv[8], bn;
    for (int i = 0; i < 8; i++)
        if (bk[i] == p->key && bm[i] == m) return bv[i]; // the engine lines shuffle while it thinks, dont redo the same move
    Pos t = *p;
    Undo u;
    if (!pos_make(&t, m, &u)) return 0;
    Move rs[256];
    int n = pos_gen_legal(&t, rs), deep[256], look[256], best = -99999, top = -99999;
    if (!n) return 0;
    for (int i = 0; i < n; i++) {
        Undo v;
        pos_make(&t, rs[i], &v);
        look[i] = -qs_caps(&t, -99999, 99999, 4);
        pos_unmake(&t, &v);
        if (look[i] > top) top = look[i];
    }
    for (int pass = 0; pass < 2; pass++) // best looking ones first so the window gets tight early
        for (int i = 0; i < n; i++) {
            deep[i] = pass ? deep[i] : 99999;
            if ((look[i] >= top - 60) == pass || look[i] < top - 250) continue; // nobody plays the ones that look awful
            Undo v;
            pos_make(&t, rs[i], &v);
            deep[i] = -quick_search(&t, 1, -99999, best > -99999 ? 600 - best : 99999);
            pos_unmake(&t, &v);
            if (deep[i] > best) best = deep[i];
        }
    double tot = 0, lost = 0;
    for (int i = 0; i < n; i++) {
        double w = exp((look[i] - top) / 90.0);
        int to = M_TO(rs[i]);
        if (t.sq[to] != EMPTY) w *= to == (int)M_TO(m) ? 2.5 : 1.6; // people look at recaptures first
        Undo v;
        pos_make(&t, rs[i], &v);
        if (pos_in_check(&t, t.side)) w *= 1.5;
        pos_unmake(&t, &v);
        tot += w;
        if (deep[i] < 99999) lost += w * (best - deep[i] > 600 ? 600 : best - deep[i]);
    }
    int k = bn++ & 7;
    bk[k] = p->key, bm[k] = m, bv[k] = (int)(lost / tot);
    return bv[k];
}

static int pawn_shape(const Pos *p, int me) // doubled and lonely pawns, lower is better
{
    u64 pw = p->bb[me * 6 + PAWN];
    int bad = 0, files[8] = {0};
    for (u64 b = pw; b; b &= b - 1) files[__builtin_ctzll(b) & 7]++;
    for (int f = 0; f < 8; f++) {
        if (files[f] > 1) bad += files[f] - 1;
        if (files[f] && (f == 0 || !files[f - 1]) && (f == 7 || !files[f + 1])) bad += files[f];
    }
    return bad;
}

static int space(const Pos *p, int me) // own pawns on c to f past the middle, plus squares we hit over there
{
    int s = 0;
    for (u64 b = p->bb[me * 6 + PAWN]; b; b &= b - 1) {
        int sq = __builtin_ctzll(b), f = sq & 7, r = me == WHITE ? sq >> 3 : 7 - (sq >> 3);
        if (f >= 2 && f <= 5 && r >= 3) s += r - 2;
    }
    for (int sq = 0; sq < 64; sq++) {
        int r = me == WHITE ? sq >> 3 : 7 - (sq >> 3);
        if (r >= 4 && pos_is_attacked(p, sq, me)) s++;
    }
    return s;
}

static int loose(const Pos *p, int sq, int me)
{
    int pc = p->sq[sq];
    return pc != EMPTY && pc / 6 == me && pc % 6 != KING && pos_is_attacked(p, sq, me ^ 1) && !pos_is_attacked(p, sq, me);
}

static int mobility(const Pos *p, int me)
{
    Pos t = *p;
    t.side = me, t.ep = -1;
    Move ms[256];
    return pos_in_check(&t, me ^ 1) ? 0 : pos_gen_legal(&t, ms);
}

Move style_choose(const Pos *pos, const Analysis *an, int style)
{
    if (!an->best || style < 1 || style > 5) return an->best;
    Move cand[3] = {an->best, 0, 0};
    int sc[3] = {an->score, 0, 0}, nc = 1;
    for (int k = 0; k < 2; k++)
        if (an->alt[k] && !an->mate && !an->alt_mate[k] && an->score - an->alt_score[k] <= margin[style])
            cand[nc] = an->alt[k], sc[nc++] = an->alt_score[k];
    if (nc == 1) return an->best;
    static u64 ck;
    static Move cm, cb, ca0, ca1;
    static int cs;
    if (ck == pos->key && cs == style && cb == an->best && ca0 == an->alt[0] && ca1 == an->alt[1]) return cm; // tricky is slow, dont redo it every frame
    int me = pos->side, best = -99999, bi = 0;
    u64 ek = pos->bb[(me ^ 1) * 6 + KING];
    int ksq = ek ? __builtin_ctzll(ek) : 0, kd0 = king_danger(pos, me);
    int shape0 = pawn_shape(pos, me), space0 = space(pos, me), mob0 = mobility(pos, me);
    for (int i = 0; i < nc; i++) {
        Pos t = *pos;
        Move m = cand[i];
        int to = M_TO(m), vic = t.sq[to], cap = vic != EMPTY || M_FLAG(m) == FL_EP, pawn = t.sq[M_FROM(m)] % 6 == PAWN;
        int closer = king_dist(M_FROM(m), ksq) - king_dist(to, ksq), gain = style == 4 ? see_gain(&t, m) : 0;
        int cost = an->score - sc[i]; // how much worse than the best line, 0 for the best
        Undo u;
        if (!pos_make(&t, m, &u)) continue;
        int chk = pos_in_check(&t, me ^ 1), v;
        if (style == 1) {
            v = 3 * chk + 2 * cap + closer + (pawn && (me == WHITE ? to >> 3 >= 4 : to >> 3 <= 3));
        } else if (style == 2) {
            int safe = pos_attackers(&t, to, me) >= pos_attackers(&t, to, me ^ 1);
            v = 3 * (M_FLAG(m) == FL_CASTLE) + (safe ? 2 : -2) + (kd0 - king_danger(&t, me)) - cap;
        } else if (style == 3) {
            int b = bait(pos, m);
            v = b < 25 ? -cost : b - cost; // nothing to fall for, then its just the best line
        } else if (style == 4) {
            int sac = gain < 0 ? -gain : 0; // what we give on that square, the engine says we get it back
            for (int sq = 0; sq < 64 && !sac; sq++) // or leaving something loose to take, like a gambit pawn
                if (loose(&t, sq, me) && !(sq != to && pos->sq[sq] == t.sq[sq] && loose(pos, sq, me)))
                    sac = t.sq[sq] % 6 == PAWN ? 100 : 300;
            v = sac ? sac + 40 * chk - cost : 10 * (3 * chk + 2 * cap + closer) - 100 - cost;
        } else {
            int trade = cap && vic != EMPTY && vic % 6 != PAWN && pos_is_attacked(&t, to, me ^ 1);
            v = 3 * (space(&t, me) - space0) + (mobility(&t, me) - mob0) - 6 * (pawn_shape(&t, me) - shape0) - 12 * trade -
                4 * cap - 2 * chk - cost / 5;
        }
        if (v > best) best = v, bi = i; // ties keep the engines order
    }
    ck = pos->key, cs = style, cb = an->best, ca0 = an->alt[0], ca1 = an->alt[1], cm = cand[bi];
    return cm;
}
