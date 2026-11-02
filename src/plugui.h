#ifndef OC_PLUGUI_H
#define OC_PLUGUI_H

#include "plugin.h"
#include "ui.h"

enum {
    ACT_PLUG_ON = ACT_PLUG_BASE, ACT_PLUG_DIR, ACT_PLUG_SET, ACT_PLUG_SLIDE, ACT_PLUG_URL, ACT_PLUG_PASTE, ACT_PLUG_ADD, ACT_PLUG_TEMPLATE,
    ACT_PLUG_HELP, ACT_PLUG_MENU, ACT_PLUG_LINK, ACT_PLUG_UPDATE, ACT_PLUG_GET, ACT_PLUG_RECHECK, ACT_PLUG_YES, ACT_PLUG_NO, ACT_NET_OFF,
    ACT_NET_OK
};

#define PLUG_MENU_MAX PL_MENU_ALL

extern int plug_typing;

void plug_boot(void);
int plug_pump(void); // 1 = redraw
void plug_rescan(void);
void plug_import_poll(void);
void plug_key(int ch);
void plug_tabs(void);
double plug_tab(Canvas *c, Rect m, double y, double pad, int t, int draw, int hit);
void plug_folder_button(Canvas *c, Rect rs, int hit);
void plug_popups(Canvas *c, Rect bl, Rect p, Rect outr, int hit);
void plug_items(Canvas *c, Rect bl, int bottom, double tk);
void plug_panels(Canvas *c, Rect bl, Rect outr, Rect info, int left);
int plug_menu_rows(const char *label[], const char *tip[], int arg[]); // arg goes back with ACT_PLUG_MENU
void plug_reset_tab(int t);
void plug_click_seen(int act); // any click, closes the popup and stops typing
void plug_click(int act, int arg, Rect r, double x, int can_hold);
int plug_sliding(void);
int plug_drag(int held, double x);

#endif
