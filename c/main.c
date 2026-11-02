// open-chease plugin example in plain C
//
// how it works: open-chease starts this program and talks to it like a uci engine.
// it writes lines to our stdin, we printf lines to stdout. one thing per line,
// words split by spaces. full list of lines is in docs/PLUGINS.md on the main repo.
//
// what it does: draws the best move as an arrow, has a bool and a choice setting,
// a "Say hi" item in the ... menu and a little box next to the board.
//
// only the C standard library, builds with gcc, clang, mingw or msvc.
// open-chease builds it for you from plugin.txt (build = ...), by hand its just
//   cc -O2 -o myplugin main.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// colors are #rgb, #rrggbb or #aarrggbb. aa is how see-through, 00 invisible, ff solid
static const char *colors[] = {"1fe0e0", "40e070", "ff5fb0", "ffd23f"};

static int arrow = 1;      // "arrow" setting, comes in with set
static int color = 0;      // "color" setting, index into colors
static int have_board = 0; // got a board line thats not "board none"
static char team[8] = "white";
static char tomove = 'w';  // from the fen, the best score is for whoever moves
static char last[16];      // san of the last move, empty till one comes
static char best[8], best_san[16], kind[8];
static int score, depth, have_best;

// splits a line into words in place. the line protocol never quotes anything,
// a word is just whatever sits between spaces
static int words(char *s, char **w, int max)
{
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        w[n++] = s;
        while (*s && *s != ' ' && *s != '\t') s++;
        if (*s) *s++ = 0;
    }
    return n;
}

static void redraw(void)
{
    if (!have_board) return;
    const char *c = colors[color];

    // draw lines pile up and nothing changes on screen till "show",
    // then it swaps in all of it at once. so build the whole picture every time
    if (have_best && arrow) {
        // squares are names like e2, they follow the board when black is at the bottom.
        // uci moves are from+to (e2e4), a promotion has a 5th letter (e7e8q) we dont need
        printf("draw arrow %.2s %.2s #c0%s\n", best, best + 2, c);
        printf("draw text %.2s #ffffff %s\n", best + 2, best_san);
    }

    // the box next to the board. panel starts it, each line is a row, max 10 rows.
    // a line can start with a color, without one it gets the normal text color
    printf("panel C example\n");
    printf("line you play %s\n", team);
    if (*last) printf("line last move %s\n", last);
    if (have_best) {
        // score is for the side to move, flip it so + always means good for white.
        // cp is hundredths of a pawn, mate is moves till mate
        int s = tomove == 'b' ? -score : score;
        if (!strcmp(kind, "mate"))
            printf("line #%s best %s  %sM%d  d%d\n", c, best_san, s > 0 ? "+" : "-", abs(s), depth);
        else
            printf("line #%s best %s  %+.2f  d%d\n", c, best_san, s / 100.0, depth);
    }
    printf("show\n");

    // stdout into a pipe is block buffered, it can sit in our buffer for ever.
    // flush after every batch or open-chease never sees it
    fflush(stdout);
}

static void hello(void)
{
    // settings: open-chease draws them in Gear > Plugins, saves them and sends
    // "set <key> <value>" right after each one with the saved value, and again on every change.
    // so the defaults here only count the very first time
    //   setting bool <key> <default 0/1> <label> | <tip>
    //   setting choice <key> <default index> <options split by commas, _ is a space> <label> | <tip>
    printf("setting bool arrow 1 Best move arrow | draws an arrow for the engines move\n");
    printf("setting choice color 0 Cyan,Green,Pink,Yellow Color | arrow and text color\n");

    // menu <key> <label> | <tip>: an item in the ... menu, label 23 chars max, 4 per plugin.
    // clicking it sends "click <key>". send the same key again to change it, "menu hi" alone removes it.
    // older open-chease versions dont know menu and just skip the line, thats fine
    printf("menu hi Say hi | pops a note so you know its alive\n");

    // log shows under the plugin in the Plugins tab (and with -v)
    printf("log running\n");
    fflush(stdout);
}

static void handle(char **w, int n)
{
    if (!strcmp(w[0], "hello")) {
        // "hello 1 myplugin": protocol version, then our folder name
        hello();
    } else if (!strcmp(w[0], "set") && n >= 3) {
        // "set color 2", values are always whole numbers
        int v = atoi(w[2]);
        if (!strcmp(w[1], "arrow")) arrow = v != 0;
        if (!strcmp(w[1], "color") && v >= 0 && v < 4) color = v;
        redraw();
    } else if (!strcmp(w[0], "click") && n >= 2) {
        // "click hi", from the menu item (or a setting button if we had one)
        if (!strcmp(w[1], "hi")) {
            printf("note hi from the C example\n");
            fflush(stdout);
        }
    } else if (!strcmp(w[0], "board") && n >= 2) {
        // "board 220 140 576 white" = x y on screen, size in px, side at the bottom
        // "board none" = lost it, so wipe what we drew. clear works right away, no show needed
        have_board = strcmp(w[1], "none") != 0;
        if (!have_board) {
            printf("clear\n");
            fflush(stdout);
        }
        redraw();
    } else if (!strcmp(w[0], "team") && n >= 2) {
        // "team white", the side the user plays
        snprintf(team, sizeof(team), "%s", w[1]);
        redraw();
    } else if (!strcmp(w[0], "position") && n >= 3) {
        // "position <fen>", after every move. the old best move is for the old position
        tomove = w[2][0];
        have_best = 0;
        redraw();
    } else if (!strcmp(w[0], "move") && n >= 2) {
        // "move e7e5 e5" = uci then san. san can be missing
        snprintf(last, sizeof(last), "%s", n >= 3 ? w[2] : w[1]);
        redraw();
    } else if (!strcmp(w[0], "best") && n >= 6) {
        // "best g1f3 Nf3 cp 36 11" = uci, san, cp or mate, score, depth.
        // comes again each time the engine gets deeper, so keep this quick
        snprintf(best, sizeof(best), "%s", w[1]);
        snprintf(best_san, sizeof(best_san), "%s", w[2]);
        snprintf(kind, sizeof(kind), "%s", w[3]);
        score = atoi(w[4]);
        depth = atoi(w[5]);
        have_best = strlen(best) >= 4;
        redraw();
    }
    // anything else gets skipped. newer open-chease versions might send more
}

int main(void)
{
    // lines are 4096 bytes max, open-chease throws away anything longer
    static char ln[4200];
    char *w[16];
    // stdin closing (fgets gives NULL) means open-chease quit or turned us off
    while (fgets(ln, sizeof(ln), stdin)) {
        size_t len = strlen(ln);
        if (len && ln[len - 1] != '\n' && !feof(stdin)) {
            // too long for the buffer, eat the rest of it and skip the line
            int ch;
            while ((ch = getchar()) != EOF && ch != '\n')
                ;
            continue;
        }
        // windows can send \r\n, cut both off
        while (len && (ln[len - 1] == '\n' || ln[len - 1] == '\r')) ln[--len] = 0;
        int n = words(ln, w, 16);
        if (n) handle(w, n);
    }
    return 0;
}
