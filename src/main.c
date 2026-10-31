#define _GNU_SOURCE
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "app.h"

enum { SCANNING, ASKING, TRACKING };

typedef struct {
    Rect r;
    int action, arg;
} Hit;

enum {
    ACT_WHITE = 1, ACT_BLACK, ACT_CLOSE, ACT_STOP, ACT_FLIP, ACT_MENU, ACT_DEBUG, ACT_FIX, ACT_RESTART, ACT_UPDATE,
    ACT_QUIT, ACT_QUIT_YES, ACT_QUIT_NO, ACT_SQ, ACT_PIECE, ACT_TURN, ACT_SETTINGS, ACT_SET, ACT_PICK, ACT_RESCAN, ACT_LIB_OPEN, ACT_LIB_CLOSE, ACT_LIB_TAB, ACT_LIB_KEY, ACT_LIB_FILTER,
    ACT_LIB_SCROLL, ACT_LIB_GET, ACT_LIB_USE, ACT_LIB_DEL, ACT_LIB_FOLDER, ACT_BLOCK, ACT_TAB, ACT_THEME, ACT_CSLOT, ACT_CCOPY,
    ACT_CPART, ACT_CSWATCH, ACT_RESET, ACT_MODE
};

typedef struct {
    Rect frame_rect;
    Board b;
    int out;
    long until_ms;
    int active;
} Ignore;

static volatile sig_atomic_t quit;
static void on_signal(int s) { quit = 1; }

static Backend *cap;
static Overlay *ov;
static int state = SCANNING;
static Board board;
static int miss_ticks;
static Tracker tracker;
static Templates tpl;
static char tpl_path[1024];
static Ignore ignores[16];
#define MAX_HITS 512
static Hit hits[MAX_HITS];
static int menu_open, settings_open, confirm_quit, debug_on, fix_mode, fix_sq = -1;

typedef struct {
    const char *name;
    uint32_t panel, title, title_off, sub, btn, btn_fg, grid, arrow, menu, item, item_fg;
} Theme;

static Theme themes[] = {
    {"Dark", 0xF0181818, 0xFFFFD44A, 0xFFDADADA, 0xFF9AE59A, 0xFF3A3A3A, 0xFFFFFFFF, 0xE600E040, 0xC8FFC400, 0xF8181818,
     0xFF2A2A2A, 0xFFF0F0F0},
    {"Light", 0xF2F4F4F4, 0xFF0A7A2A, 0xFF444444, 0xFF555555, 0xFFDADADA, 0xFF222222, 0xE600E040, 0xC8FF8A00, 0xF8F4F4F4,
     0xFFE4E4E4, 0xFF1A1A1A},
    {"Ocean", 0xF00E2233, 0xFF5FD7FF, 0xFFB8C7D1, 0xFF7FB3D5, 0xFF1E3A52, 0xFFFFFFFF, 0xE600C8FF, 0xC800E0FF, 0xF80E2233,
     0xFF173149, 0xFFE6F2FA},
    {"Candy", 0xF02A1028, 0xFFFF7AD9, 0xFFE6C8E0, 0xFFC59BD0, 0xFF4A1F45, 0xFFFFFFFF, 0xE6FF5FC8, 0xC8FFE14A, 0xF82A1028,
     0xFF3D1838, 0xFFFFE6F7},
    {"Forest", 0xF0142018, 0xFF9BE564, 0xFFCFE0C8, 0xFF8FC28A, 0xFF24402A, 0xFFFFFFFF, 0xE64CD964, 0xC8C6FF4A, 0xF8142018,
     0xFF1D3022, 0xFFE6F5E0},
    {"Sunset", 0xF02A1414, 0xFFFFB04A, 0xFFF0D4C0, 0xFFFF8A6A, 0xFF4A2420, 0xFFFFFFFF, 0xE6FF7A3A, 0xC8FF5A3A, 0xF82A1414,
     0xFF3A1C1A, 0xFFFFEDE0},
    {"Mono", 0xF0101010, 0xFFFFFFFF, 0xFFBDBDBD, 0xFF9A9A9A, 0xFF2E2E2E, 0xFFFFFFFF, 0xE6E0E0E0, 0xC8FFFFFF, 0xF8101010,
     0xFF222222, 0xFFF0F0F0},
    {"Neon", 0xF00A0A14, 0xFF39FF14, 0xFFB0FFB0, 0xFFFF2BD6, 0xFF1E1E3A, 0xFFFFFFFF, 0xE6FF2BD6, 0xC839FF14, 0xF80A0A14,
     0xFF16162A, 0xFFE8E8FF},
    {"Coffee", 0xF02B1E16, 0xFFE8C39E, 0xFFD9C7B5, 0xFFB8977A, 0xFF4A3426, 0xFFFFFFFF, 0xE6C8915A, 0xC8F0B070, 0xF82B1E16,
     0xFF3A2A1E, 0xFFF5EBE0},
    {"Mint", 0xF2EEFAF5, 0xFF0E8C6A, 0xFF4A6B60, 0xFF3A9A80, 0xFFCDEBDF, 0xFF16352C, 0xE61FCF9A, 0xC80EA87F, 0xF8EEFAF5,
     0xFFDDF2E9, 0xFF16352C},
    {"Royal", 0xF0140F2E, 0xFFFFD24A, 0xFFD8D0F0, 0xFFA99BE0, 0xFF2E2560, 0xFFFFFFFF, 0xE68A6BFF, 0xC8FFD24A, 0xF8140F2E,
     0xFF201A44, 0xFFEDE8FF},
    {"Lava", 0xF0180806, 0xFFFF5A1F, 0xFFF2C9B8, 0xFFFF8F5A, 0xFF3A1410, 0xFFFFFFFF, 0xE6FF3A1F, 0xC8FFB31F, 0xF8180806,
     0xFF2A0F0B, 0xFFFFE9E0},
    {"Ice", 0xF00F1E2A, 0xFFBFF3FF, 0xFFC9DCE6, 0xFF8FD1EE, 0xFF1F3A4D, 0xFFFFFFFF, 0xE6A8E8FF, 0xC8E0FAFF, 0xF80F1E2A,
     0xFF16303F, 0xFFEAF8FF},
    {"Paper", 0xF2FBF7EE, 0xFF8A3B12, 0xFF5E5547, 0xFF7A6E5C, 0xFFE8DFCC, 0xFF2E271D, 0xE6B08A4A, 0xC8C2410C, 0xF8FBF7EE,
     0xFFF1EADB, 0xFF2E271D},
    {"Custom 1", 0xF0181818, 0xFFFFD44A, 0xFFDADADA, 0xFF9AE59A, 0xFF3A3A3A, 0xFFFFFFFF, 0xE600E040, 0xC8FFC400, 0xF8181818,
     0xFF2A2A2A, 0xFFF0F0F0},
    {"Custom 2", 0xF0101A2A, 0xFFFF8AD8, 0xFFD8E2F0, 0xFF8AD8FF, 0xFF24344E, 0xFFFFFFFF, 0xE68AD8FF, 0xC8FF8AD8, 0xF8101A2A,
     0xFF1A2840, 0xFFEAF2FF},
    {"Custom 3", 0xF0F5F0FF, 0xFF6A3DD8, 0xFF4A4460, 0xFF7A6AA8, 0xFFE2DAF5, 0xFF2A2240, 0xE6A08AF0, 0xC86A3DD8, 0xF8F5F0FF,
     0xFFEAE4F8, 0xFF2A2240},
};
#define NTHEMES ((int)(sizeof(themes) / sizeof(themes[0])))
#define NCUSTOM 3
#define CUSTOM0 (NTHEMES - NCUSTOM)
static const char *part_names[] = {"Box", "Big text", "Quiet text", "Status line", "Buttons", "Button icons",
                                   "Grid", "Arrow", "Menus", "Rows", "Row text"};
static const size_t part_off[] = {offsetof(Theme, panel), offsetof(Theme, title), offsetof(Theme, title_off),
                                  offsetof(Theme, sub), offsetof(Theme, btn), offsetof(Theme, btn_fg),
                                  offsetof(Theme, grid), offsetof(Theme, arrow), offsetof(Theme, menu),
                                  offsetof(Theme, item), offsetof(Theme, item_fg)};
#define NPARTS 11
static uint32_t *part(Theme *t, int k) { return (uint32_t *)((char *)t + part_off[k]); }

static struct {
    int theme; // index into themes[]
    int spot;
    int size;
    int fade;
    int grid; // Off, lines, outline or corners
    int status;
    int score;
    int arrow;
    int thick;
    int from;
    int to;
    int label;
    int mcolor;
    int theirs; // Off, text, on the board or both
    int tcolor;
    int think;
    int rate;
    int gcolor;
    int scorefmt; // Pawns, centipawns or win chance
    int evalbar;
    int lines;
    int threads;
    int hash;
    int theirtoo;
    int tarrow, tthick, tfrom, tto, tlabel; // Same but for their move
    int engine; // Index into the engine list
    int library; // Picking one starts the download
    int strength; // Full or an elo cap
    int chaos;
    int hint;
    int delay;
    int mode; // Cheats or helper only
    int sqnames;
    int debug;
    int trash;
    int rateme;
    int confetti;
    int material, opening, wgraph, lastmove, checkwarn;
    int taken, timer, report, hanging;
    int movelist, danger, session;
} cfg = {0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 4, 2, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1},
  cfg_default;

static const char *theme_names[NTHEMES];
static const char *spot_names[] = {"Top", "Bottom", "Left", "Right"};
static const char *size_names[] = {"Small", "Normal", "Big"};
static const char *fade_names[] = {"0%", "25%", "50%", "75%"};
static const char *onoff[] = {"Off", "On"};
static const char *grid_names[] = {"Off", "Lines", "Outline", "Corners"};
static const char *arrow_names[] = {"Off", "Straight", "Knight bends", "Curve", "Swoosh", "Dashed", "Dotted", "Glow", "Double"};
static const char *thick_names[] = {"Thin", "Normal", "Thick", "Huge"};
static const char *mark_names[] = {"Off", "Outline", "Filled", "Corners", "Dot", "Ring", "Big dot",
                                   "Cross", "Triangles", "Brackets", "Dashed", "Double", "Glow", "Underline"};
static const char *color_names[] = {"Theme", "Gold", "Green", "Blue", "Red", "Pink", "Purple", "White", "Orange", "Cyan", "Rainbow"};
static const uint32_t color_vals[] = {0, 0xC8FFC400, 0xC82BD957, 0xC83D9BFF, 0xC8FF4D4D, 0xC8FF5FC8, 0xC8A86BFF,
                                      0xC8F5F5F5, 0xC8FF8A1F, 0xC81FE0E0, 0};
static const char *strength_names[] = {"Full", "2400", "2000", "1700", "1400"};
static const int strength_elo[] = {0, 2400, 2000, 1700, 1400};
static const char *chaos_names[] = {"Best move", "Close enough", "Random top 3", "Coin flip", "Random move", "Worst move",
                                    "Blunders only", "Human-like"};
static const char *hint_names[] = {"Whole move", "Just the piece", "Just where"};
static const char *delay_names[] = {"Right away", "After 3 sec", "After 10 sec"};
static const int delay_secs[] = {0, 3, 10};
static const char *mode_names[] = {"Cheats", "Helper"};

typedef struct {
    char name[32], url[512], kind[8], tags[48], about[96];
    double mb;
    int tier;
} LibEngine;
#define MAX_LIB 96
static LibEngine lib[MAX_LIB];
static int nlib;
static const char *lib_names[MAX_LIB];
static char lib_labels[MAX_LIB][48];
static pid_t lib_pid;
static const char builtin_library[] = // for when engines.txt on github cant be reached
    "Stockfish 19|https://github.com/official-stockfish/Stockfish/releases/download/sf_19/stockfish-linux-x86-64-universal.tar.gz|tgz|77.6|1||the strongest one out there\n"
    "Stockfish 16|https://github.com/official-stockfish/Stockfish/releases/download/sf_16/stockfish-ubuntu-x86-64.tar|tar|39.7|1|old|from 2023, smaller download\n"
    "Reckless 0.9|https://github.com/codedeliveryservice/Reckless/releases/download/v0.9.0/reckless-linux-generic|bin|62.0|1||one of the very top engines right now, rust\n"
    "Pawnocchio 2|https://github.com/JonathanHallstrom/pawnocchio/releases/download/v2.0.1/pawnocchio-2.0.1-linux-x86_64|bin|67.0|1||crazy strong. its written in zig which almost nobody does for chess\n"
    "Arasan 26.1|https://github.com/jdart1/arasan-chess/releases/download/v26.1/arasan-linux-binaries-26.1.tar.gz#arasanx-64-modern|tgz|28.0|1||been around since the 90s and somehow keeps getting better\n"
    "Tarnished 6|https://github.com/Bobingstern/Tarnished/releases/download/v6.0/tarnished-6.0-eternal_x86-64-avx2|bin|45.1|2|cpu|newer engine thats already really good\n"
    "Willow 4|https://github.com/Adam-Kulju/Willow/releases/download/4.0.0.1/willow-v2-linux|bin|4.6|2|small|same dev as patricia but plays it straight\n"
    "PlentyChess 8|https://github.com/Yoshie2000/PlentyChess/releases/download/b-v8.0.0/PlentyChess-8.0.0-linux-generic|bin|51.1|1||one of the best right behind stockfish\n"
    "Viridithas 20|https://github.com/cosmobobak/viridithas/releases/download/v20.0.0/viridithas-20-linux-x86-64-v3|bin|55.1|1|cpu|one of the strongest rust engines\n"
    "Stormphrax 8|https://github.com/Ciekce/Stormphrax/releases/download/v8.0.0/stormphrax-8.0.0-avx2-bmi2|bin|54.2|1|cpu|top tier\n"
    "Koivisto 9|https://github.com/Luecx/Koivisto/releases/download/v9.0/Koivisto_9.0-linux-sse2-pgo|bin|17.0|1||really strong and runs on old cpus\n"
    "Fairy-Stockfish 14|https://github.com/fairy-stockfish/Fairy-Stockfish/releases/download/fairy_sf_14/fairy-stockfish_x86-64|bin|2.2|1|variants,small|stockfish that also knows a ton of chess variants\n"
    "Halogen 16|https://github.com/KierenP/Halogen/releases/download/v16/Halogen-16.0.0-linux-x86_64-legacy|bin|25.1|2||runs on any cpu\n"
    "Patricia 3|https://github.com/Adam-Kulju/Patricia/releases/download/5.1/patricia_v3|bin|4.8|2|fun,small|the most aggressive engine around, loves throwing pieces at your king\n"
    "Princhess|https://github.com/princesslana/princhess/releases/download/0.22.0/princhess|bin|52.6|2|fun|thinks like alphazero (mcts), plays weird human looking moves\n"
    "Akimbo 1|https://github.com/jw1912/akimbo/releases/download/v1.0.0/akimbo-1.0.0|bin|6.5|2|small|small and way stronger than it looks\n"
    "Carp 3|https://github.com/dede1751/carp/releases/download/v3.0.1/carp-v3.0.1-linux-x86_64-V1|bin|5.7|2|small|tiny download for how good it is\n"
    "Sirius 9|https://github.com/mcthouacbb/Sirius/releases/download/v9.0/sirius-9.0-linux-x86-64-v1|bin|0.2|2|small|the whole thing is 0.2 MB\n"
    "Counter 5.5|https://github.com/ChizhovVadim/CounterGo/releases/download/v1.55.0/counter-5.5-linux-amd64|bin|4.1|3|small|also go\n"
    "4ku 5.1|https://github.com/kz04px/4ku/releases/download/v5.1/4ku-5.1-popcnt|bin|10.4|3|fun|started life as a 4 KB engine\n"
    "ice4|https://github.com/MinusKelvin/ice4/releases/download/v6.1/ice4-ob|bin|0.5|4|fun,small|from a 4 KB engine contest, tiny\n"
    "Mess|https://github.com/raklaptudirm/mess/releases/download/v0.3.0/mess-linux-amd64|bin|5.1|4|fun,small|a lot weaker, good for chaos games\n";
static const char *theirs_names[] = {"Off", "Text", "On board", "Both"};
static const char *think_names[] = {"2 sec", "5 sec", "8 sec", "15 sec", "30 sec"};
static const int think_secs[] = {2, 5, 8, 15, 30};
static const char *rate_names[] = {"1x a sec", "2x a sec", "4x a sec"};
static const int rate_hz[] = {1, 2, 4};
static const int fade_pct[] = {0, 25, 50, 75};
static const char *scorefmt_names[] = {"Pawns", "Centipawns", "Win %"};
static const char *evalbar_names[] = {"Off", "Left", "Right"};
static const char *lines_names[] = {"Best only", "Top 2", "Top 3"};
static const char *threads_names[] = {"Auto", "1", "2", "4", "8"};
static const int threads_vals[] = {0, 1, 2, 4, 8};
static const char *hash_names[] = {"64 MB", "128 MB", "256 MB", "512 MB", "1 GB"};
static const int hash_vals[] = {64, 128, 256, 512, 1024};

#define MAX_ENGINES 64
static const char *engine_names[MAX_ENGINES];
static char engine_paths[MAX_ENGINES][1024], engine_labels[MAX_ENGINES][32];
static int nengines;
static char saved_engine[1024];
static int have_saved_engine;

typedef struct {
    const char *label, *key;
    int *v, n;
    const char **names;
    const char *section;
} Setting;

static Setting sets[] = {
    {"Theme", "theme", &cfg.theme, NTHEMES, theme_names, "Look"},
    {"Box spot", "spot", &cfg.spot, 4, spot_names, NULL},
    {"Box size", "size", &cfg.size, 3, size_names, NULL},
    {"See-through", "fade", &cfg.fade, 4, fade_names, NULL},
    {"Grid", "grid", &cfg.grid, 4, grid_names, NULL},
    {"Grid color", "gcolor", &cfg.gcolor, 11, color_names, NULL},
    {"Status line", "status", &cfg.status, 2, onoff, NULL},
    {"Show score", "score", &cfg.score, 2, onoff, NULL},
    {"Score as", "scorefmt", &cfg.scorefmt, 3, scorefmt_names, NULL},
    {"Eval bar", "evalbar", &cfg.evalbar, 3, evalbar_names, NULL},
    {"Square names", "sqnames", &cfg.sqnames, 2, onoff, NULL},
    {"Show what it sees", "debug", &cfg.debug, 2, onoff, NULL},
    {"Mode", "mode", &cfg.mode, 2, mode_names, "Fun"},
    {"Move picker", "chaos", &cfg.chaos, 8, chaos_names, NULL},
    {"Strength", "strength", &cfg.strength, 5, strength_names, NULL},
    {"Trash talk", "trash", &cfg.trash, 2, onoff, NULL},
    {"Rate my moves", "rateme", &cfg.rateme, 2, onoff, NULL},
    {"Confetti", "confetti", &cfg.confetti, 2, onoff, NULL},
    {"Arrow", "arrow", &cfg.arrow, 9, arrow_names, "Your move"},
    {"Arrow size", "thick", &cfg.thick, 4, thick_names, NULL},
    {"Piece to move", "from", &cfg.from, 14, mark_names, NULL},
    {"Where it goes", "to", &cfg.to, 14, mark_names, NULL},
    {"Move name", "label", &cfg.label, 2, onoff, NULL},
    {"Color", "mcolor", &cfg.mcolor, 11, color_names, NULL},
    {"Show", "hint", &cfg.hint, 3, hint_names, NULL},
    {"Show it", "delay", &cfg.delay, 3, delay_names, NULL},
    {"Engine", "engine", &cfg.engine, 1, engine_names, "Engine"},
    {"Get engines", "library", &cfg.library, 1, lib_names, NULL},
    {"Think time", "think", &cfg.think, 5, think_names, NULL},
    {"Updates", "rate", &cfg.rate, 3, rate_names, NULL},
    {"Lines", "lines", &cfg.lines, 3, lines_names, NULL},
    {"Threads", "threads", &cfg.threads, 5, threads_names, NULL},
    {"Memory", "hash", &cfg.hash, 5, hash_names, NULL},
    {"Their turn too", "theirtoo", &cfg.theirtoo, 2, onoff, NULL},
    {"Show", "theirs", &cfg.theirs, 4, theirs_names, "Their move"},
    {"Arrow", "tarrow", &cfg.tarrow, 9, arrow_names, NULL},
    {"Arrow size", "tthick", &cfg.tthick, 4, thick_names, NULL},
    {"Piece to move", "tfrom", &cfg.tfrom, 14, mark_names, NULL},
    {"Where it goes", "tto", &cfg.tto, 14, mark_names, NULL},
    {"Move name", "tlabel", &cfg.tlabel, 2, onoff, NULL},
    {"Color", "tcolor", &cfg.tcolor, 11, color_names, NULL},
    {"Material", "material", &cfg.material, 2, onoff, "Helper"},
    {"Taken pieces", "taken", &cfg.taken, 2, onoff, NULL},
    {"Opening & phase", "opening", &cfg.opening, 2, onoff, NULL},
    {"Win graph", "wgraph", &cfg.wgraph, 2, onoff, NULL},
    {"Turn timer", "timer", &cfg.timer, 2, onoff, NULL},
    {"Game report", "report", &cfg.report, 2, onoff, NULL},
    {"Last move", "lastmove", &cfg.lastmove, 2, onoff, NULL},
    {"Check warning", "checkwarn", &cfg.checkwarn, 2, onoff, NULL},
    {"Hanging pieces", "hanging", &cfg.hanging, 2, onoff, NULL},
    {"Move list", "movelist", &cfg.movelist, 2, onoff, NULL},
    {"Danger alerts", "danger", &cfg.danger, 2, onoff, NULL},
    {"Session score", "session", &cfg.session, 2, onoff, NULL},
};
#define NSET ((int)(sizeof(sets) / sizeof(sets[0])))
static int dd_open = -1;
static void check_download(void);
static pid_t dl_pid;
static int dl_index = -1, lib_reload;
static char dl_out[1200], dl_name[32];
static char data_dir[1024];
static void set_note(const char *msg);
static char cfg_path[1024];

#define TH (&themes[cfg.theme])

static uint32_t faded(uint32_t argb)
{
    uint32_t a = argb >> 24;
    a = a * (100 - fade_pct[cfg.fade]) / 100;
    return (a << 24) | (argb & 0xFFFFFF);
}

static void load_cfg(void)
{
    FILE *f = fopen(cfg_path, "r");
    if (!f) return;
    char line[1100];
    int old_squares = -1, has_marks = 0;
    while (fgets(line, sizeof(line), f)) { // no fscanf/atoi, glibc 2.38 symbols
        char *k = line, *sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = 0;
        int v = 0;
        for (char *q = sp + 1; *q >= '0' && *q <= '9' && v < 100000; q++) v = v * 10 + *q - '0';
        if (!strcmp(k, "squares")) old_squares = !!v;
        if (!strcmp(k, "enginepath")) {
            snprintf(saved_engine, sizeof(saved_engine), "%s", sp + 1);
            saved_engine[strcspn(saved_engine, "\n")] = 0;
            have_saved_engine = 1;
            continue;
        }
        if (!strncmp(k, "custom", 6) && k[6] >= '1' && k[6] < '1' + NCUSTOM && !k[7]) {
            Theme *t = &themes[CUSTOM0 + k[6] - '1'];
            char *q = sp + 1;
            for (int j = 0; j < NPARTS; j++) {
                uint32_t v = 0;
                int nd = 0;
                for (; *q == ' '; q++) {}
                for (; isxdigit((unsigned char)*q) && nd < 8; q++, nd++) v = v * 16 + (*q <= '9' ? *q - '0' : (*q | 32) - 'a' + 10);
                if (nd == 8) *part(t, j) = v;
            }
            continue;
        }
        if (!strcmp(k, "engine") || !strcmp(k, "library")) continue;
        if (!strcmp(k, "from") || !strcmp(k, "to")) has_marks = 1;
        for (int i = 0; i < NSET; i++)
            if (!strcmp(k, sets[i].key) && v >= 0 && v < sets[i].n) *sets[i].v = v;
    }
    fclose(f);
    if (old_squares >= 0 && !has_marks) cfg.from = cfg.to = old_squares; // old settings files from before the dropdowns
}

static void save_cfg(void)
{
    FILE *f = fopen(cfg_path, "w");
    if (!f) return;
    for (int i = 0; i < NSET; i++)
        if (sets[i].v != &cfg.engine && sets[i].v != &cfg.library) fprintf(f, "%s %d\n", sets[i].key, *sets[i].v);
    if (have_saved_engine) fprintf(f, "enginepath %s\n", saved_engine);
    for (int i = 0; i < NCUSTOM; i++) {
        fprintf(f, "custom%d", i + 1);
        for (int j = 0; j < NPARTS; j++) fprintf(f, " %08X", *part(&themes[CUSTOM0 + i], j));
        fputc('\n', f);
    }
    fclose(f);
}
static char note[96];
static long note_until;
static char **g_argv;
static int nhits;
static int verbose;
static int auto_team = -1;
static const char *dump_dir;
static Move last_logged;
static char engine_name[48] = "engine";

static Tracker saved_tracker;
static Board saved_board;
static long saved_at;
static int have_saved;

static long replay_ticks = -1;

static long now_ms(void)
{
    if (replay_ticks >= 0) return replay_ticks * 500; // replay: every frame is 500ms apart
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

typedef struct {
    Output *o;
} MaskCtx;

// skip our own overlay or it reads its own arrow as the board
static int masked_cb(void *ctx, int fx, int fy)
{
    MaskCtx *m = ctx;
    Output *o = m->o;
    double lx = o->logical.x + fx * o->logical.w / o->frame.w;
    double ly = o->logical.y + fy * o->logical.h / o->frame.h;
    for (int i = 0; i < ov->ncanvas; i++) {
        Canvas *c = ov->canvas(ov, i);
        if (!c) continue;
        int a = canvas_alpha_at(c, lx, ly);
        if (a > 0) return 1;
    }
    return 0;
}

static double kx(int out) { return cap->out[out].logical.w / cap->out[out].frame.w; }

static Rect board_logical(const Board *b)
{
    Output *o = &cap->out[b->out];
    double k = kx(b->out);
    return (Rect){o->logical.x + b->x * k, o->logical.y + b->y * k, b->s * 8 * k, b->s * 8 * k};
}

static void add_hit_arg(Rect r, int action, int arg)
{
    if (nhits < MAX_HITS) hits[nhits++] = (Hit){r, action, arg};
}

static void add_hit(Rect r, int action)
{
    add_hit_arg(r, action, 0);
}

static void sq_display(int sq, int white_bottom, int *r, int *c)
{
    int rank = sq >> 3, file = sq & 7;
    if (white_bottom) {
        *r = 7 - rank;
        *c = file;
    } else {
        *r = rank;
        *c = 7 - file;
    }
}

static void move_to_san(Pos *p, Move m, char *out)
{
    int from = M_FROM(m), to = M_TO(m), pc = p->sq[from], ty = pc % 6;
    char *o = out;
    if (M_FLAG(m) == FL_CASTLE) {
        strcpy(o, (to & 7) == 6 ? "O-O" : "O-O-O");
        o += strlen(o);
    } else {
        int takes = p->sq[to] != EMPTY || M_FLAG(m) == FL_EP;
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
            if (amb) {
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

static void draw_button(Canvas *c, Rect r, const char *label, uint32_t bg, uint32_t fg)
{
    draw_round_rect(c, r, 6, bg);
    double tw = text_lw(c, label, 0);
    draw_text(c, r.x + (r.w - tw) / 2, r.y + (r.h - 14) / 2 - 1, label, 0, fg);
}

static void draw_close(Canvas *c, Rect r)
{
    draw_round_rect(c, r, 5, TH->btn);
    double m = r.w * 0.3;
    draw_line(c, r.x + m, r.y + m, r.x + r.w - m, r.y + r.h - m, r.w * 0.085, TH->btn_fg);
    draw_line(c, r.x + r.w - m, r.y + m, r.x + m, r.y + r.h - m, r.w * 0.085, TH->btn_fg);
}

static void draw_dots(Canvas *c, Rect r, int active)
{
    draw_round_rect(c, r, 5, active ? (TH->btn | 0xFF000000) + 0x00202020 : TH->btn);
    double d = r.w * 0.16, cy = r.y + r.h / 2;
    for (int k = -1; k <= 1; k++) {
        double cx = r.x + r.w / 2 + k * r.w * 0.24;
        draw_round_rect(c, (Rect){cx - d / 2, cy - d / 2, d, d}, d / 2, TH->btn_fg);
    }
}

static void draw_gear(Canvas *c, Rect r, int active)
{
    draw_round_rect(c, r, 5, active ? (TH->btn | 0xFF000000) + 0x00202020 : TH->btn);
    double cx = r.x + r.w / 2, cy = r.y + r.h / 2, rr = r.w * 0.24;
    for (int k = 0; k < 8; k++) {
        double a = k * M_PI / 4;
        draw_line(c, cx, cy, cx + cos(a) * rr * 1.38, cy + sin(a) * rr * 1.38, r.w * 0.13, TH->btn_fg);
    }
    draw_round_rect(c, (Rect){cx - rr, cy - rr, rr * 2, rr * 2}, rr, TH->btn_fg);
    double h = rr * 0.48;
    draw_round_rect(c, (Rect){cx - h, cy - h, h * 2, h * 2}, h, active ? (TH->btn | 0xFF000000) + 0x00202020 : TH->btn);
}

static Rect panel_rect(const Rect *bl, double w, double h, const Rect *outr)
{
    if (cfg.spot == 2 || cfg.spot == 3) {
        double x = cfg.spot == 2 ? bl->x - w - 8 : bl->x + bl->w + 8;
        double y = bl->y;
        if (x >= outr->x + 4 && x + w <= outr->x + outr->w - 4) return (Rect){x, y, w, h};
    }
    if (cfg.spot == 1) {
        double x = bl->x + (bl->w - w) / 2;
        if (x < outr->x + 4) x = outr->x + 4;
        if (x + w > outr->x + outr->w - 4) x = outr->x + outr->w - 4 - w;
        double y = bl->y + bl->h + 8;
        if (y + h <= outr->y + outr->h - 4) return (Rect){x, y, w, h};
    }
    double x = bl->x + (bl->w - w) / 2;
    if (x < outr->x + 4) x = outr->x + 4;
    if (x + w > outr->x + outr->w - 4) x = outr->x + outr->w - 4 - w;
    double y = bl->y - h - 8;
    if (y < outr->y + 4) y = bl->y + bl->h + 8;
    if (y + h > outr->y + outr->h - 4) y = bl->y + 4;
    return (Rect){x, y, w, h};
}

static uint32_t pick_color(int idx, uint32_t fallback);

static void draw_grid(Canvas *c, Rect bl, int style)
{
    double s = bl.w / 8, w = fmax(1.6, s * 0.035);
    uint32_t col = faded(cfg.gcolor ? (pick_color(cfg.gcolor, 0) | 0xE6000000) : TH->grid);
    if (style == 1)
        for (int i = 0; i <= 8; i++) {
            draw_rect(c, (Rect){bl.x + i * s - w / 2, bl.y - w / 2, w, bl.h + w}, col);
            draw_rect(c, (Rect){bl.x - w / 2, bl.y + i * s - w / 2, bl.w + w, w}, col);
        }
    else if (style == 2)
        draw_frame(c, (Rect){bl.x - w, bl.y - w, bl.w + 2 * w, bl.h + 2 * w}, w * 1.5, col);
    else if (style == 3) {
        double l = s * 0.9, t = w * 2;
        for (int k = 0; k < 4; k++) {
            double x = k & 1 ? bl.x + bl.w : bl.x, y = k & 2 ? bl.y + bl.h : bl.y;
            draw_rect(c, (Rect){k & 1 ? x - l : x - t / 2, y - t / 2, l + t / 2, t}, col);
            draw_rect(c, (Rect){x - t / 2, k & 2 ? y - l : y - t / 2, t, l + t / 2}, col);
        }
    }
}

static double wrap(double a, double m) { return a - floor(a / m) * m; } // fmod pulls in 2.38 too
static double len2(double x, double y) { return sqrt(x * x + y * y); }

static void mark_square(Canvas *c, Rect r, int style, uint32_t col)
{
    double s = r.w;
    switch (style) {
    case 1:
        draw_frame(c, (Rect){r.x + s * 0.02, r.y + s * 0.02, s * 0.96, s * 0.96}, s * 0.075, col | 0xFF000000);
        break;
    case 2:
        draw_rect(c, r, (col & 0xFFFFFF) | 0x70000000);
        break;
    case 3: {
        double l = s * 0.28, w = s * 0.08, m = s * 0.04;
        uint32_t cc = col | 0xFF000000;
        for (int k = 0; k < 4; k++) {
            double x = k & 1 ? r.x + s - m - l : r.x + m, y = k & 2 ? r.y + s - m - w : r.y + m;
            draw_rect(c, (Rect){x, y, l, w}, cc);
            draw_rect(c, (Rect){k & 1 ? r.x + s - m - w : r.x + m, k & 2 ? r.y + s - m - l : r.y + m, w, l}, cc);
        }
        break;
    }
    case 4:
    case 6: {
        double d = s * (style == 4 ? 0.3 : 0.5);
        draw_round_rect(c, (Rect){r.x + (s - d) / 2, r.y + (s - d) / 2, d, d}, d / 2, col | 0xFF000000);
        break;
    }
    case 5: {
        double cx = r.x + s / 2, cy = r.y + s / 2, rr = s * 0.36;
        for (int k = 0; k < 48; k++) {
            double a0 = k * M_PI / 24, a1 = (k + 1) * M_PI / 24;
            draw_line(c, cx + cos(a0) * rr, cy + sin(a0) * rr, cx + cos(a1) * rr, cy + sin(a1) * rr, s * 0.07, col | 0xFF000000);
        }
        break;
    }
    case 7: {
        double m = s * 0.24;
        draw_line(c, r.x + m, r.y + m, r.x + s - m, r.y + s - m, s * 0.09, col | 0xFF000000);
        draw_line(c, r.x + s - m, r.y + m, r.x + m, r.y + s - m, s * 0.09, col | 0xFF000000);
        break;
    }
    case 8: {
        double t = s * 0.24, m = s * 0.03;
        for (int k = 0; k < 4; k++) {
            double x = k & 1 ? r.x + s - m : r.x + m, y = k & 2 ? r.y + s - m : r.y + m, dx = k & 1 ? -t : t, dy = k & 2 ? -t : t;
            draw_triangle(c, x, y, x + dx, y, x, y + dy, col | 0xFF000000);
        }
        break;
    }
    case 9: {
        double l = s * 0.34, w = s * 0.13, m = s * 0.03;
        uint32_t cc = col | 0xFF000000;
        for (int k = 0; k < 4; k++) {
            draw_rect(c, (Rect){k & 1 ? r.x + s - m - l : r.x + m, k & 2 ? r.y + s - m - w : r.y + m, l, w}, cc);
            draw_rect(c, (Rect){k & 1 ? r.x + s - m - w : r.x + m, k & 2 ? r.y + s - m - l : r.y + m, w, l}, cc);
        }
        break;
    }
    case 10: {
        double w = s * 0.06, m = s * 0.04, len = s - 2 * m, dash = s * 0.12, gap = s * 0.07;
        uint32_t cc = col | 0xFF000000;
        for (double d = 0; d < len; d += dash + gap) {
            double l = fmin(dash, len - d);
            draw_rect(c, (Rect){r.x + m + d, r.y + m, l, w}, cc);
            draw_rect(c, (Rect){r.x + m + d, r.y + s - m - w, l, w}, cc);
            draw_rect(c, (Rect){r.x + m, r.y + m + d, w, l}, cc);
            draw_rect(c, (Rect){r.x + s - m - w, r.y + m + d, w, l}, cc);
        }
        break;
    }
    case 11:
        draw_frame(c, (Rect){r.x + s * 0.03, r.y + s * 0.03, s * 0.94, s * 0.94}, s * 0.045, col | 0xFF000000);
        draw_frame(c, (Rect){r.x + s * 0.13, r.y + s * 0.13, s * 0.74, s * 0.74}, s * 0.035, col | 0xFF000000);
        break;
    case 12:
        for (int k = 0; k < 6; k++) {
            double in = s * 0.035 * k;
            uint32_t a = (uint32_t)(200 * (6 - k) / 7);
            draw_frame(c, (Rect){r.x + in, r.y + in, s - 2 * in, s - 2 * in}, s * 0.035, (col & 0xFFFFFF) | a << 24);
        }
        break;
    case 13:
        draw_round_rect(c, (Rect){r.x + s * 0.15, r.y + s * 0.84, s * 0.7, s * 0.08}, s * 0.04, col | 0xFF000000);
        break;
    }
}

static void draw_move(Canvas *c, Rect bl, Move m, int white_bottom, uint32_t col, int arrow, double tk, int from_style,
                      int to_style, const char *label)
{
    double s = bl.w / 8;
    int r0, c0, r1, c1;
    sq_display(M_FROM(m), white_bottom, &r0, &c0);
    sq_display(M_TO(m), white_bottom, &r1, &c1);
    col = faded(col);
    mark_square(c, (Rect){bl.x + c0 * s, bl.y + r0 * s, s, s}, from_style, col);
    mark_square(c, (Rect){bl.x + c1 * s, bl.y + r1 * s, s, s}, to_style, col);
    if (arrow) {
        double ax = bl.x + (c0 + 0.5) * s, ay = bl.y + (r0 + 0.5) * s, bx = bl.x + (c1 + 0.5) * s, by = bl.y + (r1 + 0.5) * s;
        double P[48][2];
        int np = 0, dc = abs(c1 - c0), dr = abs(r1 - r0);
        P[np][0] = ax, P[np++][1] = ay;
        if (arrow == 2 && dc + dr == 3 && dc && dr) { // knight, long leg first
            P[np][0] = dr == 2 ? ax : bx, P[np++][1] = dr == 2 ? by : ay;
        } else if (arrow == 3 || arrow == 4) {
            double mx = (ax + bx) / 2, my = (ay + by) / 2, nx = -(by - ay) * 0.3, ny = (bx - ax) * 0.3;
            for (int k = 1; k < 40; k++) {
                double t = k / 40.0, u = 1 - t;
                P[np][0] = u * u * ax + 2 * u * t * (mx + nx) + t * t * bx;
                P[np++][1] = u * u * ay + 2 * u * t * (my + ny) + t * t * by;
            }
        }
        P[np][0] = bx, P[np++][1] = by;
        double head = s * 0.42 * (0.7 + 0.3 * tk), hw = s * 0.3 * (0.7 + 0.3 * tk), lw = s * 0.16 * tk;
        double cum[48] = {0};
        for (int k = 1; k < np; k++) cum[k] = cum[k - 1] + len2(P[k][0] - P[k - 1][0], P[k][1] - P[k - 1][1]);
        double L = cum[np - 1], from_d = s * 0.18, to_d = L - head;
        if (L > 1 && to_d > from_d) {
            double Q[48][2], qd[48];
            int nq = 0;
            for (int k = 0; k < np - 1; k++) {
                double d0 = cum[k], d1 = cum[k + 1];
                if (d1 <= from_d || d0 >= to_d) continue;
                double t0 = d0 < from_d ? (from_d - d0) / (d1 - d0) : 0, t1 = d1 > to_d ? (to_d - d0) / (d1 - d0) : 1;
                if (!nq) {
                    Q[nq][0] = P[k][0] + (P[k + 1][0] - P[k][0]) * t0, Q[nq][1] = P[k][1] + (P[k + 1][1] - P[k][1]) * t0;
                    qd[nq++] = d0 + (d1 - d0) * t0 - from_d;
                }
                Q[nq][0] = P[k][0] + (P[k + 1][0] - P[k][0]) * t1, Q[nq][1] = P[k][1] + (P[k + 1][1] - P[k][1]) * t1;
                qd[nq++] = d0 + (d1 - d0) * t1 - from_d;
            }
            double span = to_d - from_d;
            if (arrow == 7)
                for (int k = 0; k + 1 < nq; k++)
                    draw_line(c, Q[k][0], Q[k][1], Q[k + 1][0], Q[k + 1][1], lw * 3.2, (col & 0xFFFFFF) | ((col >> 26) << 24));
            for (int k = 0; k + 1 < nq; k++) {
                double x0 = Q[k][0], y0 = Q[k][1], x1 = Q[k + 1][0], y1 = Q[k + 1][1];
                if (arrow == 4) {
                    double w = lw * (0.25 + 0.95 * qd[k + 1] / span);
                    draw_line(c, x0, y0, x1, y1, w, col);
                } else if (arrow == 5) {
                    double seg = s * 0.32, len = qd[k + 1] - qd[k];
                    for (double d = 0; d < len; d += 1) {
                        if (wrap(qd[k] + d, seg) > seg * 0.6) continue;
                        double e = fmin(d + 1, len), ta = d / len, tb = e / len;
                        draw_line(c, x0 + (x1 - x0) * ta, y0 + (y1 - y0) * ta, x0 + (x1 - x0) * tb, y0 + (y1 - y0) * tb, lw, col);
                    }
                } else if (arrow == 6) {
                    double gap = s * 0.27, len = qd[k + 1] - qd[k];
                    for (double d = wrap(gap - wrap(qd[k], gap), gap); d < len; d += gap) {
                        double t = d / len, r = lw * 0.75;
                        draw_round_rect(c, (Rect){x0 + (x1 - x0) * t - r, y0 + (y1 - y0) * t - r, r * 2, r * 2}, r, col);
                    }
                } else if (arrow == 8) {
                    double dx = x1 - x0, dy = y1 - y0, l = len2(dx, dy);
                    if (l < 0.01) continue;
                    double ox = -dy / l * lw * 0.55, oy = dx / l * lw * 0.55;
                    draw_line(c, x0 + ox, y0 + oy, x1 + ox, y1 + oy, lw * 0.4, col);
                    draw_line(c, x0 - ox, y0 - oy, x1 - ox, y1 - oy, lw * 0.4, col);
                } else {
                    draw_line(c, x0, y0, x1, y1, lw, col);
                }
            }
            double ex = Q[nq - 1][0], ey = Q[nq - 1][1], dx = bx - ex, dy = by - ey, l = len2(dx, dy);
            if (l > 0.01) {
                dx /= l, dy /= l;
                draw_triangle(c, bx, by, ex - dy * hw, ey + dx * hw, ex + dy * hw, ey - dx * hw, col);
            }
        }
    }
    if (label && *label) {
        double tw = text_lw(c, label, 0), w = tw + 10, h = 19;
        Rect lr = {bl.x + c1 * s + (s - w) / 2, bl.y + r1 * s + s - h - 3, w, h};
        draw_round_rect(c, lr, 5, faded(0xF0181818));
        draw_text(c, lr.x + 5, lr.y + 1, label, 0, faded(0xFFF5F5F5));
    }
}

static uint32_t pick_color(int idx, uint32_t fallback)
{
    if (idx == 10) {
        double h = wrap(now_ms() / 6000.0, 1.0) * 6, f = h - floor(h);
        int i = (int)h;
        double rgb[6][3] = {{1, f, 0}, {1 - f, 1, 0}, {0, 1, f}, {0, 1 - f, 1}, {f, 0, 1}, {1, 0, 1 - f}};
        return 0xC8000000u | (uint32_t)(rgb[i][0] * 255) << 16 | (uint32_t)(rgb[i][1] * 255) << 8 | (uint32_t)(rgb[i][2] * 255);
    }
    return idx > 0 ? color_vals[idx] : fallback;
}

static double arrow_tk(int t) { return t == 0 ? 0.6 : t == 2 ? 1.5 : t == 3 ? 2.1 : 1.0; }

static void format_score(const Analysis *a, int user_is_side_to_move, char *out, size_t n)
{
    int sc = user_is_side_to_move ? a->score : -a->score;
    int mate = user_is_side_to_move ? a->mate : -a->mate;
    if (mate > 0)
        snprintf(out, n, "mate in %d", mate);
    else if (mate < 0)
        snprintf(out, n, "mated in %d", -mate);
    else if (cfg.scorefmt == 1)
        snprintf(out, n, "%+d", sc);
    else if (cfg.scorefmt == 2)
        snprintf(out, n, "%.0f%% to win", 50 + 50 * (2 / (1 + exp(-0.00368208 * sc)) - 1)); // lichess win% curve
    else
        snprintf(out, n, "%+.1f", sc / 100.0);
}

static int bar_moving;
static long pick_t = -99999;

static void draw_evalbar(Canvas *c, Rect bl, const Analysis *a, int white_bottom, int side)
{
    double w = fmax(10, bl.w / 8 * 0.2), x = cfg.evalbar == 1 ? bl.x - w - 6 : bl.x + bl.w + 6;
    int sc = side == WHITE ? a->score : -a->score, mate = side == WHITE ? a->mate : -a->mate;
    double target = mate > 0 ? 1 : mate < 0 ? 0 : 1 / (1 + exp(-0.00368208 * sc));
    static double wp = 0.5;
    wp += (target - wp) * 0.3;
    if (fabs(target - wp) < 0.003) wp = target;
    bar_moving = wp != target;
    double wh = bl.h * wp;
    draw_rect(c, (Rect){x, bl.y, w, bl.h}, faded(0xF0303030));
    if (white_bottom)
        draw_rect(c, (Rect){x, bl.y + bl.h - wh, w, wh}, faded(0xF0F2F2F2));
    else
        draw_rect(c, (Rect){x, bl.y, w, wh}, faded(0xF0F2F2F2));
    draw_frame(c, (Rect){x, bl.y, w, bl.h}, 1, faded(0xFF6A6A6A));
}

static const char *piece_letters = "PNBRQK";

static int ui_over_board, learned_here, want_rescan;
static int lib_open, lib_tab, lib_filter, lib_scroll, lib_del = -1;
static char lib_query[24];

static void covers(Rect a, Rect bl)
{
    if (a.x < bl.x + bl.w && bl.x < a.x + a.w && a.y < bl.y + bl.h && bl.y < a.y + a.h) ui_over_board = 1;
}

static int has_ci(const char *hay, const char *needle)
{
    if (!*needle) return 1;
    for (; *hay; hay++) {
        int k = 0;
        while (needle[k] && hay[k] && (hay[k] | 32) == (needle[k] | 32)) k++;
        if (!needle[k]) return 1;
    }
    return 0;
}

static int lib_matches(int i)
{
    LibEngine *e = &lib[i];
    if (!has_ci(e->name, lib_query) && !has_ci(e->about, lib_query) && !has_ci(e->tags, lib_query)) return 0;
    switch (lib_filter) {
    case 1: return e->tier == 1;
    case 2: return e->mb < 10 || strstr(e->tags, "small");
    case 3: return strstr(e->tags, "fun") || strstr(e->tags, "variants");
    case 4: return strstr(e->tags, "old") != NULL;
    }
    return 1;
}

static void lib_out_path(int i, char *out, size_t n);
static int engine_index(const char *path);
static void cached_name(const char *path, char *out, size_t n);
static void remember_name(const char *path, const char *name);

static int mine(int i) { return !strncmp(engine_paths[i], data_dir, strlen(data_dir)) && *data_dir; } // only delete stuff we downloaded or you dropped in

static const char *fit_text(Canvas *c, const char *t, double maxw)
{
    static char buf[200];
    snprintf(buf, sizeof(buf), "%s", t);
    if (text_lw(c, buf, 0) <= maxw) return buf;
    for (size_t l = strlen(buf); l > 3; l--) {
        buf[l - 3] = '.', buf[l - 2] = '.', buf[l - 1] = '.', buf[l] = 0;
        if (text_lw(c, buf, 0) <= maxw) break;
    }
    return buf;
}

static void cut_to(char *t, double maxw, int big)
{
    size_t l = strlen(t);
    if (text_width(t, big) <= maxw) return;
    while (l > 3) {
        t[--l] = 0;
        char save[4];
        memcpy(save, t + l - 3, 4);
        memcpy(t + l - 3, "...", 4);
        if (text_width(t, big) <= maxw) return;
        memcpy(t + l - 3, save, 4);
    }
}

static int dl_percent(double mb)
{
    DIR *d = opendir(data_dir);
    if (!d || mb <= 0) {
        if (d) closedir(d);
        return -1;
    }
    struct dirent *e;
    long long got = -1;
    while ((e = readdir(d)))
        if (!strncmp(e->d_name, "dl.", 3)) {
            char f[1400];
            struct stat st;
            snprintf(f, sizeof(f), "%s/%s/got", data_dir, e->d_name);
            if (!stat(f, &st) && st.st_size > got) got = st.st_size;
        }
    closedir(d);
    if (got < 0) return -1;
    int pct = (int)(got * 100.0 / (mb * 1048576));
    return pct > 99 ? 99 : pct;
}

static int set_tab;
static long menu_t = -99999, menu_shut = -99999, set_t = -99999, set_shut = -99999, lib_t = -99999, conf_t = -99999,
            tab_t = -99999, dd_t = -99999, tog_t = -99999;
static int tab_dir, tab_prev, tog_i = -1;
static double anim_force = -1;

static double ease(long since, int ms)
{
    if (anim_force >= 0) return anim_force;
    double t = (now_ms() - since) / (double)ms;
    if (t >= 1) return 1;
    if (t <= 0) return 0;
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

static int animating(void)
{
    long n = now_ms();
    return n - menu_t < 200 || n - menu_shut < 140 || n - set_t < 220 || n - set_shut < 140 || n - lib_t < 240 ||
           n - conf_t < 200 || n - tab_t < 200 || n - dd_t < 170 || n - tog_t < 170 || n - pick_t < 240 || bar_moving;
}

static void track_anims(void)
{
    static int was_menu, was_set, was_lib, was_conf, was_tab, was_dd = -1;
    long n = now_ms();
    if (menu_open != was_menu) {
        if (menu_open) menu_t = n;
        else menu_shut = n;
        was_menu = menu_open;
    }
    if (settings_open != was_set) {
        if (settings_open) set_t = n;
        else set_shut = n;
        was_set = settings_open;
    }
    if (lib_open && !was_lib) lib_t = n;
    if (confirm_quit && !was_conf) conf_t = n;
    if (dd_open >= 0 && dd_open != was_dd) dd_t = n;
    if (set_tab != was_tab) {
        tab_dir = set_tab > was_tab ? 1 : -1;
        tab_prev = was_tab;
        tab_t = n;
        was_tab = set_tab;
    }
    was_lib = lib_open, was_conf = confirm_quit, was_dd = dd_open;
}

static uint32_t mix(uint32_t a, uint32_t b, double t)
{
    uint32_t o = 0;
    for (int k = 0; k < 32; k += 8) {
        double x = ((a >> k) & 255) * (1 - t) + ((b >> k) & 255) * t;
        o |= (uint32_t)(x + 0.5) << k;
    }
    return o;
}

static Canvas lays[3];
static size_t lay_cap[3];

static Canvas *layer(int k, const Canvas *c, Rect r)
{
    double pad = 16;
    int w = (int)ceil((r.w + 2 * pad) * c->scale) + 2, h = (int)ceil((r.h + 2 * pad) * c->scale) + 2;
    size_t need = (size_t)w * h;
    if (need > lay_cap[k]) {
        free(lays[k].px);
        lays[k].px = malloc(need * 4);
        lay_cap[k] = lays[k].px ? need : 0;
        if (!lays[k].px) return NULL;
    }
    lays[k].w = w, lays[k].h = h, lays[k].scale = c->scale, lays[k].ox = r.x - pad, lays[k].oy = r.y - pad;
    lays[k].dx0 = lays[k].dy0 = lays[k].dx1 = lays[k].dy1 = 0;
    memset(lays[k].px, 0, need * 4);
    return &lays[k];
}

static void panel_bg(Canvas *c, Rect m, double rad, uint32_t fill)
{
    for (int k = 4; k >= 1; k--)
        draw_round_rect(c, (Rect){m.x - k + 1, m.y - k + 4, m.w + 2 * k - 2, m.h + 2 * k - 2}, rad + k, 0x16000000);
    draw_round_rect(c, (Rect){m.x - 1, m.y - 1, m.w + 2, m.h + 2}, rad + 1, (TH->btn & 0xFFFFFF) | 0xC0000000);
    draw_round_rect(c, m, rad, fill);
}

static void draw_library(Canvas *dst, Rect outr, Rect bl, int hit)
{
    double W = fmin(680, outr.w - 20), H = fmin(640, outr.h - 20);
    Rect P = {outr.x + (outr.w - W) / 2, outr.y + (outr.h - H) / 2, W, H};
    covers(P, bl);
    Canvas *c = layer(0, dst, P);
    if (!c) return;
    for (int k = 4; k >= 1; k--)
        draw_round_rect(c, (Rect){P.x - k + 1, P.y - k + 4, P.w + 2 * k - 2, P.h + 2 * k - 2}, 14 + k, 0x16000000);
    draw_round_rect(c, (Rect){P.x - 1, P.y - 1, P.w + 2, P.h + 2}, 15, TH->title);
    draw_round_rect(c, P, 14, TH->menu);
    if (hit) add_hit(P, ACT_BLOCK); // eat clicks on empty popup space so they dont hit stuff underneath
    double x = P.x + 14, y = P.y + 12;
    draw_text(c, x, y, "Engine library", 1, TH->title);
    Rect cx = {P.x + P.w - 40, P.y + 10, 28, 28};
    draw_close(c, cx);
    if (hit) add_hit(cx, ACT_LIB_CLOSE);
    y += 40;
    char inst[32];
    snprintf(inst, sizeof(inst), "Installed (%d)", nengines);
    const char *tabs[2] = {"Get more", inst};
    for (int k = 0; k < 2; k++) {
        Rect t = {x + k * 150, y, 140, 28};
        draw_button(c, t, tabs[k], lib_tab == k ? TH->title : TH->btn, lib_tab == k ? 0xFF181818 : TH->btn_fg);
        if (hit) add_hit_arg(t, ACT_LIB_TAB, k);
    }
    if (lib_tab == 1) {
        Rect of = {P.x + P.w - 130, y, 116, 28};
        draw_button(c, of, "Open folder", TH->btn, TH->btn_fg);
        if (hit) add_hit(of, ACT_LIB_FOLDER);
    }
    y += 38;
    Rect sb = {x, y, P.w - 28, 30};
    draw_round_rect(c, sb, 6, TH->item);
    char q[48];
    snprintf(q, sizeof(q), "search: %s_", lib_query);
    draw_text(c, sb.x + 10, sb.y + 6, q, 0, TH->item_fg);
    y += 36;
    static const char *rows[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
    for (int r = 0; r < 4; r++) {
        int n = (int)strlen(rows[r]) + (r == 3 ? 2 : 0);
        double kw = 34, rx = P.x + (P.w - n * kw) / 2;
        for (int k = 0; k < n; k++) {
            int ch = k < (int)strlen(rows[r]) ? rows[r][k] : (k == n - 2 ? 8 : 127);
            char lab[8];
            snprintf(lab, sizeof(lab), "%s", ch == 8 ? "<" : ch == 127 ? "clr" : (char[2]){(char)ch, 0});
            Rect key = {rx + k * kw, y, kw - 4, 26};
            draw_button(c, key, lab, TH->item, TH->item_fg);
            if (hit) add_hit_arg(key, ACT_LIB_KEY, ch);
        }
        y += 30;
    }
    Rect space = {P.x + (P.w - 200) / 2, y, 200, 24};
    draw_button(c, space, "space", TH->item, TH->item_fg);
    if (hit) add_hit_arg(space, ACT_LIB_KEY, ' ');
    y += 32;
    int idx[MAX_LIB > MAX_ENGINES ? MAX_LIB : MAX_ENGINES], n = 0;
    if (lib_tab == 0) {
        static const char *chips[5] = {"All", "Strongest", "Small", "Fun & weird", "Old ones"};
        double cxp = x;
        for (int k = 0; k < 5; k++) {
            double w = text_lw(c, chips[k], 0) + 22;
            Rect ch = {cxp, y, w, 26};
            draw_button(c, ch, chips[k], lib_filter == k ? TH->title : TH->btn, lib_filter == k ? 0xFF181818 : TH->btn_fg);
            if (hit) add_hit_arg(ch, ACT_LIB_FILTER, k);
            cxp += w + 6;
        }
        y += 34;
        for (int i = 0; i < nlib && n < MAX_LIB; i++)
            if (lib_matches(i)) idx[n++] = i;
    } else {
        for (int i = 0; i < nengines && n < MAX_ENGINES; i++)
            if (has_ci(engine_labels[i], lib_query)) idx[n++] = i;
    }
    double rh = 62, top = y, bottom = P.y + P.h - 12;
    int fit = (int)((bottom - top) / rh);
    if (fit < 1) fit = 1;
    if (lib_scroll > n - fit) lib_scroll = n - fit;
    if (lib_scroll < 0) lib_scroll = 0;
    if (!n) draw_text(c, x + 4, y + 8, "nothing here", 0, TH->sub);
    static const char *tiers[5] = {"", "insane (3500+)", "really strong (3200+)", "strong (2800+)", "beatable-ish"};
    for (int k = 0; k < fit && lib_scroll + k < n; k++) {
        int i = idx[lib_scroll + k];
        Rect row = {x, top + k * rh, P.w - 28 - 36, rh - 6};
        draw_round_rect(c, row, 8, TH->item);
        Rect b1 = {row.x + row.w - 92, row.y + 8, 84, 26};
        if (lib_tab == 0) {
            LibEngine *e = &lib[i];
            char out[1200], info[160];
            lib_out_path(i, out, sizeof(out));
            int got = access(out, X_OK) == 0, using = got && engine_index(out) == cfg.engine && cfg.engine;
            int getting = dl_pid > 0 && dl_index == i;
            draw_text(c, row.x + 10, row.y + 5, e->name, 0, TH->title);
            snprintf(info, sizeof(info), "%s  -  %.1f MB%s%s%s", tiers[e->tier], e->mb, strstr(e->tags, "cpu") ? "  -  needs a newer cpu" : "",
                     strstr(e->tags, "variants") ? "  -  variants" : "", strstr(e->tags, "old") ? "  -  older version" : "");
            draw_text(c, row.x + 10, row.y + 22, fit_text(c, info, row.w - 110), 0, TH->sub);
            draw_text(c, row.x + 10, row.y + 38, fit_text(c, e->about, row.w - 110), 0, TH->item_fg);
            char pct[16] = "getting";
            if (getting && dl_percent(e->mb) >= 0) snprintf(pct, sizeof(pct), "%d%%", dl_percent(e->mb));
            const char *bt = getting ? pct : using ? "Using" : got ? "Use" : "Get";
            draw_button(c, b1, bt, using ? TH->title : TH->btn, using ? 0xFF181818 : TH->btn_fg);
            if (hit && !getting && !using) add_hit_arg(b1, got ? ACT_LIB_USE : ACT_LIB_GET, got ? -1 - i : i);
        } else {
            char info[160];
            struct stat st;
            double mb = *engine_paths[i] && !stat(engine_paths[i], &st) ? st.st_size / 1048576.0 : 0;
            draw_text(c, row.x + 10, row.y + 5, engine_labels[i], 0, TH->title);
            if (!*engine_paths[i])
                snprintf(info, sizeof(info), "built into open-chease, always there");
            else
                snprintf(info, sizeof(info), mb < 1 ? "%.0f KB  -  %s" : "%.1f MB  -  %s", mb < 1 ? mb * 1024 : mb,
                         mine(i) ? "in your engines folder" : "found on your system");
            draw_text(c, row.x + 10, row.y + 22, info, 0, TH->sub);
            int using = i == cfg.engine;
            draw_button(c, b1, using ? "Using" : "Use", using ? TH->title : TH->btn, using ? 0xFF181818 : TH->btn_fg);
            if (hit && !using) add_hit_arg(b1, ACT_LIB_USE, i);
            if (mine(i)) {
                Rect b2 = {b1.x, b1.y + 26, 84, 22};
                int sure = lib_del == i;
                draw_button(c, b2, sure ? "Sure?" : "Delete", sure ? 0xFFD03A3A : TH->btn, 0xFFFFFFFF);
                if (hit) add_hit_arg(b2, ACT_LIB_DEL, i);
            }
        }
    }
    if (n > fit) {
        Rect up = {P.x + P.w - 46, top, 32, 32}, dn = {P.x + P.w - 46, bottom - 32, 32, 32};
        draw_button(c, up, "^", TH->btn, TH->btn_fg);
        draw_button(c, dn, "v", TH->btn, TH->btn_fg);
        if (hit) add_hit_arg(up, ACT_LIB_SCROLL, -fit), add_hit_arg(dn, ACT_LIB_SCROLL, fit);
        char pg[32];
        snprintf(pg, sizeof(pg), "%d-%d", lib_scroll + 1, lib_scroll + fit < n ? lib_scroll + fit : n);
        draw_text(c, up.x - 2, (top + bottom) / 2 - 8, pg, 0, TH->sub);
        snprintf(pg, sizeof(pg), "of %d", n);
        draw_text(c, up.x - 2, (top + bottom) / 2 + 8, pg, 0, TH->sub);
    }
    double an = ease(lib_t, 230);
    canvas_blit(dst, c, an, 0, 18 * (1 - an), -1);
}


static void arc(Canvas *c, double cx, double cy, double rx, double ry, double a0, double a1, double w, uint32_t col)
{
    int n = 24;
    for (int k = 0; k < n; k++) {
        double t0 = a0 + (a1 - a0) * k / n, t1 = a0 + (a1 - a0) * (k + 1) / n;
        draw_line(c, cx + cos(t0) * rx, cy + sin(t0) * ry, cx + cos(t1) * rx, cy + sin(t1) * ry, w, col);
    }
}

static void draw_icon(Canvas *c, Rect r, int kind, uint32_t col)
{
    double x = r.x, y = r.y, s = r.w, cx = x + s / 2, cy = y + s / 2, w = fmax(1.6, s * 0.11);
    switch (kind) {
    case 0:
        arc(c, cx, cy, s * 0.45, s * 0.28, 0, 2 * M_PI, w, col);
        draw_round_rect(c, (Rect){cx - s * 0.15, cy - s * 0.15, s * 0.3, s * 0.3}, s * 0.15, col);
        break;
    case 1:
        draw_line(c, x + s * 0.3, y + s * 0.7, x + s * 0.85, y + s * 0.15, s * 0.2, col);
        draw_triangle(c, x + s * 0.08, y + s * 0.92, x + s * 0.17, y + s * 0.6, x + s * 0.4, y + s * 0.83, col);
        break;
    case 2:
        arc(c, cx, cy, s * 0.42, s * 0.42, 0, 2 * M_PI, w, col);
        for (int k = 0; k < 12; k++) {
            double a = M_PI / 2 + M_PI * k / 12, b = M_PI / 2 + M_PI * (k + 1) / 12;
            draw_triangle(c, cx, cy, cx + cos(a) * s * 0.42, cy + sin(a) * s * 0.42, cx + cos(b) * s * 0.42, cy + sin(b) * s * 0.42, col);
        }
        break;
    case 3:
        for (int k = 0; k < 4; k++) {
            double px = k & 1 ? x + s * 0.92 : x + s * 0.08, py = k & 2 ? y + s * 0.92 : y + s * 0.08, dx = k & 1 ? -1 : 1, dy = k & 2 ? -1 : 1;
            draw_line(c, px, py, px + dx * s * 0.3, py, w, col);
            draw_line(c, px, py, px, py + dy * s * 0.3, w, col);
        }
        draw_line(c, x + s * 0.2, cy, x + s * 0.8, cy, w, col);
        break;
    case 4:
        draw_frame(c, (Rect){x + s * 0.22, y + s * 0.22, s * 0.56, s * 0.56}, w, col);
        for (int k = 0; k < 3; k++) {
            double o = s * (0.34 + 0.16 * k);
            draw_line(c, x + o, y, x + o, y + s * 0.22, w * 0.8, col);
            draw_line(c, x + o, y + s * 0.78, x + o, y + s, w * 0.8, col);
            draw_line(c, x, y + o, x + s * 0.22, y + o, w * 0.8, col);
            draw_line(c, x + s * 0.78, y + o, x + s, y + o, w * 0.8, col);
        }
        break;
    case 5:
        draw_line(c, x + s * 0.1, y + s * 0.3, x + s * 0.75, y + s * 0.3, w, col);
        draw_triangle(c, x + s * 0.95, y + s * 0.3, x + s * 0.7, y + s * 0.12, x + s * 0.7, y + s * 0.48, col);
        draw_line(c, x + s * 0.25, y + s * 0.72, x + s * 0.9, y + s * 0.72, w, col);
        draw_triangle(c, x + s * 0.05, y + s * 0.72, x + s * 0.3, y + s * 0.54, x + s * 0.3, y + s * 0.9, col);
        break;
    case 6:
        arc(c, cx, cy, s * 0.38, s * 0.38, -M_PI * 0.35, M_PI * 1.5, w, col);
        draw_triangle(c, cx + s * 0.18, y + s * 0.02, cx + s * 0.48, y + s * 0.2, cx + s * 0.16, y + s * 0.34, col);
        break;
    case 7:
        draw_line(c, cx, y + s * 0.05, cx, y + s * 0.55, w * 1.2, col);
        draw_triangle(c, cx, y + s * 0.78, cx - s * 0.3, y + s * 0.45, cx + s * 0.3, y + s * 0.45, col);
        draw_line(c, x + s * 0.1, y + s * 0.93, x + s * 0.9, y + s * 0.93, w, col);
        break;
    case 9:
    case 10: {
        Rect pill = {x, y + s * 0.22, s, s * 0.56};
        draw_round_rect(c, pill, pill.h / 2, col);
        draw_round_rect(c, (Rect){pill.x + w, pill.y + w, pill.w - 2 * w, pill.h - 2 * w}, pill.h / 2 - w, TH->btn | 0xFF000000);
        double d = pill.h - 4 * w, kx = kind == 10 ? pill.x + pill.w - d - 2 * w : pill.x + 2 * w;
        draw_round_rect(c, (Rect){kx, pill.y + 2 * w, d, d}, d / 2, col);
        break;
    }
    default:
        draw_line(c, x + s * 0.15, y + s * 0.15, x + s * 0.85, y + s * 0.85, w * 1.3, col);
        draw_line(c, x + s * 0.85, y + s * 0.15, x + s * 0.15, y + s * 0.85, w * 1.3, col);
    }
}

static void draw_menu(Canvas *dst, Rect p, Rect bl, Rect outr, int hit)
{
    const char *items[10] = {"Helper mode", debug_on ? "Hide what it sees" : "Show what it sees",
                             fix_mode ? "Done fixing" : "Fix pieces", tracker.pos.side == WHITE ? "Its black's turn" : "Its white's turn",
                             "Rescan board", "Engines", "Switch team", "Restart", "Update", "Quit"};
    const char *hint[10] = {cfg.mode ? "on, moves are hidden" : "hides the moves", "tags on every square",
                            "click one, pick whats there", "", "if it got lost", "get, pick or delete them", "", "",
                            "pulls the newest version", ""};
    int acts[10] = {ACT_MODE, ACT_DEBUG, ACT_FIX, ACT_TURN, ACT_RESCAN, ACT_LIB_OPEN, ACT_FLIP, ACT_RESTART, ACT_UPDATE, ACT_QUIT};
    int icons[10] = {9, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    int group[10] = {0, 1, 1, 1, 1, 2, 2, 3, 3, 4};
    double mw = 236, ih = 42, gap = 9, mh = 10 * ih + 4 * gap + 12;
    Rect m = {p.x + p.w - mw, p.y + p.h + 6, mw, mh};
    if (m.y + mh > outr.y + outr.h) m.y = p.y - mh - 6;
    if (m.y < outr.y) m.y = outr.y + 4;
    if (m.x < outr.x + 4) m.x = outr.x + 4;
    int closing = !menu_open;
    double an = closing ? ease(menu_shut, 130) : ease(menu_t, 190);
    Canvas *c = layer(0, dst, m);
    if (!c) return;
    if (!closing) covers(m, bl);
    panel_bg(c, m, 14, TH->menu);
    if (hit) add_hit(m, ACT_BLOCK);
    double y = m.y + 6;
    for (int k = 0; k < 10; k++) {
        if (k && group[k] != group[k - 1]) {
            draw_line(c, m.x + 14, y + gap / 2 - 1, m.x + m.w - 14, y + gap / 2 - 1, 1, TH->btn);
            y += gap;
        }
        Rect it = {m.x + 6, y, m.w - 12, ih - 3};
        int on = (k == 0 && cfg.mode) || (k == 1 && debug_on) || (k == 2 && fix_mode), quit = acts[k] == ACT_QUIT;
        uint32_t fg = quit ? 0xFFFF5A5A : TH->item_fg;
        draw_round_rect(c, it, 10, on ? TH->btn : TH->item);
        if (on) draw_round_rect(c, (Rect){it.x + 2, it.y + 8, 3, it.h - 16}, 1.5, TH->title);
        Rect ib = {it.x + 10, it.y + (it.h - 30) / 2, 30, 30};
        draw_round_rect(c, ib, 8, quit ? 0x40FF5A5A : (TH->btn | 0xFF000000));
        draw_icon(c, (Rect){ib.x + 7, ib.y + 7, 16, 16}, icons[k] == 9 && on ? 10 : icons[k], quit ? 0xFFFF5A5A : on ? TH->title : TH->btn_fg);
        if (*hint[k]) {
            draw_text(c, it.x + 48, it.y + 2, items[k], 0, fg);
            draw_text(c, it.x + 48, it.y + 19, fit_text(c, hint[k], it.w - 54), 0, (TH->item_fg & 0xFFFFFF) | 0x90000000);
        } else {
            draw_text(c, it.x + 48, it.y + (it.h - 17) / 2, items[k], 0, fg);
        }
        if (hit) add_hit(it, acts[k]);
        y += ih;
    }
    canvas_blit(dst, c, closing ? 1 - an : an, 0, closing ? -6 * an : -12 * (1 - an), -1);
}

static const char *tab_names[] = {"Look", "Your move", "Their move", "Engine", "Fun", "Helper", "Themes"};
#define NTABS 7
#define TAB_THEMES 6
static int set_tab, cslot, cpart;

static int tab_of(int i)
{
    int t = 0;
    for (int k = 0; k <= i; k++)
        if (sets[k].section)
            for (int j = 0; j < NTABS; j++)
                if (!strcmp(sets[k].section, tab_names[j])) t = j;
    return t;
}

static uint32_t swatch(int i)
{
    if (i >= 60) {
        uint32_t g = (uint32_t)(255 * (i - 60) / 11);
        return 0xFF000000u | g << 16 | g << 8 | g;
    }
    double h = (i % 12) / 12.0 * 6, l = (double[]){0.22, 0.38, 0.52, 0.68, 0.84}[i / 12], sat = 0.85;
    double ch = (1 - fabs(2 * l - 1)) * sat, x = ch * (1 - fabs(wrap(h, 2) - 1)), m = l - ch / 2, r = 0, g = 0, b = 0;
    switch ((int)h) {
    case 0: r = ch, g = x; break;
    case 1: r = x, g = ch; break;
    case 2: g = ch, b = x; break;
    case 3: g = x, b = ch; break;
    case 4: r = x, b = ch; break;
    default: r = ch, b = x;
    }
    return 0xFF000000u | (uint32_t)((r + m) * 255) << 16 | (uint32_t)((g + m) * 255) << 8 | (uint32_t)((b + m) * 255);
}

static void draw_theme_card(Canvas *c, Rect r, int ti, int hit)
{
    const Theme *t = &themes[ti];
    if (ti == cfg.theme) draw_round_rect(c, (Rect){r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 9, TH->title);
    draw_round_rect(c, r, 7, t->panel | 0xFF000000);
    draw_text(c, r.x + 7, r.y + 4, fit_text(c, t->name, r.w - 9), 0, t->title);
    uint32_t dots[] = {t->sub, t->btn, t->grid, t->arrow};
    for (int k = 0; k < 4; k++)
        draw_round_rect(c, (Rect){r.x + 8 + k * 15, r.y + r.h - 16, 11, 11}, 5.5, dots[k] | 0xFF000000);
    if (hit) add_hit_arg(r, ACT_THEME, ti);
}

static double themes_tab(Canvas *c, Rect m, double y, double pad, int draw, int hit)
{
    double w = m.w - 2 * pad, x0 = m.x + pad, chh = 44, g = 8;
    int per = (int)((w + g) / (80 + g));
    if (per < 1) per = 1;
    double cw = (w - (per - 1) * g) / per;
    if (draw) draw_text(c, x0 + 2, y, "Pick one", 0, TH->title);
    y += 22;
    for (int i = 0; i < NTHEMES; i++) {
        Rect r = {x0 + (i % per) * (cw + g), y + (i / per) * (chh + g), cw, chh};
        if (draw) draw_theme_card(c, r, i, hit);
    }
    y += ((NTHEMES + per - 1) / per) * (chh + g) + 6;
    if (draw) draw_text(c, x0 + 2, y, "Make your own", 0, TH->title);
    y += 24;
    double bx = x0;
    for (int k = 0; k < NCUSTOM; k++) {
        Rect b = {bx, y, 84, 26};
        int on = cslot == k;
        if (draw) {
            draw_button(c, b, themes[CUSTOM0 + k].name, on ? TH->title : TH->btn, on ? 0xFF181818 : TH->btn_fg);
            if (hit) add_hit_arg(b, ACT_CSLOT, k);
        }
        bx += 90;
    }
    const char *cpl = cfg.theme >= CUSTOM0 ? "Start from Dark" : "Copy current theme";
    Rect cp = {bx, y, text_lw(c, cpl, 0) + 22, 26};
    if (cp.x + cp.w > x0 + w) cp.x = x0, cp.y = y += 32;
    if (draw) {
        draw_button(c, cp, cpl, TH->btn, TH->btn_fg);
        if (hit) add_hit(cp, ACT_CCOPY);
    }
    y += 34;
    double lw = 150, sw = 22, sg = 4, pw = 12 * (sw + sg);
    int side = w >= lw + 12 + pw;
    for (int k = 0; k < NPARTS; k++) {
        Rect r = {x0, y + k * 24, lw, 22};
        if (draw) {
            draw_round_rect(c, r, 5, k == cpart ? TH->btn : TH->item);
            draw_round_rect(c, (Rect){r.x + 5, r.y + 4, 14, 14}, 7, (TH->item_fg & 0xFFFFFF) | 0x60000000);
            draw_round_rect(c, (Rect){r.x + 6, r.y + 5, 12, 12}, 6, *part(&themes[CUSTOM0 + cslot], k) | 0xFF000000);
            draw_text(c, r.x + 24, r.y + 2, part_names[k], 0, TH->item_fg);
            if (hit) add_hit_arg(r, ACT_CPART, k);
        }
    }
    double py = side ? y : y + NPARTS * 24 + 8, px = side ? x0 + lw + 12 : x0;
    uint32_t cur = *part(&themes[CUSTOM0 + cslot], cpart) | 0xFF000000;
    for (int i = 0; i < 72; i++) {
        Rect r = {px + (i % 12) * (sw + sg), py + (i / 12) * (sw + sg), sw, sw};
        if (!draw) continue;
        uint32_t col = swatch(i);
        if (col == cur) draw_round_rect(c, (Rect){r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 7, TH->item_fg);
        else draw_round_rect(c, (Rect){r.x - 1, r.y - 1, r.w + 2, r.h + 2}, 6, (TH->item_fg & 0xFFFFFF) | 0x30000000);
        draw_round_rect(c, r, 5, col);
        if (hit) add_hit_arg(r, ACT_CSWATCH, i);
    }
    double ph = 6 * (sw + sg);
    if (draw) draw_text(c, px, py + ph + 4, "Changes show up right away", 0, TH->sub);
    return side ? y + fmax(NPARTS * 24, ph + 24) + 4 : py + ph + 28;
}

static void draw_settings(Canvas *dst, Rect p, Rect bl, Rect outr, int hit)
{
    Canvas *c = dst;
    double cw = 272, rh = 28, pad = 12;
    int ncol = outr.w >= cw * 2 + pad * 3 + 8 ? 2 : 1;
    double mw = cw * ncol + pad * (ncol + 1);
    int idx[NSET], n = 0;
    for (int i = 0; i < NSET; i++)
        if (tab_of(i) == set_tab) idx[n++] = i;
    double tabh = 26, tg = 4, tx = pad, ty = 46;
    Rect tabr[NTABS];
    for (int k = 0; k < NTABS; k++) {
        double tw = text_lw(c, tab_names[k], 0) + 16;
        if (tx + tw > mw - pad) tx = pad, ty += tabh + tg;
        tabr[k] = (Rect){tx, ty, tw, tabh};
        tx += tw + tg;
    }
    double top = ty + tabh + 14, body;
    int per = (n + ncol - 1) / ncol;
    Rect m = {0, 0, mw, 0};
    if (set_tab == TAB_THEMES)
        body = themes_tab(c, m, top, pad, 0, 0) - top;
    else
        body = per * rh + 40;
    double mh = top + body + pad;
    static double from_h, to_h;
    static long h_t = -99999;
    if (mh != to_h) {
        from_h = to_h ? from_h + (to_h - from_h) * ease(h_t, 200) : mh;
        to_h = mh, h_t = now_ms();
    }
    mh = from_h + (to_h - from_h) * ease(h_t, 200);
    m = (Rect){p.x + p.w - mw, p.y + p.h + 6, mw, mh};
    if (m.x < outr.x + 4) m.x = outr.x + 4;
    if (m.x + mw > outr.x + outr.w - 4) m.x = outr.x + outr.w - 4 - mw;
    if (m.y + mh > outr.y + outr.h) m.y = p.y - mh - 6;
    if (m.y < outr.y) m.y = outr.y + 4;
    int closing = !settings_open;
    double an = closing ? ease(set_shut, 130) : ease(set_t, 210);
    c = layer(0, dst, m);
    if (!c) return;
    if (!closing) covers(m, bl);
    panel_bg(c, m, 14, TH->menu);
    if (hit) add_hit(m, ACT_BLOCK);
    set_text_zoom(1);
    draw_text(c, m.x + pad, m.y + 10, "Settings", 1, TH->title);
    Rect x = {m.x + m.w - pad - 26, m.y + 10, 26, 26};
    draw_close(c, x);
    if (hit) add_hit(x, ACT_SETTINGS);
    draw_line(c, m.x + pad, m.y + 40, m.x + m.w - pad, m.y + 40, 1, TH->btn);
    double tq = ease(tab_t, 200);
    Rect a0 = tabr[tab_prev < NTABS ? tab_prev : set_tab], a1 = tabr[set_tab];
    Rect pill = {m.x + a0.x + (a1.x - a0.x) * tq, m.y + a0.y + (a1.y - a0.y) * tq, a0.w + (a1.w - a0.w) * tq, tabh};
    for (int k = 0; k < NTABS; k++) {
        Rect r = tabr[k];
        r.x += m.x, r.y += m.y;
        draw_round_rect(c, r, 13, TH->item);
        if (hit) add_hit_arg(r, ACT_TAB, k);
    }
    draw_round_rect(c, pill, 13, TH->title);
    for (int k = 0; k < NTABS; k++) {
        Rect r = tabr[k];
        r.x += m.x, r.y += m.y;
        double tw = text_lw(c, tab_names[k], 0), cx = r.x + r.w / 2;
        int under = cx > pill.x && cx < pill.x + pill.w && fabs(r.y - pill.y) < 4;
        draw_text(c, r.x + (r.w - tw) / 2, r.y + 4, tab_names[k], 0, under ? 0xFF181818 : TH->item_fg);
    }
    Canvas *panel = c;
    Rect area = {m.x, m.y + top - 6, m.w, m.h - top + 6};
    if (tq < 1 && (c = layer(1, panel, area)) == NULL) c = panel;
    Rect ddbox = {0};
    if (set_tab == TAB_THEMES) themes_tab(c, m, m.y + top, pad, 1, hit);
    for (int j = 0; set_tab != TAB_THEMES && j < n; j++) {
        int i = idx[j];
        const Setting *st = &sets[i];
        int col = j / per;
        Rect row = {m.x + pad + col * (cw + pad), m.y + top + (j % per) * rh, cw, rh - 3};
        draw_round_rect(c, row, 7, TH->item);
        draw_text(c, row.x + 10, row.y + 4, st->label, 0, TH->item_fg);
        if (st->names == onoff) {
            int on = *st->v;
            Rect sw = {row.x + row.w - 46, row.y + 3.5, 38, 18};
            double d = 14, q = i == tog_i ? ease(tog_t, 160) : 1, v = on ? q : 1 - q;
            draw_round_rect(c, sw, 9, mix(TH->btn, TH->title, v));
            draw_round_rect(c, (Rect){sw.x + 2 + (sw.w - d - 4) * v, sw.y + 2, d, d}, d / 2, mix(0xFFE8E8E8, 0xFF181818, v));
        } else {
            Rect box = {row.x + row.w - 126, row.y + 3, 120, row.h - 6};
            draw_round_rect(c, box, 5, dd_open == i ? TH->title : TH->btn);
            uint32_t fg = dd_open == i ? 0xFF181818 : TH->btn_fg;
            if (st->v == &cfg.mcolor || st->v == &cfg.tcolor || st->v == &cfg.gcolor || st->v == &cfg.theme) {
                uint32_t sw = st->v == &cfg.theme ? TH->panel : pick_color(*st->v, st->v == &cfg.mcolor ? TH->arrow : st->v == &cfg.gcolor ? TH->grid : 0xC8FF4D4D);
                draw_round_rect(c, (Rect){box.x - 18, box.y + 4, 12, 12}, 6, sw | 0xFF000000);
            }
            draw_text(c, box.x + 8, box.y + 1,
                      fit_text(c, st->v == &cfg.library ? (dl_pid > 0 ? "Getting..." : "Browse") : st->names[*st->v], box.w - 26),
                      0, fg);
            double tx2 = box.x + box.w - 15, ty2 = box.y + box.h / 2 - 2;
            if (st->v == &cfg.library || st->v == &cfg.engine)
                draw_text(c, tx2 + 1, box.y + 1, ">", 0, fg);
            else
                draw_triangle(c, tx2, ty2, tx2 + 9, ty2, tx2 + 4.5, ty2 + 5, fg);
            if (dd_open == i) ddbox = box;
        }
        if (hit) add_hit_arg(row, ACT_SET, i);
    }
    Rect rs = {m.x + pad, m.y + top + body - 30, 150, 26};
    if (set_tab != TAB_THEMES) {
        draw_button(c, rs, "Reset this tab", TH->btn, TH->btn_fg);
        if (hit) add_hit(rs, ACT_RESET);
    }
    if (set_tab == 0) {
        Rect tt = {rs.x + rs.w + 8, rs.y, 140, 26};
        draw_button(c, tt, "More themes >", TH->btn, TH->btn_fg);
        if (hit) add_hit_arg(tt, ACT_TAB, TAB_THEMES);
    }
    if (c != panel) canvas_blit(panel, c, tq, 30 * (1 - tq) * tab_dir, 0, -1);
    canvas_blit(dst, panel, closing ? 1 - an : an, 0, closing ? -6 * an : -12 * (1 - an), -1);
    c = dst;
    if (closing) return;
    if (dd_open >= 0 && ddbox.w > 0) {
        const Setting *st = &sets[dd_open];
        double iw = ddbox.w, ih = 24;
        for (int k = 0; k < st->n; k++) iw = fmax(iw, text_lw(c, st->names[k], 0) + 30);
        Rect l = {ddbox.x + ddbox.w - iw, ddbox.y + ddbox.h + 3, iw, st->n * ih + 6};
        if (l.y + l.h > outr.y + outr.h) l.y = ddbox.y - l.h - 3;
        if (l.y < outr.y) l.y = outr.y + 4;
        covers(l, bl);
        double dq = ease(dd_t, 160);
        Canvas *base = c;
        if ((c = layer(2, base, l)) == NULL) c = base;
        for (int k = 3; k >= 1; k--)
            draw_round_rect(c, (Rect){l.x - k, l.y - k + 3, l.w + 2 * k, l.h + 2 * k}, 9 + k, 0x18000000);
        draw_round_rect(c, (Rect){l.x - 1, l.y - 1, l.w + 2, l.h + 2}, 10, TH->title);
        draw_round_rect(c, l, 9, TH->menu);
        if (hit) add_hit(l, ACT_BLOCK);
        for (int k = 0; k < st->n; k++) {
            Rect it = {l.x + 3, l.y + 3 + k * ih, l.w - 6, ih - 2};
            if (k == *st->v) draw_round_rect(c, it, 5, TH->btn);
            if (st->v == &cfg.mcolor || st->v == &cfg.tcolor || st->v == &cfg.gcolor || st->v == &cfg.theme) {
                uint32_t sw = (st->v == &cfg.theme ? themes[k].panel : pick_color(k, st->v == &cfg.mcolor ? TH->arrow : st->v == &cfg.gcolor ? TH->grid : 0xC8FF4D4D)) | 0xFF000000;
                draw_round_rect(c, (Rect){it.x + 6, it.y + 5, 12, 12}, 6, sw);
                draw_text(c, it.x + 24, it.y + 3, st->names[k], 0, TH->item_fg);
            } else {
                draw_text(c, it.x + 8, it.y + 3, st->names[k], 0, TH->item_fg);
            }
            if (hit) add_hit_arg(it, ACT_PICK, dd_open * 64 + k);
        }
        if (c != base) canvas_blit(base, c, fmin(1, dq * 1.5), 0, -5 * (1 - dq), 18 + l.h * dq);
    }
}

static void draw_extras(Canvas *c, Rect bl, Rect p, Rect outr, int hit)
{
    double s = bl.w / 8;
    if (debug_on || fix_mode) {
        for (int sq = 0; sq < 64; sq++) {
            int pc = tracker.pos.sq[sq];
            if (pc == EMPTY) continue;
            int r, col;
            sq_display(sq, tracker.white_bottom, &r, &col);
            Rect lb = {bl.x + col * s + 4, bl.y + r * s + 4, 18, 19};
            int white = pc / 6 == WHITE;
            draw_round_rect(c, lb, 4, white ? 0xF0FFFFFF : 0xF0101010);
            draw_frame(c, lb, 1, 0xFFFF2BD6);
            char t[2] = {piece_letters[pc % 6], 0};
            draw_text(c, lb.x + 4, lb.y + 1, t, 0, white ? 0xFF000000 : 0xFFFFFFFF);
        }
    }
    if (fix_mode) {
        for (int sq = 0; sq < 64; sq++) {
            int r, col;
            sq_display(sq, tracker.white_bottom, &r, &col);
            Rect sr = {bl.x + col * s, bl.y + r * s, s, s};
            if (sq == fix_sq) draw_frame(c, (Rect){sr.x + 2, sr.y + 2, s - 4, s - 4}, 4, 0xFFFF2BD6);
            if (hit) add_hit_arg(sr, ACT_SQ, sq);
        }
        if (fix_sq >= 0) {
            double h = 78;
            Rect pk = {bl.x, bl.y + bl.h + 8, bl.w, h};
            if (pk.y + h > outr.y + outr.h) pk.y = bl.y + bl.h - h - 4;
            draw_round_rect(c, pk, 10, 0xF8181818);
            if (hit) add_hit(pk, ACT_BLOCK);
            double bw = (pk.w - 8 * 6) / 7, bh = 30;
            for (int row = 0; row < 2; row++)
                for (int k = 0; k < 7; k++) {
                    Rect b = {pk.x + 6 + k * (bw + 6), pk.y + 6 + row * (bh + 6), bw, bh};
                    char lab[24];
                    uint32_t bg = row == 0 ? 0xFFF2F2F2 : 0xFF000000, fg = row == 0 ? 0xFF111111 : 0xFFF2F2F2;
                    int act = ACT_PIECE, arg;
                    if (k < 6) {
                        snprintf(lab, sizeof(lab), "%c", piece_letters[k]);
                        arg = row * 6 + k;
                    } else if (row == 0) {
                        snprintf(lab, sizeof(lab), "Empty");
                        arg = EMPTY;
                        bg = 0xFF3A3A3A;
                        fg = 0xFFFFFFFF;
                    } else {
                        snprintf(lab, sizeof(lab), "%s", tracker.pos.side == WHITE ? "W to go" : "B to go");
                        act = ACT_TURN;
                        arg = 0;
                        bg = 0xFF3A3A3A;
                        fg = 0xFFFFFFFF;
                    }
                    draw_button(c, b, lab, bg, fg);
                    if (row == 1 && k < 6) draw_frame(c, b, 1.2, 0xFF6A6A6A);
                    if (hit) add_hit_arg(b, act, arg);
                }
        }
    }
    if (menu_open || now_ms() - menu_shut < 130) draw_menu(c, p, bl, outr, menu_open && hit);
    if (settings_open || now_ms() - set_shut < 130) draw_settings(c, p, bl, outr, settings_open && hit);
    if (confirm_quit) {
        Rect q = {p.x + p.w - 250, p.y + p.h + 6, 250, 74};
        if (q.y + q.h > outr.y + outr.h) q.y = p.y - q.h - 6;
        covers(q, bl);
        Canvas *dst = c;
        if ((c = layer(0, dst, q)) == NULL) c = dst;
        panel_bg(c, q, 14, TH->menu);
        if (hit) add_hit(q, ACT_BLOCK);
        draw_text(c, q.x + 14, q.y + 9, "Quit open-chease?", 0, TH->item_fg);
        Rect yes = {q.x + 14, q.y + 36, 104, 28}, no = {q.x + 128, q.y + 36, 104, 28};
        draw_button(c, yes, "Quit", 0xFFD03A3A, 0xFFFFFFFF);
        draw_button(c, no, "Cancel", 0xFF3A3A3A, 0xFFFFFFFF);
        if (hit) {
            add_hit(yes, ACT_QUIT_YES);
            add_hit(no, ACT_QUIT_NO);
        }
        if (c != dst) {
            double an = ease(conf_t, 180);
            canvas_blit(dst, c, an, 0, -10 * (1 - an), -1);
            c = dst;
        }
    }
    if (lib_open) draw_library(c, outr, bl, hit);
}

static int qs(Pos *p, int alpha, int beta, int d)
{
    int stand = evaluate(p);
    if (d == 0 || stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    Move ms[256];
    int n = pos_gen_legal(p, ms);
    for (int i = 0; i < n && alpha < beta; i++) {
        if (p->sq[M_TO(ms[i])] == EMPTY && M_FLAG(ms[i]) != FL_EP) continue;
        Undo u;
        if (!pos_make(p, ms[i], &u)) continue;
        int sc = -qs(p, -beta, -alpha, d - 1);
        pos_unmake(p, &u);
        if (sc > alpha) alpha = sc;
    }
    return alpha;
}

static int quick(Pos *p, int depth, int alpha, int beta) // own stack, doesnt touch the shared tables
{
    if (!depth) return qs(p, alpha, beta, 4);
    Move ms[256];
    int n = pos_gen_legal(p, ms);
    if (!n) return pos_in_check(p, p->side) ? -30000 : 0;
    for (int i = 0; i < n && alpha < beta; i++) {
        Undo u;
        if (!pos_make(p, ms[i], &u)) continue;
        int sc = -quick(p, depth - 1, -beta, -alpha);
        pos_unmake(p, &u);
        if (sc > alpha) alpha = sc;
    }
    return alpha;
}

static Move fun_pick(const Analysis *an)
{
    static u64 ck;
    static int cmode = -1;
    static Move cm;
    if (!cfg.chaos || !an->best) return an->best;
    if (ck == tracker.pos.key && cmode == cfg.chaos) return cm;
    Pos p = tracker.pos;
    Move ms[256];
    int sc[256], n = pos_gen_legal(&p, ms);
    if (!n) return an->best;
    int best = -99999, worst = 99999;
    for (int i = 0; i < n; i++) {
        Undo u;
        pos_make(&p, ms[i], &u);
        sc[i] = -quick(&p, 1, -99999, 99999);
        pos_unmake(&p, &u);
        if (sc[i] > best) best = sc[i];
        if (sc[i] < worst) worst = sc[i];
    }
    Move pool[256];
    int np = 0;
    Move pick = an->best;
    switch (cfg.chaos) {
    case 1:
        for (int i = 0; i < n; i++)
            if (sc[i] >= best - 60) pool[np++] = ms[i];
        break;
    case 2:
        for (int k = 0; k < 3 && k < n; k++) {
            int bi = -1;
            for (int i = 0; i < n; i++) {
                int used = 0;
                for (int j = 0; j < np; j++) used |= pool[j] == ms[i];
                if (!used && (bi < 0 || sc[i] > sc[bi])) bi = i;
            }
            if (bi >= 0) pool[np++] = ms[bi];
        }
        break;
    case 3:
        if (rand() & 1) pool[np++] = an->best;
        else
            for (int i = 0; i < n; i++) pool[np++] = ms[i];
        break;
    case 4:
        for (int i = 0; i < n; i++) pool[np++] = ms[i];
        break;
    case 5:
        for (int i = 0; i < n; i++)
            if (sc[i] == worst) pool[np++] = ms[i];
        break;
    case 6:
        for (int i = 0; i < n; i++)
            if (sc[i] <= best - 200) pool[np++] = ms[i];
        if (!np)
            for (int i = 0; i < n; i++)
                if (sc[i] == worst) pool[np++] = ms[i];
        break;
    case 7: { // what a decent human would play
        double w[256], total = 0;
        int us = p.side, them = us ^ 1, early = p.fullmove < 12;
        int last_to = tracker.last_move ? (int)M_TO(tracker.last_move) : -1;
        for (int i = 0; i < n; i++) {
            w[i] = 0;
            if (sc[i] < best - 120) continue;
            int from = M_FROM(ms[i]), to = M_TO(ms[i]), ty = p.sq[from] % 6, cap = p.sq[to] != EMPTY;
            double x = exp((sc[i] - best) / 60.0);
            if (cap) x *= to == last_to ? 2.5 : 1.6; // people always look at recaptures first
            if (M_FLAG(ms[i]) == FL_CASTLE) x *= 1.8;
            if (pos_is_attacked(&p, from, them) && ty != PAWN) x *= 1.5;
            if (early && (ty == KNIGHT || ty == BISHOP) && (from >> 3) == (us == WHITE ? 0 : 7)) x *= 1.4;
            if (early && ty == QUEEN) x *= 0.6;
            if (ty == KING && M_FLAG(ms[i]) != FL_CASTLE && !early && p.fullmove < 40) x *= 0.5;
            if (ty == PAWN && !cap && ((to & 7) == 0 || (to & 7) == 7)) x *= 0.7;
            Undo u;
            pos_make(&p, ms[i], &u);
            if (pos_in_check(&p, p.side)) x *= 1.4;
            pos_unmake(&p, &u);
            w[i] = x, total += x;
        }
        double r = (rand() / (RAND_MAX + 1.0)) * total;
        for (int i = 0; i < n && total > 0; i++) {
            r -= w[i];
            if (w[i] > 0 && r <= 0) {
                pool[np++] = ms[i];
                break;
            }
        }
        break;
    }
    }
    if (np) pick = pool[rand() % np];
    ck = tracker.pos.key, cmode = cfg.chaos, cm = pick;
    return pick;
}

static const char *quip(u64 key)
{
    static const char *good[] = {"trust me bro", "easy", "big brain time", "this is the one", "dont think, just do it",
                                 "gm move right here", "they wont see it coming", "100% sure (90%)", "free elo",
                                 "stockfish would be proud", "textbook", "clean", "this one slaps", "lock in",
                                 "magnus would play this", "certified banger", "ez clap", "no thoughts just this",
                                 "they're not ready", "chef's kiss", "play it fast, look confident", "calculated",
                                 "your opponent is sweating", "W move", "go go go", "brilliant (probably)"};
    static const char *bad[] = {"this is definitely good", "bold strategy", "trust the process", "chaos mode go",
                                "what could go wrong", "its a gambit i promise", "they'll never expect this",
                                "galaxy brain", "for the content", "sacrifice everything", "the engine said so (it didnt)",
                                "reverse psychology", "hold my pawn", "play it with confidence", "nobody has ever tried this",
                                "it's called strategy", "this is fine", "speedrunning a loss", "just vibes",
                                "you asked for chaos", "chess is a social game anyway", "for science",
                                "i saw it in a dream", "they'll be too confused", "sorry in advance", "yolo",
                                "let him cook", "statistically a move", "your friend is gonna laugh"};
    const char **list = cfg.chaos >= 4 ? bad : good;
    size_t n = cfg.chaos >= 4 ? sizeof(bad) / sizeof(*bad) : sizeof(good) / sizeof(*good);
    return list[(key * 2654435761u >> 7) % n];
}

static const char *roast(int mine, int grade)
{
    static const char *me[5][8] = {
        {"great move!", "big brain", "clean", "gm vibes", "stockfish approves", "you actually saw that?", "nice one", "cooking"},
        {"good move", "solid", "fine i guess", "not bad", "respectable", "that works", "ok ok", "decent"},
        {"eh, inaccuracy", "there was better", "kinda mid", "close but no", "hmm", "could be worse", "slightly cooked", "meh"},
        {"mistake", "that was not it", "oof", "why", "the engine is disappointed", "rough", "bruh", "you felt that one"},
        {"blunder!!", "you hung it", "bro what", "free stuff for them", "we're cooked", "uninstall", "the engine is crying",
         "certified blunder"}};
    static const char *them[5][8] = {
        {"they found a good one", "they're cooking", "uh oh they saw it", "they're locked in", "respect", "they're not bad huh",
         "careful now", "they're on it"},
        {"they played fine", "solid from them", "normal move", "nothing special", "they're holding", "okay from them",
         "meh", "fair"},
        {"they slipped a bit", "inaccurate from them", "they missed something", "small gift", "they're wobbling",
         "kinda mid from them", "look closer", "chance coming"},
        {"they messed up", "mistake from them, punish it", "they fumbled", "go get em", "they're slipping", "oops from them",
         "free chances", "they'll regret that"},
        {"they blundered", "free piece incoming", "they hung something, go get it", "thank you for the gift",
         "they're throwing", "huge blunder from them", "they're cooked", "christmas came early"}};
    return (mine ? me : them)[grade][rand() % 8];
}

static void draw_sqnames(Canvas *c, Rect bl, int white_bottom)
{
    double s = bl.w / 8;
    for (int sq = 0; sq < 64; sq++) {
        int r, col;
        sq_display(sq, white_bottom, &r, &col);
        char t[3] = {(char)('a' + (sq & 7)), (char)('1' + (sq >> 3)), 0};
        double tw = text_lw(c, t, 0);
        Rect lb = {bl.x + (col + 1) * s - tw - 9, bl.y + r * s + 3, tw + 6, 17};
        draw_round_rect(c, lb, 4, faded(0x90101010));
        draw_text(c, lb.x + 3, lb.y, t, 0, faded(0xE6F5F5F5));
    }
}

static void draw_confetti(Canvas *c, Rect bl)
{
    static const uint32_t cols[] = {0xFFFF4D4D, 0xFFFFC400, 0xFF2BD957, 0xFF3D9BFF, 0xFFFF5FC8, 0xFFA86BFF};
    unsigned seed = (unsigned)(now_ms() / 300);
    for (int i = 0; i < 120; i++) {
        seed = seed * 1103515245u + 12345u;
        double x = bl.x + (seed >> 8) % (unsigned)bl.w;
        seed = seed * 1103515245u + 12345u;
        double y = bl.y + (seed >> 8) % (unsigned)bl.h;
        double w = 5 + (seed >> 20) % 7, h = 3 + (seed >> 24) % 5;
        draw_rect(c, (Rect){x, y, w, h}, cols[(seed >> 16) % 6]);
    }
}

static long pos_since;
static u64 seen_key, rate_key, known_key;
static int known_score, rate_pending, rate_before, rate_mine;
static Move rate_move;
static long badge_until;
static char badge[4];
static uint32_t badge_col;

static int user_score(const Analysis *a, int user_to_move)
{
    int sc = a->mate ? (a->mate > 0 ? 1000 : -1000) : a->score;
    return user_to_move ? sc : -sc;
}

static const char *openings[][2] = {
    {"e2e4", "King's Pawn"},
    {"d2d4", "Queen's Pawn"},
    {"c2c4", "English Opening"},
    {"g1f3", "Reti Opening"},
    {"f2f4", "Bird's Opening"},
    {"b2b3", "Nimzo-Larsen Attack"},
    {"g2g3", "King's Fianchetto"},
    {"b2b4", "Polish Opening"},
    {"b1c3", "Van Geet Opening"},
    {"g2g4", "Grob Opening"},
    {"a2a3", "Anderssen's Opening"},
    {"e2e4 e7e5", "Open Game"},
    {"e2e4 c7c5", "Sicilian Defense"},
    {"e2e4 e7e6", "French Defense"},
    {"e2e4 c7c6", "Caro-Kann Defense"},
    {"e2e4 d7d5", "Scandinavian Defense"},
    {"e2e4 g8f6", "Alekhine's Defense"},
    {"e2e4 d7d6", "Pirc Defense"},
    {"e2e4 g7g6", "Modern Defense"},
    {"e2e4 b8c6", "Nimzowitsch Defense"},
    {"e2e4 b7b6", "Owen's Defense"},
    {"e2e4 a7a6", "St. George Defense"},
    {"e2e4 e7e5 g1f3", "King's Knight Opening"},
    {"e2e4 e7e5 f2f4", "King's Gambit"},
    {"e2e4 e7e5 b1c3", "Vienna Game"},
    {"e2e4 e7e5 f1c4", "Bishop's Opening"},
    {"e2e4 e7e5 d2d4", "Center Game"},
    {"e2e4 e7e5 d1h5", "Wayward Queen Attack"},
    {"e2e4 e7e5 e1e2", "Bongcloud lmao"},
    {"e2e4 e7e5 g1f3 g8f6", "Petrov's Defense"},
    {"e2e4 e7e5 g1f3 d7d6", "Philidor Defense"},
    {"e2e4 e7e5 g1f3 f7f6", "Damiano Defense"},
    {"e2e4 e7e5 g1f3 d7d5", "Elephant Gambit"},
    {"e2e4 e7e5 g1f3 f7f5", "Latvian Gambit"},
    {"e2e4 e7e5 g1f3 b8c6 f1b5", "Ruy Lopez"},
    {"e2e4 e7e5 g1f3 b8c6 f1b5 a7a6", "Ruy Lopez, Morphy"},
    {"e2e4 e7e5 g1f3 b8c6 f1b5 g8f6", "Ruy Lopez, Berlin"},
    {"e2e4 e7e5 g1f3 b8c6 f1c4", "Italian Game"},
    {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5", "Giuoco Piano"},
    {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 b2b4", "Evans Gambit"},
    {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6", "Two Knights Defense"},
    {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 f3g5", "Two Knights, Knight Attack"},
    {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 f3g5 d7d5 e4d5 f6d5 g5f7", "Fried Liver Attack"},
    {"e2e4 e7e5 g1f3 b8c6 d2d4", "Scotch Game"},
    {"e2e4 e7e5 g1f3 b8c6 b1c3", "Three Knights"},
    {"e2e4 e7e5 g1f3 b8c6 b1c3 g8f6", "Four Knights Game"},
    {"e2e4 e7e5 f1c4 b8c6 d1h5 g8f6 h5f7", "Scholar's Mate"},
    {"e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6", "Sicilian, Najdorf"},
    {"e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6", "Sicilian, Dragon"},
    {"e2e4 c7c5 b1c3", "Closed Sicilian"},
    {"e2e4 c7c5 c2c3", "Sicilian, Alapin"},
    {"e2e4 c7c5 d2d4 c5d4 c2c3", "Smith-Morra Gambit"},
    {"e2e4 e7e6 d2d4 d7d5 e4e5", "French, Advance"},
    {"e2e4 c7c6 d2d4 d7d5 e4e5", "Caro-Kann, Advance"},
    {"d2d4 d7d5 c2c4", "Queen's Gambit"},
    {"d2d4 d7d5 c2c4 e7e6", "Queen's Gambit Declined"},
    {"d2d4 d7d5 c2c4 d5c4", "Queen's Gambit Accepted"},
    {"d2d4 d7d5 c2c4 c7c6", "Slav Defense"},
    {"d2d4 d7d5 c2c4 b8c6", "Chigorin Defense"},
    {"d2d4 d7d5 c1f4", "London System"},
    {"d2d4 g8f6 c1f4", "London System"},
    {"d2d4 d7d5 g1f3 g8f6 c1f4", "London System"},
    {"d2d4 d7d5 e2e4", "Blackmar-Diemer Gambit"},
    {"d2d4 g8f6 c2c4 g7g6", "King's Indian Defense"},
    {"d2d4 g8f6 c2c4 g7g6 b1c3 d7d5", "Grunfeld Defense"},
    {"d2d4 g8f6 c2c4 e7e6 b1c3 f8b4", "Nimzo-Indian Defense"},
    {"d2d4 g8f6 c2c4 e7e6 g1f3 b7b6", "Queen's Indian Defense"},
    {"d2d4 g8f6 c2c4 c7c5", "Benoni Defense"},
    {"d2d4 g8f6 c2c4 c7c5 d4d5 b7b5", "Benko Gambit"},
    {"d2d4 g8f6 c2c4 e7e5", "Budapest Gambit"},
    {"d2d4 g8f6 c1g5", "Trompowsky Attack"},
    {"d2d4 f7f5", "Dutch Defense"},
    {"d2d4 e7e5", "Englund Gambit"},
    {"c2c4 e7e5", "English, Reversed Sicilian"},
    {"f2f3 e7e5 g2g4 d8h4", "Fool's Mate"},
};
#define NOPEN ((int)(sizeof(openings) / sizeof(openings[0])))
static u64 open_keys[NOPEN];
static u64 start_key;
static const char *open_name;

static void init_openings(void)
{
    Pos p;
    pos_startpos(&p);
    start_key = p.key;
    for (int i = 0; i < NOPEN; i++) {
        pos_startpos(&p);
        char buf[160], *save, *tok;
        snprintf(buf, sizeof(buf), "%s", openings[i][0]);
        for (tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
            Move m = move_from_uci(&p, tok);
            Undo u;
            if (!m || !pos_make(&p, m, &u)) break;
        }
        open_keys[i] = tok ? 0 : p.key;
    }
}

#define WMAX 160
static float whist[WMAX];
static int nwhist;
static u64 whist_key;

static int hplies, grades[2][5], acc_n[2], nsan, wins, losses, draws;
static char sans[96][8];
static Pos san_prev;
static u64 san_key, counted_key;
static double acc_sum[2];

static double win_pct(int cp) { return 100 / (1 + exp(-0.00368208 * cp)); }

static void helper_track(const Analysis *an, int have_an, int my_turn)
{
    static u64 last;
    u64 k = tracker.pos.key;
    if (k != last) {
        last = k;
        hplies++;
        if (san_key && tracker.last_move && san_prev.key == san_key) {
            Move ms[256];
            Pos t = san_prev;
            int nm = pos_gen_legal(&t, ms), ok = 0;
            for (int q = 0; q < nm; q++) ok |= ms[q] == tracker.last_move;
            if (ok) {
                if (nsan == 96) memmove(sans, sans + 1, sizeof(sans[0]) * --nsan);
                t = san_prev;
                move_to_san(&t, tracker.last_move, sans[nsan++]);
            }
        }
        san_prev = tracker.pos, san_key = k;
        if (k == start_key) {
            nsan = 0, counted_key = 0;
            open_name = NULL, nwhist = 0, whist_key = 0, hplies = 0;
            memset(grades, 0, sizeof(grades));
            memset(acc_n, 0, sizeof(acc_n));
            memset(acc_sum, 0, sizeof(acc_sum));
        }
        for (int i = 0; i < NOPEN; i++)
            if (open_keys[i] == k) open_name = openings[i][1];
    }
    if (!have_an || an->depth < 6) return;
    float w = an->mate ? (user_score(an, my_turn) > 0 ? 100 : 0) : win_pct(user_score(an, my_turn));
    if (whist_key == k && nwhist) {
        whist[nwhist - 1] = w;
        return;
    }
    whist_key = k;
    if (nwhist == WMAX) memmove(whist, whist + 1, sizeof(whist[0]) * --nwhist);
    whist[nwhist++] = w;
}

static int material_diff(int color)
{
    static const int val[] = {1, 3, 3, 5, 9, 0};
    int d = 0;
    for (int sq = 0; sq < 64; sq++) {
        int pc = tracker.pos.sq[sq];
        if (pc != EMPTY) d += pc / 6 == color ? val[pc % 6] : -val[pc % 6];
    }
    return d;
}

static void missing(int color, char *out, size_t n)
{
    static const int start[] = {8, 2, 2, 2, 1};
    static const int order[] = {QUEEN, ROOK, BISHOP, KNIGHT, PAWN};
    int have[6] = {0};
    for (int sq = 0; sq < 64; sq++)
        if (tracker.pos.sq[sq] != EMPTY && tracker.pos.sq[sq] / 6 == color) have[tracker.pos.sq[sq] % 6]++;
    out[0] = 0;
    for (int k = 0; k < 5; k++) {
        int ty = order[k], m = start[ty] - have[ty];
        size_t l = strlen(out);
        if (m == 1) snprintf(out + l, n - l, "%s%c", l ? " " : "", piece_letters[ty]);
        if (m > 1) snprintf(out + l, n - l, "%s%d%c", l ? " " : "", m, piece_letters[ty]);
    }
}

static const char *phase_name(void)
{
    static const int val[] = {0, 3, 3, 5, 9, 0};
    int total = 0, queens = 0;
    for (int sq = 0; sq < 64; sq++) {
        int pc = tracker.pos.sq[sq];
        if (pc == EMPTY) continue;
        total += val[pc % 6];
        queens += pc % 6 == QUEEN;
    }
    if (total <= 26 || (!queens && total <= 36)) return "Endgame";
    if (hplies < 16 && total >= 56) return "Opening";
    return "Middlegame";
}

static void helper_text(char *out, size_t n)
{
    out[0] = 0;
    size_t l;
    if (cfg.material) {
        int d = material_diff(tracker.user_color);
        if (d)
            snprintf(out, n, "%s %d", d > 0 ? "up" : "down", abs(d));
        else
            snprintf(out, n, "even material");
    }
    if (cfg.taken) {
        char took[32], lost[32];
        missing(tracker.user_color ^ 1, took, sizeof(took));
        missing(tracker.user_color, lost, sizeof(lost));
        l = strlen(out);
        if (*took && *lost)
            snprintf(out + l, n - l, "%stook %s, lost %s", l ? "   " : "", took, lost);
        else if (*took || *lost)
            snprintf(out + l, n - l, "%s%s %s", l ? "   " : "", *took ? "took" : "lost", *took ? took : lost);
    }
    if (cfg.opening) {
        l = strlen(out);
        snprintf(out + l, n - l, "%s%s", l ? "   " : "", open_name && hplies <= 24 ? open_name : phase_name());
    }
    if (cfg.session && wins + losses + draws) {
        l = strlen(out);
        snprintf(out + l, n - l, "%sW%d L%d", l ? "   " : "", wins, losses);
        if (draws) l = strlen(out), snprintf(out + l, n - l, " D%d", draws);
    }
}

static void moves_text(char *out, size_t n, int last)
{
    out[0] = 0;
    for (int i = nsan > last ? nsan - last : 0; i < nsan; i++) {
        size_t l = strlen(out);
        snprintf(out + l, n - l, "%s%s", l ? " " : "", sans[i]);
    }
}

static void danger_text(char *out, size_t n, const Analysis *an, int have_an, int my_turn)
{
    out[0] = 0;
    if (!cfg.danger) return;
    int mate = have_an ? (my_turn ? an->mate : -an->mate) : 0;
    if (mate < 0) {
        snprintf(out, n, "they have mate in %d", -mate);
        return;
    }
    int me = tracker.user_color;
    if (!my_turn || pos_in_check(&tracker.pos, me)) return;
    static const int val[] = {1, 3, 3, 5, 9, 0};
    static Pos t;
    t = tracker.pos;
    t.side = me ^ 1;
    Move ms[256];
    int nm = pos_gen_legal(&t, ms), worst = -1;
    for (int q = 0; q < nm; q++) {
        int vic = tracker.pos.sq[M_TO(ms[q])], att = tracker.pos.sq[M_FROM(ms[q])];
        if (vic == EMPTY || vic / 6 != me || vic % 6 == PAWN || vic % 6 == KING) continue;
        int cheap = val[att % 6] < val[vic % 6], loose = !pos_is_attacked(&tracker.pos, M_TO(ms[q]), me);
        if ((cheap || loose) && (worst < 0 || val[vic % 6] > val[worst])) worst = vic % 6;
    }
    static const char *nm6[] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
    if (worst >= 0) snprintf(out, n, "your %s is in danger", nm6[worst]);
}

static void game_report(char *out, size_t n)
{
    out[0] = 0;
    if (!cfg.report || (!acc_n[0] && !acc_n[1])) return;
    for (int s = 0; s < 2; s++) {
        size_t l = strlen(out);
        int bad = grades[s][3] + grades[s][4];
        if (!acc_n[s]) continue;
        snprintf(out + l, n - l, "%s%s %.0f%% acc, %d bad", l ? "   " : "", s ? "Them" : "You", acc_sum[s] / acc_n[s], bad);
    }
}

static void draw_helper_board(Canvas *c, Rect bl, int bottom)
{
    double s = bl.w / 8;
    int r, col;
    if (cfg.lastmove && tracker.last_move) {
        int sqs[2] = {M_FROM(tracker.last_move), M_TO(tracker.last_move)};
        for (int k = 0; k < 2; k++) {
            sq_display(sqs[k], bottom, &r, &col);
            draw_frame(c, (Rect){bl.x + col * s + 1, bl.y + r * s + 1, s - 2, s - 2}, fmax(2, s * 0.045), faded(0xC0FFE066));
        }
    }
    if (cfg.hanging) {
        int me = tracker.user_color;
        for (int sq = 0; sq < 64; sq++) {
            int pc = tracker.pos.sq[sq];
            if (pc == EMPTY || pc / 6 != me || pc % 6 == KING) continue;
            if (!pos_is_attacked(&tracker.pos, sq, me ^ 1) || pos_is_attacked(&tracker.pos, sq, me)) continue;
            sq_display(sq, bottom, &r, &col);
            double in = s * 0.07;
            draw_frame(c, (Rect){bl.x + col * s + in, bl.y + r * s + in, s - 2 * in, s - 2 * in}, fmax(2, s * 0.045), faded(0xD8FF8A1F));
        }
    }
    if (cfg.checkwarn && pos_in_check(&tracker.pos, tracker.pos.side)) {
        u64 kb = tracker.pos.bb[tracker.pos.side * 6 + KING];
        if (kb) {
            sq_display(__builtin_ctzll(kb), bottom, &r, &col);
            Rect kr = {bl.x + col * s, bl.y + r * s, s, s};
            for (int k = 0; k < 4; k++) {
                double in = s * 0.03 * k;
                draw_frame(c, (Rect){kr.x + in, kr.y + in, s - 2 * in, s - 2 * in}, s * 0.035, faded((uint32_t)(220 - 45 * k) << 24 | 0xFF3B3B));
            }
        }
    }
}

static void draw_wgraph(Canvas *c, Rect p, Rect bl, Rect outr)
{
    if (!cfg.wgraph || nwhist < 2) return;
    double h = 42;
    Rect g = {p.x, p.y + p.h + 6, p.w, h};
    int over = g.x < bl.x + bl.w && bl.x < g.x + g.w && g.y < bl.y + bl.h && bl.y < g.y + h;
    if (over || g.y + h > outr.y + outr.h - 4) g.y = p.y - h - 6;
    over = g.x < bl.x + bl.w && bl.x < g.x + g.w && g.y < bl.y + bl.h && bl.y < g.y + h;
    if (over || g.y < outr.y + 4) return;
    panel_bg(c, g, 10, TH->panel);
    double x0 = g.x + 8, w = g.w - 16, y0 = g.y + 5, gh = h - 10;
    draw_line(c, x0, y0 + gh / 2, x0 + w, y0 + gh / 2, 1, faded(0x60FFFFFF));
    for (int k = 1; k < nwhist; k++) {
        double xa = x0 + w * (k - 1) / (nwhist - 1), xb = x0 + w * k / (nwhist - 1);
        double ya = y0 + gh * (1 - whist[k - 1] / 100), yb = y0 + gh * (1 - whist[k] / 100);
        draw_line(c, xa, ya, xb, yb, 2, faded(whist[k] >= 50 ? 0xFF2BD957 : 0xFFFF4D4D));
    }
}

static void rate_moves(const Analysis *an, int have_an, int my_turn)
{
    if (!cfg.rateme && !cfg.report) return;
    if (tracker.pos.key != rate_key) {
        rate_key = tracker.pos.key;
        rate_pending = 0;
        if (known_key && known_key != tracker.pos.key && tracker.last_move) {
            rate_pending = 1;
            rate_before = known_score;
            rate_mine = !my_turn; // whoever isnt on move just moved
            rate_move = tracker.last_move;
        }
        known_key = 0;
    }
    if (!have_an || an->depth < 8) return;
    int now = user_score(an, my_turn);
    known_key = tracker.pos.key, known_score = now;
    if (!rate_pending) return;
    int d = rate_mine ? now - rate_before : rate_before - now;
    static const char *g[] = {"!", "ok", "?!", "?", "??"};
    static const uint32_t gc[] = {0xFF2BD957, 0xFF9AD95A, 0xFFFFC400, 0xFFFF8A1F, 0xFFFF3B3B};
    int k = d >= -30 ? 0 : d >= -80 ? 1 : d >= -160 ? 2 : d >= -300 ? 3 : 4;
    double drop = rate_mine ? win_pct(rate_before) - win_pct(now) : win_pct(now) - win_pct(rate_before);
    double acc = 103.1668 * exp(-0.04354 * fmax(0, drop)) - 3.1669;
    grades[!rate_mine][k]++;
    acc_sum[!rate_mine] += fmin(100, fmax(0, acc));
    acc_n[!rate_mine]++;
    rate_pending = 0;
    if (!cfg.rateme) return;
    snprintf(badge, sizeof(badge), "%s", g[k]);
    badge_col = gc[k];
    badge_until = now_ms() + 5000;
    set_note(roast(rate_mine, k));
    if (verbose) fprintf(stderr, "%s move rated %s (%+d)\n", rate_mine ? "your" : "their", g[k], d);
}

static void redraw(void)
{
    track_anims();
    bar_moving = 0;
    nhits = 0;
    ui_over_board = 0;
    for (int i = 0; i < ov->ncanvas; i++) {
        Canvas *c = ov->canvas(ov, i);
        if (c) canvas_clear(c);
    }
    if (state == SCANNING) {
        ov->commit(ov, NULL, 0);
        return;
    }
    Rect bl = board_logical(&board);
    Rect outr = cap->out[board.out].logical;
    Analysis an = {0};
    int have_an = state == TRACKING && analyzer_get(&an) && an.key == tracker.pos.key;
    snprintf(engine_name, sizeof(engine_name), "%s", an.engine);
    if (cfg.engine > 0 && cfg.engine < nengines && *an.engine_base && strcmp(an.engine_base, "open-chease") &&
        strcmp(engine_labels[cfg.engine], an.engine_base)) {
        snprintf(engine_labels[cfg.engine], sizeof(engine_labels[0]), "%.31s", an.engine_base);
        remember_name(engine_paths[cfg.engine], an.engine_base);
    }
    for (int i = 0; i < ov->ncanvas; i++) {
        Canvas *c = ov->canvas(ov, i);
        if (!c) continue;
        if (cfg.grid || state == ASKING) draw_grid(c, bl, state == ASKING ? 1 : cfg.grid);
        if (state == ASKING) {
            Rect p = panel_rect(&bl, 330, 96, &outr);
            draw_round_rect(c, p, 10, 0xF0181818);
            draw_text(c, p.x + 14, p.y + 10, "Chess board found!", 1, 0xFF7CFF7C);
            draw_text(c, p.x + 14, p.y + 36, "Which team are you?", 0, 0xFFE8E8E8);
            Rect bw = {p.x + 14, p.y + 60, 110, 28}, bb = {p.x + 134, p.y + 60, 110, 28};
            Rect bx = {p.x + p.w - 34, p.y + 8, 26, 26};
            draw_button(c, bw, "White", 0xFFF2F2F2, 0xFF111111);
            draw_button(c, bb, "Black", 0xFF000000, 0xFFF2F2F2);
            draw_frame(c, bb, 1.2, 0xFF6A6A6A);
            draw_close(c, bx);
            if (i == 0) {
                add_hit(bw, ACT_WHITE);
                add_hit(bb, ACT_BLACK);
                add_hit(bx, ACT_CLOSE);
            }
        } else {
            int wb = tracker.user_color == WHITE;
            int bottom = tracker.white_bottom;
            int my_turn = tracker.pos.side == tracker.user_color;
            if (i == 0) {
                if (tracker.pos.key != seen_key) seen_key = tracker.pos.key, pos_since = now_ms();
                rate_moves(&an, have_an, my_turn);
                helper_track(&an, have_an, my_turn);
            }
            draw_helper_board(c, bl, bottom);
            char extra[96];
            helper_text(extra, sizeof(extra));
            int checked = cfg.checkwarn && pos_in_check(&tracker.pos, tracker.pos.side);
            char line1[256], line2[320], san[16] = "", sc[32] = "";
            if (have_an && an.best) {
                Pos tmp = tracker.pos;
                move_to_san(&tmp, an.best, san);
                format_score(&an, my_turn, sc, sizeof(sc));
            }
            Move moves[256];
            Pos tmp = tracker.pos;
            int nlegal = pos_gen_legal(&tmp, moves);
            if (!nlegal && i == 0 && counted_key != tracker.pos.key) {
                counted_key = tracker.pos.key;
                if (!pos_in_check(&tracker.pos, tracker.pos.side)) draws++;
                else if (my_turn) losses++;
                else wins++;
            }
            char alert[64] = "", mvs[96] = "";
            if (cfg.mode == 1 && nlegal) {
                danger_text(alert, sizeof(alert), &an, have_an, my_turn);
                if (cfg.movelist) moves_text(mvs, sizeof(mvs), 8);
            }
            if (!nlegal) {
                char rep[160];
                game_report(rep, sizeof(rep));
                snprintf(line1, sizeof(line1), "%s%s%s", pos_in_check(&tracker.pos, tracker.pos.side)
                                                              ? (my_turn ? "Checkmate. You lost." : "Checkmate! You won.")
                                                              : "Stalemate.", *rep ? "   " : "", rep);
                line2[0] = 0;
            } else if (cfg.mode == 1) {
                char wbuf[48] = "";
                if (have_an) {
                    int us = user_score(&an, my_turn);
                    snprintf(wbuf, sizeof(wbuf), "   %.0f%% to win", win_pct(us));
                    if (nwhist >= 2 && whist_key == tracker.pos.key && fabs(whist[nwhist - 1] - whist[nwhist - 2]) >= 3) {
                        size_t l = strlen(wbuf);
                        snprintf(wbuf + l, sizeof(wbuf) - l, " (%+.0f)", whist[nwhist - 1] - whist[nwhist - 2]);
                    }
                }
                char clock[32] = "";
                long secs = (now_ms() - pos_since) / 1000;
                if (cfg.timer) snprintf(clock, sizeof(clock), " %ld:%02ld", secs / 60, secs % 60);
                snprintf(line1, sizeof(line1), "%s%s%s%s%s%s", my_turn ? "Your turn" : "Their turn", clock,
                         checked ? (my_turn ? "   You're in check!" : "   They're in check") : "", wbuf, *extra ? "   " : "", extra);
            } else if (my_turn) {
                Move pick = have_an && an.best ? fun_pick(&an) : 0;
                int waiting = now_ms() - pos_since < delay_secs[cfg.delay] * 1000L;
                if (pick && !waiting) {
                    char psan[16];
                    Pos t2 = tracker.pos;
                    move_to_san(&t2, pick, psan);
                    static Move shown;
                    if (pick != shown && i == 0) shown = pick, pick_t = now_ms();
                    uint32_t mc = pick_color(cfg.mcolor, TH->arrow);
                    mc = (mc & 0xFFFFFF) | (uint32_t)((mc >> 24) * ease(pick_t, 220)) << 24;
                    static const char *names[] = {"Best move", "Close enough", "Chaos pick", "Coin says", "Random move",
                                                  "Worst move", "Blunder time", "Human pick"};
                    static const char *pnames[] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
                    if (cfg.hint == 1) {
                        draw_move(c, bl, pick, bottom, mc, 0, 1, cfg.from ? cfg.from : 5, 0, NULL);
                        snprintf(line1, sizeof(line1), "Move a %s", pnames[tracker.pos.sq[M_FROM(pick)] % 6]);
                    } else if (cfg.hint == 2) {
                        draw_move(c, bl, pick, bottom, mc, 0, 1, 0, cfg.to ? cfg.to : 4, NULL);
                        snprintf(line1, sizeof(line1), "Something goes to %c%c", 'a' + (M_TO(pick) & 7), '1' + (M_TO(pick) >> 3));
                    } else {
                        for (int k = cfg.chaos ? -1 : cfg.lines - 1; k >= 0; k--) {
                            if (!an.alt[k]) continue;
                            char num[12];
                            snprintf(num, sizeof(num), "%d", k + 2);
                            draw_move(c, bl, an.alt[k], bottom, (mc & 0xFFFFFF) | ((mc >> 25) << 24), cfg.arrow ? cfg.arrow : 1,
                                      arrow_tk(cfg.thick) * 0.7, 0, 0, num);
                        }
                        draw_move(c, bl, pick, bottom, mc, cfg.arrow, arrow_tk(cfg.thick), cfg.from, cfg.to, cfg.label ? psan : NULL);
                        if (cfg.score && !cfg.chaos)
                            snprintf(line1, sizeof(line1), "%s: %s   (%s)", names[cfg.chaos], psan, sc);
                        else
                            snprintf(line1, sizeof(line1), "%s: %s", names[cfg.chaos], psan);
                    }
                    if (cfg.trash) {
                        size_t l = strlen(line1);
                        snprintf(line1 + l, sizeof(line1) - l, "   %s", quip(tracker.pos.key));
                    }
                } else {
                    snprintf(line1, sizeof(line1), "%s", waiting && pick ? "Your turn, think first..." : "Thinking...");
                }
            } else {
                if (have_an && an.best && (cfg.theirs == 1 || cfg.theirs == 3))
                    snprintf(line1, sizeof(line1), "Their turn. They'll likely play %s", san);
                else
                    snprintf(line1, sizeof(line1), "Their turn.");
                if (have_an && an.best && cfg.theirs >= 2)
                    draw_move(c, bl, an.best, bottom, pick_color(cfg.tcolor, 0xC8FF4D4D), cfg.tarrow, arrow_tk(cfg.tthick),
                              cfg.tfrom, cfg.tto, cfg.tlabel ? san : NULL);
            }
            if (!nlegal && cfg.confetti && pos_in_check(&tracker.pos, tracker.pos.side) && !my_turn) draw_confetti(c, bl);
            if (cfg.sqnames) draw_sqnames(c, bl, bottom);
            if (now_ms() < badge_until && rate_move) {
                int r, col;
                double s8 = bl.w / 8;
                sq_display(M_TO(rate_move), bottom, &r, &col);
                double d = fmax(20, s8 * 0.32);
                Rect bd = {bl.x + (col + 1) * s8 - d * 0.75, bl.y + r * s8 - d * 0.25, d, d};
                draw_round_rect(c, bd, d / 2, badge_col);
                double tw = text_lw(c, badge, 0);
                draw_text(c, bd.x + (d - tw) / 2, bd.y + (d - 16) / 2, badge, 0, 0xFFFFFFFF);
            }
            if (cfg.evalbar && have_an && nlegal) draw_evalbar(c, bl, &an, bottom, tracker.pos.side);
            snprintf(line2, sizeof(line2), "You: %s  |  %s d%d  |  %s", wb ? "White" : "Black",
                     engine_name, have_an ? an.depth : 0, now_ms() < note_until ? note : tracker.status);
            if (*mvs || *alert) {
                char tmp[320];
                snprintf(tmp, sizeof(tmp), "%s%s%s%s%s", alert, *alert ? "  |  " : "", mvs, *mvs ? "  |  " : "", line2);
                snprintf(line2, sizeof(line2), "%s", tmp);
            }
            if (cfg.mode == 0 && *extra && nlegal) {
                size_t l = strlen(line2);
                snprintf(line2 + l, sizeof(line2) - l, "  |  %s", extra);
                for (char *q = line2 + l; (q = strstr(q, "   ")); q++) q[0] = ',', q[1] = ' ', memmove(q + 2, q + 3, strlen(q + 3) + 1);
            }
            int t1big = cfg.size > 0, t1zoom = cfg.size == 2 ? 2 : 1, t2big = cfg.size == 2;
            double bs = cfg.size == 0 ? 22 : (cfg.size == 1 ? 26 : 34);
            Rect p, gb, mb, bx;
            double vw = cfg.size == 0 ? 190 : (cfg.size == 1 ? 220 : 270);
            int side = (cfg.spot == 2 && bl.x - vw - 8 >= outr.x + 4) ||
                       (cfg.spot == 3 && bl.x + bl.w + 8 + vw <= outr.x + outr.w - 4);
            if (side) {
                const char *ls[16];
                int big[16], zoom[16], nl = 0;
                uint32_t lc[16];
                char movebuf[64], scbuf[48], theirbuf[64], st[3][128];
                int plain = cfg.mode || cfg.chaos || cfg.hint || cfg.trash || now_ms() - pos_since < delay_secs[cfg.delay] * 1000L;
                char parts[8][128];
                if (plain || !nlegal) {
                    const char *src = line1;
                    for (int k = 0; k < 8 && *src; k++) {
                        const char *cut = strstr(src, "   ");
                        size_t len = cut ? (size_t)(cut - src) : strlen(src);
                        snprintf(parts[k], sizeof(parts[k]), "%.*s", (int)(len < 127 ? len : 127), src);
                        ls[nl] = parts[k], big[nl] = k == 0 && t1big, zoom[nl] = 1, lc[nl++] = k ? TH->title_off : TH->title;
                        src += len;
                        while (*src == ' ') src++;
                    }
                } else if (my_turn && nlegal && have_an && an.best) {
                    ls[nl] = "Best move", big[nl] = 0, zoom[nl] = 1, lc[nl++] = TH->title_off;
                    snprintf(movebuf, sizeof(movebuf), "%s", san);
                    ls[nl] = movebuf, big[nl] = 1, zoom[nl] = cfg.size == 0 ? 1 : 2, lc[nl++] = TH->title;
                    if (cfg.score) {
                        snprintf(scbuf, sizeof(scbuf), "%s", sc);
                        ls[nl] = scbuf, big[nl] = t1big, zoom[nl] = 1, lc[nl++] = TH->title;
                    }
                } else if (my_turn && nlegal) {
                    ls[nl] = "Thinking...", big[nl] = t1big, zoom[nl] = t1zoom, lc[nl++] = TH->title;
                } else if (nlegal) {
                    ls[nl] = "Their turn.", big[nl] = t1big, zoom[nl] = 1, lc[nl++] = TH->title_off;
                    if (have_an && an.best && (cfg.theirs == 1 || cfg.theirs == 3)) {
                        snprintf(theirbuf, sizeof(theirbuf), "likely %s", san);
                        ls[nl] = theirbuf, big[nl] = t1big, zoom[nl] = 1, lc[nl++] = TH->title_off;
                    }
                } else {
                    ls[nl] = line1, big[nl] = 0, zoom[nl] = 1, lc[nl++] = TH->title;
                }
                char exbuf[96];
                if (cfg.mode == 0 && *extra && nlegal) {
                    snprintf(exbuf, sizeof(exbuf), "%s", extra);
                    for (char *q = exbuf; q && *q && nl < 9;) {
                        char *cut = strstr(q, "   ");
                        if (cut) *cut = 0;
                        ls[nl] = q, big[nl] = 0, zoom[nl] = 1, lc[nl++] = TH->title_off;
                        q = cut ? cut + 3 : NULL;
                    }
                }
                if (*alert) ls[nl] = alert, big[nl] = 0, zoom[nl] = 1, lc[nl++] = 0xFFFF7A5A;
                char few[48];
                moves_text(few, sizeof(few), 4);
                if (*mvs) ls[nl] = few, big[nl] = 0, zoom[nl] = 1, lc[nl++] = TH->title_off;
                if (cfg.status) {
                    snprintf(st[0], sizeof(st[0]), "You: %s", wb ? "White" : "Black");
                    snprintf(st[1], sizeof(st[1]), "%s d%d", engine_name, have_an ? an.depth : 0);
                    snprintf(st[2], sizeof(st[2]), "%s", now_ms() < note_until ? note : tracker.status);
                    for (int k = 0; k < 3; k++) ls[nl] = st[k], big[nl] = t2big, zoom[nl] = 1, lc[nl++] = TH->sub;
                }
                double h = bs + 16;
                for (int k = 0; k < nl; k++) h += (big[k] ? 24 : 19) * zoom[k] + 2;
                p = (Rect){cfg.spot == 2 ? bl.x - vw - 8 : bl.x + bl.w + 8, bl.y, vw, h};
                panel_bg(c, p, 12, TH->panel);
                double y = p.y + bs + 14;
                for (int k = 0; k < nl; k++) {
                    set_text_zoom(zoom[k]);
                    draw_text(c, p.x + 12, y, big[k] || zoom[k] > 1 ? ls[k] : fit_text(c, ls[k], p.w - 24), big[k], lc[k]);
                    set_text_zoom(1);
                    y += (big[k] ? 24 : 19) * zoom[k] + 2;
                }
                gb = (Rect){p.x + p.w - 3 * (bs + 4) - 4, p.y + 8, bs, bs};
                mb = (Rect){p.x + p.w - 2 * (bs + 4) - 4, p.y + 8, bs, bs};
                bx = (Rect){p.x + p.w - (bs + 4) - 4, p.y + 8, bs, bs};
            } else {
                double ph = cfg.size == 0 ? 46 : (cfg.size == 1 ? 58 : 84);
                if (!cfg.status) {
                    line2[0] = 0;
                    ph = cfg.size == 0 ? 30 : (cfg.size == 1 ? 38 : 64);
                    bs = ph - 10;
                }
                double y2 = cfg.size == 0 ? 26 : (cfg.size == 1 ? 34 : 54);
                double room = outr.w - 8 - 28 - 3 * (bs + 4);
                set_text_zoom(t1zoom);
                cut_to(line1, room, t1big);
                double w1 = text_width(line1, t1big);
                set_text_zoom(1);
                cut_to(line2, room, t2big);
                double w = fmax(w1, text_width(line2, t2big)) + 28 + 3 * (bs + 4);
                if (w < 300) w = 300;
                p = panel_rect(&bl, w, ph, &outr);
                panel_bg(c, p, 12, TH->panel);
                set_text_zoom(t1zoom);
                draw_text(c, p.x + 14, p.y + 6, line1, t1big, my_turn ? TH->title : TH->title_off);
                set_text_zoom(1);
                draw_text(c, p.x + 14, p.y + y2, line2, t2big, TH->sub);
                double by = p.y + (ph - bs) / 2;
                gb = (Rect){p.x + p.w - 3 * (bs + 4) - 4, by, bs, bs};
                mb = (Rect){p.x + p.w - 2 * (bs + 4) - 4, by, bs, bs};
                bx = (Rect){p.x + p.w - (bs + 4) - 4, by, bs, bs};
            }
            draw_wgraph(c, p, bl, outr);
            draw_gear(c, gb, settings_open);
            draw_dots(c, mb, menu_open);
            draw_close(c, bx);
            if (i == 0) {
                add_hit(gb, ACT_SETTINGS);
                add_hit(mb, ACT_MENU);
                add_hit(bx, ACT_STOP);
            }
            draw_extras(c, bl, p, outr, i == 0);
        }
    }
    Rect hr[MAX_HITS];
    for (int i = 0; i < nhits; i++) hr[i] = hits[i].r;
    ov->commit(ov, hr, nhits);
}

static void ignore_board(void)
{
    for (int i = 0; i < 16; i++)
        if (!ignores[i].active) {
            ignores[i].active = 1;
            ignores[i].out = board.out;
            ignores[i].frame_rect = (Rect){board.x - board.s, board.y - board.s, board.s * 10, board.s * 10};
            ignores[i].b = board;
            ignores[i].until_ms = now_ms() + 4000;
            return;
        }
}

static void refresh_ignores(int out)
{
    Output *o = &cap->out[out];
    for (int i = 0; i < 16; i++) {
        Ignore *g = &ignores[i];
        if (!g->active || g->out != out) continue;
        MaskCtx mc = {o};
        if (o->have_frame && vision_verify(&o->frame, &g->b, masked_cb, &mc))
            g->until_ms = now_ms() + 3000;
        else if (now_ms() > g->until_ms)
            g->active = 0;
    }
}

static int is_ignored(int out, const Board *b)
{
    for (int i = 0; i < 16; i++) {
        Ignore *g = &ignores[i];
        if (!g->active || g->out != out) continue;
        double cx = b->x + b->s * 4, cy = b->y + b->s * 4;
        if (cx >= g->frame_rect.x && cx < g->frame_rect.x + g->frame_rect.w && cy >= g->frame_rect.y &&
            cy < g->frame_rect.y + g->frame_rect.h)
            return 1;
    }
    return 0;
}

static int colors_close(const uint8_t *a, const uint8_t *b)
{
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) < 40;
}

static void read_and_track(int first)
{
    Output *o = &cap->out[board.out];
    MaskCtx mc = {o};
    BoardRead *r = malloc(sizeof(BoardRead));
    vision_read(&o->frame, &board, tracker.white_bottom, tpl.color_thr, r, masked_cb, &mc);
    if (first == 3) {
        if (!r->nhidden) {
            tracker_resync(&tracker, r, &tpl);
            snprintf(tracker.status, sizeof(tracker.status), "rescanned");
        } else
            snprintf(tracker.status, sizeof(tracker.status), "cant rescan, board is covered");
        first = -1;
    }
    if (first == 2) { // back from a pause
        int same = !r->nhidden;
        for (int i = 0; i < 64 && same; i++) {
            int pc = tracker.pos.sq[i], want = pc == EMPTY ? OBS_EMPTY : (pc / 6 == WHITE ? OBS_WHITE : OBS_BLACK);
            if (r->obs[i] != want) same = 0;
        }
        if (same) first = 0;
        else if (!r->nhidden) {
            tracker_resync(&tracker, r, &tpl);
            snprintf(tracker.status, sizeof(tracker.status), "caught up");
            first = -1;
        } else first = 0;
    }
    if (first > 0 || tracker.flip_pending) {
        int sc = orientation_score(r);
        if (sc < -2 || (tracker.flip_pending && sc < 0)) {
            tracker.white_bottom ^= 1;
            vision_read(&o->frame, &board, tracker.white_bottom, tpl.color_thr, r, masked_cb, &mc);
        }
        tracker.flip_pending = 0;
    }
    if (first == -1) {
    } else if (first) {
        int start = 1;
        for (int i = 0; i < 64; i++) {
            int want = i < 16 ? OBS_WHITE : (i >= 48 ? OBS_BLACK : OBS_EMPTY);
            if (r->obs[i] != want) start = 0;
        }
        if (!start && r->nhidden) {
            snprintf(tracker.status, sizeof(tracker.status), "board partly hidden");
        } else if (!start) {
            tracker_resync(&tracker, r, &tpl);
            snprintf(tracker.status, sizeof(tracker.status), "read mid-game");
        }
    } else {
        tracker_update(&tracker, r, &tpl);
    }
    Pos sp;
    pos_startpos(&sp);
    if (tracker.pos.key == sp.key && !learned_here && !r->nhidden) { // other sites use other pieces
        int ok = 1;
        for (int i = 0; i < 64; i++) {
            int want = i < 16 ? OBS_WHITE : (i >= 48 ? OBS_BLACK : OBS_EMPTY);
            if (r->obs[i] != want) ok = 0;
        }
        if (ok) {
            templates_learn(&tpl, r, &tracker.pos);
            learned_here = 1;
            templates_save(&tpl, tpl_path);
            if (verbose) fprintf(stderr, "learned piece set from starting position\n");
        }
    }
    if (verbose >= 2) {
        char fen[128];
        pos_to_fen(&tracker.pos, fen);
        fprintf(stderr, "obs:");
        for (int rr = 7; rr >= 0; rr--) {
            fputc(' ', stderr);
            for (int f = 0; f < 8; f++) fputc(".WB?"[r->obs[rr * 8 + f]], stderr);
        }
        fprintf(stderr, "  fen %s  [%s]\n", fen, tracker.status);
    }
    free(r);
    if (cfg.theirtoo || tracker.pos.side == tracker.user_color)
        analyzer_set_position(&tracker.pos);
    else
        analyzer_stop();
}

static void dump_frame(void)
{
    static int n;
    if (!dump_dir || (n++ % 4)) return; // every 4th tick, they're big
    for (int i = 0; i < cap->nout; i++) {
        Output *o = &cap->out[i];
        if (!o->have_frame) continue;
        char path[1024], tmp[1100];
        snprintf(path, sizeof(path), "%s/frame%d.ppm", dump_dir, i);
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        FILE *f = fopen(tmp, "wb");
        if (!f) continue;
        fprintf(f, "P6 %d %d 255\n", o->frame.w, o->frame.h);
        uint8_t *row = malloc((size_t)o->frame.w * 3);
        for (int y = 0; y < o->frame.h; y++) {
            const uint8_t *p = o->frame.px + (size_t)y * o->frame.stride;
            for (int x = 0; x < o->frame.w; x++) {
                row[x * 3] = o->frame.bgr ? p[x * 4 + 2] : p[x * 4];
                row[x * 3 + 1] = p[x * 4 + 1];
                row[x * 3 + 2] = o->frame.bgr ? p[x * 4] : p[x * 4 + 2];
            }
            fwrite(row, 3, (size_t)o->frame.w, f);
        }
        free(row);
        fclose(f);
        rename(tmp, path);
    }
}

static void log_analysis(void)
{
    if (!verbose || state != TRACKING) return;
    Analysis a;
    if (!analyzer_get(&a) || a.key != tracker.pos.key || a.best == last_logged) return;
    last_logged = a.best;
    char san[16];
    Pos tmp = tracker.pos;
    move_to_san(&tmp, a.best, san);
    fprintf(stderr, "best for %s to move: %s (depth %d, score %d)\n", tracker.pos.side ? "black" : "white", san,
            a.depth, a.score);
}

static void scan_log(int grabbed, const int *scores)
{
    static long last;
    if (!verbose || now_ms() - last < 3000) return;
    last = now_ms();
    if (!grabbed) {
        fprintf(stderr, "couldnt grab the screen this time\n");
        return;
    }
    fprintf(stderr, "looking for a board:");
    for (int i = 0; i < cap->nout; i++) {
        Output *o = &cap->out[i];
        if (!o->have_frame) fprintf(stderr, "  %s no frame", o->name);
        else fprintf(stderr, "  %s %dx%d at %.0f,%.0f best %d/64", o->name, o->frame.w, o->frame.h, o->logical.x,
                         o->logical.y, scores[i]);
    }
    fprintf(stderr, " (needs 52)\n");
}

static void tick(void)
{
    check_download();
    if (!cap->capture(cap)) {
        if (replay_ticks >= 0) quit = 1;
        if (state == SCANNING) scan_log(0, NULL);
        return;
    }
    dump_frame();
    log_analysis();
    if (state == SCANNING) {
        int scores[MAX_OUTPUTS] = {0};
        for (int i = 0; i < cap->nout; i++) refresh_ignores(i);
        for (int i = 0; i < cap->nout; i++) {
            Output *o = &cap->out[i];
            if (!o->have_frame) continue;
            Rect ig[16];
            int nig = 0;
            for (int k = 0; k < 16; k++)
                if (ignores[k].active && ignores[k].out == i) ig[nig++] = ignores[k].frame_rect;
            MaskCtx mc = {o};
            Board b = {0};
            int hit = vision_find_board(&o->frame, &b, ig, nig, masked_cb, &mc);
            scores[i] = b.score;
            if (hit && !is_ignored(i, &b)) {
                b.out = i;
                if (fabs(b.s - board.s) > 1 || b.out != board.out || !colors_close(b.light, board.light)) learned_here = 0;
                board = b;
                miss_ticks = 0;
                if (verbose)
                    fprintf(stderr, "board found on %s at %.0f,%.0f square %.1fpx score %d, popup goes at %.0f,%.0f\n",
                            o->name, b.x, b.y, b.s, b.score, o->logical.x + b.x * o->logical.w / o->frame.w,
                            o->logical.y + b.y * o->logical.h / o->frame.h);
                if (have_saved && now_ms() - saved_at < 15000 && saved_board.out == i &&
                    colors_close(saved_board.light, b.light) && colors_close(saved_board.dark, b.dark) &&
                    fabs(saved_board.s - b.s) < b.s * 0.15) {
                    tracker = saved_tracker;
                    have_saved = 0;
                    state = TRACKING;
                    read_and_track(0);
                } else if (auto_team >= 0) {
                    tracker_start(&tracker, auto_team);
                    state = TRACKING;
                    read_and_track(1);
                } else {
                    state = ASKING;
                }
                break;
            }
        }
        if (state == SCANNING) scan_log(1, scores);
        return;
    }
    Output *o = &cap->out[board.out];
    if (!o->have_frame) return;
    static int was_paused;
    if (state == TRACKING && ui_over_board) { // cant see under our own menu
        snprintf(note, sizeof(note), "paused, menu open");
        note_until = now_ms() + 1200;
        was_paused = 1;
        return;
    }
    MaskCtx mc = {o};
    if (!vision_verify(&o->frame, &board, masked_cb, &mc)) {
        if (++miss_ticks >= 3) {
            if (verbose) fprintf(stderr, "board lost\n");
            if (state == TRACKING) {
                saved_tracker = tracker;
                saved_board = board;
                saved_at = now_ms();
                have_saved = 1;
                analyzer_stop();
            }
            state = SCANNING;
        }
        return;
    }
    miss_ticks = 0;
    if (state == ASKING) return;
    read_and_track(want_rescan ? 3 : was_paused ? 2 : 0);
    was_paused = want_rescan = 0;
}

static void set_note(const char *msg)
{
    snprintf(note, sizeof(note), "%s", msg);
    note_until = now_ms() + 4000;
}

static void fix_piece(int action, int arg)
{
    Pos p = tracker.pos;
    if (action == ACT_TURN) {
        p.side ^= 1;
        tracker.lock_side = p.side;
    } else if (fix_sq >= 0) {
        pos_put(&p, fix_sq, arg);
        tracker_lock(&tracker, fix_sq, arg);
        fix_sq = -1;
    }
    pos_finish_setup(&p);
    if (!p.bb[KING] || !p.bb[6 + KING] || __builtin_popcountll(p.bb[KING]) > 1 || __builtin_popcountll(p.bb[6 + KING]) > 1) {
        set_note("each side needs exactly one king");
        for (int sq = 0; sq < 64; sq++) // dont keep re-applying a fix that broke the kings
            if ((tracker.locked >> sq & 1) && p.sq[sq] != tracker.pos.sq[sq]) tracker.locked &= ~(1ULL << sq);
        tracker.pos = p;
        return;
    }
    if (pos_in_check(&p, p.side ^ 1)) set_note("the side that just moved cant be in check");
    tracker.pos = p;
    tracker.mismatch_ticks = 0;
    tracker.pending_count = 0;
    tracker.side_uncertain = 0;
    tracker.nprev = 0;
    analyzer_set_position(&tracker.pos);
}


static int same_file(const char *a, const char *b)
{
    char ra[4096], rb[4096];
    if (!*a || !*b) return !*a && !*b;
    if (!realpath(a, ra) || !realpath(b, rb)) return !strcmp(a, b);
    return !strcmp(ra, rb);
}

static void add_engine(const char *label, const char *path)
{
    if (nengines >= MAX_ENGINES) return;
    for (int i = 0; i < nengines; i++)
        if (*path && same_file(engine_paths[i], path)) return;
    snprintf(engine_labels[nengines], sizeof(engine_labels[0]), "%s", label);
    for (int i = 0; i < nlib && *path; i++) {
        char out[1200];
        lib_out_path(i, out, sizeof(out));
        if (!strcmp(out, path)) snprintf(engine_labels[nengines], sizeof(engine_labels[0]), "%.31s", lib[i].name);
    }
    if (*path) cached_name(path, engine_labels[nengines], sizeof(engine_labels[0]));
    snprintf(engine_paths[nengines], sizeof(engine_paths[0]), "%.1000s", path);
    engine_names[nengines] = engine_labels[nengines];
    nengines++;
}

static int in_path(const char *name, char *out, size_t n)
{
    const char *pth = getenv("PATH");
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s:/usr/games", pth ? pth : "/usr/bin");
    char *sv;
    for (char *d = strtok_r(buf, ":", &sv); d; d = strtok_r(NULL, ":", &sv)) {
        snprintf(out, n, "%s/%s", d, name);
        if (access(out, X_OK) == 0) return 1;
    }
    return 0;
}

static void scan_engines(const char *extra)
{
    static char keep[1024];
    if (extra) snprintf(keep, sizeof(keep), "%s", extra);
    extra = keep;
    nengines = 0;
    add_engine("Built-in", "");
    char p[2048];
    int sf = 0;
    snprintf(p, sizeof(p), "%s/stockfish", data_dir);
    DIR *d = opendir(p);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && !sf) {
            char f[4096];
            snprintf(f, sizeof(f), "%s/%s", p, e->d_name);
            if (!strncmp(e->d_name, "stockfish", 9) && access(f, X_OK) == 0) add_engine("Stockfish", f), sf = 1;
        }
        closedir(d);
    }
    if (!sf && in_path("stockfish", p, sizeof(p))) add_engine("Stockfish", p), sf = 1;
    snprintf(p, sizeof(p), "%s/engines", data_dir); // drop any uci engine in here and it shows up
    mkdir(p, 0700);
    d = opendir(p);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            char f[4096];
            snprintf(f, sizeof(f), "%s/%s", p, e->d_name);
            struct stat st;
            if (e->d_name[0] != '.' && !stat(f, &st) && S_ISREG(st.st_mode) && access(f, X_OK) == 0) add_engine(e->d_name, f);
        }
        closedir(d);
    }
    if (extra && *extra && access(extra, X_OK) == 0) {
        const char *b = strrchr(extra, '/');
        add_engine(b ? b + 1 : extra, extra);
    }
    for (int i = 0; i < NSET; i++)
        if (sets[i].v == &cfg.engine) sets[i].n = nengines;
}

static void cached_name(const char *path, char *out, size_t n)
{
    char f[1100], line[1200];
    snprintf(f, sizeof(f), "%s/engine-names", data_dir);
    FILE *fp = fopen(f, "r");
    if (!fp) return;
    size_t pl = strlen(path);
    while (fgets(line, sizeof(line), fp))
        if (!strncmp(line, path, pl) && line[pl] == '|') {
            snprintf(out, n, "%.31s", line + pl + 1);
            out[strcspn(out, "\n")] = 0;
        }
    fclose(fp);
}

static void remember_name(const char *path, const char *name)
{
    char have[48] = "";
    cached_name(path, have, sizeof(have));
    if (!*path || !strcmp(have, name)) return;
    char f[1100];
    snprintf(f, sizeof(f), "%s/engine-names", data_dir);
    FILE *fp = fopen(f, "a");
    if (!fp) return;
    fprintf(fp, "%s|%s\n", path, name);
    fclose(fp);
}

static int engine_index(const char *path)
{
    if (!path || !*path) return 0;
    for (int i = 1; i < nengines; i++)
        if (same_file(engine_paths[i], path)) return i;
    return 0;
}


static void lib_slug(const char *name, char *out, size_t n) // names come off the internet, keep them boring
{
    size_t k = 0;
    for (const char *c = name; *c && k + 1 < n; c++) {
        char ch = *c >= 'A' && *c <= 'Z' ? *c + 32 : *c == ' ' ? '-' : *c;
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || (ch == '.' && k)) out[k++] = ch;
    }
    out[k] = 0;
    if (!k || strstr(out, "..")) snprintf(out, n, "engine");
}

static void lib_out_path(int i, char *out, size_t n)
{
    char slug[40];
    lib_slug(lib[i].name, slug, sizeof(slug));
    snprintf(out, n, "%s/engines/%s", data_dir, slug);
}

static void load_library(void)
{
    char path[1100], *text = NULL;
    snprintf(path, sizeof(path), "%s/engines.txt", data_dir);
    FILE *f = fopen(path, "r");
    if (f) {
        text = calloc(1, 65536);
        size_t got = fread(text, 1, 65535, f);
        text[got] = 0;
        fclose(f);
    }
    const char *src = text && strchr(text, '|') ? text : builtin_library;
    nlib = 0;
    for (const char *ln = src; *ln && nlib < MAX_LIB;) {
        const char *end = strchr(ln, '\n');
        size_t len = end ? (size_t)(end - ln) : strlen(ln);
        char row[700];
        snprintf(row, sizeof(row), "%.*s", (int)(len < sizeof(row) - 1 ? len : sizeof(row) - 1), ln);
        ln += len + (end ? 1 : 0);
        char *f[7] = {row};
        int nf = 1;
        for (char *q = row; *q && nf < 7; q++)
            if (*q == '|') *q = 0, f[nf++] = q + 1;
        if (row[0] == '#' || nf < 3) continue;
        for (int k = nf; k < 7; k++) f[k] = "";
        LibEngine *e = &lib[nlib];
        snprintf(e->name, sizeof(e->name), "%.31s", f[0]);
        snprintf(e->url, sizeof(e->url), "%.511s", f[1]);
        snprintf(e->kind, sizeof(e->kind), "%.7s", f[2]);
        e->kind[strcspn(e->kind, " \r")] = 0;
        e->mb = 0;
        for (const char *q = f[3]; *q; q++)
            if (*q >= '0' && *q <= '9') e->mb = e->mb * 10 + (*q - '0');
        if (strchr(f[3], '.')) {
            const char *dot = strchr(f[3], '.');
            int decimals = 0;
            for (const char *q = dot + 1; *q >= '0' && *q <= '9'; q++) decimals++;
            for (int k = 0; k < decimals; k++) e->mb /= 10;
        }
        e->tier = f[4][0] >= '1' && f[4][0] <= '4' ? f[4][0] - '0' : 3;
        snprintf(e->tags, sizeof(e->tags), "%.47s", f[5]);
        snprintf(e->about, sizeof(e->about), "%.95s", f[6]);
        e->about[strcspn(e->about, "\r")] = 0;
        char out[1200];
        lib_out_path(nlib, out, sizeof(out));
        snprintf(lib_labels[nlib], sizeof(lib_labels[0]), "%s%s", e->name, access(out, X_OK) == 0 ? " (got it)" : "");
        lib_names[nlib] = lib_labels[nlib];
        nlib++;
    }
    free(text);
    for (int i = 0; i < NSET; i++)
        if (sets[i].v == &cfg.library) sets[i].n = nlib ? nlib : 1;
    if (!nlib) lib_names[0] = "nothing to get";
}

static void fetch_library(void)
{
    lib_pid = fork();
    if (lib_pid == 0) {
        for (int fd = 3; fd < 1024; fd++) close(fd);
        setenv("D", data_dir, 1);
        execl("/bin/sh", "sh", "-c",
              "u=https://raw.githubusercontent.com/sevvy-wevvy/open-chease/main/engines.txt; "
              "(curl -fsL --max-time 15 \"$u\" -o \"$D/engines.txt.new\" || wget -q -T 15 -O \"$D/engines.txt.new\" \"$u\") "
              "&& grep -q '|' \"$D/engines.txt.new\" && mv \"$D/engines.txt.new\" \"$D/engines.txt\"; rm -f \"$D/engines.txt.new\"",
              (char *)NULL);
        _exit(127);
    }
    if (lib_pid < 0) lib_pid = 0;
}

static void start_download(int i)
{
    if (dl_pid > 0) {
        set_note("already downloading one, hang on");
        return;
    }
    char out[1200];
    lib_out_path(i, out, sizeof(out));
    dl_index = i;
    snprintf(dl_out, sizeof(dl_out), "%s", out);
    snprintf(dl_name, sizeof(dl_name), "%s", lib[i].name);
    dl_pid = fork();
    if (dl_pid == 0) {
        for (int fd = 3; fd < 1024; fd++) close(fd);
        char url[512];
        snprintf(url, sizeof(url), "%s", lib[i].url);
        char *pick = strchr(url, '#');
        if (pick) *pick++ = 0;
        setenv("U", url, 1);
        setenv("K", lib[i].kind, 1);
        setenv("P", pick ? pick : "", 1);
        setenv("O", out, 1);
        setenv("D", data_dir, 1);
        execl("/bin/sh", "sh", "-c",
              "set -e; mkdir -p \"$D/engines\"; t=$(mktemp -d \"$D/dl.XXXXXX\"); trap 'rm -rf \"$t\"' EXIT; cd \"$t\"; "
              "if command -v curl >/dev/null; then curl -fsL \"$U\" -o got; else wget -qO got \"$U\"; fi; "
              "case \"$K\" in tgz) tar xzf got; rm got;; tar) tar xf got; rm got;; zip) unzip -q got 2>/dev/null || python3 -m zipfile -e got .; rm got;; esac; "
              "if [ \"$K\" = bin ]; then chmod +x got; rm -rf \"$O.files\"; mv got \"$O\"; exit 0; fi; "
              "b=$(find . -type f -printf '%s %p\\n' | sort -rn | while read -r n f; do "
              "[ \"$(head -c4 \"$f\" | od -An -tx1 | tr -d ' \\n')\" = 7f454c46 ] || continue; "
              "case \"$f\" in *\"$P\"*) echo \"$f\"; break;; esac; done); "
              "[ -n \"$b\" ]; chmod +x \"$b\"; rm -rf \"$O.files\" \"$O\"; mkdir \"$O.files\"; cp -R . \"$O.files/\"; "
              "ln -s \"$O.files/${b#./}\" \"$O\"",
              (char *)NULL);
        _exit(127);
    }
    if (dl_pid < 0) {
        dl_pid = 0;
        set_note("couldnt start the download");
        return;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "downloading %s...", lib[i].name);
    set_note(msg);
}

static void choose_engine(int idx)
{
    if (idx < 0 || idx >= nengines) return;
    cfg.engine = idx;
    if (!analyzer_switch(*engine_paths[idx] ? engine_paths[idx] : NULL) && *engine_paths[idx]) {
        set_note("that engine wont start on this pc, using mine");
        cfg.engine = 0;
    }
    snprintf(saved_engine, sizeof(saved_engine), "%.1000s", engine_paths[cfg.engine]);
    have_saved_engine = 1;
    save_cfg();
    if (state == TRACKING) analyzer_set_position(&tracker.pos); // might be paused
}

static void check_download(void)
{
    int st;
    if (lib_pid > 0 && waitpid(lib_pid, &st, WNOHANG) == lib_pid) lib_pid = 0, lib_reload = 1;
    if (lib_reload && dl_pid <= 0) lib_reload = 0, load_library(); // not mid download, the indexes would shift
    if (dl_pid <= 0 || waitpid(dl_pid, &st, WNOHANG) != dl_pid) return;
    dl_pid = 0;
    if (!WIFEXITED(st) || WEXITSTATUS(st)) {
        set_note("download failed, check your internet");
        return;
    }
    scan_engines(NULL);
    load_library();
    int k = engine_index(dl_out);
    if (!k) {
        set_note("too many engines, delete some first");
        return;
    }
    choose_engine(k);
    if (cfg.engine) {
        char msg[96];
        snprintf(msg, sizeof(msg), "got %s, using it now", dl_name);
        set_note(msg);
    }
}

static void relaunch(void)
{
    state = SCANNING;
    redraw();
    analyzer_stop();
    if (dl_pid > 0) kill(dl_pid, SIGKILL), waitpid(dl_pid, NULL, 0);
    if (lib_pid > 0) kill(lib_pid, SIGKILL), waitpid(lib_pid, NULL, 0);
    usleep(150000);
    for (int fd = 3; fd < 1024; fd++) close(fd);
    const char *home = getenv("OPEN_CHEASE_HOME");
    char path[1024];
    if (home) {
        snprintf(path, sizeof(path), "%s/run.sh", home);
        if (access(path, X_OK) == 0) {
            g_argv[0] = path;
            execv(path, g_argv);
        }
    }
    execv("/proc/self/exe", g_argv);
    _exit(1);
}

static int run_quiet(char *const argv[]) // no shell, so quotes in paths cant break anything
{
    pid_t pid = fork();
    if (pid == 0) {
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) dup2(nul, 1), dup2(nul, 2);
        for (int fd = 3; fd < 1024; fd++) close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (pid < 0 || waitpid(pid, &st, 0) != pid) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void update_and_relaunch(void)
{
    const char *mode = getenv("OPEN_CHEASE_UPDATE"), *home = getenv("OPEN_CHEASE_HOME");
    if (!mode || !home) {
        set_note("cant update this copy, start it with run.sh or get.sh");
        return;
    }
    char getsh[1100];
    snprintf(getsh, sizeof(getsh), "%s/get.sh", home);
    char *git[] = {"git", "-C", (char *)home, "pull", "--ff-only", NULL};
    char *get[] = {"bash", getsh, "--update", "--fetch-only", NULL};
    set_note("updating...");
    redraw();
    if (run_quiet(!strcmp(mode, "git") ? git : get) == 0)
        relaunch();
    else
        set_note("update failed, check your internet");
}

static void handle_key(int ch)
{
    if (lib_open) {
        size_t l = strlen(lib_query);
        if (ch == 27) lib_open = 0;
        else if (ch == 8 && l) lib_query[l - 1] = 0;
        else if (ch >= 32 && ch < 127 && l + 1 < sizeof(lib_query)) lib_query[l] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch), lib_query[l + 1] = 0;
        lib_scroll = 0;
    } else if (ch == 27) {
        settings_open = menu_open = confirm_quit = 0;
        dd_open = -1;
    }
}

static void sync_keyboard(void) // the game needs the keys when the library's closed
{
    static int on, hook = 1;
    if (hook && state == TRACKING && getenv("OPEN_CHEASE_OPEN_LIBRARY")) lib_open = 1, hook = 0;
    if (lib_open != on && ov->keyboard) ov->keyboard(ov, lib_open);
    on = lib_open;
}

static void handle_click(double x, double y)
{
    for (int i = nhits - 1; i >= 0; i--) {
        Rect r = hits[i].r;
        if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) continue;
        switch (hits[i].action) {
        case ACT_WHITE:
        case ACT_BLACK:
            tracker_start(&tracker, hits[i].action == ACT_WHITE ? WHITE : BLACK);
            state = TRACKING;
            read_and_track(1);
            break;
        case ACT_BLOCK:
            break;
        case ACT_CLOSE:
        case ACT_STOP:
            lib_open = menu_open = settings_open = confirm_quit = fix_mode = 0;
            dd_open = -1;
            ignore_board();
            analyzer_stop();
            have_saved = 0;
            state = SCANNING;
            break;
        case ACT_FLIP:
            menu_open = 0;
            tracker_start(&tracker, tracker.user_color ^ 1);
            read_and_track(1);
            break;
        case ACT_MENU:
            menu_open = !menu_open;
            settings_open = 0;
            dd_open = -1;
            confirm_quit = 0;
            break;
        case ACT_SETTINGS:
            settings_open = !settings_open;
            dd_open = -1;
            menu_open = 0;
            confirm_quit = 0;
            break;
        case ACT_SET: {
            const Setting *st = &sets[hits[i].arg];
            if (st->names == onoff) {
                *st->v = !*st->v;
                tog_i = hits[i].arg, tog_t = now_ms();
                if (st->v == &cfg.debug) debug_on = cfg.debug;
                dd_open = -1;
                save_cfg();
            } else if (st->v == &cfg.library || st->v == &cfg.engine) {
                lib_open = 1, lib_tab = st->v == &cfg.engine, lib_scroll = 0, lib_del = -1, lib_query[0] = 0;
                settings_open = 0, dd_open = -1;
            } else {
                dd_open = dd_open == hits[i].arg ? -1 : hits[i].arg;
            }
            break;
        }
        case ACT_MODE:
            cfg.mode ^= 1;
            save_cfg();
            menu_open = 0;
            set_note(cfg.mode ? "helper mode, no more moves" : "cheat mode, moves are back");
            break;
        case ACT_TAB:
            set_tab = hits[i].arg;
            dd_open = -1;
            break;
        case ACT_THEME:
            cfg.theme = hits[i].arg;
            if (cfg.theme >= CUSTOM0) cslot = cfg.theme - CUSTOM0;
            save_cfg();
            break;
        case ACT_CSLOT:
            cslot = hits[i].arg;
            cfg.theme = CUSTOM0 + cslot;
            save_cfg();
            break;
        case ACT_CCOPY: {
            Theme *t = &themes[CUSTOM0 + cslot];
            const Theme *from = cfg.theme >= CUSTOM0 ? &themes[0] : TH;
            for (int k = 0; k < NPARTS; k++) *part(t, k) = *part((Theme *)from, k);
            cfg.theme = CUSTOM0 + cslot;
            save_cfg();
            break;
        }
        case ACT_CPART:
            cpart = hits[i].arg;
            break;
        case ACT_CSWATCH: {
            uint32_t *v = part(&themes[CUSTOM0 + cslot], cpart);
            *v = (*v & 0xFF000000) | (swatch(hits[i].arg) & 0xFFFFFF);
            cfg.theme = CUSTOM0 + cslot;
            save_cfg();
            break;
        }
        case ACT_RESET:
            for (int k = 0; k < NSET; k++) {
                if (tab_of(k) != set_tab || sets[k].v == &cfg.engine || sets[k].v == &cfg.library) continue;
                *sets[k].v = *(int *)((char *)&cfg_default + ((char *)sets[k].v - (char *)&cfg));
            }
            debug_on = cfg.debug;
            dd_open = -1;
            analyzer_set_think_ms(think_secs[cfg.think] * 1000);
            analyzer_set_options(threads_vals[cfg.threads], hash_vals[cfg.hash], cfg.lines + 1);
            analyzer_set_elo(strength_elo[cfg.strength]);
            save_cfg();
            break;
        case ACT_LIB_OPEN:
            lib_open = 1, lib_tab = 0, lib_scroll = 0, lib_del = -1;
            menu_open = settings_open = 0;
            break;
        case ACT_LIB_CLOSE:
            lib_open = 0;
            break;
        case ACT_LIB_TAB:
            lib_tab = hits[i].arg, lib_scroll = 0, lib_del = -1, lib_query[0] = 0;
            break;
        case ACT_LIB_FILTER:
            lib_filter = hits[i].arg, lib_scroll = 0;
            break;
        case ACT_LIB_SCROLL:
            lib_scroll += hits[i].arg;
            break;
        case ACT_LIB_KEY: {
            size_t l = strlen(lib_query);
            int ch = hits[i].arg;
            if (ch == 8 && l) lib_query[l - 1] = 0;
            else if (ch == 127) lib_query[0] = 0;
            else if (ch >= 32 && ch < 127 && l + 1 < sizeof(lib_query)) lib_query[l] = (char)ch, lib_query[l + 1] = 0;
            lib_scroll = 0;
            break;
        }
        case ACT_LIB_GET:
            start_download(hits[i].arg);
            break;
        case ACT_LIB_USE:
            if (hits[i].arg < 0) {
                char out[1200];
                lib_out_path(-1 - hits[i].arg, out, sizeof(out));
                scan_engines(NULL);
                if (engine_index(out)) choose_engine(engine_index(out));
                else set_note("too many engines, delete some first");
            } else
                choose_engine(hits[i].arg);
            break;
        case ACT_LIB_DEL: {
            int k = hits[i].arg;
            if (lib_del != k) {
                lib_del = k;
                break;
            }
            lib_del = -1;
            if (k <= 0 || k >= nengines || !mine(k)) break;
            char gone[32];
            snprintf(gone, sizeof(gone), "%s", engine_labels[k]);
            if (k == cfg.engine) choose_engine(0);
            unlink(engine_paths[k]);
            char dir[1100];
            snprintf(dir, sizeof(dir), "%s.files", engine_paths[k]);
            struct stat ds;
            if (!lstat(dir, &ds) && S_ISDIR(ds.st_mode)) {
                char *argv[] = {"rm", "-rf", dir, NULL};
                run_quiet(argv);
            }
            scan_engines(NULL);
            load_library();
            cfg.engine = engine_index(saved_engine);
            char msg[64];
            snprintf(msg, sizeof(msg), "deleted %s", gone);
            set_note(msg);
            break;
        }
        case ACT_LIB_FOLDER: {
            char dir[1100];
            snprintf(dir, sizeof(dir), "%s/engines", data_dir);
            char *argv[] = {"xdg-open", dir, NULL};
            if (run_quiet(argv)) set_note("couldnt open the folder");
            break;
        }
        case ACT_PICK: {
            const Setting *st = &sets[hits[i].arg / 64];
            *st->v = hits[i].arg % 64;
            dd_open = -1;
            if (st->v == &cfg.think) analyzer_set_think_ms(think_secs[cfg.think] * 1000);
            if (st->v == &cfg.lines || st->v == &cfg.threads || st->v == &cfg.hash)
                analyzer_set_options(threads_vals[cfg.threads], hash_vals[cfg.hash], cfg.lines + 1);
            if (st->v == &cfg.engine) choose_engine(cfg.engine);
            if (st->v == &cfg.strength) analyzer_set_elo(strength_elo[cfg.strength]);
            if (st->v == &cfg.library && nlib) {
                char out[1200];
                lib_out_path(cfg.library, out, sizeof(out));
                if (access(out, X_OK) == 0) {
                    scan_engines(NULL);
                    choose_engine(engine_index(out));
                } else
                    start_download(cfg.library);
            }
            save_cfg();
            break;
        }
        case ACT_DEBUG:
            debug_on = !debug_on;
            cfg.debug = debug_on;
            save_cfg();
            menu_open = 0;
            break;
        case ACT_FIX:
            fix_mode = !fix_mode;
            fix_sq = -1;
            menu_open = 0;
            if (fix_mode) set_note("click a square to fix it");
            break;
        case ACT_SQ:
            fix_sq = hits[i].arg;
            break;
        case ACT_PIECE:
        case ACT_TURN:
            if (menu_open) {
                menu_open = 0;
                set_note(tracker.pos.side == WHITE ? "ok, black to move" : "ok, white to move");
            }
            fix_piece(hits[i].action, hits[i].arg);
            break;
        case ACT_RESCAN:
            menu_open = 0;
            want_rescan = 1;
            set_note("rescanning the board");
            break;
        case ACT_RESTART:
            relaunch();
            break;
        case ACT_UPDATE:
            menu_open = 0;
            update_and_relaunch();
            break;
        case ACT_QUIT:
            menu_open = 0;
            confirm_quit = 1;
            break;
        case ACT_QUIT_YES:
            quit = 1;
            break;
        case ACT_QUIT_NO:
            confirm_quit = 0;
            break;
        }
        redraw();
        return;
    }
}

static void usage(void)
{
    printf("open-chease, shows the best move on any chess board on your screen\n\n"
           "  --team white|black   skip the popup and always play that side\n"
           "  --builtin            use my engine even if stockfish is around\n"
           "  --engine PATH        use any other uci engine (like stockfish)\n"
           "  --backend X          force how it grabs the screen: wayland, gnome or x11\n"
           "  -v / -vv             print what its seeing\n");
}

int main(int argc, char **argv)
{
    g_argv = argv;
    const char *engine = getenv("OPEN_CHEASE_ENGINE");
    int engine_cli = 0;
    dump_dir = getenv("OPEN_CHEASE_DUMP");
    const char *force = NULL;
    const char *replay_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--engine") && i + 1 < argc)
            engine = argv[++i], engine_cli = 1;
        else if (!strcmp(argv[i], "--version")) {
            printf("open-chease 1.0\n");
            return 0;
        } else if (!strcmp(argv[i], "--builtin"))
            engine = NULL, engine_cli = 1;
        else if (!strcmp(argv[i], "--backend") && i + 1 < argc)
            force = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc)
            replay_dir = argv[++i];
        else if (!strcmp(argv[i], "--team") && i + 1 < argc) {
            i++;
            auto_team = !strcmp(argv[i], "black") ? BLACK : WHITE;
        } else if (!strcmp(argv[i], "-v"))
            verbose = 1;
        else if (!strcmp(argv[i], "-vv"))
            verbose = 2;
        else {
            usage();
            return strcmp(argv[i], "-h") && strcmp(argv[i], "--help") ? 1 : 0;
        }
    }
    engine_init();
    init_openings();
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    const char *data = getenv("OPEN_CHEASE_DATA");
    char dbuf[512];
    if (!data) {
        snprintf(dbuf, sizeof(dbuf), "/tmp/open-chease-%d", (int)getuid());
        data = dbuf;
    }
    mkdir(data, 0700);
    struct stat dst;
    if (lstat(data, &dst) || !S_ISDIR(dst.st_mode) || dst.st_uid != getuid()) { // someone else could plant an engine in there
        fprintf(stderr, "%s isnt a folder you own, delete it or point OPEN_CHEASE_DATA somewhere else\n", data);
        return 1;
    }
    if (dst.st_mode & 022) chmod(data, 0700);
    snprintf(tpl_path, sizeof(tpl_path), "%s/pieces.bin", data);
    snprintf(cfg_path, sizeof(cfg_path), "%s/settings", data);
    cfg_default = cfg;
    load_cfg();
    for (int i = 0; i < NTHEMES; i++) theme_names[i] = themes[i].name;
    char real_data[4096];
    const char *dd = realpath(data, real_data) && strlen(real_data) < sizeof(data_dir) ? real_data : data;
    snprintf(data_dir, sizeof(data_dir), "%.1023s", dd);
    load_library();
    scan_engines(engine);
    if (!replay_dir) fetch_library();
    if (!engine_cli && have_saved_engine && (!*saved_engine || access(saved_engine, X_OK) == 0))
        engine = *saved_engine ? saved_engine : NULL;
    cfg.engine = engine_index(engine);
    analyzer_set_options(threads_vals[cfg.threads], hash_vals[cfg.hash], cfg.lines + 1);
    analyzer_set_elo(strength_elo[cfg.strength]);
    debug_on = cfg.debug;
    srand((unsigned)time(NULL));
    templates_builtin(&tpl);
    if (templates_load(&tpl, tpl_path) && verbose) fprintf(stderr, "loaded learned piece set\n");

    const char *desk = getenv("XDG_CURRENT_DESKTOP");
    int gnome = desk && strstr(desk, "GNOME");
    const char *stype = getenv("XDG_SESSION_TYPE");
    int x11_session = (stype && !strcmp(stype, "x11")) || (!getenv("WAYLAND_DISPLAY") && getenv("DISPLAY"));
    if (replay_dir) {
        cap = backend_replay(replay_dir);
        ov = overlay_replay();
        if (!cap->init(cap) || !ov->init(ov, cap)) {
            fprintf(stderr, "cant read frames from %s\n", replay_dir);
            return 1;
        }
        replay_ticks = 0;
        int slow = getenv("OPEN_CHEASE_SHOTS") != NULL;
        if (getenv("OPEN_CHEASE_DEBUG_VIEW")) debug_on = 1; // test hook
        if (slow) {
            analyzer_set_think_ms(300);
            analyzer_start(engine);
        }
        int open_a = -1, open_b = -1;
        const char *so = getenv("OPEN_CHEASE_SETTINGS_OPEN"); // "100-200"
        const char *af = getenv("OPEN_CHEASE_ANIM_AT");
        if (af && af[0] == '0' && af[1] == '.') {
            double v = 0, sc = 0.1;
            for (const char *q = af + 2; *q >= '0' && *q <= '9'; q++, sc /= 10) v += (*q - '0') * sc;
            anim_force = v;
        }
        const char *st = getenv("OPEN_CHEASE_SETTINGS_TAB");
        if (st && *st >= '0' && *st < '0' + NTABS) set_tab = *st - '0';
        if (so) {
            open_a = open_b = 0;
            while (*so >= '0' && *so <= '9') open_a = open_a * 10 + *so++ - '0';
            if (*so == '-') so++;
            while (*so >= '0' && *so <= '9') open_b = open_b * 10 + *so++ - '0';
        }
        while (!quit) {
            if (open_a >= 0) settings_open = replay_frame() >= open_a && replay_frame() <= open_b;
            tick();
            if (slow) usleep(400000); // so the shots have a move in them
            redraw();
            double cx, cy;
            int ev;
            while ((ev = ov->wait_events(ov, 0, &cx, &cy)) > 0) {
                if (ev == 1) handle_click(cx, cy);
                else if (ev == 2) handle_key(ov->key);
                redraw();
            }
            char fen[128];
            pos_to_fen(&tracker.pos, fen);
            printf("%d %s %s\n", replay_frame() - 1, state == TRACKING ? "T" : (state == ASKING ? "A" : "S"), fen);
            replay_ticks++;
        }
        fflush(stdout);
        fflush(stderr);
        _exit(0); // plain exit locks every FILE and the engine reader is stuck in fgets on one
    }
    if (force ? !strcmp(force, "x11") : x11_session) {
        cap = backend_x11();
        ov = overlay_x11();
        if (!cap->init(cap) || !ov->init(ov, cap)) {
            fprintf(stderr, "couldnt open the x11 display (DISPLAY=%s)\n",
                    getenv("DISPLAY") ? getenv("DISPLAY") : "");
            return 1;
        }
    }
    int want_wl = !cap && (force ? !strcmp(force, "wayland") : !gnome);
    if (want_wl && getenv("WAYLAND_DISPLAY") && backend_wl_init_shared()) {
        cap = backend_wl();
        ov = overlay_wl();
        if (!cap->init(cap) || !ov->init(ov, cap)) cap = NULL;
    }
    if (!cap && !x11_session && (!force || !strcmp(force, "gnome"))) {
        cap = backend_gnome();
        if (!cap->init(cap)) {
            cap = NULL;
        } else {
            cap->capture(cap);
            ov = overlay_x11();
            if (!ov->init(ov, cap)) {
                fprintf(stderr, "couldnt open the overlay window, is xwayland turned off? (DISPLAY=%s)\n",
                        getenv("DISPLAY") ? getenv("DISPLAY") : "");
                return 1;
            }
        }
    }
    if (!cap) {
        fprintf(stderr, "couldnt find a way to see your screen. it works on hyprland, sway, gnome and x11 desktops like xfce, "
                        "try --backend if yours is one of those\n");
        return 1;
    }
    analyzer_set_think_ms(think_secs[cfg.think] * 1000);
    analyzer_start(engine);
    Analysis a;
    analyzer_get(&a);
    snprintf(engine_name, sizeof(engine_name), "%s", a.engine);
    fprintf(stderr, "open-chease is running (%s, %s, engine: %s), ctrl+c to quit\n", cap->name, ov->name,
            a.engine);
    redraw();
    long next = now_ms();
    while (!quit) {
        long t = now_ms();
        if (t >= next) {
            tick();
            redraw();
            next = t + 1000 / rate_hz[cfg.rate];
            continue;
        }
        double cx, cy;
        int rainbow = state == TRACKING && (cfg.mcolor == 10 || cfg.tcolor == 10 || cfg.gcolor == 10 || (lib_open && dl_pid > 0));
        int wait = (int)(next - t), busy = animating();
        if (rainbow && wait > 80) wait = 80; // rainbow fades instead of jumping
        if (busy && wait > 15) wait = 15;
        int r = ov->wait_events(ov, wait, &cx, &cy);
        if (r < 0) break;
        if (r == 1) handle_click(cx, cy);
        else if (r == 2) handle_key(ov->key), redraw();
        else if (r == 3 && lib_open) lib_scroll += ov->key, redraw();
        else if (rainbow || busy) redraw();
        sync_keyboard();
    }
    state = SCANNING;
    redraw();
    analyzer_stop();
    usleep(100000);
    fflush(stdout);
    fflush(stderr);
    _exit(0);
}
