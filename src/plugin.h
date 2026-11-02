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
#define PL_MENU 4
#define PL_MENU_ALL 6 // for the whole ... menu
#define PL_PROTO_S "1"

#ifdef _WIN32
#define PL_OS "windows"
#elif defined(__APPLE__)
#define PL_OS "mac"
#else
#define PL_OS "linux"
#endif

enum { PF_NET, PF_RUN, PF_BUILD, PF_BIN, PF_SHADY, PF_SECRET, PF_START, PF_KEYS, PF_WIPE, PF_N }; // PF_BIN and up are the scary ones
#define PL_WHY 4

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
    char key[32], label[24], tip[100];
    long seq;
} PlugMenu;

enum { PT_PYTHON, PT_NODE, PT_CC, PT_CXX, PT_RUST, PT_DOTNET, PT_N };

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
    PlugMenu menu[PL_MENU];
    int nmenu;
    char author[40], url[200], lang[16], needs[120], platforms[60], build[400], update[200];
    int want, bfail; // want = start it once the checks or the build are done
    intptr_t bjob;
    long bstart;
    char blog[3][90]; // end of build.log when it failed
    uint64_t srchash;
    int nflag[PF_N];
    char flag[PF_N][PL_WHY][80];
    char net_why[100], online[64];
    int net_ok, net_new;
    long net_t;
    uint64_t hash; // every file plus the run line, an ok only counts for these exact files
    int okv, trusted;
    uint64_t made[64];
    int nmade;
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
void plug_launch(Plugin *p); // starts the program, plug_start does the checks first
int plug_menu_list(int *pi, int *k, int max);
void plug_menu_click(int pi, int k);

int plug_os_ok(const Plugin *p);
int plug_lang(const Plugin *p); // PT_* it needs, -1 = nothing to check
int plug_ready(Plugin *p, char *why, int n); // 1 good to go, 0 still checking, -1 cant (why says it)
int plug_missing(const Plugin *p);
const char *plug_tool_name(int t);
const char *plug_tool_url(int t);
void plug_tools_recheck(void);
int plug_run_poll(long now);
int plug_busy(void);
void plug_run_stop(Plugin *p);

extern const char *plug_flag_name[PF_N], *plug_flag_says[PF_N];
int plug_flags(const Plugin *p);
void plug_says(const Plugin *p, char *out, int n); // "uses the internet and runs programs"
void plug_net_fine(Plugin *p, int ok); // going online is fine, remembered till its files change
void plug_trust(Plugin *p);
void plug_recheck(Plugin *p); // stops it if the files changed under it
int plug_cleared(Plugin *p); // call right before a build
void plug_built(Plugin *p); // whatever the build made stops counting as the plugins own files
int plug_check_dir(const char *dir, FILE *out); // --check-plugin

#endif
