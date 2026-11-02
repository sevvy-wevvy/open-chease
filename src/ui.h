#ifndef OC_UI_H
#define OC_UI_H

// the parts of main.c that other ui files (plugui.c) draw with

#include "app.h"

enum { SCANNING, ASKING, TRACKING };

enum {
    ACT_WHITE = 1, ACT_BLACK, ACT_CLOSE, ACT_STOP, ACT_FLIP, ACT_MENU, ACT_DEBUG, ACT_FIX, ACT_RESTART, ACT_UPDATE,
    ACT_QUIT, ACT_QUIT_YES, ACT_QUIT_NO, ACT_SQ, ACT_PIECE, ACT_TURN, ACT_SETTINGS, ACT_SET, ACT_PICK, ACT_RESCAN, ACT_LIB_OPEN, ACT_LIB_CLOSE, ACT_LIB_TAB, ACT_LIB_KEY, ACT_LIB_FILTER,
    ACT_LIB_SCROLL, ACT_LIB_GET, ACT_LIB_USE, ACT_LIB_DEL, ACT_LIB_FOLDER, ACT_BLOCK, ACT_TAB, ACT_THEME, ACT_CSLOT, ACT_CCOPY,
    ACT_CPART, ACT_CSWATCH, ACT_RESET, ACT_MODE, ACT_HINT, ACT_LIB_SECOND, ACT_SLIDE, ACT_HIDE, ACT_KEYROW, ACT_SHEET, ACT_SEARCH, ACT_EXPORT,
    ACT_IMPORT, ACT_EYES, ACT_UPGRADE, ACT_COMPACT,
    ACT_PLUG_BASE // plugui.h numbers its own from here
};

typedef struct {
    const char *name;
    uint32_t panel, title, title_off, sub, btn, btn_fg, grid, arrow, menu, item, item_fg;
} Theme;

#define NTABS 10
#define TAB_PLUG NTABS // then one per plugin that wants its own
#define MAX_TABS (NTABS + 5)

extern Backend *cap;
extern int state;
extern Board board;
extern Tracker tracker;
extern char data_dir[1024];
extern int verbose, menu_open, settings_open, set_typing, dd_open, set_tab, ntabs;
extern const char *tab_names[NTABS], *all_tabs[MAX_TABS];
extern Rect set_box; // where the settings box is right now

const Theme *cur_theme(void);
long now_ms(void);
void set_note(const char *msg);
void save_cfg(void);
void redraw(void);
double kx(int out);
Rect board_logical(const Board *b);
Rect sq_rect(Rect bl, int sq, int bottom);
void add_hit(Rect r, int action);
void add_hit_arg(Rect r, int action, int arg);
void tip_spot(Rect r, const char *t);
void covers(Rect a, Rect bl);
uint32_t mix(uint32_t a, uint32_t b, double t);
uint32_t faded(uint32_t argb);
const char *fit_text(Canvas *c, const char *t, double maxw);
int wrap_text(Canvas *c, const char *t, double maxw, char out[][100], int max);
void panel_bg(Canvas *c, Rect m, double rad, uint32_t fill);
void draw_button(Canvas *c, Rect r, const char *label, uint32_t bg, uint32_t fg);
void mark_square(Canvas *c, Rect r, int style, uint32_t col);
void draw_move(Canvas *c, Rect bl, Move m, int white_bottom, uint32_t col, int arrow, double tk, int from_style, int to_style, const char *label);

#endif
