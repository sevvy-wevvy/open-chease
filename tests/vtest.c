#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

static int read_ppm(const char *path, Frame *f)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    int w, h, mx;
    if (fscanf(fp, "P6 %d %d %d", &w, &h, &mx) != 3) return 0;
    fgetc(fp);
    uint8_t *rgb = malloc((size_t)w * h * 3);
    if (fread(rgb, 3, (size_t)w * h, fp) != (size_t)w * h) return 0;
    fclose(fp);
    f->px = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        f->px[i * 4] = rgb[i * 3 + 2];
        f->px[i * 4 + 1] = rgb[i * 3 + 1];
        f->px[i * 4 + 2] = rgb[i * 3];
        f->px[i * 4 + 3] = 255;
    }
    free(rgb);
    f->w = w;
    f->h = h;
    f->stride = w * 4;
    f->bgr = 1;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: vtest image.ppm white|black [dump]\n");
        return 2;
    }
    engine_init();
    Frame f;
    if (!read_ppm(argv[1], &f)) {
        fprintf(stderr, "cant read %s, it needs to be a P6 ppm\n", argv[1]);
        return 2;
    }
    Board b;
    if (!vision_find_board(&f, &b, NULL, 0, NULL, NULL)) {
        printf("NOBOARD\n");
        return 1;
    }
    int wb = strcmp(argv[2], "black") != 0;
    BoardRead *r = malloc(sizeof(BoardRead));
    vision_read(&f, &b, wb, 0, r, NULL, NULL);
    Templates t;
    templates_builtin(&t);
    Tracker tr;
    tracker_start(&tr, wb ? WHITE : BLACK);
    tracker_resync(&tr, r, &t);
    char fen[128];
    pos_to_fen(&tr.pos, fen);
    printf("BOARD %.1f %.1f %.2f score %d\n", b.x, b.y, b.s, b.score);
    printf("OBS ");
    for (int rr = 7; rr >= 0; rr--) {
        for (int ff = 0; ff < 8; ff++) putchar(".WB?"[r->obs[rr * 8 + ff]]);
        putchar(rr ? '/' : '\n');
    }
    printf("FEN %s\n", fen);
    if (argc > 3) { // any extra arg dumps every occupied square
        for (int sq = 0; sq < 64; sq++)
            if (r->obs[sq]) printf("  %c%c occ %.2f lum %.0f hl %d\n", 'a' + (sq & 7), '1' + (sq >> 3), r->occ_frac[sq], r->lum[sq], r->highlighted[sq]);
    }
    return 0;
}
