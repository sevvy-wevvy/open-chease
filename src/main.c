#define _GNU_SOURCE
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "plat.h"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

enum { SCANNING, ASKING, TRACKING };

typedef struct {
    Rect r;
    int action, arg;
} Hit;

enum {
    ACT_WHITE = 1, ACT_BLACK, ACT_CLOSE, ACT_STOP, ACT_FLIP, ACT_MENU, ACT_DEBUG, ACT_FIX, ACT_RESTART, ACT_UPDATE,
    ACT_QUIT, ACT_QUIT_YES, ACT_QUIT_NO, ACT_SQ, ACT_PIECE, ACT_TURN, ACT_SETTINGS, ACT_SET, ACT_PICK, ACT_RESCAN, ACT_LIB_OPEN, ACT_LIB_CLOSE, ACT_LIB_TAB, ACT_LIB_KEY, ACT_LIB_FILTER,
    ACT_LIB_SCROLL, ACT_LIB_GET, ACT_LIB_USE, ACT_LIB_DEL, ACT_LIB_FOLDER, ACT_BLOCK, ACT_TAB, ACT_THEME, ACT_CSLOT, ACT_CCOPY,
    ACT_CPART, ACT_CSWATCH, ACT_RESET, ACT_MODE, ACT_HINT, ACT_LIB_SECOND
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
static uint32_t *part(Theme *t, int k) { return (uint32_t *)((char *)t + part_off[k]); } // so the theme maker and the settings file can loop the colors

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
    int threats, hints, level, second;
    int hcolor, hstyle;
    int tharrow, thcolor, ththick; // threat and danger arrows
// positional, has to stay in the same order as the fields
} cfg = {0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 4, 2, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 2, 1, 0, 0, 8, 0, 5, 4, 1},
  cfg_default; // copied before load_cfg, the reset button goes back to this

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
static const char *color_names[] = {"Theme", "Gold", "Green", "Blue", "Red", "Pink", "Purple", "White", "Orange", "Cyan", "Rainbow", "Black"}; // black goes last so old saves dont shift
static const uint32_t color_vals[] = {0, 0xC8FFC400, 0xC82BD957, 0xC83D9BFF, 0xC8FF4D4D, 0xC8FF5FC8, 0xC8A86BFF,
                                      0xC8F5F5F5, 0xC8FF8A1F, 0xC81FE0E0, 0, 0xD8101010};
static const char *strength_names[] = {"Full", "2400", "2000", "1700", "1400"};
static const int strength_elo[] = {0, 2400, 2000, 1700, 1400};
static const char *chaos_names[] = {"Best move", "Close enough", "Random top 3", "Coin flip", "Random move", "Worst move",
                                    "Blunders only", "Human 800", "Human 1200", "Human 1600", "Human 2000", "Human 2400"};
static const char *hint_names[] = {"Whole move", "Just the piece", "Just where"};
static const char *delay_names[] = {"Right away", "After 3 sec", "After 10 sec"};
static const int delay_secs[] = {0, 3, 10};
static const char *mode_names[] = {"Cheats", "Helper", "Both"};
static const char *hang_names[] = {"Off", "Mine", "Theirs", "Both"};
static const char *hstyle_names[] = {"Outline", "Corners", "Brackets", "Dashed", "Underline"};
static const int hstyle_mark[] = {1, 3, 9, 10, 13}; // only ones that stay off the piece, a filled square hides it from the vision

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
static intptr_t lib_pid;
#ifdef _WIN32
static const char builtin_library[] = // for when engines-win.txt on github cant be reached
    "Stockfish 19|https://github.com/official-stockfish/Stockfish/releases/download/sf_19/stockfish-windows-x86-64-universal.zip#stockfish-windows-x86-64-universal.exe|zip|77.7|1||the strongest one out there\n"
    "Stockfish 16|https://github.com/official-stockfish/Stockfish/releases/download/sf_16/stockfish-windows-x86-64.zip#stockfish/stockfish-windows-x86-64.exe|zip|33.3|1|old|from 2023, smaller download\n"
    "Reckless 0.9|https://github.com/codedeliveryservice/Reckless/releases/download/v0.9.0/reckless-windows-generic.exe|bin|61.6|1|vcrt|one of the very top engines right now, rust\n"
    "Pawnocchio 2|https://github.com/JonathanHallstrom/pawnocchio/releases/download/v2.0.1/pawnocchio-2.0.1-windows-x86_64.exe|bin|67.2|1||crazy strong. its written in zig which almost nobody does for chess\n"
    "Arasan 26.1|https://github.com/jdart1/arasan-chess/releases/download/v26.1/arasan26.1.zip#arasanx-64.exe|zip|28.5|1||been around since the 90s and somehow keeps getting better\n"
    "PlentyChess 8|https://github.com/Yoshie2000/PlentyChess/releases/download/b-v8.0.0/PlentyChess-8.0.0-windows-generic.exe|bin|52.6|1||one of the best right behind stockfish\n"
    "Viridithas 20|https://github.com/cosmobobak/viridithas/releases/download/v20.0.0/viridithas-20-win-x86-64-v3.exe|bin|53.9|1|cpu|one of the strongest rust engines\n"
    "Stormphrax 8|https://github.com/Ciekce/Stormphrax/releases/download/v8.0.0/stormphrax-8.0.0-avx2-bmi2.exe|bin|54.4|1|cpu|top tier\n"
    "Koivisto 9|https://github.com/Luecx/Koivisto/releases/download/v9.0/Koivisto_9.0-windows-sse2-pgo.exe|bin|15.0|1||really strong and runs on old cpus\n"
    "Fairy-Stockfish 14|https://github.com/fairy-stockfish/Fairy-Stockfish/releases/download/fairy_sf_14/fairy-stockfish_x86-64.exe|bin|1.7|1|variants,small|stockfish that also knows a ton of chess variants\n"
    "Halogen 16|https://github.com/KierenP/Halogen/releases/download/v16/Halogen-16.0.0-windows-x86_64-legacy.exe|bin|21.3|2||runs on any cpu\n"
    "Tarnished 6|https://github.com/Bobingstern/Tarnished/releases/download/v6.0/tarnished-6.0-eternal_x86-64-avx2.exe|bin|44.2|2|cpu|newer engine thats already really good\n"
    "Willow 4|https://github.com/Adam-Kulju/Willow/releases/download/4.0.0.1/willow-v2-windows.exe|bin|4.8|2|small|same dev as patricia but plays it straight\n"
    "Patricia 3|https://github.com/Adam-Kulju/Patricia/releases/download/5.1/patricia_v2.exe|bin|5.1|2|fun,small|the most aggressive engine around, loves throwing pieces at your king\n"
    "Princhess|https://github.com/princesslana/princhess/releases/download/0.22.0/princhess.exe|bin|45.8|2|fun|thinks like alphazero (mcts), plays weird human looking moves\n"
    "Akimbo 1|https://github.com/jw1912/akimbo/releases/download/v1.0.0/akimbo-1.0.0.exe|bin|6.3|2|small,vcrt|small and way stronger than it looks\n"
    "Carp 3|https://github.com/dede1751/carp/releases/download/v3.0.1/carp-v3.0.1-win-x86_64-V1.exe|bin|2.7|2|small|tiny download for how good it is\n"
    "Sirius 9|https://github.com/mcthouacbb/Sirius/releases/download/v9.0/sirius-9.0-windows-x86-64-v1.exe|bin|0.5|2|small|the whole thing is 0.2 MB\n"
    "Counter 5.5|https://github.com/ChizhovVadim/CounterGo/releases/download/v1.55.0/counter-5.5-windows-amd64.exe|bin|4.2|3|small|also go\n"
    "4ku 5.1|https://github.com/kz04px/4ku/releases/download/v5.1/4ku-5.1-popcnt.exe|bin|3.0|3|fun|started life as a 4 KB engine\n"
    "ice4|https://github.com/MinusKelvin/ice4/releases/download/v6.1/ice4.exe|bin|12.7|4|fun,small|from a 4 KB engine contest, tiny\n"
    "Mess|https://github.com/raklaptudirm/mess/releases/download/v0.3.0/mess-windows-amd64.exe|bin|5.2|4|fun,small|a lot weaker, good for chaos games\n"
    "Berserk 14|https://github.com/jhonnold/berserk/releases/download/14/berserk-14-x86-64.exe|bin|25.3|1||the new berserk, one of the very best and windows only\n"
    "Obsidian 16|https://github.com/gab8192/Obsidian/releases/download/v16.0/Obsidian160-avx2.exe|bin|30.7|1|cpu|right up there with stockfish, wants avx2\n";
#elif defined(__APPLE__) && defined(__aarch64__)
static const char builtin_library[] =
    "Stockfish 19|https://github.com/official-stockfish/Stockfish/releases/download/sf_19/stockfish-macos-universal.tar.gz#stockfish/stockfish-macos-universal|tgz|78.5|1||the strongest one out there\n"
    "Stockfish 18|https://github.com/official-stockfish/Stockfish/releases/download/sf_18/stockfish-macos-m1-apple-silicon.tar#stockfish/stockfish-macos-m1-apple-silicon|tar|110.0|1|old|last years stockfish, you wont notice a difference\n"
    "Stockfish 17.1|https://github.com/official-stockfish/Stockfish/releases/download/sf_17.1/stockfish-macos-m1-apple-silicon.tar#stockfish/stockfish-macos-m1-apple-silicon|tar|77.0|1|old|older stockfish\n"
    "Reckless 0.9|https://github.com/codedeliveryservice/Reckless/releases/download/v0.9.0/reckless-macos|bin|62.3|1||one of the very top engines right now, rust\n"
    "Pawnocchio 2|https://github.com/JonathanHallstrom/pawnocchio/releases/download/v2.0.1/pawnocchio-2.0.1-macos-aarch64-apple_m1|bin|66.9|1||crazy strong. its written in zig which almost nobody does for chess\n"
    "PlentyChess 8|https://github.com/Yoshie2000/PlentyChess/releases/download/b-v8.0.0/PlentyChess-8.0.0-macos-neon|bin|51.4|1||one of the best right behind stockfish\n"
    "Viridithas 20|https://github.com/cosmobobak/viridithas/releases/download/v20.0.0/viridithas-20-macos-aarch64|bin|54.3|1||one of the strongest rust engines\n"
    "Stormphrax 8|https://github.com/Ciekce/Stormphrax/releases/download/v8.0.0/stormphrax-8.0.0-apple-m1|bin|54.4|1||top tier\n"
    "Halogen 16|https://github.com/KierenP/Halogen/releases/download/v16/Halogen-16.0.0-macos-arm64-neon-dotprod|bin|18.8|2||solid c++ engine thats been around for years\n"
    "Avalanche 5|https://github.com/SnowballSH/Avalanche/releases/download/v5.0.0/Avalanche-5.0.0-aarch64-macos|bin|25.0|2||zig again\n"
    "Velvet 8|https://github.com/mhonert/velvet-chess/releases/download/v8.1.1/velvet-v8.1.1-macOS-apple-silicon|bin|49.3|2||rust engine\n"
    "Lynx 1.11|https://github.com/lynx-chess/Lynx/releases/download/v1.11.0/Lynx-1.11.0-osx-arm64.zip#Lynx.Cli|zip|12.6|3||written in c#, surprisingly good\n"
    "StockDory 0.3|https://github.com/TheBlackPlague/StockDory/releases/download/0.3/StockDory-Koi-0.3-macos-latest-apple-m1|bin|1.2|4|small|small, beatable if youre decent\n"
    "Mess|https://github.com/raklaptudirm/mess/releases/download/v0.3.0/mess-darwin-arm64|bin|5.1|4|fun,small|a lot weaker, good for chaos games\n";
#elif defined(__APPLE__)
static const char builtin_library[] =
    "Stockfish 19|https://github.com/official-stockfish/Stockfish/releases/download/sf_19/stockfish-macos-universal.tar.gz#stockfish/stockfish-macos-universal|tgz|78.5|1||the strongest one out there\n"
    "Stockfish 18|https://github.com/official-stockfish/Stockfish/releases/download/sf_18/stockfish-macos-x86-64.tar#stockfish/stockfish-macos-x86-64|tar|109.2|1|old|last years stockfish, you wont notice a difference\n"
    "Stockfish 17.1|https://github.com/official-stockfish/Stockfish/releases/download/sf_17.1/stockfish-macos-x86-64.tar#stockfish/stockfish-macos-x86-64|tar|76.5|1|old|older stockfish\n"
    "Stockfish 16|https://github.com/official-stockfish/Stockfish/releases/download/sf_16/stockfish-macos-x86-64.tar#stockfish/stockfish-macos-x86-64|tar|39.8|1|old|from 2023, smaller download\n"
    "Pawnocchio 2|https://github.com/JonathanHallstrom/pawnocchio/releases/download/v2.0.1/pawnocchio-2.0.1-macos-x86_64|bin|66.8|1||crazy strong. its written in zig which almost nobody does for chess\n"
    "Halogen 16|https://github.com/KierenP/Halogen/releases/download/v16/Halogen-16.0.0-macos-x86_64-legacy|bin|18.7|2||runs on any cpu\n"
    "Avalanche 5|https://github.com/SnowballSH/Avalanche/releases/download/v5.0.0/Avalanche-5.0.0-x86_64-macos-v3|bin|25.1|2|cpu|zig again, wants a newer cpu\n"
    "Velvet 8|https://github.com/mhonert/velvet-chess/releases/download/v8.1.1/velvet-v8.1.1-macOS-x86_64-nopopcnt|bin|49.0|2||rust engine, works on really old cpus\n"
    "Carp 3|https://github.com/dede1751/carp/releases/download/v3.0.1/carp-v3.0.1-darwin-x86_64-V1|bin|2.8|2|small|tiny download for how good it is\n"
    "Zahak 10|https://github.com/amanjpro/zahak/releases/download/10.0/zahak-darwin-amd64-10.0-avx|bin|5.4|3|small|go engine\n"
    "Lynx 1.11|https://github.com/lynx-chess/Lynx/releases/download/v1.11.0/Lynx-1.11.0-osx-x64.zip#Lynx.Cli|zip|13.4|3||written in c#, surprisingly good\n"
    "Nalwald 19|https://github.com/tsoj/Nalwald/releases/download/19/Nalwald-19-macos-amd64|bin|12.0|3||written in nim\n"
    "Mess|https://github.com/raklaptudirm/mess/releases/download/v0.3.0/mess-darwin-amd64|bin|5.1|4|fun,small|a lot weaker, good for chaos games\n"
    "Blunder 8.5|https://github.com/deanmchris/blunder/releases/download/v8.5.5/blunder-8.5.5.zip#macOS/blunder-8.5.5-default|zip|14.8|4|fun|plays better than the name says\n";
#else
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
#endif
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
static const char *threat_names[] = {"Off", "Text", "With arrow"};
static const char *level_names[] = {"Off", "1400", "1700", "2000", "2400"};
static const int level_elo[] = {0, 1400, 1700, 2000, 2400};
static const char *second_names[25] = {"Off"}; // then built in, then the engine list
static const int hash_vals[] = {64, 128, 256, 512, 1024};

#define MAX_ENGINES 64
static const char *engine_names[MAX_ENGINES];
static char engine_paths[MAX_ENGINES][1024], engine_labels[MAX_ENGINES][32];
static int nengines;
static char saved_engine[1024], saved_second[1024]; // by path, the list order moves around when you add one
static int have_saved_engine;
static int btn_teach, btn_forget, btn_rescan; // buttons, not settings, never saved

static int is_action(const int *v) { return v == &btn_teach || v == &btn_forget || v == &btn_rescan; }
static int is_button(const int *v) { return v == &cfg.library || v == &cfg.engine || v == &cfg.second || is_action(v); }

typedef struct {
    const char *label, *key;
    int *v, n;
    const char **names;
    const char *section;
    const char *tip; // shows when the mouse sits on the row
} Setting;

static Setting sets[] = {
    {"Theme", "theme", &cfg.theme, NTHEMES, theme_names, "Look", "the colors for the whole thing, the Themes tab has more and lets you make your own"},
    {"Box spot", "spot", &cfg.spot, 4, spot_names, NULL, "where the info box sits around the board, move it if its covering something"},
    {"Box size", "size", &cfg.size, 3, size_names, NULL, "how big the info box and its text are"},
    {"See-through", "fade", &cfg.fade, 4, fade_names, NULL, "makes the box and arrows see-through so you can still see the game under them"},
    {"Grid", "grid", &cfg.grid, 4, grid_names, NULL, "draws lines on the squares, handy to check it found the board right"},
    {"Grid color", "gcolor", &cfg.gcolor, 12, color_names, NULL, "what color the grid is"},
    {"Status line", "status", &cfg.status, 2, onoff, NULL, "the small line with your team, the engine and what its doing"},
    {"Show score", "score", &cfg.score, 2, onoff, NULL, "shows whos winning as a number next to the move"},
    {"Score as", "scorefmt", &cfg.scorefmt, 3, scorefmt_names, NULL, "pawns is +1.5 style, centipawns is +150, win % is your chance of winning"},
    {"Eval bar", "evalbar", &cfg.evalbar, 3, evalbar_names, NULL, "a black and white bar next to the board like on chess sites, shows whos ahead"},
    {"Mode", "mode", &cfg.mode, 3, mode_names, "Fun", "cheats shows moves, helper only gives tips without moves, both does both"},
    {"Move picker", "picker", &cfg.chaos, 12, chaos_names, NULL, "which move it shows you. best is best, the rest are for messing around"},
    {"Strength", "strength", &cfg.strength, 5, strength_names, NULL, "caps how strong the engine plays, lower ones look more human"},
    {"Trash talk", "trash", &cfg.trash, 2, onoff, NULL, "adds a little roast to the move line, just for fun"},
    {"Rate my moves", "rateme", &cfg.rateme, 2, onoff, NULL, "grades every move after you play it, from ! to ??"},
    {"Confetti", "confetti", &cfg.confetti, 2, onoff, NULL, "confetti on the board when you checkmate them"},
    {"Arrow", "arrow", &cfg.arrow, 9, arrow_names, "Yours", "how the arrow for your best move looks, or off for no arrow"},
    {"Arrow size", "thick", &cfg.thick, 4, thick_names, NULL, "how thick your move arrow is"},
    {"Piece to move", "from", &cfg.from, 14, mark_names, NULL, "marks the piece you should move"},
    {"Where it goes", "to", &cfg.to, 14, mark_names, NULL, "marks the square it should go to"},
    {"Move name", "label", &cfg.label, 2, onoff, NULL, "writes the move like Nf3 on the board"},
    {"Top moves", "lines", &cfg.lines, 3, lines_names, NULL, "shows the 2nd and 3rd best moves too, as thinner arrows with a number on them"},
    {"Color", "mcolor", &cfg.mcolor, 12, color_names, NULL, "color of your arrow and marks, theme uses the themes own color"},
    {"Show", "hint", &cfg.hint, 3, hint_names, NULL, "show the whole move, just which piece to move, or just where something goes"},
    {"Show it", "delay", &cfg.delay, 3, delay_names, NULL, "waits a bit before showing the move so you think first"},
    {"Engine", "engine", &cfg.engine, 1, engine_names, "Engine", "which chess engine does the thinking, opens the engine list"},
    {"Get engines", "library", &cfg.library, 1, lib_names, NULL, "download more engines, some are stronger and some are just weird"},
    {"Think time", "think", &cfg.think, 5, think_names, NULL, "how long it thinks each move. longer is stronger but slower"},
    {"Updates", "rate", &cfg.rate, 3, rate_names, NULL, "how often it looks at the screen, faster catches moves quicker but uses more cpu"},
    {"Threads", "threads", &cfg.threads, 5, threads_names, NULL, "how many cpu cores the engine gets, auto is fine for most pcs"},
    {"Memory", "hash", &cfg.hash, 5, hash_names, NULL, "how much ram the engine gets, more helps a bit on long thinks"},
    {"Their turn too", "theirtoo", &cfg.theirtoo, 2, onoff, NULL, "keeps the engine thinking on their turn too so it can guess their move"},
    {"Second opinion", "second", &cfg.second, 1, second_names, NULL, "a second engine checks the first one and tells you when they disagree"},
    {"Show", "theirs", &cfg.theirs, 4, theirs_names, "Theirs", "shows what they will probably play, as text, on the board or both"},
    {"Arrow", "tarrow", &cfg.tarrow, 9, arrow_names, NULL, "how the arrow for their move looks"},
    {"Arrow size", "tthick", &cfg.tthick, 4, thick_names, NULL, "how thick their move arrow is"},
    {"Piece to move", "tfrom", &cfg.tfrom, 14, mark_names, NULL, "marks the piece theyll probably move"},
    {"Where it goes", "tto", &cfg.tto, 14, mark_names, NULL, "marks where theyll probably move it"},
    {"Move name", "tlabel", &cfg.tlabel, 2, onoff, NULL, "writes their move like Nf3 on the board"},
    {"Color", "tcolor", &cfg.tcolor, 12, color_names, NULL, "color of their arrow and marks"},
    {"Material", "material", &cfg.material, 2, onoff, "Helper", "shows whos up in material, like +3"},
    {"Taken pieces", "taken", &cfg.taken, 2, onoff, NULL, "lists the pieces each side has taken"},
    {"Opening & phase", "opening", &cfg.opening, 2, onoff, NULL, "names the opening youre in and if its the opening, middlegame or endgame"},
    {"Win graph", "wgraph", &cfg.wgraph, 2, onoff, NULL, "a little graph of who was winning over the game"},
    {"Turn timer", "timer", &cfg.timer, 2, onoff, NULL, "how long the current turn has been going"},
    {"Game report", "report", &cfg.report, 2, onoff, NULL, "when the game ends it shows how accurate you both played"},
    {"Last move", "lastmove", &cfg.lastmove, 2, onoff, NULL, "outlines the two squares of the last move"},
    {"Check warning", "checkwarn", &cfg.checkwarn, 2, onoff, NULL, "puts a red ring on the king when its in check"},
    {"Hanging pieces", "hanging", &cfg.hanging, 4, hang_names, NULL, "marks pieces that can be taken for free, yours, theirs or both"},
    {"Hanging color", "hcolor", &cfg.hcolor, 12, color_names, NULL, "color of the hanging piece marks"},
    {"Hanging look", "hstyle", &cfg.hstyle, 5, hstyle_names, NULL, "how hanging pieces get marked"},
    {"Move list", "movelist", &cfg.movelist, 2, onoff, NULL, "shows the last few moves of the game"},
    {"Danger alerts", "danger", &cfg.danger, 3, threat_names, NULL, "warns when something of yours can get taken cheap. with arrow also draws who attacks it"},
    {"Session score", "session", &cfg.session, 2, onoff, NULL, "counts your wins, losses and draws since you opened it"},
    {"Threats", "threats", &cfg.threats, 3, threat_names, NULL, "shows what theyd do if they got a free move, like a fork. with arrow draws it too"},
    {"Threat arrow", "threatarrow", &cfg.tharrow, 9, arrow_names, NULL, "how the threat and danger arrows look"},
    {"Threat color", "threatcolor", &cfg.thcolor, 12, color_names, NULL, "color of the threat and danger arrows"},
    {"Threat size", "threatsize", &cfg.ththick, 4, thick_names, NULL, "how thick the threat and danger arrows are"},
    {"Hint button", "hints", &cfg.hints, 2, onoff, NULL, "adds a hint button, each click shows a bit more of the best move"},
    {"What a ___ plays", "level", &cfg.level, 5, level_names, NULL, "shows what a player at that rating would probably play here"},
    {"Show what it sees", "debug", &cfg.debug, 2, onoff, "Vision", "tags every square with what it thinks is there, good for checking it read right"},
    {"Square names", "sqnames", &cfg.sqnames, 2, onoff, NULL, "writes a1, b2 and so on on every square"},
    {"Rescan board", "rescan", &btn_rescan, 1, NULL, NULL, "reads the whole board again from scratch if it got lost"},
    {"Teach these pieces", "teach", &btn_teach, 1, NULL, NULL, "learns your piece set from the board right now. fix wrong pieces first"},
    {"Forget learned pieces", "forget", &btn_forget, 1, NULL, NULL, "throws away the pieces it learned and goes back to the built in ones"},
};
#define NSET ((int)(sizeof(sets) / sizeof(sets[0])))
static int dd_open = -1;
static void check_download(void);
static void check_switch(void);
static intptr_t dl_pid;
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
        for (char *q = sp + 1; *q >= '0' && *q <= '9' && v < 100000; q++) v = v * 10 + *q - '0'; // capped so a junk line cant overflow it
        if (!strcmp(k, "squares")) old_squares = !!v;
        if (!strcmp(k, "enginepath")) {
            snprintf(saved_engine, sizeof(saved_engine), "%s", sp + 1);
            saved_engine[strcspn(saved_engine, "\n")] = 0;
            have_saved_engine = 1;
            continue;
        }
        if (!strcmp(k, "chaos")) { // old name, 7 was the one human mode back then
            cfg.chaos = v == 7 ? 9 : v < 7 ? v : 0;
            continue;
        }
        if (!strcmp(k, "secondpath")) {
            snprintf(saved_second, sizeof(saved_second), "%s", sp + 1);
            saved_second[strcspn(saved_second, "\n")] = 0;
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
                if (nd == 8) *part(t, j) = v; // half a color is worse than the old one
            }
            continue;
        }
        if (!strcmp(k, "engine") || !strcmp(k, "library")) continue; // indexes go stale, enginepath is what counts
        if (!strcmp(k, "from") || !strcmp(k, "to")) has_marks = 1;
        for (int i = 0; i < NSET; i++)
            if (!strcmp(k, sets[i].key) && v >= 0 && v < sets[i].n) *sets[i].v = v;
    }
    fclose(f);
    if (old_squares >= 0 && !has_marks) cfg.from = cfg.to = old_squares; // old settings files from before the dropdowns
}

static void save_cfg(void)
{
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", cfg_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int i = 0; i < NSET; i++)
        if (!is_button(sets[i].v)) // engine and second get saved as paths below
            fprintf(f, "%s %d\n", sets[i].key, *sets[i].v);
    if (have_saved_engine) fprintf(f, "enginepath %s\n", saved_engine);
    if (*saved_second) fprintf(f, "secondpath %s\n", saved_second);
    for (int i = 0; i < NCUSTOM; i++) {
        fprintf(f, "custom%d", i + 1);
        for (int j = 0; j < NPARTS; j++) fprintf(f, " %08X", *part(&themes[CUSTOM0 + i], j));
        fputc('\n', f);
    }
    if (fclose(f) == 0) replace_file(tmp, cfg_path);
}
static char note[128];
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
    static time_t t0;
    static int have_t0;
    if (!have_t0) t0 = t.tv_sec - 1000, have_t0 = 1; // long is 32 bit on windows
    return (long)(t.tv_sec - t0) * 1000 + t.tv_nsec / 1000000;
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

static double kx(int out) { return cap->out[out].logical.w / cap->out[out].frame.w; } // 0.5 on a 2x screen

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
    int rank = sq >> 3, file = sq & 7; // sq 0 is a1 but row 0 is the top of the screen
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

static void draw_button(Canvas *c, Rect r, const char *label, uint32_t bg, uint32_t fg)
{
    draw_round_rect(c, r, 6, bg);
    double tw = text_lw(c, label, 0);
    draw_text(c, r.x + (r.w - tw) / 2, r.y + (r.h - 14) / 2 - 1, label, 0, fg); // 14 = small font height
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
    if (cfg.spot == 2 || cfg.spot == 3) { // left/right, falls back to the top if its off screen
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
    if (y + h > outr->y + outr->h - 4) y = bl->y + 4; // fullscreen board, nowhere left but on top of it
    return (Rect){x, y, w, h};
}

static uint32_t pick_color(int idx, uint32_t fallback);

static long hush_until; // stuck with squares hidden under our own drawings, so draw nothing on the board for a moment

static int hushed(void) { return now_ms() < hush_until; }

static void draw_grid(Canvas *c, Rect bl, int style)
{
    if (hushed()) return;
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
    if (hushed()) return;
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
        for (int k = 0; k < 48; k++) { // no ring in draw.c, 48 short lines look round enough
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
    if (hushed()) return;
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
        double L = cum[np - 1], from_d = s * 0.18, to_d = L - head; // shaft stops where the head starts
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
                    draw_line(c, Q[k][0], Q[k][1], Q[k + 1][0], Q[k + 1][1], lw * 3.2, (col & 0xFFFFFF) | ((col >> 26) << 24)); // quarter alpha
            for (int k = 0; k + 1 < nq; k++) {
                double x0 = Q[k][0], y0 = Q[k][1], x1 = Q[k + 1][0], y1 = Q[k + 1][1];
                if (arrow == 4) {
                    double w = lw * (0.25 + 0.95 * qd[k + 1] / span);
                    draw_line(c, x0, y0, x1, y1, w, col);
                } else if (arrow == 5) {
                    double seg = s * 0.32, len = qd[k + 1] - qd[k];
                    for (double d = 0; d < len; d += 1) { // phase comes off the whole length so dashes carry across curve pieces
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
        double h = wrap(now_ms() / 6000.0, 1.0) * 6, f = h - floor(h); // one lap every 6 sec
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
    wp += (target - wp) * 0.3; // slides instead of jumping, bar_moving keeps the frames coming till its there
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

static int ui_over_board, learned_here, want_rescan, rescan_hush, want_teach, teach_hush, teach_tries;
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
    case 2: return e->mb < 10 || strstr(e->tags, "small"); // under 10 MB counts even without the tag
    case 3: return strstr(e->tags, "fun") || strstr(e->tags, "variants");
    case 4: return strstr(e->tags, "old") != NULL;
    }
    return 1;
}

static void lib_out_path(int i, char *out, size_t n);
static int engine_index(const char *path);
static void cached_name(const char *path, char *out, size_t n);
static void remember_name(const char *path, const char *name);

static int mine(int i) // only delete stuff we downloaded or you dropped in
{
    size_t n = strlen(data_dir);
    return n && !strncmp(engine_paths[i], data_dir, n) && (engine_paths[i][n] == '/' || engine_paths[i][n] == '\\');
}

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
    if (anim_force >= 0) return anim_force; // OPEN_CHEASE_ANIM_AT, freezes every animation for screenshots
    double t = (now_ms() - since) / (double)ms;
    if (t >= 1) return 1;
    if (t <= 0) return 0;
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

static int animating(void)
{
    long n = now_ms();
    // a bit longer than each ease() so the last frame lands at 1
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

static Canvas lays[3]; // popup, tab slide, dropdown. each fades on its own so they cant share
static size_t lay_cap[3];

static Canvas *layer(int k, const Canvas *c, Rect r)
{
    double pad = 16; // room for the drop shadow and the slide in
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
    for (int k = 4; k >= 1; k--) // fake soft shadow, 4 faint rects stacked and pushed down a bit
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
    // clickable keys too, not every backend can grab the keyboard
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
            int got = is_exec(out), using = got && engine_index(out) == cfg.engine && cfg.engine;
            int getting = dl_pid > 0 && dl_index == i;
            draw_text(c, row.x + 10, row.y + 5, e->name, 0, TH->title);
            snprintf(info, sizeof(info), "%s  -  %.1f MB%s%s%s", tiers[e->tier], e->mb, strstr(e->tags, "cpu") ? "  -  needs a newer cpu" : strstr(e->tags, "vcrt") ? "  -  needs the vc++ runtime" : "",
                     strstr(e->tags, "variants") ? "  -  variants" : "", strstr(e->tags, "old") ? "  -  older version" : "");
            draw_text(c, row.x + 10, row.y + 22, fit_text(c, info, row.w - 110), 0, TH->sub);
            draw_text(c, row.x + 10, row.y + 38, fit_text(c, e->about, row.w - 110), 0, TH->item_fg);
            char pct[16] = "getting";
            if (getting && dl_percent(e->mb) >= 0) snprintf(pct, sizeof(pct), "%d%%", dl_percent(e->mb));
            const char *bt = getting ? pct : using ? "Using" : got ? "Use" : "Get";
            draw_button(c, b1, bt, using ? TH->title : TH->btn, using ? 0xFF181818 : TH->btn_fg);
            if (hit && !getting && !using) add_hit_arg(b1, got ? ACT_LIB_USE : ACT_LIB_GET, got ? -1 - i : i); // negative = library index, the installed tab sends engine indexes
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
            if (!using && i < 24) { // second_names only has room for 24
                int on = cfg.second == i + 1;
                Rect b3 = {b1.x - 74, b1.y, 66, 26};
                draw_button(c, b3, on ? "2nd on" : "2nd", on ? 0xFF1FB8B8 : TH->btn, on ? 0xFF181818 : TH->btn_fg);
                if (hit) add_hit_arg(b3, ACT_LIB_SECOND, i);
            }
            if (mine(i)) {
                Rect b2 = {b1.x, b1.y + 26, 84, 22};
                int sure = lib_del == i; // two clicks to delete
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
    const char *hint[10] = {cfg.mode == 1 ? "on, moves are hidden" : cfg.mode == 2 ? "moves + helper stuff" : "hides the moves", "tags on every square",
                            "click one, pick whats there", "", "if it got lost", "get, pick or delete them", "", "",
                            "pulls the newest version", ""};
    int acts[10] = {ACT_MODE, ACT_DEBUG, ACT_FIX, ACT_TURN, ACT_RESCAN, ACT_LIB_OPEN, ACT_FLIP, ACT_RESTART, ACT_UPDATE, ACT_QUIT};
    int icons[10] = {9, 0, 1, 2, 3, 4, 5, 6, 7, 8}; // 9 is the switch, flips to 10 when its on
    int group[10] = {0, 1, 1, 1, 1, 2, 2, 3, 3, 4}; // divider line wherever this changes
    double mw = 236, ih = 42, gap = 9, mh = 10 * ih + 4 * gap + 12; // 4 dividers
    Rect m = {p.x + p.w - mw, p.y + p.h + 6, mw, mh};
    if (m.y + mh > outr.y + outr.h) m.y = p.y - mh - 6; // box at the bottom, open upwards
    if (m.y < outr.y) m.y = outr.y + 4;
    if (m.x < outr.x + 4) m.x = outr.x + 4;
    int closing = !menu_open;
    double an = closing ? ease(menu_shut, 130) : ease(menu_t, 190);
    Canvas *c = layer(0, dst, m);
    if (!c) return;
    if (!closing) covers(m, bl); // a closing menu shouldnt pause tracking
    panel_bg(c, m, 14, TH->menu);
    if (hit) add_hit(m, ACT_BLOCK); // goes first, clicks check hits newest first so the rows win
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

static const char *tab_names[] = {"Look", "Yours", "Theirs", "Engine", "Fun", "Helper", "Vision", "Themes"};
#define NTABS 8
#define TAB_THEMES 7
static int set_tab, cslot, cpart;

static int tab_of(int i)
{
    int t = 0;
    for (int k = 0; k <= i; k++) // the last section header above it, sets[] is in tab order
        if (sets[k].section)
            for (int j = 0; j < NTABS; j++)
                if (!strcmp(sets[k].section, tab_names[j])) t = j;
    return t;
}

static double hov_x, hov_y;
static long hov_t;
static int hov_in, tip_wait, tip_shown;
static const char *tip_text;

static int hover_moved(void) // 1 = the tooltip has to show up or go away
{
    double x = 0, y = 0;
    int in = ov->pointer && ov->pointer(ov, &x, &y);
    if (in != hov_in || fabs(x - hov_x) > 1 || fabs(y - hov_y) > 1) {
        hov_in = in, hov_x = x, hov_y = y, hov_t = now_ms();
        return tip_shown;
    }
    return tip_wait && now_ms() - hov_t >= 500;
}

static void tip_spot(Rect r, const char *t)
{
    if (t && hov_in && hov_x >= r.x && hov_x < r.x + r.w && hov_y >= r.y && hov_y < r.y + r.h) tip_text = t;
}

static int wrap_text(Canvas *c, const char *t, double maxw, char out[][100], int max)
{
    int n = 0;
    while (*t == ' ') t++;
    while (*t && n < max) {
        size_t end = 0, k = 0;
        while (t[k]) {
            size_t w = k;
            while (t[w] == ' ') w++;
            while (t[w] && t[w] != ' ') w++;
            if (w > 98) break;
            char buf[100];
            memcpy(buf, t, w), buf[w] = 0;
            if (end && text_lw(c, buf, 0) > maxw) break;
            end = k = w;
        }
        if (!end) end = 98;
        if (n == max - 1 && t[end]) snprintf(out[n], 100, "%s", fit_text(c, t, maxw)); // out of lines, ... the rest
        else memcpy(out[n], t, end), out[n][end] = 0;
        n++;
        t += end;
        while (*t == ' ') t++;
    }
    return n;
}

static void draw_tip(Canvas *c, Rect outr, Rect bl)
{
    char ln[3][100];
    int n = wrap_text(c, tip_text, 250, ln, 3);
    double w = 0;
    for (int k = 0; k < n; k++) w = fmax(w, text_lw(c, ln[k], 0));
    Rect r = {hov_x + 14, hov_y + 22, w + 20, n * 18 + 10};
    if (r.x + r.w > outr.x + outr.w - 4) r.x = outr.x + outr.w - 4 - r.w;
    if (r.x < outr.x + 4) r.x = outr.x + 4;
    if (r.y + r.h > outr.y + outr.h - 4) r.y = hov_y - r.h - 10; // no room under the mouse, go above it
    covers(r, bl);
    panel_bg(c, r, 8, TH->menu);
    for (int k = 0; k < n; k++) draw_text(c, r.x + 10, r.y + 5 + k * 18, ln[k], 0, TH->item_fg);
}

static const char *teach_steps[] = {
    "1. open a game so the board is on screen",
    "2. hit ... > Fix pieces, click a wrong square and pick whats really there",
    "3. do that for every wrong one, then hit Done fixing",
    "4. come back here and hit Teach these pieces, it remembers them for this board",
};
#define TAB_VISION 6

static uint32_t swatch(int i)
{
    if (i >= 60) { // 12 hues x 5 shades, then a row of greys
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
    tip_spot(r, "click to use this theme");
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
            tip_spot(b, "your own themes, pick one to edit it. hit it in the list above to use it");
        }
        bx += 90;
    }
    const char *cpl = cfg.theme >= CUSTOM0 ? "Start from Dark" : "Copy current theme";
    Rect cp = {bx, y, text_lw(c, cpl, 0) + 22, 26};
    if (cp.x + cp.w > x0 + w) cp.x = x0, cp.y = y += 32; // one column panel, wont fit next to the slots
    if (draw) {
        draw_button(c, cp, cpl, TH->btn, TH->btn_fg);
        if (hit) add_hit(cp, ACT_CCOPY);
        tip_spot(cp, "fills this slot with a theme you already have so you dont start from nothing");
    }
    y += 34;
    double lw = 150, sw = 22, sg = 4, pw = 12 * (sw + sg);
    int side = w >= lw + 12 + pw; // swatches beside the parts list, or under it on narrow screens
    for (int k = 0; k < NPARTS; k++) {
        Rect r = {x0, y + k * 24, lw, 22};
        if (draw) {
            draw_round_rect(c, r, 5, k == cpart ? TH->btn : TH->item);
            draw_round_rect(c, (Rect){r.x + 5, r.y + 4, 14, 14}, 7, (TH->item_fg & 0xFFFFFF) | 0x60000000);
            draw_round_rect(c, (Rect){r.x + 6, r.y + 5, 12, 12}, 6, *part(&themes[CUSTOM0 + cslot], k) | 0xFF000000);
            draw_text(c, r.x + 24, r.y + 2, part_names[k], 0, TH->item_fg);
            if (hit) add_hit_arg(r, ACT_CPART, k);
            tip_spot(r, "pick a part, then click a color to change it");
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
    double top = ty + tabh + 14, body, helpw = mw - 2 * pad - 32;
    int per = (n + ncol - 1) / ncol, nhelp = 0, wrapped[12] = {0};
    char help[12][100];
    for (int k = 0; set_tab == TAB_VISION && k < 4; k++) {
        int got = wrap_text(c, teach_steps[k], helpw, help + nhelp, 3);
        for (int j = 1; j < got; j++) wrapped[nhelp + j] = 1;
        nhelp += got;
    }
    Rect m = {0, 0, mw, 0};
    if (set_tab == TAB_THEMES)
        body = themes_tab(c, m, top, pad, 0, 0) - top; // draw 0 = just measure it
    else
        body = per * rh + 40 + (nhelp ? nhelp * 18 + 40 : 0);
    double mh = top + body + pad;
    static double from_h, to_h;
    static long h_t = -99999;
    if (mh != to_h) { // tabs are different heights, grow from wherever it is right now
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
    set_text_zoom(1); // box size shouldnt blow up the settings too
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
    // labels go on after the pill, dark text only where the pill is under it mid slide
    for (int k = 0; k < NTABS; k++) {
        Rect r = tabr[k];
        r.x += m.x, r.y += m.y;
        double tw = text_lw(c, tab_names[k], 0), cx = r.x + r.w / 2;
        int under = cx > pill.x && cx < pill.x + pill.w && fabs(r.y - pill.y) < 4;
        draw_text(c, r.x + (r.w - tw) / 2, r.y + 4, tab_names[k], 0, under ? 0xFF181818 : TH->item_fg);
    }
    Canvas *panel = c;
    Rect area = {m.x, m.y + top - 6, m.w, m.h - top + 6};
    if (tq < 1 && (c = layer(1, panel, area)) == NULL) c = panel; // only needs its own layer while it slides
    Rect ddbox = {0};
    tip_text = NULL;
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
            double d = 14, q = i == tog_i ? ease(tog_t, 160) : 1, v = on ? q : 1 - q; // only the one you just clicked slides
            draw_round_rect(c, sw, 9, mix(TH->btn, TH->title, v));
            draw_round_rect(c, (Rect){sw.x + 2 + (sw.w - d - 4) * v, sw.y + 2, d, d}, d / 2, mix(0xFFE8E8E8, 0xFF181818, v));
        } else {
            int act = is_action(st->v);
            Rect box = {row.x + row.w - (act ? 36 : 126), row.y + 3, act ? 30 : 120, row.h - 6};
            draw_round_rect(c, box, 5, dd_open == i ? TH->title : TH->btn);
            uint32_t fg = dd_open == i ? 0xFF181818 : TH->btn_fg;
            if (st->v == &cfg.mcolor || st->v == &cfg.tcolor || st->v == &cfg.gcolor || st->v == &cfg.hcolor || st->v == &cfg.thcolor || st->v == &cfg.theme) {
                uint32_t sw = st->v == &cfg.theme ? TH->panel : pick_color(*st->v, st->v == &cfg.mcolor ? TH->arrow : st->v == &cfg.gcolor ? TH->grid : st->v == &cfg.hcolor ? 0xD8FF8A1F : 0xC8FF4D4D);
                draw_round_rect(c, (Rect){box.x - 18, box.y + 4, 12, 12}, 6, sw | 0xFF000000);
            }
            if (!act)
                draw_text(c, box.x + 8, box.y + 1,
                          fit_text(c, st->v == &cfg.library ? (dl_pid > 0 ? "Getting..." : "Browse") : st->names[*st->v], box.w - 26),
                          0, fg);
            double tx2 = box.x + box.w - (act ? 19 : 15), ty2 = box.y + box.h / 2 - 2;
            if (is_button(st->v))
                draw_text(c, tx2 + 1, box.y + 1, ">", 0, fg);
            else
                draw_triangle(c, tx2, ty2, tx2 + 9, ty2, tx2 + 4.5, ty2 + 5, fg);
            if (dd_open == i) ddbox = box; // list gets drawn after the panel so it can hang off the bottom
        }
        if (hit) add_hit_arg(row, ACT_SET, i);
        tip_spot(row, st->tip);
    }
    if (nhelp) {
        Rect hb = {m.x + pad, m.y + top + per * rh + 4, m.w - 2 * pad, nhelp * 18 + 30};
        draw_round_rect(c, hb, 7, TH->item);
        draw_text(c, hb.x + 8, hb.y + 5, "How to teach it your pieces", 0, TH->title);
        for (int k = 0; k < nhelp; k++) draw_text(c, hb.x + (wrapped[k] ? 24 : 8), hb.y + 25 + k * 18, help[k], 0, TH->item_fg);
    }
    Rect rs = {m.x + pad, m.y + top + body - 30, 150, 26};
    if (set_tab != TAB_THEMES) {
        draw_button(c, rs, "Reset this tab", TH->btn, TH->btn_fg);
        if (hit) add_hit(rs, ACT_RESET);
        tip_spot(rs, "puts every setting in this tab back to how it started");
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
            if (st->v == &cfg.mcolor || st->v == &cfg.tcolor || st->v == &cfg.gcolor || st->v == &cfg.hcolor || st->v == &cfg.thcolor || st->v == &cfg.theme) {
                uint32_t sw = (st->v == &cfg.theme ? themes[k].panel : pick_color(k, st->v == &cfg.mcolor ? TH->arrow : st->v == &cfg.gcolor ? TH->grid : st->v == &cfg.hcolor ? 0xD8FF8A1F : 0xC8FF4D4D)) | 0xFF000000;
                if (st->v != &cfg.theme && k == 11) draw_round_rect(c, (Rect){it.x + 5, it.y + 4, 14, 14}, 7, 0xFFB0B0B0); // rim, black on the dark menu is invisible
                draw_round_rect(c, (Rect){it.x + 6, it.y + 5, 12, 12}, 6, sw);
                draw_text(c, it.x + 24, it.y + 3, st->names[k], 0, TH->item_fg);
            } else {
                draw_text(c, it.x + 8, it.y + 3, st->names[k], 0, TH->item_fg);
            }
            if (hit) add_hit_arg(it, ACT_PICK, dd_open * 64 + k); // setting and choice in one int, no list has 64 things
        }
        if (c != base) canvas_blit(base, c, fmin(1, dq * 1.5), 0, -5 * (1 - dq), 18 + l.h * dq); // clip_h unrolls it top down
    }
    if (tip_text && dd_open < 0) {
        if (now_ms() - hov_t >= 500) draw_tip(dst, outr, bl), tip_shown = 1;
        else tip_wait = 1;
    }
}

static void draw_extras(Canvas *c, Rect bl, Rect p, Rect outr, int hit)
{
    double s = bl.w / 8;
    if ((debug_on || fix_mode) && !hushed()) {
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
            if (pk.y + h > outr.y + outr.h) pk.y = bl.y + bl.h - h - 4; // covers the bottom rank but thats fine while fixing
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
                        arg = row * 6 + k; // same codes as pos.sq, white row then black
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
    if (menu_open || now_ms() - menu_shut < 130) draw_menu(c, p, bl, outr, menu_open && hit); // keep drawing it while it fades out, but no clicks
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
    if (lib_open) draw_library(c, outr, bl, hit); // last so its on top of everything
}

static int qs(Pos *p, int alpha, int beta, int d)
{
    int stand = evaluate(p);
    if (d == 0 || stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    Move ms[256];
    int n = pos_gen_legal(p, ms);
    for (int i = 0; i < n && alpha < beta; i++) {
        if (p->sq[M_TO(ms[i])] == EMPTY && M_FLAG(ms[i]) != FL_EP) continue; // captures only, so a trade isnt scored halfway
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
    if (!depth) return qs(p, alpha, beta, 4); // 4 captures deep is plenty for these picks
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
    static int cdeep;
    static int cmode = -1;
    static Move cm;
    if (!cfg.chaos || !an->best) return an->best;
    int deep = an->depth >= 12;
    if (ck == tracker.pos.key && cmode == cfg.chaos && (cdeep || !deep)) return cm; // same spot same pick, or the arrow jumps around every redraw
    Pos p = tracker.pos;
    Move ms[256];
    int sc[256], n = pos_gen_legal(&p, ms);
    if (!n) return an->best;
    int best = -99999, worst = 99999;
    for (int i = 0; i < n; i++) {
        Undo u;
        pos_make(&p, ms[i], &u);
        sc[i] = -quick(&p, 1, -99999, 99999); // the engine only scores its top lines, this needs every move
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
            if (sc[i] >= best - 60) pool[np++] = ms[i]; // within 0.6 of the best
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
        if (!np) // every move is about equal, so just the worst
            for (int i = 0; i < n; i++)
                if (sc[i] == worst) pool[np++] = ms[i];
        break;
    case 10: case 11: { // 2000 and 2400 only pick from the engines top 3, the quick search above is too weak to trust up here
        double win = cfg.chaos == 11 ? 25 : 70, w3[3], tot = 0;
        Move cand[3] = {an->best, an->alt[0], an->alt[1]};
        int last_to = tracker.last_move ? (int)M_TO(tracker.last_move) : -1;
        for (int k = 0; k < 3; k++) {
            w3[k] = 0;
            if (!cand[k]) continue;
            int sc3 = k ? an->alt_score[k - 1] : an->score, d = an->score - sc3;
            if (k && (d > win || an->alt_mate[k - 1] < 0)) continue;
            double x = exp(-d / (win / 2.5));
            int to = M_TO(cand[k]);
            if (p.sq[to] != EMPTY) x *= to == last_to ? 1.6 : 1.2;
            Undo u;
            pos_make(&p, cand[k], &u);
            if (pos_in_check(&p, p.side)) x *= 1.2;
            pos_unmake(&p, &u);
            w3[k] = x, tot += x;
        }
        double r3 = (rand() / (RAND_MAX + 1.0)) * tot;
        for (int k = 0; k < 3 && tot > 0; k++)
            if (w3[k] > 0 && (r3 -= w3[k]) <= 0) {
                pool[np++] = cand[k];
                break;
            }
        break;
    }
    case 7: case 8: case 9: {
        static const double temp[] = {150, 95, 60, 38, 22}, cut[] = {350, 220, 130, 80, 50}, eng[] = {1, 1, 1.6, 3, 6};
        int lv = cfg.chaos - 7;
        double w[256], total = 0;
        int us = p.side, them = us ^ 1, early = p.fullmove < 12;
        int last_to = tracker.last_move ? (int)M_TO(tracker.last_move) : -1;
        for (int i = 0; i < n; i++) {
            w[i] = 0;
            if (sc[i] < best - cut[lv]) continue; // past this even a player that rated wouldnt go there
            int from = M_FROM(ms[i]), to = M_TO(ms[i]), ty = p.sq[from] % 6, cap = p.sq[to] != EMPTY;
            double x = exp((sc[i] - best) / temp[lv]); // weaker = flatter, so worse moves still come up
            if (ms[i] == an->best) x *= eng[lv]; // strong players find the engine move way more often
            for (int k = 0; k < 2; k++)
                if (an->alt[k] && ms[i] == an->alt[k]) x *= 1 + (eng[lv] - 1) / 2;
            if (cap) x *= to == last_to ? 2.5 : 1.6; // people always look at recaptures first
            if (M_FLAG(ms[i]) == FL_CASTLE) x *= 1.8;
            if (pos_is_attacked(&p, from, them) && ty != PAWN) x *= 1.5; // moving the piece thats being hit
            if (early && (ty == KNIGHT || ty == BISHOP) && (from >> 3) == (us == WHITE ? 0 : 7)) x *= 1.4;
            if (early && ty == QUEEN) x *= lv < 2 ? 1.3 : 0.6; // beginners love an early queen, everyone else got told not to
            if (ty == KING && M_FLAG(ms[i]) != FL_CASTLE && !early && p.fullmove < 40) x *= 0.5;
            if (ty == PAWN && !cap && ((to & 7) == 0 || (to & 7) == 7)) x *= lv < 2 ? 1.2 : 0.7; // a and h pawn pushes
            Undo u;
            pos_make(&p, ms[i], &u);
            if (pos_in_check(&p, p.side)) x *= lv < 2 ? 2.2 : 1.4; // checks get looked at, low rated players cant resist one
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
    ck = tracker.pos.key, cmode = cfg.chaos, cm = pick, cdeep = deep; // repick once when the engine got deep, the early best is shaky
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
    const char **list = cfg.chaos >= 4 ? bad : good; // random move and down, human-like lands in here too
    size_t n = cfg.chaos >= 4 ? sizeof(bad) / sizeof(*bad) : sizeof(good) / sizeof(*good);
    return list[(key * 2654435761u >> 7) % n]; // off the position key so it doesnt change every frame
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
    if (hushed()) return;
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
    if (hushed()) return;
    static const uint32_t cols[] = {0xFFFF4D4D, 0xFFFFC400, 0xFF2BD957, 0xFF3D9BFF, 0xFFFF5FC8, 0xFFA86BFF};
    unsigned seed = (unsigned)(now_ms() / 300); // no particles to keep track of, it just reshuffles every 300ms
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
    int sc = a->mate ? (a->mate > 0 ? 1000 : -1000) : a->score; // mate counts as 10 pawns so the rating math stays sane
    return user_to_move ? sc : -sc;
}

static const char *openings[][2] = { // matched by position, so other move orders still get the name
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
        open_keys[i] = tok ? 0 : p.key; // a bad move in the table just never matches
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
static int new_stats;

static double win_pct(int cp) { return 100 / (1 + exp(-0.00368208 * cp)); } // same curve lichess uses

static void helper_track(const Analysis *an, int have_an, int my_turn)
{
    static u64 last;
    u64 k = tracker.pos.key;
    if (k != last || new_stats) {
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
        if (k == start_key || new_stats) {
            new_stats = 0;
            nsan = 0, counted_key = 0;
            open_name = NULL, nwhist = 0, whist_key = 0, hplies = 0;
            memset(grades, 0, sizeof(grades));
            memset(acc_n, 0, sizeof(acc_n));
            memset(acc_sum, 0, sizeof(acc_sum));
        }
        for (int i = 0; i < NOPEN; i++)
            if (open_keys[i] == k) open_name = openings[i][1];
    }
    if (!have_an || an->depth < 6) return; // shallow scores jump all over the graph
    float w = an->mate ? (user_score(an, my_turn) > 0 ? 100 : 0) : win_pct(user_score(an, my_turn));
    if (whist_key == k && nwhist) { // same spot, deeper search, just fix the last point
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
    if (total <= 26 || (!queens && total <= 36)) return "Endgame"; // full set is 62, pawns dont count
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

static Move danger_mv;

static void danger_text(char *out, size_t n, const Analysis *an, int have_an, int my_turn)
{
    out[0] = 0;
    danger_mv = 0;
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
    t.side = me ^ 1; // what could they grab if it was their move
    Move ms[256];
    int nm = pos_gen_legal(&t, ms), worst = -1;
    for (int q = 0; q < nm; q++) {
        int vic = tracker.pos.sq[M_TO(ms[q])], att = tracker.pos.sq[M_FROM(ms[q])];
        if (vic == EMPTY || vic / 6 != me || vic % 6 == PAWN || vic % 6 == KING) continue; // a pawn isnt worth yelling about
        int cheap = val[att % 6] < val[vic % 6], loose = !pos_is_attacked(&tracker.pos, M_TO(ms[q]), me);
        if ((cheap || loose) && (worst < 0 || val[vic % 6] > val[worst])) worst = vic % 6, danger_mv = ms[q];
    }
    static const char *nm6[] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
    if (worst >= 0) snprintf(out, n, "your %s is in danger", nm6[worst]);
}

static u64 piece_hits(const Pos *p, int sq)
{
    static const int kn[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int pc = p->sq[sq], ty = pc % 6, f = sq & 7, r = sq >> 3;
    u64 out = 0;
    if (pc == EMPTY) return 0;
    if (ty == PAWN) {
        int dr = pc / 6 == WHITE ? 1 : -1;
        for (int df = -1; df <= 1; df += 2)
            if (f + df >= 0 && f + df < 8 && r + dr >= 0 && r + dr < 8) out |= 1ULL << ((r + dr) * 8 + f + df);
        return out;
    }
    if (ty == KNIGHT || ty == KING) {
        for (int k = 0; k < 8; k++) {
            int ff = f + (ty == KNIGHT ? kn[k][0] : dirs[k][0]), rr = r + (ty == KNIGHT ? kn[k][1] : dirs[k][1]);
            if (ff >= 0 && ff < 8 && rr >= 0 && rr < 8) out |= 1ULL << (rr * 8 + ff);
        }
        return out;
    }
    for (int k = ty == BISHOP ? 4 : 0; k < (ty == ROOK ? 4 : 8); k++)
        for (int ff = f + dirs[k][0], rr = r + dirs[k][1]; ff >= 0 && ff < 8 && rr >= 0 && rr < 8; ff += dirs[k][0], rr += dirs[k][1]) {
            out |= 1ULL << (rr * 8 + ff);
            if (p->sq[rr * 8 + ff] != EMPTY) break; // the blocker still counts, thats what gets hit
        }
    return out;
}

static Move threat_mv;
static char threat_txt[96];

static void find_threat(void)
{
    static u64 done_key;
    static int done_side = -1;
    if (done_key == tracker.pos.key && done_side == tracker.user_color) return;
    done_key = tracker.pos.key, done_side = tracker.user_color;
    threat_mv = 0, threat_txt[0] = 0;
    int me = tracker.user_color;
    if (tracker.pos.side != me || pos_in_check(&tracker.pos, me)) return;
    // pretend we pass and see what they would do with a free move
    static Pos t;
    t = tracker.pos;
    t.side = me ^ 1;
    t.ep = -1; // that ep square was ours to take, not theirs
    Move ms[256];
    int n = pos_gen_legal(&t, ms), best = -99999, base = evaluate(&t);
    Move bm = 0;
    for (int i = 0; i < n; i++) {
        Undo u;
        if (!pos_make(&t, ms[i], &u)) continue;
        int sc = -quick(&t, 1, -99999, 99999);
        pos_unmake(&t, &u);
        if (sc > best) best = sc, bm = ms[i];
    }
    if (!bm || best - base < 90) return; // less than about a pawn isnt a threat, its noise
    static const char *pn[] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
    char san[16];
    Pos s2 = t;
    move_to_san(&s2, bm, san);
    threat_mv = bm;
    int vic = t.sq[M_TO(bm)];
    if (best >= 20000) { // quick() gives 30000 for mate
        snprintf(threat_txt, sizeof(threat_txt), "they threaten mate with %s", san);
        return;
    }
    if (vic != EMPTY) {
        snprintf(threat_txt, sizeof(threat_txt), "they can take your %s with %s", pn[vic % 6], san);
        return;
    }
    Undo u;
    pos_make(&t, bm, &u);
    u64 hit = piece_hits(&t, M_TO(bm));
    int seen[2], ns = 0;
    for (int ty = KING; ty >= KNIGHT && ns < 2; ty--) // biggest first, forking two pawns isnt news
        for (u64 b = t.bb[me * 6 + ty] & hit; b && ns < 2; b &= b - 1) seen[ns++] = ty;
    pos_unmake(&t, &u);
    if (ns == 2)
        snprintf(threat_txt, sizeof(threat_txt), "%s would fork your %s and %s", san, pn[seen[0]], pn[seen[1]]);
    else
        snprintf(threat_txt, sizeof(threat_txt), "watch out for %s", san);
}

static int hint_lv;
static u64 hint_key;

static void engine_bin(char *out, size_t n)
{
    char exe[4096];
    exe_dir(exe, sizeof(exe));
    snprintf(out, n, "%.1000s/open-chease-engine" EXE, exe); // mine as a uci exe, it sits next to us in bin/
}

static void keep_second(void)
{
    if (!cfg.second) saved_second[0] = 0;
    else if (cfg.second == 1) snprintf(saved_second, sizeof(saved_second), "builtin");
    else snprintf(saved_second, sizeof(saved_second), "%.1000s", engine_paths[cfg.second - 1]);
}

static void sync_seconds(int my_turn)
{
    char p0[1100] = "", p1[1100] = "";
    if (cfg.second == 1) engine_bin(p0, sizeof(p0));
    else if (cfg.second > 1 && cfg.second - 1 < nengines) snprintf(p0, sizeof(p0), "%s", engine_paths[cfg.second - 1]);
    if (cfg.mode && cfg.level) {
        if (cfg.engine > 0) snprintf(p1, sizeof(p1), "%s", engine_paths[cfg.engine]);
        else engine_bin(p1, sizeof(p1));
    }
    static char was0[1100] = "-", was1[1100] = "-";
    static int welo = -1, wms = -1;
    int ms = think_secs[cfg.think] * 1000 > 3000 ? 3000 : think_secs[cfg.think] * 1000; // its a second opinion, 3s is plenty
    if (strcmp(was0, p0) || wms != ms) second_set(0, p0, 0, ms), snprintf(was0, sizeof(was0), "%s", p0), wms = ms;
    if (strcmp(was1, p1) || welo != level_elo[cfg.level]) {
        welo = level_elo[cfg.level];
        second_set(1, p1, welo, 1000);
        snprintf(was1, sizeof(was1), "%s", p1);
    }
    if (state != TRACKING) return;
    if (*p0 && (my_turn || cfg.theirtoo)) second_ask(0, &tracker.pos);
    if (*p1 && my_turn) second_ask(1, &tracker.pos);
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
    if (hushed()) return;
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
        uint32_t hc = faded(pick_color(cfg.hcolor, 0xD8FF8A1F) | 0xD8000000);
        for (int sq = 0; sq < 64; sq++) {
            int pc = tracker.pos.sq[sq];
            if (pc == EMPTY || pc % 6 == KING) continue;
            int mine = pc / 6 == me;
            if ((mine && !(cfg.hanging & 1)) || (!mine && !(cfg.hanging & 2))) continue; // 1 mine, 2 theirs, 3 both
            if (!pos_is_attacked(&tracker.pos, sq, pc / 6 ^ 1) || pos_is_attacked(&tracker.pos, sq, pc / 6)) continue;
            sq_display(sq, bottom, &r, &col);
            mark_square(c, (Rect){bl.x + col * s, bl.y + r * s, s, s}, hstyle_mark[cfg.hstyle], hc);
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
    panel_bg(c, g, 10, faded(TH->panel));
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
    tip_wait = tip_shown = 0;
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
        strncmp(engine_labels[cfg.engine], an.engine_base, 31)) { // swap the file name for what the engine calls itself
        snprintf(engine_labels[cfg.engine], sizeof(engine_labels[0]), "%.31s", an.engine_base);
        remember_name(engine_paths[cfg.engine], an.engine_base);
    }
    // one canvas per monitor, only the first real one adds hitboxes
    int lead = 0;
    while (lead < ov->ncanvas - 1 && !ov->canvas(ov, lead)) lead++;
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
            if (i == lead) {
                add_hit(bw, ACT_WHITE);
                add_hit(bb, ACT_BLACK);
                add_hit(bx, ACT_CLOSE);
            }
        } else {
            int wb = tracker.user_color == WHITE;
            int bottom = tracker.white_bottom;
            int my_turn = tracker.pos.side == tracker.user_color;
            if (i == lead) {
                if (tracker.pos.key != seen_key) seen_key = tracker.pos.key, pos_since = now_ms();
                rate_moves(&an, have_an, my_turn);
                helper_track(&an, have_an, my_turn);
                sync_seconds(my_turn);
                if (cfg.mode && cfg.threats) find_threat();
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
            if (!nlegal && i == lead && counted_key != tracker.pos.key) { // redraw runs a lot, count the game once
                counted_key = tracker.pos.key;
                if (!pos_in_check(&tracker.pos, tracker.pos.side)) draws++;
                else if (my_turn) losses++;
                else wins++;
            }
            char alert[64] = "", mvs[96] = "", tips[3][100];
            int ntips = 0;
            uint32_t tipc[3];
            Opinion o0, o1;
            int have0 = second_get(0, &o0) && o0.key == tracker.pos.key && o0.best;
            int have1 = second_get(1, &o1) && o1.key == tracker.pos.key && o1.best;
            if (cfg.mode && nlegal) {
                danger_text(alert, sizeof(alert), &an, have_an, my_turn);
                if (cfg.movelist) moves_text(mvs, sizeof(mvs), 8);
                uint32_t thc = faded((pick_color(cfg.thcolor, 0xC8FF4D4D) & 0xFFFFFF) | 0xB0000000);
                double thk = arrow_tk(cfg.ththick) * 0.75;
                if (cfg.danger == 2 && danger_mv && !(my_turn && cfg.threats == 2 && danger_mv == threat_mv))
                    draw_move(c, bl, danger_mv, bottom, thc, cfg.tharrow, thk, 0, 0, NULL);
                if (my_turn && cfg.threats && threat_mv) {
                    if (cfg.threats == 2)
                        draw_move(c, bl, threat_mv, bottom, thc, cfg.tharrow, thk, 0, 0, NULL);
                    snprintf(tips[ntips], 100, "%s", threat_txt), tipc[ntips++] = 0xFFFF7A5A;
                }
                if (my_turn && cfg.hints && hint_key == tracker.pos.key && hint_lv && have_an && an.best) {
                    static const char *pn[] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
                    char hs[16];
                    Pos t2 = tracker.pos;
                    move_to_san(&t2, an.best, hs);
                    uint32_t hc = pick_color(cfg.mcolor, TH->arrow);
                    int f = M_FROM(an.best), to = M_TO(an.best);
                    if (hint_lv == 1)
                        snprintf(tips[ntips], 100, "hint: look at your %s", pn[tracker.pos.sq[f] % 6]);
                    else if (hint_lv == 2)
                        snprintf(tips[ntips], 100, "hint: your %s goes to %c%c", pn[tracker.pos.sq[f] % 6], 'a' + (to & 7), '1' + (to >> 3));
                    else
                        snprintf(tips[ntips], 100, "hint: %s", hs);
                    tipc[ntips++] = TH->title;
                    draw_move(c, bl, an.best, bottom, hc, hint_lv == 3 ? (cfg.arrow ? cfg.arrow : 1) : 0, arrow_tk(cfg.thick),
                              1, hint_lv >= 2 ? 3 : 0, NULL); // outline + corners, a ring or dot in the middle hides the piece from the vision
                }
                if (my_turn && cfg.level && have1) {
                    char ls2[16];
                    Pos t2 = tracker.pos;
                    move_to_san(&t2, o1.best, ls2);
                    snprintf(tips[ntips], 100, "a %s would play %s", level_names[cfg.level], ls2);
                    tipc[ntips++] = TH->title_off;
                }
                if (cfg.mode == 1 && ntips < 3 && have0 && have_an && an.best && an.depth >= 10 && o0.best != an.best && (my_turn || cfg.theirtoo))
                    snprintf(tips[ntips], 100, "the engines dont agree on this one"), tipc[ntips++] = TH->title_off;
            }
            if (cfg.mode != 1 && ntips < 3 && nlegal && have0 && have_an && an.best && (my_turn || cfg.theirtoo)) {
                char ss[16], osc[24] = "";
                Pos t2 = tracker.pos;
                move_to_san(&t2, o0.best, ss);
                Analysis fake = an;
                fake.score = o0.score, fake.mate = o0.mate;
                format_score(&fake, my_turn, osc, sizeof(osc));
                int hide = my_turn && (cfg.hint || cfg.chaos || now_ms() - pos_since < delay_secs[cfg.delay] * 1000L);
                if (o0.best == an.best)
                    snprintf(tips[ntips], 100, "%s agrees", o0.name);
                else if (hide)
                    snprintf(tips[ntips], 100, "%s has a different idea", o0.name);
                else {
                    snprintf(tips[ntips], 100, "%s says %s (%s)", o0.name, ss, osc);
                    if (my_turn)
                        draw_move(c, bl, o0.best, bottom, faded(0xB01FE0E0), 6, arrow_tk(cfg.thick) * 0.7, 0, 0, NULL);
                }
                tipc[ntips++] = TH->title_off;
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
                    if (nwhist >= 2 && whist_key == tracker.pos.key && fabs(whist[nwhist - 1] - whist[nwhist - 2]) >= 3) { // +1s are just noise
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
                int waiting = now_ms() - pos_since < delay_secs[cfg.delay] * 1000L; // instant moves every turn look like a bot
                if (pick && !waiting) {
                    char psan[16];
                    Pos t2 = tracker.pos;
                    move_to_san(&t2, pick, psan);
                    static Move shown;
                    if (pick != shown && i == lead) shown = pick, pick_t = now_ms();
                    uint32_t mc = pick_color(cfg.mcolor, TH->arrow);
                    mc = (mc & 0xFFFFFF) | (uint32_t)((mc >> 24) * ease(pick_t, 220)) << 24;
                    static const char *names[] = {"Best move", "Close enough", "Chaos pick", "Coin says", "Random move",
                                                  "Worst move", "Blunder time", "Human 800", "Human 1200", "Human 1600",
                                                  "Human 2000", "Human 2400"};
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
                            draw_move(c, bl, an.alt[k], bottom, (mc & 0xFFFFFF) | ((mc >> 25) << 24), cfg.arrow ? cfg.arrow : 1, // half alpha
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
            if (now_ms() < badge_until && rate_move && !hushed()) {
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
            for (int k = ntips - 1; k >= 0; k--) {
                char tmp[320];
                snprintf(tmp, sizeof(tmp), "%s  |  %s", tips[k], line2);
                snprintf(line2, sizeof(line2), "%s", tmp);
            }
            if (cfg.mode != 1 && *extra && nlegal) {
                size_t l = strlen(line2);
                snprintf(line2 + l, sizeof(line2) - l, "  |  %s", extra);
                for (char *q = line2 + l; (q = strstr(q, "   ")); q++) q[0] = ',', q[1] = ' ', memmove(q + 2, q + 3, strlen(q + 3) + 1);
            }
            int t1big = cfg.size > 0, t1zoom = cfg.size == 2 ? 2 : 1, t2big = cfg.size == 2;
            double bs = cfg.size == 0 ? 22 : (cfg.size == 1 ? 26 : 34);
            Rect p, gb, mb, bx, hb = {0, 0, 0, 0};
            int show_hint = cfg.mode == 1 && cfg.hints && my_turn && nlegal, nbt = 3 + show_hint;
            double vw = cfg.size == 0 ? 190 : (cfg.size == 1 ? 220 : 270);
            int side = (cfg.spot == 2 && bl.x - vw - 8 >= outr.x + 4) ||
                       (cfg.spot == 3 && bl.x + bl.w + 8 + vw <= outr.x + outr.w - 4);
            if (side) {
                const char *ls[32];
                int big[32], zoom[32], nl = 0;
                uint32_t lc[32];
                char movebuf[64], scbuf[48], theirbuf[64], st[3][128];
                int plain = cfg.mode == 1 || cfg.chaos || cfg.hint || cfg.trash || now_ms() - pos_since < delay_secs[cfg.delay] * 1000L;
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
                if (cfg.mode != 1 && *extra && nlegal) {
                    snprintf(exbuf, sizeof(exbuf), "%s", extra);
                    for (char *q = exbuf; q && *q && nl < 9;) {
                        char *cut = strstr(q, "   ");
                        if (cut) *cut = 0;
                        ls[nl] = q, big[nl] = 0, zoom[nl] = 1, lc[nl++] = TH->title_off;
                        q = cut ? cut + 3 : NULL;
                    }
                }
                if (*alert) ls[nl] = alert, big[nl] = 0, zoom[nl] = 1, lc[nl++] = 0xFFFF7A5A;
                for (int k = 0; k < ntips && nl < 15; k++) ls[nl] = tips[k], big[nl] = 0, zoom[nl] = 1, lc[nl++] = tipc[k];
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
                panel_bg(c, p, 12, faded(TH->panel));
                double y = p.y + bs + 14;
                for (int k = 0; k < nl; k++) {
                    set_text_zoom(zoom[k]);
                    draw_text(c, p.x + 12, y, big[k] || zoom[k] > 1 ? ls[k] : fit_text(c, ls[k], p.w - 24), big[k], lc[k]);
                    set_text_zoom(1);
                    y += (big[k] ? 24 : 19) * zoom[k] + 2;
                }
                hb = (Rect){p.x + p.w - 4 * (bs + 4) - 4, p.y + 8, bs, bs};
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
                double room = outr.w - 8 - 28 - nbt * (bs + 4);
                set_text_zoom(t1zoom);
                cut_to(line1, room, t1big);
                double w1 = text_width(line1, t1big);
                set_text_zoom(1);
                cut_to(line2, room, t2big);
                double w = fmax(w1, text_width(line2, t2big)) + 28 + nbt * (bs + 4);
                if (w < 300) w = 300;
                p = panel_rect(&bl, w, ph, &outr);
                panel_bg(c, p, 12, faded(TH->panel));
                set_text_zoom(t1zoom);
                draw_text(c, p.x + 14, p.y + 6, line1, t1big, my_turn ? TH->title : TH->title_off);
                set_text_zoom(1);
                draw_text(c, p.x + 14, p.y + y2, line2, t2big, TH->sub);
                double by = p.y + (ph - bs) / 2;
                hb = (Rect){p.x + p.w - 4 * (bs + 4) - 4, by, bs, bs};
                gb = (Rect){p.x + p.w - 3 * (bs + 4) - 4, by, bs, bs};
                mb = (Rect){p.x + p.w - 2 * (bs + 4) - 4, by, bs, bs};
                bx = (Rect){p.x + p.w - (bs + 4) - 4, by, bs, bs};
            }
            draw_wgraph(c, p, bl, outr);
            if (show_hint) {
                int lv = hint_key == tracker.pos.key ? hint_lv : 0;
                draw_round_rect(c, hb, 5, lv ? (TH->btn | 0xFF000000) + 0x00202020 : TH->btn);
                char q[4] = "?";
                if (lv) q[0] = (char)('0' + lv);
                double tw = text_lw(c, q, 1);
                draw_text(c, hb.x + (hb.w - tw) / 2, hb.y + (hb.h - 22) / 2, q, 1, TH->btn_fg);
                if (i == lead) add_hit(hb, ACT_HINT);
            }
            draw_gear(c, gb, settings_open);
            draw_dots(c, mb, menu_open);
            draw_close(c, bx);
            if (i == lead) {
                add_hit(gb, ACT_SETTINGS);
                add_hit(mb, ACT_MENU);
                add_hit(bx, ACT_STOP);
            }
            draw_extras(c, bl, p, outr, i == lead);
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
            ignores[i].frame_rect = (Rect){board.x - board.s, board.y - board.s, board.s * 10, board.s * 10}; // a square of slack each side
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
            g->until_ms = now_ms() + 3000; // closed boards stay closed while theyre still on screen
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

static int hexv(const char *h, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        char ch = h[i];
        int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
        if (d < 0) return -1;
        v = v * 16 + d;
    }
    return v;
}

static void profile_save(const Board *b)
{
    char dir[1100], path[1200];
    snprintf(dir, sizeof(dir), "%s/pieces", data_dir);
    make_dir(dir);
    snprintf(path, sizeof(path), "%s/%02x%02x%02x-%02x%02x%02x.bin", dir, b->light[0], b->light[1], b->light[2], b->dark[0],
             b->dark[1], b->dark[2]);
    templates_save(&tpl, path);
}

static int profile_find(const Board *b, char *path, size_t size)
{
    char dir[1100];
    snprintf(dir, sizeof(dir), "%s/pieces", data_dir);
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    char best[300] = "";
    int bd = 40; // further off than this is some other site, keep what we have
    while ((e = readdir(d))) {
        if (strlen(e->d_name) != 17 || strcmp(e->d_name + 13, ".bin") || e->d_name[6] != '-') continue; // rrggbb-rrggbb.bin from profile_save
        int dist = 0;
        for (int k = 0; k < 3 && dist >= 0; k++) {
            int l = hexv(e->d_name + k * 2, 2), dk = hexv(e->d_name + 7 + k * 2, 2);
            if (l < 0 || dk < 0) dist = -1;
            else dist += abs(l - b->light[k]) + abs(dk - b->dark[k]);
        }
        if (dist >= 0 && dist < bd) bd = dist, snprintf(best, sizeof(best), "%s", e->d_name);
    }
    closedir(d);
    if (!*best) return 0;
    snprintf(path, size, "%s/%s", dir, best);
    return 1;
}

static int profile_load(const Board *b)
{
    char path[1500];
    if (!profile_find(b, path, sizeof(path))) return 0;
    Templates t = tpl;
    if (!templates_load(&t, path)) return 0;
    tpl = t;
    if (verbose) fprintf(stderr, "using the piece set saved for this board (%s)\n", path);
    return 1;
}

static void forget_pieces(void)
{
    char path[1500];
    templates_builtin(&tpl);
    learned_here = 0;
    remove(tpl_path);
    if (state != SCANNING && profile_find(&board, path, sizeof(path))) remove(path);
    set_note("back to the built in pieces. start a new game or hit Teach to learn them again");
}

static int teach_pieces(void)
{
    Output *o = &cap->out[board.out];
    MaskCtx mc = {o};
    BoardRead *r = malloc(sizeof(BoardRead));
    vision_read(&o->frame, &board, tracker.white_bottom, tpl.color_thr, r, masked_cb, &mc);
    if (r->nhidden && (!teach_hush || hushed())) { // same as rescan, maybe its our own marks in the way
        if (!teach_hush) teach_hush = 1, hush_until = now_ms() + 1500;
        free(r);
        return 1;
    }
    int same = 1;
    for (int i = 0; i < 64 && same; i++) {
        int pc = tracker.pos.sq[i], want = pc == EMPTY ? OBS_EMPTY : (pc / 6 == WHITE ? OBS_WHITE : OBS_BLACK);
        if (r->obs[i] != want) same = 0;
    }
    if (!r->nhidden && !same && ++teach_tries < 20) { // a move landed, let the tracker catch up before it learns from it
        free(r);
        return 0;
    }
    if (r->nhidden) {
        set_note("something is covering the board, close popups and menus then hit Teach again");
    } else if (!same) {
        set_note("some squares look wrong, fix them with ... > Fix pieces then hit Teach again");
    } else {
        templates_learn(&tpl, r, &tracker.pos);
        learned_here = 1;
        templates_save(&tpl, tpl_path);
        profile_save(&board);
        set_note("got it, learned your pieces. itll know them every time it sees this board");
        if (verbose) fprintf(stderr, "learned piece set from the tracked position\n");
    }
    want_teach = teach_hush = teach_tries = 0;
    free(r);
    return 1;
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
            want_rescan = 0;
        } else if (!rescan_hush || hushed()) { // maybe its just our own arrows in the way, clear them and look again
            if (!rescan_hush) rescan_hush = 1, hush_until = now_ms() + 1500;
            snprintf(tracker.status, sizeof(tracker.status), "rescanning...");
            free(r);
            return;
        } else {
            snprintf(tracker.status, sizeof(tracker.status), "cant rescan, board is covered");
            want_rescan = 0;
        }
        rescan_hush = 0;
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
        if (sc < -2 || (tracker.flip_pending && sc < 0)) { // only flip on a clear read unless the tracker already thinks it rotated
            tracker.white_bottom ^= 1;
            vision_read(&o->frame, &board, tracker.white_bottom, tpl.color_thr, r, masked_cb, &mc);
        }
        tracker.flip_pending = 0;
    }
    if (first == -1) { // already resynced up there
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
        static long last_hush;
        if (r->nhidden && tracker.mismatch_ticks >= 4 && now_ms() - last_hush > 8000) { // stuck, maybe behind our own marks
            last_hush = now_ms();
            hush_until = last_hush + 1500;
        }
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
            profile_save(&board);
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
        replace_file(tmp, path); // whatever reads these never sees half a frame
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

static u64 frame_print(const Frame *f) // sparse sample, an unchanged screen doesnt need another look
{
    u64 h = 1469598103934665603ULL;
    for (int y = 3; y < f->h; y += 23)
        for (int x = 5; x < f->w; x += 29) {
            const uint8_t *p = f->px + (size_t)y * f->stride + (size_t)x * 4;
            h = (h ^ (p[0] | (u64)p[1] << 8 | (u64)p[2] << 16)) * 1099511628211ULL;
        }
    return h ^ (u64)f->w << 40 ^ (u64)f->h;
}

static int overlay_empty(void)
{
    for (int i = 0; i < ov->ncanvas; i++) {
        Canvas *c = ov->canvas(ov, i);
        if (c && c->dx1 > c->dx0) return 0;
    }
    return 1;
}

static void lose_board(void)
{
    want_rescan = rescan_hush = 0;
    want_teach = teach_hush = teach_tries = 0;
    lib_open = 0; // it holds the keyboard
    if (verbose) fprintf(stderr, "board lost\n");
    if (state == TRACKING) { // scrolling or a quick tab switch shouldnt throw the game away
        saved_tracker = tracker;
        saved_board = board;
        saved_at = now_ms();
        have_saved = 1;
        analyzer_stop();
    }
    state = SCANNING;
}

static u64 prints[MAX_OUTPUTS];
static int seen[MAX_OUTPUTS];

static void tick(void)
{
    check_download();
    check_switch();
    if (!cap->capture(cap)) {
        if (replay_ticks >= 0) quit = 1;
        if (state == SCANNING) scan_log(0, NULL);
        return;
    }
    dump_frame();
    log_analysis();
    if (state == SCANNING) {
        static int scores[MAX_OUTPUTS];
        int quiet = overlay_empty(); // nothing of ours on screen, so skip asking about every pixel
        for (int i = 0; i < cap->nout; i++) refresh_ignores(i);
        for (int i = 0; i < cap->nout; i++) {
            Output *o = &cap->out[i];
            if (!o->have_frame) continue;
            Rect ig[16];
            int nig = 0;
            for (int k = 0; k < 16; k++)
                if (ignores[k].active && ignores[k].out == i) ig[nig++] = ignores[k].frame_rect;
            u64 fp = frame_print(&o->frame) ^ (u64)nig * 0x9E3779B97F4A7C15ULL;
            if (fp == prints[i] && seen[i] >= 4) continue;
            MaskCtx mc = {o};
            Board b = {0};
            int hit = vision_find_board(&o->frame, &b, ig, nig, quiet ? NULL : masked_cb, &mc);
            scores[i] = b.score;
            if (!hit) seen[i] = fp == prints[i] ? seen[i] + 1 : 1, prints[i] = fp; // 4 in a row, so the slow blurred pass had a go at it too
            if (hit && !is_ignored(i, &b)) {
                b.out = i;
                if (fabs(b.s - board.s) > 1 || b.out != board.out || !colors_close(b.light, board.light)) { // not the board we had, pieces get relearned
                    learned_here = 0;
                    if (!colors_close(b.light, board.light) || !colors_close(b.dark, board.dark)) profile_load(&b);
                }
                board = b;
                miss_ticks = 0;
                if (verbose)
                    fprintf(stderr, "board found on %s at %.0f,%.0f square %.1fpx score %d, popup goes at %.0f,%.0f\n",
                            o->name, b.x, b.y, b.s, b.score, o->logical.x + b.x * o->logical.w / o->frame.w,
                            o->logical.y + b.y * o->logical.h / o->frame.h);
                if (have_saved && now_ms() - saved_at < 15000 && colors_close(saved_board.light, b.light) && colors_close(saved_board.dark, b.dark) &&
                    fabs(saved_board.s - b.s) < b.s * 0.15) { // same board back soon, pick up where we were. zoom can change the size a bit
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
        else memset(prints, 0, sizeof(prints)), memset(seen, 0, sizeof(seen));
        return;
    }
    memset(prints, 0, sizeof(prints));
    memset(seen, 0, sizeof(seen));
    Output *o = &cap->out[board.out];
    static int was_paused, no_frame;
    if (!o->have_frame) {
        if (++no_frame >= 10 && (board.out >= cap->nout || o->logical.w <= 0)) { // monitor unplugged, not just a late frame
            no_frame = 0;
            lose_board();
        }
        return;
    }
    no_frame = 0;
    if (state == TRACKING && ui_over_board) { // cant see under our own menu
        snprintf(note, sizeof(note), "paused, menu open");
        note_until = now_ms() + 1200;
        was_paused = 1;
        return;
    }
    MaskCtx mc = {o};
    if (!vision_verify(&o->frame, &board, masked_cb, &mc)) {
        if (++miss_ticks >= 3) lose_board(); // one bad frame from an animation or popup isnt a lost board
        return;
    }
    miss_ticks = 0;
    if (state == ASKING) return;
    if (want_teach && state == TRACKING && teach_pieces()) return;
    read_and_track(want_rescan ? 3 : was_paused ? 2 : 0);
    was_paused = 0;
}

static void set_note(const char *msg)
{
    snprintf(note, sizeof(note), "%s", msg);
    note_until = now_ms() + (strlen(msg) > 60 ? 7000 : 4000); // long ones need time to read
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
    tracker.last_move = 0;
    if (!p.bb[KING] || !p.bb[6 + KING] || __builtin_popcountll(p.bb[KING]) > 1 || __builtin_popcountll(p.bb[6 + KING]) > 1) {
        set_note("each side needs exactly one king");
        for (int sq = 0; sq < 64; sq++) // dont keep re-applying a fix that broke the kings
            if ((tracker.locked >> sq & 1) && p.sq[sq] != tracker.pos.sq[sq]) tracker.locked &= ~(1ULL << sq);
        tracker.pos = p;
        return;
    }
    if (pos_in_check(&p, p.side ^ 1)) set_note("the side that just moved cant be in check");
    else if (action == ACT_PIECE) set_note("fixed it. once theyre all right, Gear > Vision > Teach these pieces");
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
    if (!full_path(a, ra) || !full_path(b, rb)) return !strcmp(a, b);
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
    char base[25];
    snprintf(base, sizeof(base), "%s", engine_labels[nengines]);
    for (int i = 0, k = 2; i < nengines; i++) // the stockfish get.sh grabbed and the library one both call themselves Stockfish 19
        if (!strcmp(engine_labels[i], engine_labels[nengines])) {
            snprintf(engine_labels[nengines], sizeof(engine_labels[0]), "%s (%d)", base, k++ % 100);
            i = -1;
        }
    snprintf(engine_paths[nengines], sizeof(engine_paths[0]), "%.1000s", path);
    engine_names[nengines] = engine_labels[nengines];
    nengines++;
}

static int in_path(const char *name, char *out, size_t n)
{
    const char *pth = getenv("PATH");
    char buf[4096];
#ifdef _WIN32
    snprintf(buf, sizeof(buf), "%s", pth ? pth : "");
    const char *sep = ";";
#else
    snprintf(buf, sizeof(buf), "%s:/usr/games:/opt/homebrew/bin:/usr/local/bin", pth ? pth : "/usr/bin"); // debian puts stockfish in /usr/games, brew in the other two
    const char *sep = ":";
#endif
    char *sv;
    for (char *d = strtok_r(buf, sep, &sv); d; d = strtok_r(NULL, sep, &sv)) {
        snprintf(out, n, "%s/%s" EXE, d, name);
        if (is_exec(out)) return 1;
    }
    return 0;
}

static void scan_engines(const char *extra)
{
    static char keep[1024];
    char using[1024] = "";
    if (cfg.engine > 0 && cfg.engine < nengines) snprintf(using, sizeof(using), "%s", engine_paths[cfg.engine]);
    if (extra) snprintf(keep, sizeof(keep), "%s", extra);
    extra = keep;
    nengines = 0;
    lib_del = -1;
    add_engine("Built-in", "");
    char p[2048];
    int sf = 0;
    snprintf(p, sizeof(p), "%s/stockfish", data_dir);
    DIR *d = opendir(p);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && !sf) { // first stockfish in there wins
            char f[4096];
            snprintf(f, sizeof(f), "%s/%s", p, e->d_name);
            if (!strncmp(e->d_name, "stockfish", 9) && is_exec(f)) add_engine("Stockfish", f), sf = 1;
        }
        closedir(d);
    }
    if (!sf && in_path("stockfish", p, sizeof(p))) add_engine("Stockfish", p), sf = 1;
    snprintf(p, sizeof(p), "%s/engines", data_dir); // drop any uci engine in here and it shows up
    make_dir(p);
    engine_list(p, add_engine);
    if (extra && *extra && is_exec(extra)) {
        const char *b = strrchr(extra, '/'), *b2 = strrchr(extra, '\\');
        if (b2 > b) b = b2;
        add_engine(b ? b + 1 : extra, extra);
    }
    int ns = nengines < 24 ? nengines : 24; // second_names has 25 slots and "Off" is one
    for (int i = 0; i < ns; i++) second_names[i + 1] = engine_labels[i];
    for (int i = 0; i < NSET; i++) {
        if (sets[i].v == &cfg.engine) sets[i].n = nengines;
        if (sets[i].v == &cfg.second) sets[i].n = ns + 1;
    }
    if (*using) cfg.engine = engine_index(using);
    cfg.second = 0;
    if (!strcmp(saved_second, "builtin")) cfg.second = 1;
    else if (*saved_second)
        for (int i = 1; i < ns; i++)
            if (same_file(engine_paths[i], saved_second)) cfg.second = i + 1;
}

static void cached_name(const char *path, char *out, size_t n)
{
    char f[1100], line[1200];
    snprintf(f, sizeof(f), "%s/engine-names", data_dir);
    FILE *fp = fopen(f, "r");
    if (!fp) return;
    size_t pl = strlen(path);
    while (fgets(line, sizeof(line), fp)) // remember_name only appends, so the last line wins
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
    if (!*path || !strncmp(have, name, 31)) return;
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
    char slug[48]; // has to match DlJob.slug
    lib_slug(lib[i].name, slug, sizeof(slug));
    engine_path_for(data_dir, slug, out, n);
}

static char lc0_bin[1024];

#ifdef __linux__
static int nvidia_gpu(void)
{
    if (access("/proc/driver/nvidia/version", R_OK)) return 0;
    DIR *d = opendir("/sys/class/drm");
    struct dirent *e;
    int found = 0;
    while (d && (e = readdir(d)) && !found) {
        if (strncmp(e->d_name, "card", 4) || strchr(e->d_name, '-')) continue; // card0-HDMI-A-1 and co are ports
        char p[600], v[16] = "";
        snprintf(p, sizeof(p), "/sys/class/drm/%.200s/device/vendor", e->d_name);
        FILE *f = fopen(p, "r");
        if (f) {
            if (fgets(v, sizeof(v), f) && !strncmp(v, "0x10de", 6)) found = 1; // nvidias pci vendor id
            fclose(f);
        }
    }
    if (d) closedir(d);
    return found;
}

static int find_lc0(char *out, size_t n)
{
    if (in_path("lc0", out, n)) return 1;
    const char *home = getenv("HOME");
    const char *spots[] = {"/home/linuxbrew/.linuxbrew/bin/lc0", "%s/.linuxbrew/bin/lc0", "%s/lc0/build/release/lc0"};
    for (int i = 0; i < 3; i++) {
        if (strchr(spots[i], '%') && !home) continue;
        snprintf(out, n, spots[i], home);
        if (is_exec(out)) return 1;
    }
    return 0;
}
#else
static int nvidia_gpu(void) { return 0; } // lc0 gets its own entries on the other systems
static int find_lc0(char *out, size_t n) { return 0; }
#endif

static void load_library(void)
{
    char path[1100], *text = NULL;
    snprintf(path, sizeof(path), "%s/" LIB_LIST, data_dir);
    FILE *f = fopen(path, "r");
    if (f) {
        text = calloc(1, 65536);
        size_t got = fread(text, 1, 65535, f);
        text[got] = 0;
        fclose(f);
    }
    const char *src = text && strchr(text, '|') ? text : builtin_library; // no list yet or a junk one, use the copy baked in
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
        snprintf(lib_labels[nlib], sizeof(lib_labels[0]), "%s%s", e->name, is_exec(out) ? " (got it)" : "");
        lib_names[nlib] = lib_labels[nlib];
        nlib++;
    }
    free(text);
    static int gpu = -1;
    if (gpu < 0) gpu = nvidia_gpu() && find_lc0(lc0_bin, sizeof(lc0_bin)); // we only fetch the net, lc0 itself has to be installed already
    if (gpu && nlib < MAX_LIB) {
        LibEngine *e = &lib[nlib];
        snprintf(e->name, sizeof(e->name), "Lc0 (GPU)");
        snprintf(e->url, sizeof(e->url), "https://storage.lczero.org/files/networks-contrib/t1-256x10-distilled-swa-2432500.pb.gz");
        snprintf(e->kind, sizeof(e->kind), "lc0");
        snprintf(e->tags, sizeof(e->tags), "gpu");
        snprintf(e->about, sizeof(e->about), "leela, a neural net engine. runs your lc0 on the nvidia card, this grabs its net");
        e->mb = 35.4;
        e->tier = 1;
        char out[1200];
        lib_out_path(nlib, out, sizeof(out));
        snprintf(lib_labels[nlib], sizeof(lib_labels[0]), "%s%s", e->name, is_exec(out) ? " (got it)" : "");
        lib_names[nlib] = lib_labels[nlib];
        nlib++;
    }
    for (int i = 0; i < NSET; i++)
        if (sets[i].v == &cfg.library) sets[i].n = nlib ? nlib : 1;
    if (!nlib) lib_names[0] = "nothing to get";
}

static void fetch_library(void)
{
    lib_pid = lists_start(data_dir, LIB_LIST, CPU_LIST);
}

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
static int cpu_level(int *zen12)
{
    static int lvl = -1, zen;
    if (lvl >= 0) return *zen12 = zen, lvl;
    lvl = zen = 0;
    unsigned a, b, c, d, v[3], b7 = 0, c7 = 0, xcr = 0;
    if (!__get_cpuid(0, &a, &v[0], &v[2], &v[1]) || !__get_cpuid(1, &a, &b, &c, &d)) return *zen12 = 0, 0;
    unsigned fam = (a >> 8) & 15;
    if (fam == 15) fam += (a >> 20) & 255;
    if (c & (1u << 27)) { // osxsave, otherwise xgetbv faults
        unsigned hi;
        __asm__ volatile("xgetbv" : "=a"(xcr), "=d"(hi) : "c"(0));
    }
    __get_cpuid_count(7, 0, &a, &b7, &c7, &d);
    int ymm = (xcr & 6) == 6, zmm = (xcr & 0xe6) == 0xe6; // the os has to save the wide registers too
    if ((c & (1u << 20)) && (c & (1u << 23))) lvl = 1;
    if (lvl && ymm && (b7 & (1u << 5))) lvl = 2;
    if (lvl == 2 && (b7 & (1u << 8))) lvl = 3;
    if (lvl == 3 && zmm && (b7 & (1u << 16)) && (b7 & (1u << 17)) && (b7 & (1u << 30)) && (b7 & (1u << 31))) lvl = 4;
    zen = v[0] == 0x68747541 && fam == 0x17; // "Auth"enticAMD, family 17h = zen 1 and 2, pext/pdep are microcoded there and really slow
    return *zen12 = zen, lvl;
}
#else
static int cpu_level(int *zen12) { return *zen12 = 0, 0; }
#endif

static const char *cpu_build(const char *name, char *url, size_t n)
{
    static const char *levels[] = {"v2", "avx2", "bmi2", "avx512"};
    int zen12, have = cpu_level(&zen12), best = 0;
    char path[1100], line[1200];
    if (!*CPU_LIST) return NULL;
    snprintf(path, sizeof(path), "%s/" CPU_LIST, data_dir);
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    size_t nl = strlen(name);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, name, nl) || line[nl] != '|') continue;
        char *lv = line + nl + 1, *u = strchr(lv, '|');
        if (!u) continue;
        *u++ = 0;
        u[strcspn(u, "\r\n")] = 0;
        for (int k = 0; k < 4; k++)
            if (!strcmp(lv, levels[k]) && k + 1 <= have && k + 1 > best && !(zen12 && k == 2) && !strncmp(u, "https://", 8)) { // avx2 beats bmi2 on old zen
                best = k + 1;
                snprintf(url, n, "%s", u);
            }
    }
    fclose(f);
    return best ? levels[best - 1] : NULL;
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
    char url[512], kind[8];
    snprintf(url, sizeof(url), "%s", lib[i].url);
    snprintf(kind, sizeof(kind), "%s", lib[i].kind);
    const char *build = cpu_build(lib[i].name, url, sizeof(url));
    if (build) {
        size_t ul = strcspn(url, "#"); // the #part isnt sent, its which file to pull out of the archive
        snprintf(kind, sizeof(kind), "bin");
        if (ul > 7 && !strncmp(url + ul - 7, ".tar.gz", 7)) snprintf(kind, sizeof(kind), "tgz");
        else if (ul > 4 && !strncmp(url + ul - 4, ".tgz", 4)) snprintf(kind, sizeof(kind), "tgz");
        else if (ul > 4 && !strncmp(url + ul - 4, ".tar", 4)) snprintf(kind, sizeof(kind), "tar");
        else if (ul > 4 && !strncmp(url + ul - 4, ".zip", 4)) snprintf(kind, sizeof(kind), "zip");
    }
    DlJob j;
    memset(&j, 0, sizeof(j));
    char *pick = strchr(url, '#');
    if (pick) *pick++ = 0;
    snprintf(j.url, sizeof(j.url), "%s", url);
    snprintf(j.kind, sizeof(j.kind), "%s", kind);
    snprintf(j.pick, sizeof(j.pick), "%s", pick ? pick : ""); // matched on the end of the path, releases nest it in a versioned folder
    snprintf(j.out, sizeof(j.out), "%s", out);
    lib_slug(lib[i].name, j.slug, sizeof(j.slug));
    snprintf(j.data, sizeof(j.data), "%s", data_dir);
    snprintf(j.helper, sizeof(j.helper), "%s", lc0_bin);
    dl_pid = dl_start(&j);
    if (!dl_pid) {
        set_note("couldnt start the download");
        return;
    }
    char msg[96];
    if (build) snprintf(msg, sizeof(msg), "downloading %s (%s build for your cpu)...", lib[i].name, build);
    else snprintf(msg, sizeof(msg), "downloading %s...", lib[i].name);
    set_note(msg);
}

static int switch_ticket;
static long switch_since;

static void choose_engine(int idx)
{
    if (idx < 0 || idx >= nengines) return;
    cfg.engine = idx;
    switch_ticket = analyzer_switch_async(*engine_paths[idx] ? engine_paths[idx] : NULL);
    switch_since = now_ms();
    if (switch_ticket <= 0 && *engine_paths[idx]) {
        set_note("that engine wont start on this pc, using mine");
        cfg.engine = 0;
    }
    if (switch_ticket <= 0) switch_ticket = 0;
    snprintf(saved_engine, sizeof(saved_engine), "%.1000s", engine_paths[cfg.engine]);
    have_saved_engine = 1;
    save_cfg();
    if (state == TRACKING) analyzer_set_position(&tracker.pos); // might be paused
}

// lc0 can take ages to load its net, dont freeze clicks waiting
static void check_switch(void)
{
    if (!switch_ticket) return;
    int r = analyzer_switch_done((unsigned)switch_ticket);
    if (r < 0 && now_ms() - switch_since < 25000) return;
    switch_ticket = 0;
    if (r == 1) {
        if (state == TRACKING) analyzer_set_position(&tracker.pos);
        return;
    }
    set_note("that engine wont start on this pc, using mine");
    cfg.engine = 0;
    analyzer_switch_async(NULL);
    snprintf(saved_engine, sizeof(saved_engine), "%s", "");
    save_cfg();
    if (state == TRACKING) analyzer_set_position(&tracker.pos);
}

static void check_download(void)
{
    int ok;
    if (lib_pid && job_done(lib_pid, &ok)) lib_pid = 0, lib_reload = 1;
    if (lib_reload && !dl_pid) lib_reload = 0, load_library(); // not mid download, the indexes would shift
    if (!dl_pid || !job_done(dl_pid, &ok)) return;
    dl_pid = 0;
    if (!ok) {
        set_note("download failed, check your internet");
        return;
    }
    load_library();
    for (int i = 0; i < nlib; i++)
        if (!strcmp(lib[i].name, dl_name)) lib_out_path(i, dl_out, sizeof(dl_out)); // windows only knows which exe it is after unpacking
    scan_engines(NULL);
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
    second_quit();
    job_kill(dl_pid);
    job_kill(lib_pid);
    usleep(150000); // let the overlay clear and the engines die before we exec
    relaunch_self(g_argv, getenv("OPEN_CHEASE_HOME"));
}

static void update_and_relaunch(void)
{
    const char *mode = getenv("OPEN_CHEASE_UPDATE"), *home = getenv("OPEN_CHEASE_HOME");
    if (!mode || !home) {
        set_note("cant update this copy, use a git clone or the one line install from the readme");
        return;
    }
    char getsh[1100];
#ifdef _WIN32
    // windows wont let anyone replace an exe thats running, so the script waits for us to quit, updates, then starts us again
    snprintf(getsh, sizeof(getsh), "%s\\%s", home, strcmp(mode, "git") ? "get.ps1" : "run.ps1");
    char *ps[40] = {"powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", getsh, "-Update"};
    int np = 7;
    for (int i = 1; g_argv[i] && np < 39; i++) ps[np++] = g_argv[i];
    ps[np] = NULL;
    if (spawn_detached(ps)) {
        set_note("couldnt start the updater");
        return;
    }
    state = SCANNING;
    redraw();
    analyzer_stop();
    second_quit();
    fflush(stdout);
    fflush(stderr);
    _exit(0);
#else
    snprintf(getsh, sizeof(getsh), "%s/get.sh", home);
    char *git[] = {"git", "-C", (char *)home, "pull", "--ff-only", NULL};
    char *get[] = {"bash", getsh, "--update", "--fetch-only", NULL};
    set_note("updating...");
    redraw();
    if (run_wait(!strcmp(mode, "git") ? git : get) == 0)
        relaunch();
    else
        set_note("update failed, check your internet");
#endif
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
    if (hook && state == TRACKING && getenv("OPEN_CHEASE_OPEN_LIBRARY")) lib_open = 1, hook = 0; // test hook for library shots
    if (lib_open != on && ov->keyboard) ov->keyboard(ov, lib_open);
    on = lib_open;
}

static void handle_click(double x, double y)
{
    hov_t = now_ms();
    for (int i = nhits - 1; i >= 0; i--) {
        Rect r = hits[i].r;
        if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) continue;
        switch (hits[i].action) {
        case ACT_WHITE:
        case ACT_BLACK:
            tracker_start(&tracker, hits[i].action == ACT_WHITE ? WHITE : BLACK);
            state = TRACKING;
            new_stats = 1;
            read_and_track(1);
            break;
        case ACT_BLOCK:
            break;
        case ACT_CLOSE:
        case ACT_STOP:
            lib_open = menu_open = settings_open = confirm_quit = fix_mode = 0;
            want_teach = teach_hush = teach_tries = 0;
            dd_open = -1;
            ignore_board();
            analyzer_stop();
            have_saved = 0;
            state = SCANNING;
            break;
        case ACT_FLIP:
            menu_open = 0;
            tracker_start(&tracker, tracker.user_color ^ 1);
            new_stats = 1;
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
            } else if (st->v == &btn_teach) {
                settings_open = 0, dd_open = -1; // the panel pauses tracking, get it off the board first
                if (state != TRACKING) set_note("no board yet, open a game first so it can see your pieces");
                else want_teach = 1, teach_hush = teach_tries = 0, set_note("looking at your pieces, dont move anything...");
            } else if (st->v == &btn_forget) {
                dd_open = -1;
                forget_pieces();
            } else if (st->v == &btn_rescan) {
                settings_open = 0, dd_open = -1;
                want_rescan = 1, rescan_hush = 0;
                set_note("rescanning the board");
            } else if (st->v == &cfg.library || st->v == &cfg.engine || st->v == &cfg.second) { // second opinion gets picked with the 2nd buttons in there
                lib_open = 1, lib_tab = st->v != &cfg.library, lib_scroll = 0, lib_del = -1, lib_query[0] = 0;
                settings_open = 0, dd_open = -1;
            } else {
                dd_open = dd_open == hits[i].arg ? -1 : hits[i].arg;
            }
            break;
        }
        case ACT_HINT:
            if (hint_key != tracker.pos.key) hint_key = tracker.pos.key, hint_lv = 0;
            if (hint_lv < 3) hint_lv++;
            break;
        case ACT_MODE:
            cfg.mode = (cfg.mode + 1) % 3;
            save_cfg();
            menu_open = 0;
            set_note(cfg.mode == 1 ? "helper mode, no more moves" : cfg.mode == 2 ? "both, moves and the helper stuff" : "cheat mode, moves are back");
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
                if (tab_of(k) != set_tab || (is_button(sets[k].v) && sets[k].v != &cfg.second)) continue;
                *sets[k].v = *(int *)((char *)&cfg_default + ((char *)sets[k].v - (char *)&cfg)); // same field in the defaults copy
            }
            debug_on = cfg.debug;
            dd_open = -1;
            keep_second();
            analyzer_set_think_ms(think_secs[cfg.think] * 1000);
            analyzer_set_options(threads_vals[cfg.threads], hash_vals[cfg.hash], cfg.chaos >= 10 && cfg.lines < 2 ? 3 : cfg.lines + 1);
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
            if (hits[i].arg < 0) { // "use" on a library row we already got
                char out[1200];
                lib_out_path(-1 - hits[i].arg, out, sizeof(out));
                scan_engines(NULL);
                if (engine_index(out)) choose_engine(engine_index(out));
                else set_note("too many engines, delete some first");
            } else
                choose_engine(hits[i].arg);
            break;
        case ACT_LIB_SECOND:
            cfg.second = cfg.second == hits[i].arg + 1 ? 0 : hits[i].arg + 1;
            keep_second();
            save_cfg();
            set_note(cfg.second ? "second opinion on, it shows what that one thinks too" : "second opinion off");
            break;
        case ACT_LIB_DEL: {
            int k = hits[i].arg;
            if (lib_del != k) { // first click arms it, second one deletes
                lib_del = k;
                break;
            }
            lib_del = -1;
            if (k <= 0 || k >= nengines || !mine(k)) break;
            char gone[32];
            snprintf(gone, sizeof(gone), "%s", engine_labels[k]);
            if (k == cfg.engine) choose_engine(0);
            second_quit(); // windows cant delete an exe thats still running, the slots start again on their own
            usleep(400000);
            engine_remove(data_dir, engine_paths[k]);
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
            if (open_folder(dir)) set_note("couldnt open the folder");
            break;
        }
        case ACT_PICK: {
            const Setting *st = &sets[hits[i].arg / 64];
            *st->v = hits[i].arg % 64;
            dd_open = -1;
            if (st->v == &cfg.think) analyzer_set_think_ms(think_secs[cfg.think] * 1000);
            if (st->v == &cfg.lines || st->v == &cfg.threads || st->v == &cfg.hash || st->v == &cfg.chaos)
                analyzer_set_options(threads_vals[cfg.threads], hash_vals[cfg.hash], cfg.chaos >= 10 && cfg.lines < 2 ? 3 : cfg.lines + 1);
            if (st->v == &cfg.engine) choose_engine(cfg.engine);
            if (st->v == &cfg.strength) analyzer_set_elo(strength_elo[cfg.strength]);
            if (st->v == &cfg.second) keep_second();
            if (st->v == &cfg.library && nlib) {
                char out[1200];
                lib_out_path(cfg.library, out, sizeof(out));
                if (is_exec(out)) {
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
            if (fix_mode) set_note("click a wrong square, then pick whats really there");
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
            want_rescan = 1, rescan_hush = 0;
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
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); // plain \n like everywhere else, the tests compare output byte for byte
    setvbuf(stderr, NULL, _IONBF, 0); // windows buffers it when its a file, a killed run would lose the -v log
#endif
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
    quit_signals(on_signal);

    const char *data = getenv("OPEN_CHEASE_DATA");
    char dbuf[512];
    if (!data) {
        default_data(dbuf, sizeof(dbuf));
        data = dbuf;
    }
    if (!data_dir_ok(data)) {
        fprintf(stderr, "%s isnt a folder you own, delete it or point OPEN_CHEASE_DATA somewhere else\n", data);
        return 1;
    }
    snprintf(tpl_path, sizeof(tpl_path), "%s/pieces.bin", data);
    snprintf(cfg_path, sizeof(cfg_path), "%s/settings", data);
    cfg_default = cfg; // before load_cfg, reset needs the shipped values
    load_cfg();
    for (int i = 0; i < NTHEMES; i++) theme_names[i] = themes[i].name;
    char real_data[4096];
    const char *dd = full_path(data, real_data) && strlen(real_data) < sizeof(data_dir) ? real_data : data;
    snprintf(data_dir, sizeof(data_dir), "%.1023s", dd);
    load_library();
    if (!engine_cli && have_saved_engine && (!*saved_engine || is_exec(saved_engine)))
        engine = *saved_engine ? saved_engine : NULL;
    scan_engines(engine);
    if (!replay_dir) fetch_library(); // tests stay offline
    const char *want = getenv("OPEN_CHEASE_GET"); // test hook: download one engine from the library, start it, exit
    if (want) {
        int i = 0;
        while (i < nlib && strcmp(lib[i].name, want)) i++;
        if (i == nlib) {
            fprintf(stderr, "no engine called %s in the list\n", want);
            _exit(2);
        }
        start_download(i);
        for (int t = 0; dl_pid && t < 6000; t++) usleep(100000), check_download();
        for (int t = 0; switch_ticket && t < 300; t++) usleep(100000), check_switch();
        Analysis an;
        analyzer_get(&an);
        printf("%s: %s, engine %s\n", want, note, cfg.engine ? an.engine : "not running");
        int ok = cfg.engine > 0;
        if (ok && getenv("OPEN_CHEASE_GET_DEL")) { // and delete it again while its running, like the library does
            char path[1024];
            snprintf(path, sizeof(path), "%s", engine_paths[cfg.engine]);
            choose_engine(0);
            second_quit();
            usleep(400000);
            engine_remove(data_dir, path);
            ok = !is_exec(path);
            printf("%s: %s\n", want, ok ? "deleted" : "still there after delete");
        }
        fflush(stdout);
        _exit(ok ? 0 : 1);
    }
    cfg.engine = engine_index(engine);
    analyzer_set_options(threads_vals[cfg.threads], hash_vals[cfg.hash], cfg.chaos >= 10 && cfg.lines < 2 ? 3 : cfg.lines + 1);
    analyzer_set_elo(strength_elo[cfg.strength]);
    debug_on = cfg.debug;
    srand((unsigned)time(NULL));
    templates_builtin(&tpl);
    if (templates_load(&tpl, tpl_path) && verbose) fprintf(stderr, "loaded learned piece set\n");

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
        const char *af = getenv("OPEN_CHEASE_ANIM_AT"); // "0.5" = everything stuck halfway
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
            hover_moved();
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
            printf("%d %s %s\n", replay_frame() - 1, state == TRACKING ? "T" : (state == ASKING ? "A" : "S"), fen); // tests/track_sim.py reads this
            replay_ticks++;
        }
        second_quit();
        fflush(stdout);
        fflush(stderr);
        _exit(0); // plain exit locks every FILE and the engine reader is stuck in fgets on one
    }
#if defined(_WIN32) || defined(__APPLE__)
    if (force) fprintf(stderr, "--backend only does something on linux, ignoring it\n");
#ifdef _WIN32
    cap = backend_win();
    ov = overlay_win();
#else
    cap = backend_mac();
    ov = overlay_mac();
#endif
    if (!cap->init(cap) || !ov->init(ov, cap)) {
        fprintf(stderr, "couldnt start the screen grab or the overlay\n");
        return 1;
    }
#else
    const char *desk = getenv("XDG_CURRENT_DESKTOP");
    int gnome = desk && strstr(desk, "GNOME");
    const char *stype = getenv("XDG_SESSION_TYPE");
    int x11_session = (stype && !strcmp(stype, "x11")) || (!getenv("WAYLAND_DISPLAY") && getenv("DISPLAY"));
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
#endif
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
            static long scan_since;
            if (state != SCANNING) scan_since = 0;
            else if (!scan_since) scan_since = t;
            int gap = 1000 / rate_hz[cfg.rate];
            if (scan_since && t - scan_since > 30000 && gap < 1000) gap = 1000; // nothing for 30s, look once a second
            next = t + gap;
            continue;
        }
        double cx, cy;
        int rainbow = state == TRACKING && (cfg.mcolor == 10 || cfg.tcolor == 10 || cfg.gcolor == 10 || (cfg.hanging && cfg.hcolor == 10) ||
                                              ((cfg.threats == 2 || cfg.danger == 2) && cfg.thcolor == 10) || (lib_open && dl_pid > 0));
        int wait = (int)(next - t), busy = animating();
        if (rainbow && wait > 80) wait = 80; // rainbow fades instead of jumping
        if (busy && wait > 15) wait = 15; // ~60fps while something slides
        if (settings_open && ov->pointer && wait > 100) wait = 100; // keeps an eye on the mouse for the tooltips
        int r = ov->wait_events(ov, wait, &cx, &cy);
        if (r < 0) break;
        int tipr = settings_open && hover_moved();
        if (r == 1) handle_click(cx, cy);
        else if (r == 2) handle_key(ov->key), redraw();
        else if (r == 3 && lib_open) lib_scroll += ov->key, redraw();
        else if (rainbow || busy || tipr) redraw();
        sync_keyboard();
    }
    state = SCANNING;
    redraw();
    analyzer_stop();
    second_quit();
    job_kill(dl_pid);
    job_kill(lib_pid);
    usleep(100000);
    fflush(stdout);
    fflush(stderr);
    _exit(0); // same reason as the replay one
}
