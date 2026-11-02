// fens on stdin, run with tests/styles.sh. styles should pick their own moves without throwing eval away
#include <stdio.h>
#include <string.h>

#include "app.h"

static EngineTable tab;

static int score_move(Pos *p, Move m, int depth)
{
    Pos t = *p;
    Undo u;
    pos_make(&t, m, &u);
    Move ms[256];
    if (!pos_gen_legal(&t, ms)) return pos_in_check(&t, t.side) ? MATE : 0;
    SearchLimits lim = {0};
    lim.max_depth = depth;
    SearchInfo fi;
    engine_search_in(&t, &lim, &fi, &tab);
    return -fi.score;
}

int main(int argc, char **argv)
{
    static const char *names[] = {"Normal", "Aggressive", "Solid", "Tricky", "Sacrificial", "Positional"};
    static const int margin[] = {0, 35, 35, 40, 60, 35};
    engine_init();
    engine_table_init(&tab, 16);
    int depth = 6, verbose = argc > 1;
    char line[256];
    int npos = 0, differ[6] = {0}, over[6] = {0}, worst[6] = {0}, deep_worst[6] = {0};
    long drop_sum[6] = {0};
    int spread[4] = {0}; // positions by how many different moves the 6 styles picked, 1 2 3 4+
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        Pos p;
        if (!pos_from_fen(&p, line)) continue;
        Move ms[256];
        int n = pos_gen_legal(&p, ms), sc[256];
        if (n < 3) continue;
        for (int i = 0; i < n; i++) sc[i] = score_move(&p, ms[i], depth);
        Analysis an;
        memset(&an, 0, sizeof(an));
        int top[3] = {-1, -1, -1};
        for (int k = 0; k < 3; k++)
            for (int i = 0; i < n; i++)
                if (i != top[0] && i != top[1] && (top[k] < 0 || sc[i] > sc[top[k]])) top[k] = i;
        if (sc[top[0]] > MATE_BOUND || sc[top[0]] < -MATE_BOUND) continue; // styles stay out of mates
        an.best = ms[top[0]], an.score = sc[top[0]], an.depth = depth, an.key = p.key;
        for (int k = 0; k < 2; k++) {
            an.alt[k] = ms[top[k + 1]], an.alt_score[k] = sc[top[k + 1]];
            if (sc[top[k + 1]] < -MATE_BOUND) an.alt_mate[k] = -1;
        }
        npos++;
        int deep_best = score_move(&p, an.best, depth + 2);
        if (verbose) printf("%s\n", line);
        Move picked[6];
        for (int s = 0; s < 6; s++) {
            Move m = s ? style_choose(&p, &an, s) : an.best;
            picked[s] = m;
            int i = 0;
            while (i < n && ms[i] != m) i++;
            int drop = an.score - sc[i];
            if (m != an.best) {
                differ[s]++;
                int dd = deep_best - score_move(&p, m, depth + 2);
                if (dd > deep_worst[s]) deep_worst[s] = dd;
            }
            if (drop > margin[s]) over[s]++;
            if (drop > worst[s]) worst[s] = drop;
            drop_sum[s] += drop;
            if (verbose) {
                char u[8];
                move_to_uci(m, u);
                printf("  %-12s %s %+d\n", names[s], u, -drop);
            }
        }
        int nd = 0;
        for (int s = 0; s < 6; s++) {
            int dup = 0;
            for (int q = 0; q < s; q++) dup |= picked[q] == picked[s];
            nd += !dup;
        }
        spread[nd > 4 ? 3 : nd - 1]++;
    }
    printf("%d positions, styles picked 1/2/3/4+ different moves in %d/%d/%d/%d\n", npos, spread[0], spread[1], spread[2], spread[3]);
    int fail = 0;
    for (int s = 1; s < 6; s++) {
        printf("%-12s not the best move %3d  worst drop %3d (margin %d, over %d)  avg drop %.1f  deeper recheck worst %d\n", names[s],
               differ[s], worst[s], margin[s], over[s], npos ? (double)drop_sum[s] / npos : 0, deep_worst[s]);
        fail |= over[s] > 0;
    }
    return fail;
}
