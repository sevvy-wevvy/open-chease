#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

static int fails;
static BoardRead *rd;
static Templates tpl;

static void check(int ok, const char *what, const char *got)
{
    printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", what, got ? "  got " : "", got ? got : "");
    fails += !ok;
}

static int play(Pos *p, const char *fen, const char *moves)
{
    if (!pos_from_fen(p, fen)) return 0;
    char buf[512], *sv, *tok;
    snprintf(buf, sizeof(buf), "%s", moves);
    for (tok = strtok_r(buf, " ", &sv); tok; tok = strtok_r(NULL, " ", &sv)) {
        Move m = move_from_uci(p, tok);
        Undo u;
        if (!m || !pos_make(p, m, &u)) return 0;
    }
    return 1;
}

static void show(Tracker *t, const Pos *p) // what the screen would look like, read twice so it holds still
{
    memset(rd, 0, sizeof(*rd));
    for (int sq = 0; sq < 64; sq++) rd->obs[sq] = p->sq[sq] == EMPTY ? OBS_EMPTY : p->sq[sq] / 6 == WHITE ? OBS_WHITE : OBS_BLACK;
    for (int k = 0; k < 3; k++) tracker_update(t, rd, &tpl);
}

static void feed(Tracker *t, Pos *g, const char *moves)
{
    char buf[512], *sv, *tok;
    snprintf(buf, sizeof(buf), "%s", moves);
    for (tok = strtok_r(buf, " ", &sv); tok; tok = strtok_r(NULL, " ", &sv)) {
        Move m = move_from_uci(g, tok);
        Undo u;
        if (!m) {
            printf("FAIL bad move %s\n", tok);
            fails++;
            return;
        }
        pos_make(g, m, &u);
        show(t, g);
    }
}

static Analysis fake(Move best, int score, Move alt, int alt_score)
{
    Analysis a;
    memset(&a, 0, sizeof(a));
    a.best = best, a.score = score, a.alt[0] = alt, a.alt_score[0] = alt_score, a.depth = 12;
    return a;
}

static const char *kq = "7k/8/8/8/8/8/8/K2Q4 w - - 0 1";
static const char *perp = "6k1/qr4p1/1r3p2/7Q/8/8/5PPP/6K1 w - - 0 1";

int main(int argc, char **argv)
{
    engine_init();
    templates_builtin(&tpl);
    rd = calloc(1, sizeof(BoardRead));
    char line[1400], txt[100], u[8];
    Tracker *t = calloc(1, sizeof(Tracker));
    Pos g, p;

    // history in the tracker
    tracker_start(t, WHITE);
    pos_startpos(&g);
    feed(t, &g, "g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1");
    tracker_line(t, line, sizeof(line));
    check(!strcmp(line, "fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 moves g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1"),
          "knight shuffle line", line);
    check(pos_repeats(&t->pos) == 1 && draw_kind(&t->pos) == 0, "seen twice, not a draw yet", NULL);
    feed(t, &g, "f6g8");
    check(t->pos.key == g.key && draw_kind(&t->pos) == 1, "third time is a draw", NULL);

    tracker_start(t, WHITE);
    pos_startpos(&g);
    feed(t, &g, "g1f3 b8c6 e2e4");
    tracker_line(t, line, sizeof(line));
    check(!strcmp(line, "fen r1bqkbnr/pppppppp/2n5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 0 2"), "pawn move starts the line over", line);
    feed(t, &g, "g8f6 b1c3 f6g8 c3b1");
    tracker_line(t, line, sizeof(line));
    check(!strcmp(line, "fen r1bqkbnr/pppppppp/2n5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 0 2 moves g8f6 b1c3 f6g8 c3b1"), "moves after it",
          line);
    check(pos_repeats(&t->pos) == 1, "back where the line started", NULL);
    play(&p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "g1f3 b8c6 e2e4 g8f6 b1c3 f6g8");
    show(t, &p); // takeback of c3b1
    tracker_line(t, line, sizeof(line));
    check(!strcmp(line, "fen r1bqkbnr/pppppppp/2n5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 0 2 moves g8f6 b1c3 f6g8") && pos_repeats(&t->pos) == 0,
          "takeback drops the move", line);
    feed(t, &p, "e4e5");
    tracker_line(t, line, sizeof(line));
    check(!strncmp(line, "fen r1bqkbnr/pppppppp/2n5/4P3/8/2N2N2/PPPP1PPP/R1BQKB1R b KQkq - 0 4", 66) && !strstr(line, "moves"),
          "and goes on from there", line);

    // the built in engine sees the history
    SearchLimits lim = {0};
    lim.movetime_ms = 300;
    lim.max_depth = 12;
    SearchInfo fi;
    play(&p, kq, "d1d2 h8g8 d2d1 g8h8 d1d2 h8g8 d2d1");
    Move m = engine_search(&p, &lim, &fi);
    move_to_uci(m, u);
    check(!strcmp(u, "g8h8") && fi.score == 0, "losing side grabs the repeat", u);
    pos_from_fen(&p, "6k1/8/8/8/8/8/8/K2Q4 b - - 0 1");
    m = engine_search(&p, &lim, &fi);
    check(fi.score < -500, "no history, no draw", NULL);
    play(&p, kq, "d1d2 h8g8 d2d1 g8h8 d1d2 h8g8");
    m = engine_search(&p, &lim, &fi);
    move_to_uci(m, u);
    check(strcmp(u, "d2d1") && fi.score > 500, "winning side stays off the repeat", u);
    play(&p, kq, "d1d2 h8g8 d2d1 g8h8 d1e2 h8g8 e2d1 g8f8 d1d2 f8g8");
    m = engine_search(&p, &lim, &fi);
    move_to_uci(m, u);
    check(strcmp(u, "d2d1") && fi.score > 500, "even when the repeat is right there", u);

    // warnings
    Move d1 = MK_MOVE(11, 3, 0, 0), d4 = MK_MOVE(11, 27, 0, 0);
    play(&p, kq, "d1d2 h8g8 d2d1 g8h8 d1d2 h8g8");
    Analysis a = fake(d1, 900, d4, 880);
    m = rep_guard(&p, &a, d1, 0, 1, txt, sizeof(txt));
    check(m == d4 && !strcmp(txt, "careful, after Qd1 they can repeat for a draw, Qd4 keeps the win"), "they could repeat", txt);
    play(&p, kq, "d1d2 h8g8 d2d1 g8h8 d1e2 h8g8 e2d1 g8f8 d1d2 f8g8");
    m = rep_guard(&p, &a, d1, 0, 1, txt, sizeof(txt));
    check(m == d4 && !strcmp(txt, "careful, Qd1 repeats the position = draw, Qd4 keeps the win"), "we would repeat", txt);
    m = rep_guard(&p, &a, d1, 0, 0, txt, sizeof(txt));
    check(m == d1 && !strcmp(txt, "careful, Qd1 repeats the position = draw"), "picker keeps its move, still warns", txt);
    m = rep_guard(&p, &a, d4, d1, 1, txt, sizeof(txt));
    check(m == d4 && !strcmp(txt, "careful, Qd1 repeats the position = draw"), "the move theyd likely play", txt);
    a = fake(d1, 40, d4, 30);
    m = rep_guard(&p, &a, d1, 0, 1, txt, sizeof(txt));
    check(m == d1 && !*txt, "about equal, a draw is fine", txt);
    a = fake(d4, 900, d1, 880);
    play(&p, kq, "d1d2 h8g8 d2d1 g8h8");
    m = rep_guard(&p, &a, d1, 0, 1, txt, sizeof(txt));
    check(m == d1 && !*txt, "only twice so far, no fuss", txt);

    play(&p, kq, "d1d2 h8g8 d2d1 g8h8 d1d2 h8g8 d2d1");
    a = fake(MK_MOVE(62, 63, 0, 0), -900, 0, 0);
    rep_guard(&p, &a, a.best, 0, 1, txt, sizeof(txt));
    check(!strcmp(txt, "you can draw by repetition with Kh8"), "losing, draw right now", txt);
    play(&p, perp, "h5e8 g8h7 e8h5 h7g8");
    a = fake(MK_MOVE(39, 60, 0, 0), -1500, 0, 0);
    rep_guard(&p, &a, a.best, 0, 1, txt, sizeof(txt));
    check(!strcmp(txt, "you can force a draw by repetition with Qe8+"), "losing, perpetual", txt);
    play(&p, perp, "");
    rep_guard(&p, &a, a.best, 0, 1, txt, sizeof(txt));
    check(!*txt, "perpetual with no history yet", txt);

    play(&p, "4k3/8/8/8/8/8/8/R3K3 w - - 99 80", "a1a2");
    check(draw_kind(&p) == 2, "50 move rule", NULL);
    play(&p, "4k3/8/8/8/8/8/8/R3K3 w - - 98 80", "a1a2");
    check(draw_kind(&p) == 0, "49 and a half moves", NULL);

    if (argc > 1) { // print lines for the uci check in repeats.sh
        tracker_start(t, BLACK);
        pos_from_fen(&t->pos, kq);
        tracker_rebase(t);
        pos_from_fen(&g, kq);
        feed(t, &g, "d1d2 h8g8 d2d1 g8h8 d1d2 h8g8 d2d1");
        tracker_line(t, line, sizeof(line));
        printf("LINE %s\n", line);
    }
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
