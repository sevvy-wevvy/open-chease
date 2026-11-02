#ifndef OC_PLUGIN_H
#define OC_PLUGIN_H

#include <stdint.h>
#include <stdio.h>

#include "plat.h"

#define PL_MAX 12
#define PL_SETS 24
#define PL_ITEMS 160
#define PL_LINES 10
#define PL_CHOICES 10

enum { PS_BOOL, PS_CHOICE, PS_SLIDER, PS_BUTTON };

typedef struct {
    int kind, v, def, lo, hi, nch;
    char key[32], label[40], tip[160];
    char ch[PL_CHOICES][24];
} PlugSet;

enum { PD_TEXT, PD_BIG, PD_RECT, PD_BOX, PD_ARROW, PD_SQUARE };

typedef struct {
    int kind, sq, sq2; // sq -1 = x y are board px, else x y get added to that squares corner
    double x, y, w, h;
    uint32_t col;
    char text[80];
} PlugItem;

typedef struct {
    char title[40], line[PL_LINES][90];
    uint32_t col[PL_LINES];
    int nline;
} PlugPanel;

typedef struct {
    char name[32], title[40], dir[1024], run[400], desc[160], ver[24];
    int on, alive, fresh;
    proc_t pid;
    int to, from;
    char status[80], said[90];
    char in[4096];
    int nin, junk;
    char out[32768];
    int nout;
    long started, last_frame, last_tick;
    int tick_ms;
    PlugSet set[PL_SETS];
    int nset;
    char tab[24];
    PlugItem item[PL_ITEMS], next[PL_ITEMS];
    int nitem, nnext;
    PlugPanel panel, npanel;
} Plugin;

extern Plugin plugs[PL_MAX];
extern int nplugs;

// main hands these in, plugin.c doesnt know about the screen or the board
extern void (*plug_note)(const Plugin *p, const char *msg);
extern int (*plug_grab)(double x, double y, double w, double h, const char *path, int *pw, int *ph); // board px, writes a ppm

void plug_scan(const char *data, int verbose);
int plug_found(const char *tmp, const char *sub, const char *repo, char src[][1100], char name[][32], int max); // plugin folders in an unpacked github download
void plug_start(Plugin *p);
void plug_stop(Plugin *p, const char *why);
void plug_send(Plugin *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void plug_send_all(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int plug_poll(long now); // reads whatever they said, 1 = the overlay needs a redraw
int plug_running(void);
void plug_quit(void);
void plug_change(Plugin *p, int k, int v); // the user changed a setting
void plug_cfg_line(const char *key, const char *val);
void plug_cfg_save(FILE *f);
int plug_square(const char *s); // "e4" -> 28, -1 if its not one

#endif
