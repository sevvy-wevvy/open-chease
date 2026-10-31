#ifndef OC_ENGINE_H
#define OC_ENGINE_H

#include <stdint.h>
#include <stdatomic.h>

typedef uint64_t u64;
typedef uint32_t Move; // 0 = no move

enum { WHITE, BLACK };
enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
#define EMPTY 12 // pieces are color * 6 + type
#define MATE 30000
#define MATE_BOUND (MATE - 1000) // anything past this is a mate score
#define MAX_PLY 128

#define M_FROM(m) ((m) & 63)
#define M_TO(m) (((m) >> 6) & 63)
#define M_PROMO(m) (((m) >> 12) & 7) // a piece type, pawn is 0 so 0 = none
#define M_FLAG(m) (((m) >> 15) & 7)
#define MK_MOVE(f, t, p, fl) ((Move)((f) | ((t) << 6) | ((p) << 12) | ((fl) << 15)))
enum { FL_NONE, FL_DOUBLE, FL_EP, FL_CASTLE };

typedef struct {
    u64 bb[12];
    u64 occ[3];
    uint8_t sq[64];
    int side, castle, ep, half, fullmove; // castle bits KQkq = 1 2 4 8, ep -1 if none
    u64 key;
    int nhist;
    u64 hist[1024]; // keys for repetition, nhist keeps counting past the end
} Pos;

typedef struct {
    Move move;
    int captured, castle, ep, half;
    u64 key;
} Undo;

typedef struct {
    int depth, seldepth, score;
    u64 nodes;
    int ms;
    Move pv[MAX_PLY];
    int pvlen;
} SearchInfo;

typedef void (*info_cb)(const SearchInfo *info, void *user);

typedef struct {
    int max_depth; // 0 = as deep as it gets
    int movetime_ms;
    atomic_int *stop; // set from another thread to bail
    info_cb cb;
    void *user;
} SearchLimits;

void engine_init(void);
void engine_tt_resize(int mb);
void engine_tt_clear(void);

int pos_from_fen(Pos *p, const char *fen);
void pos_to_fen(const Pos *p, char *out);
void pos_startpos(Pos *p);
int pos_make(Pos *p, Move m, Undo *u); // 0 = illegal, already undone
void pos_unmake(Pos *p, const Undo *u);
int pos_in_check(const Pos *p, int side);
int pos_gen_legal(Pos *p, Move *out);
int pos_is_attacked(const Pos *p, int sq, int by);
void pos_put(Pos *p, int sq, int piece);
void pos_finish_setup(Pos *p); // after pos_put, fixes castling, the key and clears hist

void move_to_uci(Move m, char *out);
Move move_from_uci(Pos *p, const char *s); // 0 if its not legal here

u64 perft(Pos *p, int depth);
int evaluate(const Pos *p);
Move engine_search(Pos *p, const SearchLimits *lim, SearchInfo *final);

#endif
