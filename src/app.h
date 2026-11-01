#ifndef OC_APP_H
#define OC_APP_H

#include <stdint.h>

#include "engine.h"
#include <stdio.h>

static inline int replace_file(const char *tmp, const char *dst)
{
#ifdef _WIN32
    remove(dst); // windows rename wont overwrite
#endif
    return rename(tmp, dst);
}

#define MAX_OUTPUTS 8

typedef struct {
    uint8_t *px;
    int w, h, stride; // stride is bytes
    int bgr;
} Frame;

typedef struct {
    double x, y, w, h;
} Rect;

typedef struct {
    uint32_t *px;
    int w, h;
    double ox, oy, scale; // where it sits in logical space, buffer px per logical px
    int dx0, dy0, dx1, dy1, cx0, cy0, cx1, cy1; // d = drawn since the clear, c = what the clear wiped. both are damage
} Canvas;

typedef struct {
    char name[64];
    Rect logical; // compositor layout coords, the frame can be bigger on hidpi
    Frame frame;
    int have_frame;
} Output;

typedef struct {
    int found;
    int out;
    double x, y, s; // frame pixels, not logical
    uint8_t light[3], dark[3];
    int score; // vision_verify wants 46+
    int soft;
} Board;

enum { OBS_EMPTY = 0, OBS_WHITE = 1, OBS_BLACK = 2, OBS_UNKNOWN = 3 }; // unknown = covered, dont guess

#define FEAT_N 24
typedef struct {
    uint8_t obs[64]; // real squares (a1 = 0), already flipped by white_bottom
    float occ_frac[64];
    float lum[64];
    uint8_t highlighted[64];
    uint8_t hidden[64]; // popup, promo picker or our own overlay on top
    int nhidden;
    float mask[64][FEAT_N * FEAT_N]; // 1 piece, 0 board, 0.5 cant tell
} BoardRead;

typedef struct Backend {
    const char *name;
    int (*init)(struct Backend *b);
    int (*capture)(struct Backend *b);
    int nout;
    Output out[MAX_OUTPUTS];
    void *priv;
    int out_gen;
} Backend;

typedef struct Overlay {
    const char *name;
    int (*init)(struct Overlay *o, Backend *cap);
    Canvas *(*canvas)(struct Overlay *o, int i); // can be NULL for a dead output
    int ncanvas;
    void (*commit)(struct Overlay *o, const Rect *hits, int nhits); // only the hits take clicks, the rest go through to the game
    int (*wait_events)(struct Overlay *o, int timeout_ms, double *click_x, double *click_y); // 0 nothing, 1 click (logical coords)
    void *priv;
    void (*keyboard)(struct Overlay *o, int on);
    int key; // wait_events 2 = key, 3 = scroll (+1 down, -1 up)
    int (*pointer)(struct Overlay *o, double *x, double *y); // 1 if the mouse is over one of our hit areas, logical coords. can be NULL
    int (*held)(struct Overlay *o, double *x, double *y); // 1 while the left button is down, for dragging sliders. can be NULL
} Overlay;

int backend_wl_init_shared(void);
Backend *backend_wl(void);
Overlay *overlay_wl(void);
Backend *backend_gnome(void); // mutter has no screencopy or layer shell, pairs with overlay_x11
Overlay *overlay_x11(void);
Backend *backend_x11(void);
// test hooks, plays back dumped .ppm frames instead of the screen
Backend *backend_win(void);
Overlay *overlay_win(void);
Backend *backend_mac(void);
Overlay *overlay_mac(void);
Backend *backend_replay(const char *dir);
Overlay *overlay_replay(void);
int replay_frame(void);

void canvas_clear(Canvas *c);
int canvas_damage(const Canvas *c, int *x0, int *y0, int *x1, int *y1);
void canvas_blit(Canvas *dst, const Canvas *src, double alpha, double dx, double dy, double clip_h); // clip_h < 0 = no clip
// logical coords from here, the canvas scales them
void draw_rect(Canvas *c, Rect r, uint32_t argb);
void draw_frame(Canvas *c, Rect r, double width, uint32_t argb);
void draw_round_rect(Canvas *c, Rect r, double radius, uint32_t argb);
void draw_line(Canvas *c, double x0, double y0, double x1, double y1, double width, uint32_t argb);
void draw_triangle(Canvas *c, double x0, double y0, double x1, double y1, double x2, double y2, uint32_t argb);
double draw_text(Canvas *c, double x, double y, const char *s, int big, uint32_t argb);
double text_width(const char *s, int big);
double text_lw(const Canvas *c, const char *s, int big);
void set_text_zoom(int z);
int canvas_alpha_at(Canvas *c, double lx, double ly);

// masked(mctx, x, y) says if a frame pixel is under our own overlay
int vision_find_board(const Frame *f, Board *b, const Rect *ignore, int nignore,
                      int (*masked)(void *, int, int), void *mctx);
int vision_verify(const Frame *f, Board *b, int (*masked)(void *, int, int), void *mctx);
void vision_fill_holes(float *m); // fills enclosed piece outlines in a FEAT_N grid
void vision_read(const Frame *f, const Board *b, int white_bottom, float color_thr, BoardRead *r,
                 int (*masked)(void *, int, int), void *mctx);

typedef struct {
    int have;
    float t[2][6][FEAT_N * FEAT_N]; // [color][type], same grid as BoardRead.mask
    int learned;
    float color_thr; // white/black lum split from the start position, 0 = work it out per read
} Templates;

void templates_builtin(Templates *t);
int templates_load(Templates *t, const char *path);
void templates_save(const Templates *t, const char *path);
void templates_learn(Templates *t, const BoardRead *r, const Pos *p);
int classify_piece(const Templates *t, const BoardRead *r, int sq);

typedef struct {
    Pos pos;
    int user_color;
    int white_bottom; // can disagree with user_color if the site draws it flipped
    int flip_pending; // main rechecks orientation once after a rotated start
    int side_uncertain;
    int implausible_ticks; // more pieces than we had, wait 30 ticks before believing it
    int promo_sq[4], promo_ticks[4], promo_color[4]; // fresh promotions still getting voted on, ticks >= 10 = free slot
    int promo_votes[4][6];
    Pos prev[3];
    int nprev;
    int synced;
    uint8_t pending[64]; // obs has to sit still a few ticks before we act on it
    int pending_count;
    int mismatch_ticks;
    Move last_move;
    char status[128];
    u64 locked; // squares the user fixed by hand, one bit each
    signed char lockpc[64];
    int lock_side;
} Tracker;

void tracker_start(Tracker *t, int user_color);
void tracker_update(Tracker *t, const BoardRead *r, Templates *tpl);
void tracker_resync(Tracker *t, const BoardRead *r, Templates *tpl);
void tracker_lock(Tracker *t, int sq, int pc);
int orientation_score(const BoardRead *r); // below 0 = we have it upside down

typedef struct {
    Move best;
    int depth;
    int score; // side to move, mates get packed near MATE
    int mate; // in moves, negative = getting mated
    u64 key;
    char engine[48], engine_base[48]; // engine can read starting... or engine died, base is the real name
    Move pv[8];
    int pvlen;
    Move alt[2];
    int alt_score[2], alt_mate[2];
} Analysis;

int analyzer_start(const char *engine_path);
int analyzer_switch(const char *engine_path); // waits up to 20s
int analyzer_switch_async(const char *engine_path);
int analyzer_switch_done(unsigned ticket); // 1 ok, 0 failed, -1 still starting
void analyzer_set_options(int threads, int hash_mb, int lines);
void analyzer_set_elo(int elo);
void analyzer_set_position(const Pos *p);
void analyzer_stop(void);
void analyzer_set_think_ms(int ms);
int analyzer_get(Analysis *a);
void uci_fen(const Pos *p, char *fen);
int pos_ok(const Pos *p); // outside engines crash on what this rejects

typedef struct {
    u64 key;
    Move best;
    int score, mate, depth, done, failed; // done = bestmove is in, failed = it wouldnt start
    char name[48];
} Opinion;

void second_set(int k, const char *path, int elo, int ms); // k 0 = second opinion, 1 = the elo capped one helper mode uses
void second_ask(int k, const Pos *p);
int second_get(int k, Opinion *o);
void second_quit(void);

#endif
