#include <stdio.h>
#include <string.h>

#include "app.h"

void move_to_san(Pos *p, Move m, char *out)
{
    int from = M_FROM(m), to = M_TO(m), pc = p->sq[from], ty = pc % 6;
    char *o = out;
    if (M_FLAG(m) == FL_CASTLE) {
        strcpy(o, (to & 7) == 6 ? "O-O" : "O-O-O");
        o += strlen(o);
    } else {
        int takes = p->sq[to] != EMPTY || M_FLAG(m) == FL_EP; // en passant lands on an empty square
        if (ty != PAWN) {
            *o++ = "PNBRQK"[ty];
            Move moves[256];
            int n = pos_gen_legal(p, moves), same_file = 0, same_rank = 0, amb = 0;
            for (int i = 0; i < n; i++) {
                Move x = moves[i];
                if (x == m || (int)M_TO(x) != to || p->sq[M_FROM(x)] != pc) continue;
                amb = 1;
                if ((int)(M_FROM(x) & 7) == (from & 7)) same_file = 1;
                if ((int)(M_FROM(x) >> 3) == (from >> 3)) same_rank = 1;
            }
            if (amb) { // pgn rules, file first and rank only if the file doesnt settle it
                if (!same_file)
                    *o++ = 'a' + (from & 7);
                else if (!same_rank)
                    *o++ = '1' + (from >> 3);
                else {
                    *o++ = 'a' + (from & 7);
                    *o++ = '1' + (from >> 3);
                }
            }
        } else if (takes) {
            *o++ = 'a' + (from & 7);
        }
        if (takes) *o++ = 'x';
        *o++ = 'a' + (to & 7);
        *o++ = '1' + (to >> 3);
        if (M_PROMO(m)) {
            *o++ = '=';
            *o++ = " NBRQ"[M_PROMO(m)];
        }
    }
    Undo u;
    if (pos_make(p, m, &u)) {
        if (pos_in_check(p, p->side)) {
            Move moves[256];
            *o++ = pos_gen_legal(p, moves) ? '+' : '#';
        }
        pos_unmake(p, &u);
    }
    *o = 0;
}

int draw_kind(const Pos *p)
{
    if (pos_repeats(p) >= 2) return 1;
    if (p->half >= 100) {
        Pos t = *p;
        Move ms[256];
        if (pos_gen_legal(&t, ms) || !pos_in_check(&t, t.side)) return 2; // mate on the 100th move still counts as mate
    }
    return 0;
}

static int twice_somewhere(const Pos *p) // a key thats there twice already, or theres no way to hit a third
{
    int lim = p->nhist - p->half;
    if (lim < 0) lim = 0;
    for (int i = p->nhist - 1; i >= lim; i--) {
        u64 k = i < 1024 ? p->hist[i] : 0;
        int n = k == p->key;
        for (int j = i - 2; j >= lim && n < 1; j -= 2) n += j < 1024 && p->hist[j] == k;
        if (k && n) return 1;
    }
    return 0;
}

int rep_after(Pos *p, Move m)
{
    if (!twice_somewhere(p)) return 0;
    Undo u;
    if (!pos_make(p, m, &u)) return 0;
    int r = pos_repeats(p) >= 2 ? 2 : 0;
    Move ms[256];
    int n = r ? 0 : pos_gen_legal(p, ms);
    for (int i = 0; i < n && !r; i++) {
        Undo v;
        if (!pos_make(p, ms[i], &v)) continue;
        if (pos_repeats(p) >= 2) r = 1;
        pos_unmake(p, &v);
    }
    pos_unmake(p, &u);
    return r;
}

static int forced(Pos *p, int d, Move *first, int *kind) // a repeat now, or a check where every answer still ends in one
{
    Move ms[256];
    int n = pos_gen_legal(p, ms);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            Undo u;
            if ((pass && !d) || !pos_make(p, ms[i], &u)) continue;
            int ok = !pass && pos_repeats(p) >= 2;
            if (pass && pos_in_check(p, p->side)) {
                Move rs[256];
                int nr = pos_gen_legal(p, rs);
                ok = nr > 0;
                for (int k = 0; k < nr && ok; k++) {
                    Undo v;
                    pos_make(p, rs[k], &v);
                    ok = pos_repeats(p) >= 2 || forced(p, d - 1, NULL, NULL);
                    pos_unmake(p, &v);
                }
            }
            pos_unmake(p, &u);
            if (ok) {
                if (first) *first = ms[i], *kind = pass + 1;
                return 1;
            }
        }
    return 0;
}

Move rep_force(Pos *p, int *kind)
{
    Move m = 0;
    *kind = 0;
    if (p->half < 3 || !forced(p, 2, &m, kind)) return 0;
    return m;
}

static int ok_score(int sc, int mate) { return mate ? mate > 0 : sc >= 100; }

Move rep_guard(const Pos *pos, const Analysis *an, Move pick, Move likely, int swap, char *out, size_t n)
{
    out[0] = 0;
    if (!an->best || pos->half < 3) return pick;
    int us = an->mate ? (an->mate > 0 ? 1000 : -1000) : an->score;
    Pos p = *pos;
    char a[16], b[16];
    if (us >= 150) {
        Move m = pick ? pick : likely;
        int r = m ? rep_after(&p, m) : 0;
        if (!r && pick && likely && likely != pick) r = rep_after(&p, m = likely);
        if (!r) return pick;
        move_to_san(&p, m, a);
        Move alt = 0;
        Move cand[3] = {an->best, an->alt[0], an->alt[1]};
        int cs[3] = {an->score, an->alt_score[0], an->alt_score[1]}, cm[3] = {an->mate, an->alt_mate[0], an->alt_mate[1]};
        for (int i = 0; i < 3 && swap && m == pick && !alt; i++)
            if (cand[i] && cand[i] != m && ok_score(cs[i], cm[i]) && !rep_after(&p, cand[i])) alt = cand[i];
        if (alt) move_to_san(&p, alt, b);
        if (r == 2) snprintf(out, n, "careful, %s repeats the position = draw%s%s%s", a, alt ? ", " : "", alt ? b : "", alt ? " keeps the win" : "");
        else snprintf(out, n, "careful, after %s they can repeat for a draw%s%s%s", a, alt ? ", " : "", alt ? b : "", alt ? " keeps the win" : "");
        return alt ? alt : pick;
    }
    if (us <= -150) {
        static u64 fk;
        static int fkind, fh = -1;
        static Move fm;
        if (fk != pos->key || fh != pos->nhist) fk = pos->key, fh = pos->nhist, fm = rep_force(&p, &fkind); // same spot can come back with a different past
        if (!fm) return pick;
        move_to_san(&p, fm, a);
        snprintf(out, n, fkind == 1 ? "you can draw by repetition with %s" : "you can force a draw by repetition with %s", a);
    }
    return pick;
}
