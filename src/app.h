#ifndef OC_APP_H
#define OC_APP_H

#include <stdint.h>

#include "engine.h"

#define MAX_OUTPUTS 8

typedef struct {
    uint8_t *px;
    int w, h, stride;
    int bgr;
} Frame;

typedef struct {
    double x, y, w, h;
} Rect;

typedef struct {
    uint32_t *px;
    int w, h;
    double ox, oy, scale;
    int dx0, dy0, dx1, dy1, cx0, cy0, cx1, cy1;
} Canvas;

typedef struct {
    char name[64];
    Rect logical;
    Frame frame;
    int have_frame;
} Output;

typedef struct {
    int found;
    int out;
    double x, y, s; // frame pixels, not logical
    uint8_t light[3], dark[3];
    int score;
    int soft;
} Board;

enum { OBS_EMPTY = 0, OBS_WHITE = 1, OBS_BLACK = 2, OBS_UNKNOWN = 3 };

#define FEAT_N 24
typedef struct {
    uint8_t obs[64];
    float occ_frac[64];
    float lum[64];
    uint8_t highlighted[64];
    uint8_t hidden[64];
    int nhidden;
    float mask[64][FEAT_N * FEAT_N];
} BoardRead;

typedef struct Backend {
    const char *name;
    int (*init)(struct Backend *b);
    int (*capture)(struct Backend *b);
    int nout;
    Output out[MAX_OUTPUTS];
    void *priv;
} Backend;

typedef struct Overlay {
    const char *name;
    int (*init)(struct Overlay *o, Backend *cap);
    Canvas *(*canvas)(struct Overlay *o, int i);
    int ncanvas;
    void (*commit)(struct Overlay *o, const Rect *hits, int nhits);
    int (*wait_events)(struct Overlay *o, int timeout_ms, double *click_x, double *click_y);
    void *priv;
    void (*keyboard)(struct Overlay *o, int on);
    int key; // wait_events 2 = key, 3 = scroll (+1 down, -1 up)
} Overlay;

int backend_wl_init_shared(void);
Backend *backend_wl(void);
Overlay *overlay_wl(void);
Backend *backend_gnome(void);
Overlay *overlay_x11(void);
Backend *backend_x11(void);
Backend *backend_replay(const char *dir);
Overlay *overlay_replay(void);
int replay_frame(void);

void canvas_clear(Canvas *c);
int canvas_damage(const Canvas *c, int *x0, int *y0, int *x1, int *y1);
void canvas_blit(Canvas *dst, const Canvas *src, double alpha, double dx, double dy, double clip_h);
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

int vision_find_board(const Frame *f, Board *b, const Rect *ignore, int nignore,
                      int (*masked)(void *, int, int), void *mctx);
int vision_verify(const Frame *f, Board *b, int (*masked)(void *, int, int), void *mctx);
void vision_read(const Frame *f, const Board *b, int white_bottom, float color_thr, BoardRead *r,
                 int (*masked)(void *, int, int), void *mctx);

typedef struct {
    int have;
    float t[2][6][FEAT_N * FEAT_N];
    int learned;
    float color_thr;
} Templates;

void templates_builtin(Templates *t);
int templates_load(Templates *t, const char *path);
void templates_save(const Templates *t, const char *path);
void templates_learn(Templates *t, const BoardRead *r, const Pos *p);
int classify_piece(const Templates *t, const BoardRead *r, int sq);

typedef struct {
    Pos pos;
    int user_color;
    int white_bottom;
    int flip_pending;
    int side_uncertain;
    int implausible_ticks;
    int promo_sq[4], promo_ticks[4], promo_color[4];
    int promo_votes[4][6];
    Pos prev[3];
    int nprev;
    int synced;
    uint8_t pending[64];
    int pending_count;
    int mismatch_ticks;
    Move last_move;
    char status[128];
    u64 locked;
    signed char lockpc[64];
    int lock_side;
} Tracker;

void tracker_start(Tracker *t, int user_color);
void tracker_update(Tracker *t, const BoardRead *r, Templates *tpl);
void tracker_resync(Tracker *t, const BoardRead *r, Templates *tpl);
void tracker_lock(Tracker *t, int sq, int pc);
int orientation_score(const BoardRead *r);

typedef struct {
    Move best;
    int depth;
    int score;
    int mate;
    u64 key;
    char engine[48], engine_base[48];
    Move pv[8];
    int pvlen;
    Move alt[2];
    int alt_score[2], alt_mate[2];
} Analysis;

int analyzer_start(const char *engine_path);
int analyzer_switch(const char *engine_path);
void analyzer_set_options(int threads, int hash_mb, int lines);
void analyzer_set_elo(int elo);
void analyzer_set_position(const Pos *p);
void analyzer_stop(void);
void analyzer_set_think_ms(int ms);
int analyzer_get(Analysis *a);

#endif
