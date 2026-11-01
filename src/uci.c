#include "engine.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Pos pos;
static atomic_int stop_flag;
static pthread_t worker;
static int worker_running;
static SearchLimits limits;

static void print_info(const SearchInfo *in, void *user)
{
    char mv[8];
    printf("info depth %d seldepth %d ", in->depth, in->seldepth);
    if (in->score > MATE_BOUND)
        printf("score mate %d ", (MATE - in->score + 1) / 2); // plies to moves
    else if (in->score < -MATE_BOUND)
        printf("score mate -%d ", (MATE + in->score) / 2);
    else
        printf("score cp %d ", in->score);
    printf("nodes %llu time %d nps %llu pv", (unsigned long long)in->nodes, in->ms,
           (unsigned long long)(in->ms ? in->nodes * 1000 / in->ms : in->nodes));
    for (int i = 0; i < in->pvlen; i++) {
        move_to_uci(in->pv[i], mv);
        printf(" %s", mv);
    }
    printf("\n");
    fflush(stdout);
}

static void *search_thread(void *arg)
{
    Pos copy = pos; // so d can still print pos while this runs
    Move best = engine_search(&copy, &limits, NULL);
    char mv[8];
    move_to_uci(best, mv);
    printf("bestmove %s\n", mv);
    fflush(stdout);
    return NULL;
}

static void stop_search(void)
{
    if (worker_running) {
        atomic_store(&stop_flag, 1);
        pthread_join(worker, NULL);
        worker_running = 0;
    }
}

static void handle_position(char *line)
{
    char *moves = strstr(line, " moves ");
    if (moves) *moves = 0;
    if (strstr(line, "startpos")) {
        pos_startpos(&pos);
    } else {
        char *fen = strstr(line, "fen ");
        if (!fen || !pos_from_fen(&pos, fen + 4)) pos_startpos(&pos);
    }
    if (moves) {
        char *tok = strtok(moves + 7, " \r\n\t");
        while (tok) {
            Move m = move_from_uci(&pos, tok);
            if (!m) break; // junk move, keep what we have
            Undo u;
            pos_make(&pos, m, &u);
            tok = strtok(NULL, " \r\n\t");
        }
    }
}

static int arg_int(const char *line, const char *name, int def)
{
    const char *p = strstr(line, name);
    if (!p) return def;
    return atoi(p + strlen(name));
}

static void handle_go(const char *line)
{
    stop_search();
    memset(&limits, 0, sizeof(limits));
    limits.stop = &stop_flag;
    limits.cb = print_info;
    atomic_store(&stop_flag, 0);
    limits.max_depth = arg_int(line, " depth ", 0);
    if (limits.max_depth > 100) limits.max_depth = 100; // tt keeps depth in a signed byte
    int movetime = arg_int(line, " movetime ", 0);
    int wtime = arg_int(line, " wtime ", -1), btime = arg_int(line, " btime ", -1);
    int winc = arg_int(line, " winc ", 0), binc = arg_int(line, " binc ", 0);
    int mtg = arg_int(line, " movestogo ", 30); // sudden death, just guess 30 left
    if (movetime) {
        limits.movetime_ms = movetime;
    } else if (!strstr(line, "infinite") && (wtime >= 0 || btime >= 0)) {
        int t = pos.side == WHITE ? wtime : btime, inc = pos.side == WHITE ? winc : binc;
        if (t < 0) t = 10000;
        int budget = t / (mtg > 0 ? mtg : 30) + inc * 3 / 4;
        if (budget > t - 50) budget = t / 2; // almost out of time, dont flag
        if (budget < 10) budget = 10;
        limits.movetime_ms = budget;
    }
    worker_running = 1;
    pthread_create(&worker, NULL, search_thread, NULL);
}

int main(int argc, char **argv)
{
    engine_init();
    pos_startpos(&pos);
    // open-chease-engine perft|bench <depth> [fen], not uci, just for testing
    if (argc >= 3 && !strcmp(argv[1], "perft")) {
        int depth = atoi(argv[2]);
        if (argc >= 4) pos_from_fen(&pos, argv[3]);
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        u64 n = perft(&pos, depth);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
        printf("%llu %.2fs\n", (unsigned long long)n, s);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "bench")) {
        if (argc >= 4) pos_from_fen(&pos, argv[3]);
        SearchLimits l = {0};
        l.max_depth = atoi(argv[2]);
        l.cb = print_info;
        Move m = engine_search(&pos, &l, NULL);
        char mv[8];
        move_to_uci(m, mv);
        printf("bestmove %s\n", mv);
        return 0;
    }
    char line[16384];
    setvbuf(stdout, NULL, _IOLBF, 0); // a pipe would be fully buffered
    while (fgets(line, sizeof(line), stdin)) {
        if (!strncmp(line, "uci", 3) && (line[3] == '\n' || line[3] == '\r' || !line[3])) { // ucinewgame starts with uci too
            printf("id name open-chease 1.0\nid author sevvy-wevvy\n");
            printf("option name Hash type spin default 32 min 1 max 4096\nuciok\n");
        } else if (!strncmp(line, "isready", 7)) {
            printf("readyok\n");
        } else if (!strncmp(line, "setoption", 9)) {
            char *v = strstr(line, "value ");
            if (strstr(line, "name Hash") && v) {
                stop_search();
                engine_tt_resize(atoi(v + 6));
            }
        } else if (!strncmp(line, "ucinewgame", 10)) {
            stop_search();
            engine_tt_clear();
        } else if (!strncmp(line, "position", 8)) {
            stop_search();
            handle_position(line);
        } else if (!strncmp(line, "go", 2)) {
            handle_go(line);
        } else if (!strncmp(line, "stop", 4)) {
            stop_search();
        } else if (!strncmp(line, "quit", 4)) {
            stop_search();
            break;
        } else if (!strncmp(line, "d", 1) && (line[1] == '\n' || !line[1])) { // like stockfish d but just the fen
            char fen[128];
            pos_to_fen(&pos, fen);
            printf("%s\n", fen);
        }
        fflush(stdout);
    }
    stop_search();
    return 0;
}
