#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BIT(s) (1ULL << (s))
#define LSB(b) __builtin_ctzll(b)
#define MSB(b) (63 - __builtin_clzll(b))
#define POP(b) __builtin_popcountll(b)
#define PC(c, t) ((c) * 6 + (t))
#define COLOR(pc) ((pc) / 6)
#define TYPE(pc) ((pc) % 6)

static const u64 FILE_A = 0x0101010101010101ULL;

static u64 knight_att[64], king_att[64], pawn_att[2][64];
static u64 rays[8][64];
static const int dir_step[8] = {8, -8, 1, -1, 9, 7, -7, -9}; // n s e w ne nw se sw, a1 is 0
static const int dir_df[8] = {0, 0, 1, -1, 1, -1, 1, -1};
static const int dir_dr[8] = {1, -1, 0, 0, 1, 1, -1, -1};

static u64 z_piece[12][64], z_castle[16], z_ep[8], z_side;
static int castle_mask[64];
static u64 file_bb[8], adj_files[8], passed_mask[2][64], front_span[2][64];

static u64 rng_state = 0x9e3779b97f4a7c15ULL; // same zobrist keys every run
static u64 rnd64(void)
{
    u64 z = (rng_state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static u64 ray_attacks(int d, int sq, u64 occ)
{
    u64 a = rays[d][sq];
    u64 b = a & occ;
    if (b) {
        int s = dir_step[d] > 0 ? LSB(b) : MSB(b); // nearest blocker, rays going down hit it from the top
        a ^= rays[d][s];
    }
    return a;
}

static inline u64 bishop_att(int sq, u64 occ)
{
    return ray_attacks(4, sq, occ) | ray_attacks(5, sq, occ) | ray_attacks(6, sq, occ) | ray_attacks(7, sq, occ);
}

static inline u64 rook_att(int sq, u64 occ)
{
    return ray_attacks(0, sq, occ) | ray_attacks(1, sq, occ) | ray_attacks(2, sq, occ) | ray_attacks(3, sq, occ);
}

typedef struct {
    u64 key;
    int16_t score;
    uint16_t move16;
    int8_t depth;
    uint8_t flag;
    uint16_t pad; // 16 bytes
} TTEntry;

enum { TT_NONE, TT_EXACT, TT_LOWER, TT_UPPER };
static TTEntry *tt;
static u64 tt_mask;

void engine_tt_resize(int mb)
{
    if (mb < 1) mb = 1;
    if (mb > 4096) mb = 4096;
    u64 n = 1;
    while (n * 2 * sizeof(TTEntry) <= (u64)mb * 1024 * 1024) n *= 2; // power of 2 so the index is just key & mask
    TTEntry *t2 = NULL;
    while (n > 1 && !(t2 = calloc(n, sizeof(TTEntry)))) n /= 2;
    if (!t2) return;
    free(tt);
    tt = t2;
    tt_mask = n - 1;
}

void engine_tt_clear(void)
{
    if (tt) memset(tt, 0, (tt_mask + 1) * sizeof(TTEntry));
}

void engine_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (int s = 0; s < 64; s++) {
        int f = s & 7, r = s >> 3;
        static const int kn[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        for (int i = 0; i < 8; i++) {
            int nf = f + kn[i][0], nr = r + kn[i][1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) knight_att[s] |= BIT(nr * 8 + nf);
        }
        for (int df = -1; df <= 1; df++)
            for (int dr = -1; dr <= 1; dr++) {
                if (!df && !dr) continue;
                int nf = f + df, nr = r + dr;
                if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) king_att[s] |= BIT(nr * 8 + nf);
            }
        if (r < 7) {
            if (f > 0) pawn_att[WHITE][s] |= BIT(s + 7);
            if (f < 7) pawn_att[WHITE][s] |= BIT(s + 9);
        }
        if (r > 0) {
            if (f > 0) pawn_att[BLACK][s] |= BIT(s - 9);
            if (f < 7) pawn_att[BLACK][s] |= BIT(s - 7);
        }
        for (int d = 0; d < 8; d++) {
            int nf = f + dir_df[d], nr = r + dir_dr[d];
            while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
                rays[d][s] |= BIT(nr * 8 + nf);
                nf += dir_df[d];
                nr += dir_dr[d];
            }
        }
    }
    for (int f = 0; f < 8; f++) file_bb[f] = FILE_A << f;
    for (int f = 0; f < 8; f++) adj_files[f] = (f > 0 ? file_bb[f - 1] : 0) | (f < 7 ? file_bb[f + 1] : 0);
    for (int s = 0; s < 64; s++) {
        int f = s & 7, r = s >> 3;
        u64 files = file_bb[f] | adj_files[f];
        u64 up = 0, down = 0;
        for (int rr = r + 1; rr < 8; rr++) up |= 0xFFULL << (rr * 8);
        for (int rr = r - 1; rr >= 0; rr--) down |= 0xFFULL << (rr * 8);
        passed_mask[WHITE][s] = files & up;
        passed_mask[BLACK][s] = files & down;
        front_span[WHITE][s] = file_bb[f] & up;
        front_span[BLACK][s] = file_bb[f] & down;
    }
    for (int p = 0; p < 12; p++)
        for (int s = 0; s < 64; s++) z_piece[p][s] = rnd64();
    for (int i = 0; i < 16; i++) z_castle[i] = rnd64();
    for (int i = 0; i < 8; i++) z_ep[i] = rnd64();
    z_side = rnd64();
    for (int s = 0; s < 64; s++) castle_mask[s] = 15;
    // touching these squares kills the rights, bits are K Q k q
    castle_mask[4] = 15 & ~3;
    castle_mask[0] = 15 & ~2;
    castle_mask[7] = 15 & ~1;
    castle_mask[60] = 15 & ~12;
    castle_mask[56] = 15 & ~8;
    castle_mask[63] = 15 & ~4;
    if (!tt) engine_tt_resize(32);
}

static u64 compute_key(const Pos *p)
{
    u64 k = 0;
    for (int s = 0; s < 64; s++)
        if (p->sq[s] != EMPTY) k ^= z_piece[p->sq[s]][s];
    k ^= z_castle[p->castle];
    if (p->ep >= 0) k ^= z_ep[p->ep & 7];
    if (p->side) k ^= z_side;
    return k;
}

static inline void put_piece(Pos *p, int s, int pc)
{
    p->bb[pc] |= BIT(s);
    p->occ[COLOR(pc)] |= BIT(s);
    p->occ[2] |= BIT(s);
    p->sq[s] = pc;
    p->key ^= z_piece[pc][s];
}

static inline void remove_piece(Pos *p, int s)
{
    int pc = p->sq[s];
    p->bb[pc] &= ~BIT(s);
    p->occ[COLOR(pc)] &= ~BIT(s);
    p->occ[2] &= ~BIT(s);
    p->sq[s] = EMPTY;
    p->key ^= z_piece[pc][s];
}

static inline void move_piece(Pos *p, int from, int to)
{
    int pc = p->sq[from];
    u64 m = BIT(from) | BIT(to);
    p->bb[pc] ^= m;
    p->occ[COLOR(pc)] ^= m;
    p->occ[2] ^= m;
    p->sq[from] = EMPTY;
    p->sq[to] = pc;
    p->key ^= z_piece[pc][from] ^ z_piece[pc][to];
}

void pos_put(Pos *p, int sq, int piece)
{
    if (p->sq[sq] != EMPTY) remove_piece(p, sq);
    if (piece != EMPTY) put_piece(p, sq, piece);
}

void pos_finish_setup(Pos *p)
{
    int c = 0;
    if (p->sq[4] == PC(WHITE, KING)) {
        if (p->sq[7] == PC(WHITE, ROOK)) c |= 1;
        if (p->sq[0] == PC(WHITE, ROOK)) c |= 2;
    }
    if (p->sq[60] == PC(BLACK, KING)) {
        if (p->sq[63] == PC(BLACK, ROOK)) c |= 4;
        if (p->sq[56] == PC(BLACK, ROOK)) c |= 8;
    }
    p->castle &= c; // fens love to claim rights the board cant have
    if (p->ep >= 0) { // turn flips and piece fixes leave stale ones
        int e = p->ep, ok = e < 64 && p->sq[e] == EMPTY;
        if (ok && p->side == WHITE) ok = e >> 3 == 5 && p->sq[e - 8] == PC(BLACK, PAWN);
        else if (ok) ok = e >> 3 == 2 && p->sq[e + 8] == PC(WHITE, PAWN);
        if (!ok) p->ep = -1;
    }
    p->key = compute_key(p);
    p->nhist = 0;
}

int pos_from_fen(Pos *p, const char *fen)
{
    memset(p, 0, sizeof(*p));
    memset(p->sq, EMPTY, sizeof(p->sq));
    p->ep = -1;
    int r = 7, f = 0;
    const char *c = fen;
    for (; *c && *c != ' '; c++) {
        if (*c == '/') {
            r--;
            f = 0;
        } else if (*c >= '1' && *c <= '8') {
            f += *c - '0';
        } else {
            const char *pcs = "PNBRQKpnbrqk";
            const char *q = strchr(pcs, *c);
            if (!q || f > 7 || r < 0) return 0;
            put_piece(p, r * 8 + f, (int)(q - pcs));
            f++;
        }
    }
    while (*c == ' ') c++;
    p->side = (*c == 'b') ? BLACK : WHITE;
    if (*c) c++;
    while (*c == ' ') c++;
    for (; *c && *c != ' '; c++) {
        if (*c == 'K') p->castle |= 1;
        if (*c == 'Q') p->castle |= 2;
        if (*c == 'k') p->castle |= 4;
        if (*c == 'q') p->castle |= 8;
    }
    while (*c == ' ') c++;
    if (*c >= 'a' && *c <= 'h' && c[1] >= '1' && c[1] <= '8') {
        p->ep = (c[1] - '1') * 8 + (c[0] - 'a');
        c += 2;
    }
    while (*c && *c != ' ') c++;
    p->half = 0;
    p->fullmove = 1;
    sscanf(c, "%d %d", &p->half, &p->fullmove);
    if (!p->bb[PC(WHITE, KING)] || !p->bb[PC(BLACK, KING)]) return 0;
    pos_finish_setup(p);
    return 1;
}

void pos_startpos(Pos *p)
{
    pos_from_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void pos_to_fen(const Pos *p, char *out)
{
    const char *pcs = "PNBRQKpnbrqk";
    char *o = out;
    for (int r = 7; r >= 0; r--) {
        int e = 0;
        for (int f = 0; f < 8; f++) {
            int pc = p->sq[r * 8 + f];
            if (pc == EMPTY) {
                e++;
                continue;
            }
            if (e) *o++ = '0' + e;
            e = 0;
            *o++ = pcs[pc];
        }
        if (e) *o++ = '0' + e;
        if (r) *o++ = '/';
    }
    *o++ = ' ';
    *o++ = p->side ? 'b' : 'w';
    *o++ = ' ';
    if (!p->castle) *o++ = '-';
    if (p->castle & 1) *o++ = 'K';
    if (p->castle & 2) *o++ = 'Q';
    if (p->castle & 4) *o++ = 'k';
    if (p->castle & 8) *o++ = 'q';
    *o++ = ' ';
    if (p->ep >= 0) {
        *o++ = 'a' + (p->ep & 7);
        *o++ = '1' + (p->ep >> 3);
    } else {
        *o++ = '-';
    }
    sprintf(o, " %d %d", p->half, p->fullmove);
}

int pos_is_attacked(const Pos *p, int s, int by)
{
    if (pawn_att[by ^ 1][s] & p->bb[PC(by, PAWN)]) return 1; // other sides pawn pattern from s finds the pawns hitting it
    if (knight_att[s] & p->bb[PC(by, KNIGHT)]) return 1;
    if (king_att[s] & p->bb[PC(by, KING)]) return 1;
    u64 bq = p->bb[PC(by, BISHOP)] | p->bb[PC(by, QUEEN)];
    if (bq && (bishop_att(s, p->occ[2]) & bq)) return 1;
    u64 rq = p->bb[PC(by, ROOK)] | p->bb[PC(by, QUEEN)];
    if (rq && (rook_att(s, p->occ[2]) & rq)) return 1;
    return 0;
}

int pos_attackers(const Pos *p, int s, int by)
{
    int n = POP(pawn_att[by ^ 1][s] & p->bb[PC(by, PAWN)]) + POP(knight_att[s] & p->bb[PC(by, KNIGHT)]) +
            POP(king_att[s] & p->bb[PC(by, KING)]);
    n += POP(bishop_att(s, p->occ[2]) & (p->bb[PC(by, BISHOP)] | p->bb[PC(by, QUEEN)]));
    n += POP(rook_att(s, p->occ[2]) & (p->bb[PC(by, ROOK)] | p->bb[PC(by, QUEEN)]));
    return n;
}

// same idea as the king part of evaluate, 0 is cozy, 10 means its getting mated soon
int king_danger(const Pos *p, int side)
{
    u64 k = p->bb[PC(side, KING)];
    if (!k) return 0;
    int s = LSB(k), d = 0;
    u64 zone = king_att[s] | k, shield = king_att[s] | k;
    shield = side == WHITE ? shield << 8 : shield >> 8;
    while (zone) {
        int q = LSB(zone);
        zone &= zone - 1;
        d += 2 * pos_attackers(p, q, side ^ 1) - pos_attackers(p, q, side);
    }
    d += 3 - POP(shield & p->bb[PC(side, PAWN)]);
    if (pos_in_check(p, side)) d += 3;
    return d < 0 ? 0 : d > 20 ? 10 : d / 2;
}

int pos_in_check(const Pos *p, int side)
{
    u64 k = p->bb[PC(side, KING)];
    if (!k) return 0;
    return pos_is_attacked(p, LSB(k), side ^ 1);
}

int pos_make(Pos *p, Move m, Undo *u)
{
    int from = M_FROM(m), to = M_TO(m), fl = M_FLAG(m), promo = M_PROMO(m);
    int us = p->side, them = us ^ 1;
    int pc = p->sq[from];
    u->move = m;
    u->castle = p->castle;
    u->ep = p->ep;
    u->half = p->half;
    u->key = p->key;
    u->captured = EMPTY;
    if (p->nhist < 1024) p->hist[p->nhist] = p->key;
    p->nhist++;

    p->key ^= z_castle[p->castle]; // old rights and ep out, the new ones go back in at the end
    if (p->ep >= 0) p->key ^= z_ep[p->ep & 7];
    p->ep = -1;
    p->half++;

    if (fl == FL_EP) {
        int cs = us == WHITE ? to - 8 : to + 8;
        u->captured = p->sq[cs];
        remove_piece(p, cs);
        p->half = 0;
    } else if (p->sq[to] != EMPTY) {
        u->captured = p->sq[to];
        remove_piece(p, to);
        p->half = 0;
    }
    move_piece(p, from, to);
    if (TYPE(pc) == PAWN) {
        p->half = 0;
        if (fl == FL_DOUBLE) {
            int eps = us == WHITE ? from + 8 : from - 8;
            if (pawn_att[us][eps] & p->bb[PC(them, PAWN)]) p->ep = eps; // only if capturable, otherwise repetitions hash different
        }
        if (promo) {
            remove_piece(p, to);
            put_piece(p, to, PC(us, promo));
        }
    }
    if (fl == FL_CASTLE) {
        if (to == from + 2)
            move_piece(p, from + 3, from + 1);
        else
            move_piece(p, from - 4, from - 1);
    }
    p->castle &= castle_mask[from] & castle_mask[to];
    p->key ^= z_castle[p->castle];
    if (p->ep >= 0) p->key ^= z_ep[p->ep & 7];
    p->side = them;
    p->key ^= z_side;
    if (us == BLACK) p->fullmove++;

    if (pos_in_check(p, us)) { // gen is pseudo legal, this is where illegal ones die
        pos_unmake(p, u);
        return 0;
    }
    return 1;
}

void pos_unmake(Pos *p, const Undo *u)
{
    Move m = u->move;
    int from = M_FROM(m), to = M_TO(m), fl = M_FLAG(m), promo = M_PROMO(m);
    int us = p->side ^ 1;
    p->side = us;
    if (us == BLACK) p->fullmove--;
    if (promo) {
        remove_piece(p, to);
        put_piece(p, to, PC(us, PAWN));
    }
    if (fl == FL_CASTLE) {
        if (to == from + 2)
            move_piece(p, from + 1, from + 3);
        else
            move_piece(p, from - 1, from - 4);
    }
    move_piece(p, to, from);
    if (u->captured != EMPTY) {
        int cs = fl == FL_EP ? (us == WHITE ? to - 8 : to + 8) : to;
        put_piece(p, cs, u->captured);
    }
    p->castle = u->castle;
    p->ep = u->ep;
    p->half = u->half;
    p->key = u->key; // the piece moves above xor it around, saved one is the truth
    p->nhist--;
}

static void make_null(Pos *p, Undo *u)
{
    u->ep = p->ep;
    u->key = p->key;
    u->half = p->half;
    if (p->nhist < 1024) p->hist[p->nhist] = p->key;
    p->nhist++;
    if (p->ep >= 0) p->key ^= z_ep[p->ep & 7]; // castling cant change on a pass, just ep and side
    p->ep = -1;
    p->side ^= 1;
    p->key ^= z_side;
    p->half++;
}

static void unmake_null(Pos *p, const Undo *u)
{
    p->side ^= 1;
    p->ep = u->ep;
    p->key = u->key;
    p->half = u->half;
    p->nhist--;
}

static int gen_moves(const Pos *p, Move *out, int captures_only)
{
    int n = 0, us = p->side, them = us ^ 1;
    u64 own = p->occ[us], enemy = p->occ[them], occ = p->occ[2];
    u64 target = captures_only ? enemy : ~own;
    u64 pawns = p->bb[PC(us, PAWN)];
    int fwd = us == WHITE ? 8 : -8;
    int promo_rank = us == WHITE ? 7 : 0;
    int start_rank = us == WHITE ? 1 : 6;
    while (pawns) {
        int s = LSB(pawns);
        pawns &= pawns - 1;
        int t = s + fwd;
        if (t >= 0 && t < 64 && !(occ & BIT(t))) {
            if ((t >> 3) == promo_rank) { // queen push counts as noisy so qsearch gets it too
                out[n++] = MK_MOVE(s, t, QUEEN, 0);
                if (!captures_only) {
                    out[n++] = MK_MOVE(s, t, ROOK, 0);
                    out[n++] = MK_MOVE(s, t, BISHOP, 0);
                    out[n++] = MK_MOVE(s, t, KNIGHT, 0);
                }
            } else if (!captures_only) {
                out[n++] = MK_MOVE(s, t, 0, 0);
                if ((s >> 3) == start_rank && !(occ & BIT(t + fwd))) out[n++] = MK_MOVE(s, t + fwd, 0, FL_DOUBLE);
            }
        }
        u64 caps = pawn_att[us][s] & enemy;
        while (caps) {
            int c = LSB(caps);
            caps &= caps - 1;
            if ((c >> 3) == promo_rank) {
                out[n++] = MK_MOVE(s, c, QUEEN, 0);
                out[n++] = MK_MOVE(s, c, ROOK, 0);
                out[n++] = MK_MOVE(s, c, BISHOP, 0);
                out[n++] = MK_MOVE(s, c, KNIGHT, 0);
            } else {
                out[n++] = MK_MOVE(s, c, 0, 0);
            }
        }
        if (p->ep >= 0 && (pawn_att[us][s] & BIT(p->ep))) out[n++] = MK_MOVE(s, p->ep, 0, FL_EP);
    }
    for (int t = KNIGHT; t <= KING; t++) {
        u64 b = p->bb[PC(us, t)];
        while (b) {
            int s = LSB(b);
            b &= b - 1;
            u64 a;
            switch (t) {
            case KNIGHT: a = knight_att[s]; break;
            case BISHOP: a = bishop_att(s, occ); break;
            case ROOK: a = rook_att(s, occ); break;
            case QUEEN: a = bishop_att(s, occ) | rook_att(s, occ); break;
            default: a = king_att[s]; break;
            }
            a &= target;
            while (a) {
                int d = LSB(a);
                a &= a - 1;
                out[n++] = MK_MOVE(s, d, 0, 0);
            }
        }
    }
    if (!captures_only) {
        int k = us == WHITE ? 4 : 60;
        int kr = us == WHITE ? 1 : 4, qr = us == WHITE ? 2 : 8;
        // not out of or through check. b1/b8 only has to be empty
        if ((p->castle & kr) && !(occ & (BIT(k + 1) | BIT(k + 2))) && !pos_is_attacked(p, k, them) &&
            !pos_is_attacked(p, k + 1, them) && !pos_is_attacked(p, k + 2, them))
            out[n++] = MK_MOVE(k, k + 2, 0, FL_CASTLE);
        if ((p->castle & qr) && !(occ & (BIT(k - 1) | BIT(k - 2) | BIT(k - 3))) && !pos_is_attacked(p, k, them) &&
            !pos_is_attacked(p, k - 1, them) && !pos_is_attacked(p, k - 2, them))
            out[n++] = MK_MOVE(k, k - 2, 0, FL_CASTLE);
    }
    return n;
}

int pos_gen_legal(Pos *p, Move *out)
{
    Move tmp[256];
    int n = gen_moves(p, tmp, 0), k = 0;
    Undo u;
    for (int i = 0; i < n; i++) {
        if (pos_make(p, tmp[i], &u)) {
            pos_unmake(p, &u);
            out[k++] = tmp[i];
        }
    }
    return k;
}

void move_to_uci(Move m, char *out)
{
    if (!m) {
        strcpy(out, "0000");
        return;
    }
    int f = M_FROM(m), t = M_TO(m);
    out[0] = 'a' + (f & 7);
    out[1] = '1' + (f >> 3);
    out[2] = 'a' + (t & 7);
    out[3] = '1' + (t >> 3);
    out[4] = 0;
    if (M_PROMO(m)) {
        out[4] = " nbrq"[M_PROMO(m)];
        out[5] = 0;
    }
}

Move move_from_uci(Pos *p, const char *s)
{
    Move moves[256];
    int n = pos_gen_legal(p, moves);
    char buf[8];
    for (int i = 0; i < n; i++) {
        move_to_uci(moves[i], buf);
        if (!strncmp(buf, s, strlen(buf)) && (s[strlen(buf)] == 0 || s[strlen(buf)] == ' ' || s[strlen(buf)] == '\n')) // so e7e8 doesnt eat e7e8q
            return moves[i];
    }
    return 0;
}

u64 perft(Pos *p, int depth)
{
    if (depth == 0) return 1;
    Move moves[256];
    int n = gen_moves(p, moves, 0);
    u64 total = 0;
    Undo u;
    for (int i = 0; i < n; i++) {
        if (!pos_make(p, moves[i], &u)) continue;
        total += perft(p, depth - 1);
        pos_unmake(p, &u);
    }
    return total;
}

// pesto. a1 is index 56 here, white gets flipped
static const int piece_val_mg[6] = {82, 337, 365, 477, 1025, 0};
static const int piece_val_eg[6] = {94, 281, 297, 512, 936, 0};

static const int pst_mg[6][64] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 98, 134, 61, 95, 68, 126, 34, -11, -6, 7, 26, 31, 65, 56, 25, -20, -14, 13, 6, 21, 23, 12,
     17, -23, -27, -2, -5, 12, 17, 6, 10, -25, -26, -4, -4, -10, 3, 3, 33, -12, -35, -1, -20, -23, -15, 24, 38, -22, 0,
     0, 0, 0, 0, 0, 0, 0},
    {-167, -89, -34, -49, 61, -97, -15, -107, -73, -41, 72, 36, 23, 62, 7, -17, -47, 60, 37, 65, 84, 129, 73, 44,
     -9, 17, 19, 53, 37, 69, 18, 22, -13, 4, 16, 13, 28, 19, 21, -8, -23, -9, 12, 10, 19, 17, 25, -16, -29, -53, -12,
     -3, -1, 18, -14, -19, -105, -21, -58, -33, -17, -28, -19, -23},
    {-29, 4, -82, -37, -25, -42, 7, -8, -26, 16, -18, -13, 30, 59, 18, -47, -16, 37, 43, 40, 35, 50, 37, -2, -4, 5,
     19, 50, 37, 37, 7, -2, -6, 13, 13, 26, 34, 12, 10, 4, 0, 15, 15, 15, 14, 27, 18, 10, 4, 15, 16, 0, 7, 21, 33, 1,
     -33, -3, -14, -21, -13, -12, -39, -21},
    {32, 42, 32, 51, 63, 9, 31, 43, 27, 32, 58, 62, 80, 67, 26, 44, -5, 19, 26, 36, 17, 45, 61, 16, -24, -11, 7, 26,
     24, 35, -8, -20, -36, -26, -12, -1, 9, -7, 6, -23, -45, -25, -16, -17, 3, 0, -5, -33, -44, -16, -20, -9, -1, 11,
     -6, -71, -19, -13, 1, 17, 16, 7, -37, -26},
    {-28, 0, 29, 12, 59, 44, 43, 45, -24, -39, -5, 1, -16, 57, 28, 54, -13, -17, 7, 8, 29, 56, 47, 57, -27, -27, -16,
     -16, -1, 17, -2, 1, -9, -26, -9, -10, -2, -4, 3, -3, -14, 2, -11, -2, -5, 2, 14, 5, -35, -8, 11, 2, 8, 15, -3, 1,
     -1, -18, -9, 10, -15, -25, -31, -50},
    {-65, 23, 16, -15, -56, -34, 2, 13, 29, -1, -20, -7, -8, -4, -38, -29, -9, 24, 2, -16, -20, 6, 22, -22, -17, -20,
     -12, -27, -30, -25, -14, -36, -49, -1, -27, -39, -46, -44, -33, -51, -14, -14, -22, -46, -44, -30, -15, -27, 1, 7,
     -8, -64, -43, -16, 9, 8, -15, 36, 12, -54, 8, -28, 24, 14},
};

static const int pst_eg[6][64] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 178, 173, 158, 134, 147, 132, 165, 187, 94, 100, 85, 67, 56, 53, 82, 84, 32, 24, 13, 5,
     -2, 4, 17, 17, 13, 9, -3, -7, -7, -8, 3, -1, 4, 7, -6, 1, 0, -5, -1, -8, 13, 8, 8, 10, 13, 0, 2, -7, 0, 0, 0, 0,
     0, 0, 0, 0},
    {-58, -38, -13, -28, -31, -27, -63, -99, -25, -8, -25, -2, -9, -25, -24, -52, -24, -20, 10, 9, -1, -9, -19, -41,
     -17, 3, 22, 22, 22, 11, 8, -18, -18, -6, 16, 25, 16, 17, 4, -18, -23, -3, -1, 15, 10, -3, -20, -22, -42, -20,
     -10, -5, -2, -20, -23, -44, -29, -51, -23, -15, -22, -18, -50, -64},
    {-14, -21, -11, -8, -7, -9, -17, -24, -8, -4, 7, -12, -3, -13, -4, -14, 2, -8, 0, -1, -2, 6, 0, 4, -3, 9, 12, 9,
     14, 10, 3, 2, -6, 3, 13, 19, 7, 10, -3, -9, -12, -3, 8, 10, 13, 3, -7, -15, -14, -18, -7, -1, 4, -9, -15, -27,
     -23, -9, -23, -5, -9, -16, -5, -17},
    {13, 10, 18, 15, 12, 12, 8, 5, 11, 13, 13, 11, -3, 3, 8, 3, 7, 7, 7, 5, 4, -3, -5, -3, 4, 3, 13, 1, 2, 1, -1, 2,
     3, 5, 8, 4, -5, -6, -8, -11, -4, 0, -5, -1, -7, -12, -8, -16, -6, -6, 0, 2, -9, -9, -11, -3, -9, 2, 3, -1, -5,
     -13, 4, -20},
    {-9, 22, 22, 27, 27, 19, 10, 20, -17, 20, 32, 41, 58, 25, 30, 0, -20, 6, 9, 49, 47, 35, 19, 9, 3, 22, 24, 45, 57,
     40, 57, 36, -18, 28, 19, 47, 31, 34, 39, 23, -16, -27, 15, 6, 9, 17, 10, 5, -22, -23, -30, -16, -16, -23, -36,
     -32, -33, -28, -22, -43, -5, -32, -20, -41},
    {-74, -35, -18, -18, -11, 15, 4, -17, -12, 17, 14, 17, 17, 38, 23, 11, 10, 17, 23, 15, 20, 45, 44, 13, -8, 22, 24,
     27, 26, 33, 26, 3, -18, -4, 21, 24, 27, 23, 9, -11, -19, -3, 11, 21, 23, 16, 7, -9, -27, -11, 4, 13, 14, 4, -5,
     -17, -53, -34, -21, -11, -28, -14, -24, -43},
};

static const int phase_inc[6] = {0, 1, 1, 2, 4, 0};
static const int passed_mg[8] = {0, 0, 5, 10, 18, 30, 50, 0};
static const int passed_eg[8] = {0, 10, 15, 28, 50, 85, 130, 0};

int evaluate(const Pos *p)
{
    int mg[2] = {0, 0}, eg[2] = {0, 0}, phase = 0;
    u64 occ = p->occ[2];
    for (int c = 0; c < 2; c++) {
        u64 own = p->occ[c];
        u64 their_pawns = p->bb[PC(c ^ 1, PAWN)];
        u64 my_pawns = p->bb[PC(c, PAWN)];
        u64 enemy_pawn_att = 0;
        u64 tp = their_pawns;
        while (tp) {
            int s = LSB(tp);
            tp &= tp - 1;
            enemy_pawn_att |= pawn_att[c ^ 1][s];
        }
        for (int t = PAWN; t <= KING; t++) {
            u64 b = p->bb[PC(c, t)];
            while (b) {
                int s = LSB(b);
                b &= b - 1;
                int ix = c == WHITE ? s ^ 56 : s; // flip the rank for white, black reads them as is
                mg[c] += piece_val_mg[t] + pst_mg[t][ix];
                eg[c] += piece_val_eg[t] + pst_eg[t][ix];
                phase += phase_inc[t];
                int f = s & 7, rel_rank = c == WHITE ? (s >> 3) : 7 - (s >> 3);
                u64 mob;
                switch (t) {
                case PAWN:
                    if (!(passed_mask[c][s] & their_pawns) && !(front_span[c][s] & my_pawns)) {
                        mg[c] += passed_mg[rel_rank];
                        eg[c] += passed_eg[rel_rank];
                        if (front_span[c][s] & occ) eg[c] -= passed_eg[rel_rank] / 3; // blocked, worth a lot less
                    }
                    if (!(adj_files[f] & my_pawns)) { // isolated
                        mg[c] -= 8;
                        eg[c] -= 14;
                    }
                    if (front_span[c][s] & my_pawns) { // doubled, only the back one pays
                        mg[c] -= 10;
                        eg[c] -= 20;
                    }
                    break;
                case KNIGHT:
                    mob = knight_att[s] & ~own & ~enemy_pawn_att;
                    mg[c] += 4 * (POP(mob) - 4); // minus a normal count so avg mobility is about 0
                    eg[c] += 4 * (POP(mob) - 4);
                    break;
                case BISHOP:
                    mob = bishop_att(s, occ) & ~own & ~enemy_pawn_att;
                    mg[c] += 5 * (POP(mob) - 6);
                    eg[c] += 5 * (POP(mob) - 6);
                    break;
                case ROOK:
                    mob = rook_att(s, occ) & ~own & ~enemy_pawn_att;
                    mg[c] += 2 * (POP(mob) - 7);
                    eg[c] += 4 * (POP(mob) - 7);
                    if (!(file_bb[f] & my_pawns)) {
                        mg[c] += (file_bb[f] & their_pawns) ? 10 : 22; // half open vs fully open
                        eg[c] += (file_bb[f] & their_pawns) ? 4 : 10;
                    }
                    break;
                case QUEEN:
                    mob = (bishop_att(s, occ) | rook_att(s, occ)) & ~own & ~enemy_pawn_att;
                    mg[c] += POP(mob) - 14;
                    eg[c] += 2 * (POP(mob) - 14);
                    break;
                case KING: {
                    u64 shield = king_att[s] | BIT(s);
                    shield = c == WHITE ? shield << 8 : shield >> 8; // 3x3 box pushed one rank forward
                    mg[c] += 8 * POP(shield & my_pawns);
                    u64 zone = king_att[s];
                    int attackers = 0;
                    u64 e;
                    e = p->bb[PC(c ^ 1, KNIGHT)];
                    while (e) {
                        int q = LSB(e);
                        e &= e - 1;
                        if (knight_att[q] & zone) attackers += 2;
                    }
                    e = p->bb[PC(c ^ 1, BISHOP)];
                    while (e) {
                        int q = LSB(e);
                        e &= e - 1;
                        if (bishop_att(q, occ) & zone) attackers += 2;
                    }
                    e = p->bb[PC(c ^ 1, ROOK)];
                    while (e) {
                        int q = LSB(e);
                        e &= e - 1;
                        if (rook_att(q, occ) & zone) attackers += 3;
                    }
                    e = p->bb[PC(c ^ 1, QUEEN)];
                    while (e) {
                        int q = LSB(e);
                        e &= e - 1;
                        if ((bishop_att(q, occ) | rook_att(q, occ)) & zone) attackers += 5;
                    }
                    mg[c] -= attackers * attackers; // squared, one attacker is nothing, 3 is scary
                    break;
                }
                }
            }
        }
        if (POP(p->bb[PC(c, BISHOP)]) >= 2) {
            mg[c] += 30;
            eg[c] += 50;
        }
    }
    if (phase > 24) phase = 24;
    int us = p->side;
    int mgs = mg[us] - mg[us ^ 1], egs = eg[us] - eg[us ^ 1];
    int score = (mgs * phase + egs * (24 - phase)) / 24;
    return score + 12; // tempo
}

typedef struct {
    Pos *p;
    const SearchLimits *lim;
    u64 nodes;
    struct timespec start;
    int stopped;
    int seldepth;
    Move killers[MAX_PLY][2];
    int history[12][64];
    Move pv[MAX_PLY][MAX_PLY];
    int pvlen[MAX_PLY];
} Searcher;

static int elapsed_ms(const struct timespec *a)
{
    struct timespec b;
    clock_gettime(CLOCK_MONOTONIC, &b);
    return (int)((b.tv_sec - a->tv_sec) * 1000 + (b.tv_nsec - a->tv_nsec) / 1000000);
}

static void check_stop(Searcher *s)
{
    if (s->lim->stop && atomic_load(s->lim->stop)) s->stopped = 1;
    if (s->lim->movetime_ms > 0 && elapsed_ms(&s->start) >= s->lim->movetime_ms) s->stopped = 1;
}

static int is_repetition(const Pos *p)
{
    int lim = p->nhist - p->half;
    if (lim < 0) lim = 0;
    for (int i = p->nhist - 2; i >= lim; i -= 2) // same side to move, nothing past the last capture or pawn push
        if (i < 1024 && p->hist[i] == p->key) return 1;
    return 0;
}

static TTEntry *tt_probe(u64 key) { return &tt[key & tt_mask]; }

static void tt_store(u64 key, int depth, int score, int flag, Move m, int ply)
{
    TTEntry *e = &tt[key & tt_mask];
    if (e->key == key && depth < e->depth - 2 && flag != TT_EXACT) return; // already have it way deeper, keep that
    if (score > MATE_BOUND) score += ply; // mates go in as distance from this node, not from the root
    if (score < -MATE_BOUND) score -= ply;
    if (!m && e->key == key) m = e->move16;
    e->key = key;
    e->score = (int16_t)score;
    e->depth = (int8_t)depth;
    e->flag = (uint8_t)flag;
    e->move16 = (uint16_t)(m & 0x7fff) | (uint16_t)((M_FLAG(m) & 1) << 15); // 1 flag bit fits, tt_move_full digs the real move back up
}

static Move tt_move_full(const Pos *p, uint16_t m16)
{
    if (!m16) return 0;
    Move moves[256];
    int n = gen_moves(p, moves, 0);
    for (int i = 0; i < n; i++)
        if ((moves[i] & 0xfff) == (m16 & 0xfff) && M_PROMO(moves[i]) == ((m16 >> 12) & 7)) return moves[i];
    return 0;
}

static const int mvv[7] = {100, 320, 330, 500, 900, 2000, 0};

static void score_moves(Searcher *s, Move *moves, int *scores, int n, Move ttm, int ply)
{
    const Pos *p = s->p;
    for (int i = 0; i < n; i++) {
        Move m = moves[i];
        int to = M_TO(m);
        int victim = p->sq[to];
        if (m == ttm) {
            scores[i] = 10000000;
        } else if (victim != EMPTY || M_FLAG(m) == FL_EP) {
            int v = victim == EMPTY ? PAWN : TYPE(victim);
            scores[i] = 1000000 + mvv[v] * 10 - mvv[TYPE(p->sq[M_FROM(m)])] / 10; // mvv lva
        } else if (M_PROMO(m) == QUEEN) {
            scores[i] = 950000;
        } else if (m == s->killers[ply][0]) {
            scores[i] = 900000;
        } else if (m == s->killers[ply][1]) {
            scores[i] = 800000;
        } else {
            scores[i] = s->history[p->sq[M_FROM(m)]][to];
        }
    }
}

static Move pick(Move *moves, int *scores, int n, int i)
{
    int best = i;
    for (int j = i + 1; j < n; j++)
        if (scores[j] > scores[best]) best = j;
    Move m = moves[best];
    int sc = scores[best];
    moves[best] = moves[i];
    scores[best] = scores[i];
    moves[i] = m;
    scores[i] = sc;
    return m;
}

static int qsearch(Searcher *s, int alpha, int beta, int ply)
{
    Pos *p = s->p;
    s->nodes++;
    if ((s->nodes & 2047) == 0) check_stop(s);
    if (s->stopped) return 0;
    if (ply > s->seldepth) s->seldepth = ply;
    int stand = evaluate(p);
    if (ply >= MAX_PLY - 1) return stand;
    if (stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    Move moves[256];
    int scores[256];
    int n = gen_moves(p, moves, 1);
    score_moves(s, moves, scores, n, 0, ply);
    Undo u;
    for (int i = 0; i < n; i++) {
        Move m = pick(moves, scores, n, i);
        int victim = p->sq[M_TO(m)];
        int gain = victim == EMPTY ? 100 : mvv[TYPE(victim)];
        if (M_PROMO(m)) gain += 800;
        if (stand + gain + 200 < alpha) continue; // delta pruning, even winning this wont reach alpha
        if (!pos_make(p, m, &u)) continue;
        int sc = -qsearch(s, -beta, -alpha, ply + 1);
        pos_unmake(p, &u);
        if (s->stopped) return 0;
        if (sc > alpha) {
            alpha = sc;
            if (sc >= beta) return sc;
        }
    }
    return alpha;
}

static int has_non_pawn(const Pos *p, int c)
{
    return (p->bb[PC(c, KNIGHT)] | p->bb[PC(c, BISHOP)] | p->bb[PC(c, ROOK)] | p->bb[PC(c, QUEEN)]) != 0;
}

static int search(Searcher *s, int alpha, int beta, int depth, int ply, int do_null)
{
    Pos *p = s->p;
    int pv_node = beta - alpha > 1;
    s->pvlen[ply] = ply;
    if (ply >= MAX_PLY - 1) return evaluate(p);
    if (ply && (p->half >= 100 || is_repetition(p))) return 0;

    int in_check = pos_in_check(p, p->side);
    if (in_check) depth++; // check extension
    if (depth <= 0) return qsearch(s, alpha, beta, ply);

    s->nodes++;
    if ((s->nodes & 2047) == 0) check_stop(s);
    if (s->stopped) return 0;

    if (ply) {
        int ma = -MATE + ply, mb = MATE - ply - 1; // mate distance pruning
        if (ma > alpha) alpha = ma;
        if (mb < beta) beta = mb;
        if (alpha >= beta) return alpha;
    }

    TTEntry *e = tt_probe(p->key);
    Move ttm = 0;
    if (e->key == p->key) {
        ttm = tt_move_full(p, e->move16);
        if (!pv_node && e->depth >= depth) { // cutting in pv nodes chops the pv short
            int sc = e->score;
            if (sc > MATE_BOUND) sc -= ply;
            if (sc < -MATE_BOUND) sc += ply;
            if (e->flag == TT_EXACT || (e->flag == TT_LOWER && sc >= beta) || (e->flag == TT_UPPER && sc <= alpha))
                return sc;
        }
    }

    int static_eval = in_check ? -MATE : evaluate(p); // eval means nothing in check

    if (!pv_node && !in_check) {
        if (depth <= 6 && static_eval - 85 * depth >= beta && abs(beta) < MATE_BOUND) return static_eval; // reverse futility
        if (do_null && depth >= 3 && static_eval >= beta && has_non_pawn(p, p->side)) {
            int r = 3 + depth / 5; // null move, has_non_pawn keeps it out of zugzwang endings
            Undo nu;
            make_null(p, &nu);
            int sc = -search(s, -beta, -beta + 1, depth - 1 - r, ply + 1, 0);
            unmake_null(p, &nu);
            if (s->stopped) return 0;
            if (sc >= beta) return sc > MATE_BOUND ? beta : sc; // dont trust a mate score from a null move
        }
    }

    Move moves[256];
    int scores[256];
    int n = gen_moves(p, moves, 0);
    score_moves(s, moves, scores, n, ttm, ply);
    int legal = 0, best = -MATE - 1, flag = TT_UPPER;
    Move best_move = 0;
    Undo u;
    int futile = !pv_node && !in_check && depth <= 3 && static_eval + 120 + 90 * depth <= alpha; // so far under alpha a quiet move wont save it

    for (int i = 0; i < n; i++) {
        Move m = pick(moves, scores, n, i);
        int is_cap = p->sq[M_TO(m)] != EMPTY || M_FLAG(m) == FL_EP;
        int quiet = !is_cap && !M_PROMO(m);
        if (!pos_make(p, m, &u)) continue;
        legal++;
        int gives_check = pos_in_check(p, p->side);
        if (futile && quiet && legal > 1 && !gives_check && best > -MATE_BOUND) { // always search one so we never return a fake mate
            pos_unmake(p, &u);
            continue;
        }
        int sc;
        if (legal == 1) {
            sc = -search(s, -beta, -alpha, depth - 1, ply + 1, 1);
        } else {
            int r = 0;
            if (depth >= 3 && quiet && !in_check && !gives_check && legal > 3) {
                r = 1 + (legal > 8) + (depth > 7); // lmr
                if (pv_node) r--;
                if (m == s->killers[ply][0] || m == s->killers[ply][1]) r--;
                if (r < 0) r = 0;
                if (r > depth - 2) r = depth - 2; // dont reduce straight into qsearch
            }
            // zero window first, only re-search when it beats alpha
            sc = -search(s, -alpha - 1, -alpha, depth - 1 - r, ply + 1, 1);
            if (r && sc > alpha) sc = -search(s, -alpha - 1, -alpha, depth - 1, ply + 1, 1);
            if (sc > alpha && sc < beta) sc = -search(s, -beta, -alpha, depth - 1, ply + 1, 1);
        }
        pos_unmake(p, &u);
        if (s->stopped) return 0;
        if (sc > best) {
            best = sc;
            best_move = m;
            if (sc > alpha) {
                alpha = sc;
                flag = TT_EXACT;
                s->pv[ply][ply] = m;
                for (int j = ply + 1; j < s->pvlen[ply + 1]; j++) s->pv[ply][j] = s->pv[ply + 1][j];
                s->pvlen[ply] = s->pvlen[ply + 1] > ply + 1 ? s->pvlen[ply + 1] : ply + 1;
                if (sc >= beta) {
                    flag = TT_LOWER;
                    if (quiet) {
                        if (s->killers[ply][0] != m) {
                            s->killers[ply][1] = s->killers[ply][0];
                            s->killers[ply][0] = m;
                        }
                        int *h = &s->history[p->sq[M_FROM(m)]][M_TO(m)];
                        *h += depth * depth;
                        if (*h > 400000) { // keep history under the killer scores
                            for (int a = 0; a < 12; a++)
                                for (int b = 0; b < 64; b++) s->history[a][b] /= 2;
                        }
                    }
                    break;
                }
            }
        }
    }
    if (!legal) return in_check ? -MATE + ply : 0;
    tt_store(p->key, depth, best, flag, best_move, ply);
    return best;
}

Move engine_search(Pos *p, const SearchLimits *lim, SearchInfo *final)
{
    engine_init();
    Searcher *s = calloc(1, sizeof(Searcher));
    s->p = p;
    s->lim = lim;
    clock_gettime(CLOCK_MONOTONIC, &s->start);
    Move legal[256];
    int nlegal = pos_gen_legal(p, legal);
    Move best = nlegal ? legal[0] : 0; // something to play even if we get stopped right away
    SearchInfo info;
    memset(&info, 0, sizeof(info));
    if (final) memset(final, 0, sizeof(*final));
    int max_depth = lim->max_depth > 0 ? lim->max_depth : MAX_PLY - 10;
    int prev = 0;
    for (int d = 1; d <= max_depth && nlegal; d++) {
        s->seldepth = 0;
        int alpha = -MATE - 1, beta = MATE + 1, delta = 30;
        if (d >= 5) { // aspiration, the first few depths swing too much for it
            alpha = prev - delta;
            beta = prev + delta;
        }
        int sc;
        for (;;) {
            sc = search(s, alpha, beta, d, 0, 0);
            if (s->stopped) break;
            if (sc <= alpha) {
                alpha -= delta;
                delta *= 2;
                if (delta > 600) alpha = -MATE - 1;
            } else if (sc >= beta) {
                beta += delta;
                delta *= 2;
                if (delta > 600) beta = MATE + 1;
            } else {
                break;
            }
        }
        if (s->stopped && d > 1) break; // half done iteration, keep the last full one
        if (s->pvlen[0] > 0) best = s->pv[0][0];
        prev = sc;
        info.depth = d;
        info.seldepth = s->seldepth;
        info.score = sc;
        info.nodes = s->nodes;
        info.ms = elapsed_ms(&s->start);
        info.pvlen = s->pvlen[0];
        for (int i = 0; i < info.pvlen; i++) info.pv[i] = s->pv[0][i];
        if (!info.pvlen) {
            info.pv[0] = best;
            info.pvlen = 1;
        }
        if (final) *final = info;
        if (lim->cb) lim->cb(&info, lim->user);
        if (abs(sc) > MATE_BOUND && d > 8 && MATE - abs(sc) < d) break; // mate shorter than the depth, going deeper wont change it
        if (s->stopped) break;
        if (nlegal == 1 && d >= 4) break; // only move, dont burn the clock
    }
    free(s);
    return best;
}
