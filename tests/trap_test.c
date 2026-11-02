// known opening traps plus a few spots that should stay quiet, run with tests/traps.sh
#include <stdio.h>
#include <string.h>

#include "app.h"

static const struct {
    const char *name, *moves, *want; // want = the bait move, "-" = it should stay quiet
} cases[] = {
    {"legal's mate", "e2e4 e7e5 g1f3 d7d6 f1c4 c8g4 b1c3 g7g6 f3e5", "g4d1"},
    {"blackburne shilling", "e2e4 e7e5 g1f3 b8c6 f1c4 c6d4 f3e5 d8g5", "e5f7"},
    {"elephant trap", "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 b8d7 c4d5 e6d5", "c3d5"},
    {"fishing pole", "e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 e1g1 f6g4 h2h3 h7h5", "h3g4"},
    {"noahs ark", "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 d7d6 d2d4 b7b5 a4b3 c6d4 f3d4 e5d4", "d1d4"},
    {"plain recapture", "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4", "-"},
    {"quiet ruy", "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7", "-"},
    {"free queen", "e2e4 e7e5 g1f3 d8h4", "-"},
    {"scandi queen", "e2e4 d7d5 e4d5 d8d5 b1c3", "-"},
};

static int play(Pos *p, const char *moves)
{
    char buf[512], *save, *tok;
    pos_startpos(p);
    snprintf(buf, sizeof(buf), "%s", moves);
    for (tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        Move m = move_from_uci(p, tok);
        Undo u;
        if (!m || !pos_make(p, m, &u)) return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    engine_init();
    int ms = 1000, fail = 0; // the app checks again at 1000, two traps need it
    if (argc > 1) {
        ms = 0;
        for (const char *s = argv[1]; *s >= '0' && *s <= '9'; s++) ms = ms * 10 + *s - '0';
    }
    if (argc > 2) { // fens on stdin, prints every one it flags
        char line[256];
        int seen = 0, hit = 0;
        while (fgets(line, sizeof(line), stdin)) {
            Pos p;
            Traps t;
            line[strcspn(line, "\n")] = 0;
            if (!pos_from_fen(&p, line)) continue;
            seen++;
            trap_scan(&p, &t, ms, NULL);
            if (!t.n) continue;
            hit++;
            char u[8];
            move_to_uci(t.mv[0], u);
            printf("%s  %s -%d%s\n", line, u, t.loss[0], t.mate[0] ? " mate" : "");
        }
        printf("%d of %d flagged\n", hit, seen);
        return 0;
    }
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        Pos p;
        Traps t;
        if (!play(&p, cases[i].moves)) {
            printf("FAIL %-20s bad move list\n", cases[i].name);
            fail = 1;
            continue;
        }
        trap_scan(&p, &t, ms, NULL);
        char got[128] = "", u[8], r[8];
        for (int k = 0; k < t.n; k++) {
            move_to_uci(t.mv[k], u);
            move_to_uci(t.reply[k], r);
            size_t l = strlen(got);
            snprintf(got + l, sizeof(got) - l, "%s%s(-%d%s%s %s)", l ? " " : "", u, t.loss[k], t.mate[k] ? " mate" : "",
                     t.lost[k] >= 0 ? " lost" : "", r);
        }
        int ok = *cases[i].want == '-' ? t.n == 0 : strstr(got, cases[i].want) != NULL;
        printf("%s %-20s want %-5s got %s\n", ok ? "ok  " : "FAIL", cases[i].name, cases[i].want, t.n ? got : "nothing");
        fail |= !ok;
    }
    return fail;
}
