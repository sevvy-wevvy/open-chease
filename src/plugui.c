#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugui.h"
#include "ui.h"

#define TH cur_theme()

static int pslide = -1, pslide_v0;
static Rect pslide_r;

static int own_tab(const Plugin *p)
{
    if (!p->alive || !*p->tab) return -1;
    for (int k = TAB_PLUG + 1; k < ntabs; k++)
        if (!strcmp(all_tabs[k], p->tab)) return k;
    return -1;
}

void plug_tabs(void)
{
    for (int k = 0; k < NTABS; k++) all_tabs[k] = tab_names[k];
    all_tabs[TAB_PLUG] = "Plugins";
    ntabs = TAB_PLUG + 1;
    for (int i = 0; i < nplugs; i++) {
        Plugin *p = &plugs[i];
        if (!p->alive || !*p->tab || !p->nset || own_tab(p) >= 0 || ntabs == MAX_TABS) continue;
        all_tabs[ntabs++] = p->tab;
    }
    if (set_tab >= ntabs) set_tab = TAB_PLUG;
}

static int plug_slider_v(const PlugSet *s, Rect r, double x)
{
    double f = fmin(1, fmax(0, (x - (r.x + 8)) / (r.w - 16)));
    return s->lo + (int)lround(f * (s->hi - s->lo));
}

static void plug_row(Canvas *c, Rect row, int pi, int k, int hit)
{
    PlugSet *s = &plugs[pi].set[k];
    int arg = pi * 64 + k;
    draw_round_rect(c, row, 7, TH->item);
    double right = row.x + row.w;
    Rect tr = {0};
    if (s->kind == PS_BOOL) {
        Rect sw = {row.x + row.w - 46, row.y + 3.5, 38, 18};
        double d = 14, v = s->v ? 1 : 0;
        draw_round_rect(c, sw, 9, mix(TH->btn, TH->title, v));
        draw_round_rect(c, (Rect){sw.x + 2 + (sw.w - d - 4) * v, sw.y + 2, d, d}, d / 2, mix(0xFFE8E8E8, 0xFF181818, v));
        right = sw.x;
    } else if (s->kind == PS_SLIDER) {
        char vt[16], a[16], b[16];
        snprintf(vt, sizeof(vt), "%d", s->v), snprintf(a, sizeof(a), "%d", s->lo), snprintf(b, sizeof(b), "%d", s->hi);
        double vw = fmax(text_lw(c, a, 0), text_lw(c, b, 0));
        double end = row.x + row.w - 22 - vw, x0 = fmax(row.x + 24 + fmin(text_lw(c, s->label, 0), row.w * 0.45), end - 90);
        tr = (Rect){x0, row.y + row.h / 2 - 3, fmax(30, end - x0), 6};
        double f = (double)(s->v - s->lo) / (s->hi - s->lo), d = 14;
        draw_round_rect(c, tr, 3, TH->btn);
        draw_round_rect(c, (Rect){tr.x, tr.y, fmax(6, tr.w * f), tr.h}, 3, TH->title);
        draw_round_rect(c, (Rect){tr.x + tr.w * f - d / 2, tr.y + 3 - d / 2, d, d}, d / 2, 0xFFE8E8E8);
        draw_text(c, row.x + row.w - 8 - text_lw(c, vt, 0), row.y + 4, vt, 0, TH->item_fg);
        right = tr.x;
    } else {
        int btn = s->kind == PS_BUTTON;
        Rect box = {row.x + row.w - (btn ? 36 : 126), row.y + 3, btn ? 30 : 120, row.h - 6};
        draw_round_rect(c, box, 5, TH->btn);
        if (!btn) draw_text(c, box.x + 8, box.y + 1, fit_text(c, s->ch[s->v], box.w - 26), 0, TH->btn_fg);
        draw_text(c, box.x + box.w - (btn ? 18 : 14), box.y + 1, ">", 0, TH->btn_fg);
        right = box.x;
    }
    draw_text(c, row.x + 10, row.y + 4, fit_text(c, s->label, right - row.x - 16), 0, TH->item_fg);
    if (hit) add_hit_arg(row, ACT_PLUG_SET, arg);
    if (hit && s->kind == PS_SLIDER) add_hit_arg((Rect){tr.x - 8, row.y, tr.w + 16, row.h}, ACT_PLUG_SLIDE, arg);
    tip_spot(row, *s->tip ? s->tip : NULL);
}

static double plug_grid(Canvas *c, Rect m, double y, double pad, int pi, int draw, int hit)
{
    int ncol = m.w >= 272 * 2 + pad * 3 ? 2 : 1, n = plugs[pi].nset;
    double cw = (m.w - pad * (ncol + 1)) / ncol, rh = 28;
    int per = (n + ncol - 1) / ncol;
    for (int k = 0; draw && k < n; k++) // down then across, same as the other tabs
        plug_row(c, (Rect){m.x + pad + (k / per) * (cw + pad), y + (k % per) * rh, cw, rh - 3}, pi, k, hit);
    return y + per * rh;
}

static char plug_url[160];
int plug_typing;
static int plug_ask = -1;
static intptr_t pget_pid;
static double plug_add_row(Canvas *c, Rect m, double y, double pad, int draw, int hit)
{
    double w = m.w - 2 * pad, bw = fmax(64, text_lw(c, "Paste", 0) + 20), aw = fmax(110, text_lw(c, "Add plugin", 0) + 20);
    Rect f = {m.x + pad, y, w - bw - aw - 12, 26}, pb = {f.x + f.w + 6, y, bw, 26}, ab = {pb.x + bw + 6, y, aw, 26};
    if (draw) {
        draw_round_rect(c, f, 7, TH->item);
        if (plug_typing) draw_frame(c, f, 1, TH->title);
        char t[170];
        snprintf(t, sizeof(t), "%s%s", plug_url, plug_typing ? "_" : "");
        const char *shown = *t ? t : "github link, like someone/their-plugin";
        double tw = text_lw(c, shown, 0);
        if (*t && tw > f.w - 16) // long links show their end, thats where you type
            for (const char *q = t; *q && text_lw(c, q, 0) > f.w - 16; q++) shown = q + 1;
        draw_text(c, f.x + 9, f.y + 4, *t ? shown : fit_text(c, shown, f.w - 16), 0, *t ? TH->item_fg : (TH->item_fg & 0xFFFFFF) | 0x80000000);
        draw_button(c, pb, "Paste", TH->btn, TH->btn_fg);
        draw_button(c, ab, pget_pid ? "Getting it..." : "Add plugin", TH->title, 0xFF181818);
        if (hit) add_hit(f, ACT_PLUG_URL), add_hit(pb, ACT_PLUG_PASTE), add_hit(ab, ACT_PLUG_ADD);
        tip_spot(f, "a github repo with a plugin.txt in it, or a folder in one (the link to that folder). its added turned off");
        tip_spot(pb, "pastes the link you copied");
        tip_spot(ab, "downloads it into your plugins folder. the same one again updates it");
    }
    return y + 34;
}

static int plug_link(const char *url, char *out, size_t n) // only plain web links get opened
{
    const char *u = url;
    if (!strncmp(u, "https://", 8)) u += 8;
    else if (!strncmp(u, "http://", 7)) u += 7;
    if (!*u || strpbrk(u, " \"'`<>|^\\") || *u == '/' || *u == '.') return 0;
    snprintf(out, n, "%s%s", u == url ? "https://" : "", url);
    return 1;
}

static double plug_entry(Canvas *c, Rect m, double y, double pad, int i, int draw, int hit)
{
    Plugin *p = &plugs[i];
    double w = m.w - 2 * pad;
    int osok = plug_os_ok(p), ot = own_tab(p);
    Rect row = {m.x + pad, y, w, 25};
    if (draw) {
        draw_round_rect(c, row, 7, TH->item);
        Rect sw = {row.x + row.w - 46, row.y + 3.5, 38, 18}, fb = {sw.x - 84, row.y + 3, 76, row.h - 6}, ub = {fb.x - 84, fb.y, 76, fb.h};
        double d = 14, v = p->on ? 1 : 0;
        if (osok) {
            draw_round_rect(c, sw, 9, mix(TH->btn, TH->title, v));
            draw_round_rect(c, (Rect){sw.x + 2 + (sw.w - d - 4) * v, sw.y + 2, d, d}, d / 2, mix(0xFFE8E8E8, 0xFF181818, v));
        }
        draw_button(c, fb, "Folder", TH->btn, TH->btn_fg);
        if (*p->update) draw_button(c, ub, pget_pid ? "..." : "Update", TH->btn, TH->btn_fg);
        char t[80];
        snprintf(t, sizeof(t), "%s%s%s", p->title, *p->ver ? "  v" : "", p->ver);
        draw_text(c, row.x + 10, row.y + 4, fit_text(c, t, (*p->update ? ub.x : fb.x) - row.x - 18), 0, osok ? TH->title : TH->sub);
        if (hit) add_hit(row, ACT_BLOCK), add_hit_arg(fb, ACT_PLUG_DIR, i);
        if (hit && osok) add_hit_arg(sw, ACT_PLUG_ON, i);
        if (hit && *p->update) add_hit_arg(ub, ACT_PLUG_UPDATE, i);
        if (osok) tip_spot(sw, p->trusted ? "turns this plugin on or off. plugins are programs, only run ones you trust" : "turns it on. the check found stuff in it, so it shows you what first");
        tip_spot(fb, "opens the folder this plugin lives in");
        if (*p->update) tip_spot(ub, "gets the newest version from its github, same as adding it again");
    }
    y += 28;
    char link[220];
    int has_link = plug_link(p->url, link, sizeof(link));
    if (*p->author || has_link) { // by sev  github.com/me/myplugin
        double x = m.x + pad + 10;
        char by[60];
        snprintf(by, sizeof(by), "by %s", p->author);
        if (*p->author) {
            if (draw) draw_text(c, x, y, fit_text(c, by, w * 0.4), 0, TH->sub);
            x += fmin(text_lw(c, by, 0), w * 0.4) + 14;
        }
        if (has_link) {
            const char *shown = link + (link[4] == 's' ? 8 : 7);
            if (!strncmp(shown, "www.", 4)) shown += 4;
            shown = fit_text(c, shown, m.x + m.w - pad - 10 - x);
            Rect lr = {x - 3, y - 1, text_lw(c, shown, 0) + 6, 19};
            if (draw) draw_text(c, x, y, shown, 0, osok ? TH->title : TH->sub);
            if (draw && hit) add_hit_arg(lr, ACT_PLUG_LINK, i);
            if (draw) tip_spot(lr, "opens its page in your browser");
        }
        y += 20;
    }
    double cx = m.x + pad + 8;
    static char tips[PL_MAX][PF_N + 1][420];
    int fl = plug_flags(p);
    for (int k = PF_N; k >= 0; k--) { // went online, then the scary ones first
        if (k < PF_N && !(fl >> k & 1)) continue;
        if (k == PF_N && !*p->online) continue;
        const char *nm = k == PF_N ? "went online" : plug_flag_name[k];
        double cw = text_lw(c, nm, 0) + 14;
        if (cx + cw > m.x + m.w - pad - 40) break;
        Rect ch = {cx, y, cw, 18};
        int bad = k == PF_N || k > PF_BIN;
        uint32_t bg = bad ? 0xFFD03A3A : k == PF_BIN ? 0xFFE0702A : 0xFFD8A23A, fg = bad ? 0xFFFFFFFF : 0xFF181818;
        if (draw) {
            draw_round_rect(c, ch, 9, bg);
            draw_text(c, ch.x + 7, ch.y + 1, nm, 0, fg);
            char *tp = tips[i][k];
            if (k == PF_N)
                snprintf(tp, 420, "it went online: %s%s%s", p->online, p->net_ok ? "\nyou said thats fine" : "", *p->net_why ? "\nit says why: " : "");
            else
                snprintf(tp, 420, "%s%s", plug_flag_says[k], k == PF_NET && *p->net_why ? ", it says why: " : "");
            if (k == PF_NET || k == PF_N) snprintf(tp + strlen(tp), 420 - strlen(tp), "%.100s", p->net_why);
            for (int j = 0; k < PF_N && j < p->nflag[k] && j < PL_WHY; j++) snprintf(tp + strlen(tp), 420 - strlen(tp), "\n%s", p->flag[k][j]);
            if (k < PF_N && p->nflag[k] > PL_WHY) snprintf(tp + strlen(tp), 420 - strlen(tp), "\nand %d more", p->nflag[k] - PL_WHY);
            tip_spot(ch, tp);
        }
        cx += cw + 5;
    }
    char info[300], why[80];
    int r = plug_ready(p, why, sizeof(why)), miss = -1, bad = 0;
    if (!osok) snprintf(info, sizeof(info), "%s", why);
    else if (p->alive && *p->said) snprintf(info, sizeof(info), "%s", p->said);
    else if (p->alive && ot >= 0) snprintf(info, sizeof(info), "%s%ssettings are in the %s tab", p->desc, *p->desc ? ", " : "", all_tabs[ot]);
    else if (p->alive) snprintf(info, sizeof(info), "%s", *p->desc ? p->desc : "running");
    else if (p->bfail || p->bjob) snprintf(info, sizeof(info), "%s", p->status), bad = p->bfail;
    else if (r < 0) snprintf(info, sizeof(info), "%s", why), miss = plug_missing(p), bad = 1;
    else if (p->on) snprintf(info, sizeof(info), "%s", *p->status ? p->status : "starting"), bad = !p->want;
    else if (!p->trusted) snprintf(info, sizeof(info), "%s", *p->desc ? p->desc : "off, it asks you first when you turn it on");
    else snprintf(info, sizeof(info), "%s", *p->desc ? p->desc : "off");
    if (draw) draw_text(c, cx + (cx > m.x + pad + 8 ? 3 : 2), y + 1, fit_text(c, info, m.x + m.w - pad - cx - 12), 0, bad ? 0xFFFF7A5A : TH->sub);
    y += 22;
    for (int k = 0; p->bfail && k < 3 && p->blog[k][0]; k++, y += 18)
        if (draw) draw_text(c, m.x + pad + 18, y, fit_text(c, p->blog[k], w - 28), 0, (TH->sub & 0xFFFFFF) | 0xC0000000);
    if (osok && (miss >= 0 || p->bfail)) {
        char gl[40];
        snprintf(gl, sizeof(gl), "Get %s", plug_tool_name(miss));
        const char *again = p->bfail ? "Build again" : "Check again";
        double gw = miss >= 0 ? fmax(100, text_lw(c, gl, 0) + 24) : 0, aw = fmax(110, text_lw(c, again, 0) + 24);
        Rect gb = {m.x + pad + 10, y, gw, 24}, ab = {gb.x + (gw ? gw + 8 : 0), y, aw, 24};
        if (draw) {
            if (miss >= 0) draw_button(c, gb, gl, TH->title, 0xFF181818);
            draw_button(c, ab, again, TH->btn, TH->btn_fg);
            if (hit && miss >= 0) add_hit_arg(gb, ACT_PLUG_GET, miss);
            if (hit) add_hit_arg(ab, ACT_PLUG_RECHECK, i);
            if (miss >= 0) tip_spot(gb, "opens the download page. install it, then hit Check again");
            tip_spot(ab, p->bfail ? "builds it again, after you fixed whatever build.log says" : "looks again, no restart needed");
        }
        y += 30;
    }
    if (p->alive && ot < 0 && p->nset) y = plug_grid(c, m, y, pad, i, draw, hit) + 2;
    return y + 6;
}

double plug_tab(Canvas *c, Rect m, double y, double pad, int t, int draw, int hit)
{
    double w = m.w - 2 * pad;
    if (t != TAB_PLUG) {
        for (int i = 0; i < nplugs; i++) {
            if (own_tab(&plugs[i]) != t) continue;
            if (draw) draw_text(c, m.x + pad + 2, y, fit_text(c, plugs[i].title, w), 0, TH->title);
            y = plug_grid(c, m, y + 22, pad, i, draw, hit) + 6;
        }
        return y + 30;
    }
    y = plug_add_row(c, m, y, pad, draw, hit);
    if (!nplugs) {
        char ln[3][100];
        int n = wrap_text(c, "plugins draw on the board, add settings and react to moves. paste a github link up there or grab the template", w - 60, ln, 3);
        Rect card = {m.x + pad, y, w, 50 + n * 18 + 40};
        if (draw) {
            const char *hd = "No plugins yet";
            draw_round_rect(c, card, 10, TH->item);
            draw_text(c, card.x + (card.w - text_lw(c, hd, 1)) / 2, card.y + 14, hd, 1, TH->title);
            for (int k = 0; k < n; k++) draw_text(c, card.x + (card.w - text_lw(c, ln[k], 0)) / 2, card.y + 46 + k * 18, ln[k], 0, TH->sub);
            double b1 = fmax(150, text_lw(c, "Get the template", 0) + 24), b2 = fmax(150, text_lw(c, "How to make one", 0) + 24);
            Rect tb = {card.x + (card.w - b1 - b2 - 8) / 2, card.y + card.h - 36, b1, 26}, hb = {tb.x + b1 + 8, tb.y, b2, 26};
            draw_button(c, tb, "Get the template", TH->btn, TH->btn_fg);
            draw_button(c, hb, "How to make one", TH->btn, TH->btn_fg);
            if (hit) add_hit(tb, ACT_PLUG_TEMPLATE), add_hit(hb, ACT_PLUG_HELP);
            tip_spot(tb, "adds the template plugin, it shows off everything a plugin can do. copy it and change it into your own");
            tip_spot(hb, "opens PLUGINS.md on github");
        }
        return card.y + card.h + 10 + 30;
    }
    for (int i = 0; i < nplugs; i++) y = plug_entry(c, m, y, pad, i, draw, hit);
    return y + 30;
}

static void draw_ask(Canvas *dst, Rect at, Rect bl, Rect outr, const char *title, char ln[][100], const uint32_t *col, int n, const char *yes, const char *no, int ayes, int ano, int hit)
{
    double w = 380;
    for (int k = 0; k < n; k++) w = fmax(w, text_lw(dst, ln[k], 0) + 28);
    w = fmin(w, 560);
    Rect q = {at.x + (at.w - w) / 2, at.y + 40, w, 44 + n * 18 + 50};
    if (q.x + q.w > outr.x + outr.w - 4) q.x = outr.x + outr.w - 4 - q.w;
    if (q.x < outr.x + 4) q.x = outr.x + 4;
    if (q.y + q.h > outr.y + outr.h - 4) q.y = outr.y + outr.h - 4 - q.h;
    if (q.y < outr.y + 4) q.y = outr.y + 4;
    covers(q, bl);
    Canvas *c = dst;
    panel_bg(c, q, 14, TH->menu);
    draw_frame(c, q, 1, 0x60FF6A5A);
    if (hit) add_hit(q, ACT_BLOCK);
    draw_text(c, q.x + 14, q.y + 12, fit_text(c, title, q.w - 28), 0, TH->title);
    for (int k = 0; k < n; k++) draw_text(c, q.x + 14, q.y + 38 + k * 18, fit_text(c, ln[k], q.w - 28), 0, col[k]);
    double yw = fmax(130, text_lw(c, yes, 0) + 24), nw = fmax(104, text_lw(c, no, 0) + 24);
    Rect a = {q.x + 14, q.y + q.h - 42, yw, 28}, b = {a.x + yw + 10, a.y, nw, 28};
    draw_button(c, a, yes, 0xFFD03A3A, 0xFFFFFFFF);
    draw_button(c, b, no, 0xFF3A3A3A, 0xFFFFFFFF);
    if (hit) add_hit(a, ayes), add_hit(b, ano);
}

void plug_items(Canvas *c, Rect bl, int bottom, double tk)
{
    for (int i = 0; i < nplugs; i++) {
        Plugin *p = &plugs[i];
        for (int k = 0; p->alive && k < p->nitem; k++) {
            PlugItem *it = &p->item[k];
            double x = bl.x + it->x, y = bl.y + it->y;
            if (it->sq >= 0 && it->kind < PD_ARROW) {
                Rect q = sq_rect(bl, it->sq, bottom);
                x = q.x + it->x + (it->kind <= PD_BIG ? 4 : 0), y = q.y + it->y + (it->kind <= PD_BIG ? 2 : 0);
            }
            if (it->kind == PD_TEXT || it->kind == PD_BIG) draw_text(c, x, y, it->text, it->kind == PD_BIG, it->col);
            else if (it->kind == PD_RECT) draw_rect(c, (Rect){x, y, it->w, it->h}, it->col);
            else if (it->kind == PD_BOX) draw_frame(c, (Rect){x, y, it->w, it->h}, 2, it->col);
            else if (it->kind == PD_ARROW && it->sq != it->sq2)
                draw_move(c, bl, MK_MOVE(it->sq, it->sq2, 0, 0), bottom, it->col, 1, tk, 0, 0, NULL);
            else if (it->kind == PD_SQUARE)
                mark_square(c, sq_rect(bl, it->sq, bottom), 1, it->col);
        }
    }
}

static int overlaps(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

void plug_panels(Canvas *c, Rect bl, Rect outr, Rect info, int left) // stacked down the side of the board the info box isnt on
{
    double y = bl.y, gap = 8;
    for (int i = 0; i < nplugs; i++) {
        PlugPanel *pn = &plugs[i].panel;
        if (!plugs[i].alive || (!*pn->title && !pn->nline)) continue;
        double w = text_lw(c, pn->title, 0);
        for (int k = 0; k < pn->nline; k++) w = fmax(w, text_lw(c, pn->line[k], 0));
        w = fmin(320, fmax(150, w + 24));
        double h = 12 + (*pn->title ? 21 : 0) + pn->nline * 19;
        Rect r = {left ? bl.x - gap - w : bl.x + bl.w + gap, y, w, h};
        if (r.x < outr.x + 4 || r.x + w > outr.x + outr.w - 4) r.x = left ? bl.x + bl.w + gap : bl.x - gap - w;
        if (r.x < outr.x + 4) r.x = outr.x + 4;
        if (r.x + w > outr.x + outr.w - 4) r.x = outr.x + outr.w - 4 - w;
        if (info.w > 0 && overlaps(r, info)) r.y = info.y + info.h + gap;
        panel_bg(c, r, 10, faded(TH->panel));
        double ty = r.y + 6;
        if (*pn->title) draw_text(c, r.x + 12, ty, fit_text(c, pn->title, w - 24), 0, TH->title), ty += 21;
        for (int k = 0; k < pn->nline; k++, ty += 19)
            draw_text(c, r.x + 12, ty, fit_text(c, pn->line[k], w - 24), 0, pn->col[k] ? pn->col[k] : TH->title_off);
        y = r.y + h + gap;
    }
}

static void note_cb(const Plugin *p, const char *msg) { set_note(msg); }

static int grab_cb(double x, double y, double w, double h, const char *path, int *pw, int *ph)
{
    if (state != TRACKING || board.out >= cap->nout) return 0;
    Output *o = &cap->out[board.out];
    if (!o->have_frame) return 0;
    Rect bl = board_logical(&board);
    double k = kx(board.out);
    int x0 = (int)floor((bl.x + x - o->logical.x) / k), y0 = (int)floor((bl.y + y - o->logical.y) / k);
    int x1 = (int)ceil((bl.x + x + w - o->logical.x) / k), y1 = (int)ceil((bl.y + y + h - o->logical.y) / k);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > o->frame.w) x1 = o->frame.w;
    if (y1 > o->frame.h) y1 = o->frame.h;
    if (x1 - x0 > 2048) x1 = x0 + 2048;
    if (y1 - y0 > 2048) y1 = y0 + 2048;
    if (x1 <= x0 || y1 <= y0) return 0;
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    int fw = x1 - x0, ok = 1;
    fprintf(f, "P6\n%d %d\n255\n", fw, y1 - y0);
    uint8_t *row = malloc((size_t)fw * 3);
    if (!row) ok = 0;
    for (int yy = y0; ok && yy < y1; yy++) {
        const uint8_t *s = o->frame.px + (size_t)yy * o->frame.stride + (size_t)x0 * 4;
        for (int xx = 0; xx < fw; xx++, s += 4) {
            row[xx * 3] = o->frame.bgr ? s[2] : s[0];
            row[xx * 3 + 1] = s[1];
            row[xx * 3 + 2] = o->frame.bgr ? s[0] : s[2];
        }
        ok = fwrite(row, 3, fw, f) == (size_t)fw;
    }
    free(row);
    if (fclose(f) || !ok || replace_file(tmp, path)) {
        remove(tmp);
        return 0;
    }
    *pw = fw, *ph = y1 - y0;
    return 1;
}

static void plug_events(void) // tells them whatever changed since last time
{
    static int was = -1, was_bottom, was_team;
    static Rect was_bl;
    static u64 was_key;
    static Move was_best;
    static int was_depth;
    static Pos was_pos;
    int fresh = 0;
    for (int i = 0; i < nplugs; i++)
        if (plugs[i].fresh) fresh = 1, plugs[i].fresh = 0;
    if (!plug_running()) return;
    if (state != TRACKING) {
        if (was == TRACKING || fresh) plug_send_all("board none");
        was = state;
        return;
    }
    Rect bl = board_logical(&board);
    if (fresh || was != TRACKING || fabs(bl.x - was_bl.x) > 0.5 || fabs(bl.y - was_bl.y) > 0.5 || fabs(bl.w - was_bl.w) > 0.5 ||
        tracker.white_bottom != was_bottom || tracker.user_color != was_team) {
        plug_send_all("board %.0f %.0f %.0f %s", bl.x, bl.y, bl.w, tracker.white_bottom ? "white" : "black");
        plug_send_all("team %s", tracker.user_color == WHITE ? "white" : "black");
        if (fresh || was != TRACKING) was_key = 0;
        was_bl = bl, was_bottom = tracker.white_bottom, was_team = tracker.user_color;
    }
    was = TRACKING;
    Move ms[256];
    if (tracker.pos.key != was_key) {
        char fen[128], u[8], san[16] = "";
        pos_to_fen(&tracker.pos, fen);
        plug_send_all("position %s", fen);
        if (was_key && tracker.last_move) {
            Pos t = was_pos;
            int nm = pos_gen_legal(&t, ms), ok = 0;
            for (int q = 0; q < nm; q++) ok |= ms[q] == tracker.last_move;
            Undo un;
            t = was_pos;
            if (ok && pos_make(&t, tracker.last_move, &un) && t.key == tracker.pos.key) t = was_pos, move_to_san(&t, tracker.last_move, san);
            move_to_uci(tracker.last_move, u);
            plug_send_all("move %s%s%s", u, *san ? " " : "", san);
        }
        Pos t = tracker.pos;
        if (!pos_gen_legal(&t, ms)) {
            int mate = pos_in_check(&tracker.pos, tracker.pos.side);
            plug_send_all("gameover %s %s", mate ? "checkmate" : "stalemate", !mate ? "draw" : tracker.pos.side == tracker.user_color ? "loss" : "win");
        }
        was_key = tracker.pos.key, was_pos = tracker.pos, was_best = 0, was_depth = 0;
    }
    Analysis an;
    if (analyzer_get(&an) && an.key == tracker.pos.key && an.best && (an.best != was_best || an.depth != was_depth)) {
        char u[8], san[16];
        Pos t = tracker.pos;
        move_to_san(&t, an.best, san);
        move_to_uci(an.best, u);
        plug_send_all("best %s %s %s %d %d", u, san, an.mate ? "mate" : "cp", an.mate ? an.mate : an.score, an.depth);
        was_best = an.best, was_depth = an.depth;
    }
}

int plug_pump(void) // 1 = redraw
{
    if (!nplugs) return 0;
    plug_events();
    return plug_poll(now_ms());
}

static char pget_sub[200], pget_repo[64];
static int gh_part(const char *s, size_t n) // letters numbers - _ . only, no . or ..
{
    if (!n || n > 60 || (n <= 2 && s[0] == '.' && s[n - 1] == '.')) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-' || s[i] == '_' || s[i] == '.')) return 0;
    return 1;
}

static int gh_parse(const char *in, char *url, size_t un)
{
    while (*in == ' ' || *in == '\t' || *in == '\n' || *in == '\r') in++;
    static const char *skip[] = {"https://", "http://", "www.", "github.com/"};
    for (int k = 0; k < 4; k++)
        if (!strncmp(in, skip[k], strlen(skip[k]))) in += strlen(skip[k]);
    const char *part[16];
    size_t len[16];
    int np = 0;
    for (const char *q = in; *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '?' && *q != '#' && np < 16;) {
        size_t l = strcspn(q, "/ \n\r?#");
        if (l) part[np] = q, len[np++] = l;
        q += l;
        if (*q == '/') q++;
    }
    if (np < 2) return 0;
    if (len[1] > 4 && !strncmp(part[1] + len[1] - 4, ".git", 4)) len[1] -= 4;
    char br[64] = "HEAD";
    *pget_sub = 0;
    if (np >= 4 && len[2] == 4 && !strncmp(part[2], "tree", 4)) {
        if (!gh_part(part[3], len[3])) return 0;
        snprintf(br, sizeof(br), "%.*s", (int)len[3], part[3]);
        for (int k = 4; k < np; k++) {
            if (!gh_part(part[k], len[k])) return 0;
            size_t l = strlen(pget_sub);
            snprintf(pget_sub + l, sizeof(pget_sub) - l, "%s%.*s", l ? "/" : "", (int)len[k], part[k]);
        }
    } else if (np > 2)
        return 0;
    if (!gh_part(part[0], len[0]) || !gh_part(part[1], len[1])) return 0;
    snprintf(pget_repo, sizeof(pget_repo), "%.*s", (int)len[1], part[1]);
    snprintf(url, un, "https://codeload.github.com/%.*s/%.*s/tar.gz/%s", (int)len[0], part[0], (int)len[1], part[1], br);
    return 1;
}

static void plug_import(const char *link)
{
    char url[400], dir[1200];
    plug_typing = 0;
    if (pget_pid) {
        set_note("already getting one, hang on");
        return;
    }
    if (!gh_parse(link, url, sizeof(url))) {
        set_note("that doesnt look like a github link");
        return;
    }
    snprintf(dir, sizeof(dir), "%s/plugin-dl", data_dir);
    pget_pid = tgz_start(url, dir);
    set_note(pget_pid ? "getting the plugin..." : "couldnt start the download");
}

void plug_import_poll(void)
{
    int ok;
    if (!pget_pid || !job_done(pget_pid, &ok)) return;
    pget_pid = 0;
    char dir[1200], pdir[1200], msg[200] = "";
    snprintf(dir, sizeof(dir), "%s/plugin-dl", data_dir);
    snprintf(pdir, sizeof(pdir), "%s/plugins", data_dir);
    if (!ok) {
        rm_tree(dir);
        set_note("download failed, check the link and your internet");
        return;
    }
    static char src[PL_MAX][1100];
    char name[PL_MAX][32], back[PL_MAX] = {0};
    int n = plug_found(dir, pget_sub, pget_repo, src, name, PL_MAX), got = 0, upd = 0;
    make_dir(pdir);
    for (int k = 0; k < n; k++) {
        int was = -1;
        for (int i = 0; i < nplugs; i++)
            if (!strcmp(plugs[i].name, name[k])) was = i;
        if (was < 0 && nplugs >= PL_MAX) break;
        back[k] = was >= 0 && plugs[was].on;
        char dst[1300];
        snprintf(dst, sizeof(dst), "%.1200s/%.31s", pdir, name[k]);
        if (was >= 0) {
            plug_stop(&plugs[was], "off");
            memmove(&plugs[was], &plugs[was + 1], sizeof(plugs[0]) * (size_t)(nplugs - was - 1));
            nplugs--;
            upd++;
        }
        rm_tree(dst);
        if (rename(src[k], dst)) continue;
        snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "%s%.31s", got ? ", " : "", name[k]);
        got++;
    }
    rm_tree(dir);
    plug_scan(data_dir, verbose);
    for (int k = 0; k < n; k++)
        for (int i = 0; i < nplugs; i++)
            if (back[k] && !strcmp(plugs[i].name, name[k]) && !plugs[i].alive) plugs[i].on = 1, plug_start(&plugs[i]);
    char note[260];
    if (!n) snprintf(note, sizeof(note), "theres no plugin.txt in there, is it a plugin?");
    else if (!got) snprintf(note, sizeof(note), "couldnt add it, %s", nplugs >= PL_MAX ? "you have the most plugins it can hold" : "the plugins folder wont take it");
    else {
        char says[200] = "";
        for (int k = 0; k < n && !*says; k++)
            for (int i = 0; i < nplugs; i++)
                if (!strcmp(plugs[i].name, name[k]) && plug_flags(&plugs[i])) plug_says(&plugs[i], says, sizeof(says));
        if (*says) snprintf(note, sizeof(note), "%s %.60s, heads up: it %.120s. it shows you what it found before it runs", upd == got ? "updated" : "added", msg, says);
        else if (upd == got) snprintf(note, sizeof(note), "updated %s", msg);
        else snprintf(note, sizeof(note), "added %s, turn it on below", msg);
    }
    set_note(note);
}

void plug_rescan(void) // picks up folders dropped in by hand, the ones it has stay as they are
{
    plug_scan(data_dir, verbose);
    for (int i = 0; i < nplugs; i++)
        if (plugs[i].on && !plugs[i].alive && !plugs[i].bjob && !plugs[i].want && !strcmp(plugs[i].status, "off")) plug_start(&plugs[i]);
}

void plug_key(int ch)
{
    size_t l = strlen(plug_url);
    char clip[160];
    if (ch == 27) plug_typing = 0;
    else if (ch == 13 || ch == 10) plug_import(plug_url);
    else if (ch == 8 && l) plug_url[l - 1] = 0;
    else if (ch == 127) plug_url[0] = 0;
    else if (ch == 22 && clip_get(clip, sizeof(clip))) snprintf(plug_url, sizeof(plug_url), "%s", clip); // ctrl+v
    else if (ch > 32 && ch < 127 && l + 1 < sizeof(plug_url)) plug_url[l] = (char)ch, plug_url[l + 1] = 0;
}

static void end_pslide(void)
{
    Plugin *p = &plugs[pslide / 64];
    int k = pslide % 64;
    pslide = -1;
    if (!p->alive || k >= p->nset || p->set[k].v == pslide_v0) return;
    int v = p->set[k].v;
    p->set[k].v = pslide_v0;
    plug_change(p, k, v);
    save_cfg();
}

static void plug_turn(Plugin *p, int on)
{
    char k[80];
    p->on = on;
    snprintf(k, sizeof(k), "plugin.%s", p->name);
    plug_cfg_line(k, on ? "1" : "0");
    if (on) plug_start(p);
    else plug_stop(p, "off");
    plug_poll(now_ms());
    save_cfg();
}

int plug_menu_rows(const char *label[], const char *tip[], int arg[])
{
    int pi[PLUG_MENU_MAX], k[PLUG_MENU_MAX], n = plug_menu_list(pi, k, PLUG_MENU_MAX);
    for (int q = 0; q < n; q++) {
        const PlugMenu *pm = &plugs[pi[q]].menu[k[q]];
        label[q] = pm->label, tip[q] = pm->tip, arg[q] = pi[q] * 64 + k[q];
    }
    return n;
}

void plug_folder_button(Canvas *c, Rect rs, int hit)
{
    Rect pf = {rs.x + rs.w + 8, rs.y, 170, 26};
    draw_button(c, pf, "Open plugins folder", TH->btn, TH->btn_fg);
    if (hit) add_hit_arg(pf, ACT_PLUG_DIR, -1);
    tip_spot(pf, "each plugin goes in its own folder in here, then turn it on above. restart open-chease after adding one");
}

void plug_popups(Canvas *c, Rect bl, Rect p, Rect outr, int hit)
{
    if (plug_ask >= 0 && plug_ask < nplugs && settings_open) {
        Plugin *pl = &plugs[plug_ask];
        char ln[14][100], t[100];
        uint32_t col[14];
        int n = 0, fl = plug_flags(pl);
        for (int k = PF_N - 1; k >= 0 && n < 12; k--) {
            if (!(fl >> k & 1)) continue;
            snprintf(ln[n], 100, "%s%s%.50s", plug_flag_says[k], k == PF_NET && *pl->net_why ? ", it says: " : "", k == PF_NET ? pl->net_why : "");
            col[n++] = k > PF_BIN ? 0xFFFF6A5A : k == PF_BIN ? 0xFFFF9A50 : 0xFFE8B850;
            for (int j = 0; j < pl->nflag[k] && j < 2 && n < 12; j++) snprintf(ln[n], 100, "    %s", pl->flag[k][j]), col[n++] = TH->sub;
            if (pl->nflag[k] > 2 && n < 12) snprintf(ln[n], 100, "    and %d more", pl->nflag[k] - 2), col[n++] = TH->sub;
        }
        snprintf(ln[n], 100, "only say yes if you trust whoever made it"), col[n++] = TH->item_fg;
        snprintf(t, sizeof(t), "Turn on %s?", pl->title);
        draw_ask(c, set_box, bl, outr, t, ln, col, n, "Turn on anyway", "Cancel", ACT_PLUG_YES, ACT_PLUG_NO, hit);
    }
    int na = -1;
    for (int i = 0; i < nplugs && na < 0; i++)
        if (plugs[i].net_new) na = i;
    if (na >= 0 && !(plug_ask >= 0 && settings_open)) {
        Plugin *pl = &plugs[na];
        char ln[4][100], t[100];
        uint32_t col[4] = {TH->item_fg, TH->sub, TH->sub, TH->sub};
        int n = 0;
        snprintf(ln[n++], 100, "it connected to %s", pl->online);
        if (*pl->net_why) snprintf(ln[n++], 100, "it says why: %.80s", pl->net_why);
        else snprintf(ln[n++], 100, "it never said it would, so its off for now");
        snprintf(t, sizeof(t), "%.60s went online", pl->title);
        Rect at = settings_open ? set_box : (Rect){p.x + p.w - 380, p.y + p.h - 34, 380, 0};
        draw_ask(c, at, bl, outr, t, ln, col, n, pl->alive ? "Turn it off" : "Keep it off", pl->alive ? "Its fine" : "Its fine, turn on", ACT_NET_OFF, ACT_NET_OK, hit);
    }
}

int plug_sliding(void) { return pslide >= 0; }

int plug_drag(int held, double x)
{
    Plugin *p = &plugs[pslide / 64];
    int k = pslide % 64;
    if (!held || !p->alive || k >= p->nset) {
        end_pslide();
        redraw();
        return 0;
    }
    int v = plug_slider_v(&p->set[k], pslide_r, x);
    if (v != p->set[k].v) p->set[k].v = v, redraw();
    return 1;
}

void plug_reset_tab(int t)
{
    for (int pi = 0; pi < nplugs; pi++) {
        Plugin *p = &plugs[pi];
        if (!p->alive || (t == TAB_PLUG ? own_tab(p) >= 0 : own_tab(p) != t)) continue;
        for (int k = 0; k < p->nset; k++)
            if (p->set[k].kind != PS_BUTTON && p->set[k].v != p->set[k].def) plug_change(p, k, p->set[k].def);
    }
}

void plug_click_seen(int act)
{
    if (act != ACT_PLUG_URL && act != ACT_PLUG_PASTE) plug_typing = 0;
    if (act != ACT_PLUG_YES && act != ACT_BLOCK) plug_ask = -1;
}

void plug_click(int act, int arg, Rect r, double x, int can_hold)
{
    switch (act) {
    case ACT_PLUG_ON: {
        Plugin *p = &plugs[arg];
        plug_ask = -1;
        int up = !p->on || (!p->alive && !p->trusted); // on but waiting for an ok counts as turning it on
        if (up) {
            plug_recheck(p);
            if (!p->trusted) {
                plug_ask = arg;
                break;
            }
        }
        plug_turn(p, up);
        break;
    }
    case ACT_PLUG_YES:
        if (plug_ask >= 0 && plug_ask < nplugs) plug_trust(&plugs[plug_ask]), plug_turn(&plugs[plug_ask], 1);
        plug_ask = -1;
        break;
    case ACT_PLUG_NO:
        if (plug_ask >= 0 && plug_ask < nplugs && plugs[plug_ask].on && !plugs[plug_ask].alive) plug_turn(&plugs[plug_ask], 0);
        plug_ask = -1;
        break;
    case ACT_NET_OFF:
    case ACT_NET_OK:
        for (int k = 0; k < nplugs; k++) {
            Plugin *p = &plugs[k];
            if (!p->net_new) continue;
            p->net_new = 0;
            if (act == ACT_NET_OFF) plug_turn(p, 0), set_note("turned it off");
            else {
                plug_trust(p);
                plug_net_fine(p, 1);
                if (!p->alive) plug_turn(p, 1);
                else save_cfg();
            }
            break;
        }
        break;
    case ACT_PLUG_URL:
        plug_typing = 1, set_typing = 0, dd_open = -1;
        break;
    case ACT_PLUG_PASTE: {
        char clip[160];
        if (clip_get(clip, sizeof(clip))) {
            char *e = clip + strcspn(clip, "\r\n");
            *e = 0;
            snprintf(plug_url, sizeof(plug_url), "%s", clip);
        } else
            set_note("nothing to paste, copy the link first");
        plug_typing = 1;
        break;
    }
    case ACT_PLUG_ADD:
        if (*plug_url) plug_import(plug_url);
        else plug_typing = 1, set_note("paste a github link in first");
        break;
    case ACT_PLUG_TEMPLATE:
        plug_import("sevvy-wevvy/open-chease/tree/plugin-template/template");
        break;
    case ACT_PLUG_HELP:
        if (open_folder("https://github.com/sevvy-wevvy/open-chease/blob/dev/docs/PLUGINS.md")) set_note("couldnt open the browser");
        break;
    case ACT_PLUG_MENU:
        menu_open = 0;
        plug_menu_click(arg / 64, arg % 64);
        break;
    case ACT_PLUG_LINK: {
        char link[220];
        if (plug_link(plugs[arg].url, link, sizeof(link)) && open_folder(link)) set_note("couldnt open the browser");
        break;
    }
    case ACT_PLUG_UPDATE:
        plug_import(plugs[arg].update);
        break;
    case ACT_PLUG_GET:
        if (plug_tool_url(arg) && open_folder(plug_tool_url(arg))) set_note("couldnt open the browser");
        break;
    case ACT_PLUG_RECHECK:
        set_note(plugs[arg].bfail ? "building it again" : "looking again");
        plug_tools_recheck();
        break;
    case ACT_PLUG_DIR: {
        char dir[1100];
        if (arg >= 0) snprintf(dir, sizeof(dir), "%s", plugs[arg].dir);
        else snprintf(dir, sizeof(dir), "%s/plugins", data_dir), make_dir(dir);
        if (open_folder(dir)) set_note("couldnt open the folder");
        break;
    }
    case ACT_PLUG_SET: {
        Plugin *p = &plugs[arg / 64];
        int k = arg % 64;
        if (!p->alive || k >= p->nset) break;
        PlugSet *ps = &p->set[k];
        if (ps->kind == PS_SLIDER) break; // the bar takes it
        plug_change(p, k, ps->kind == PS_BOOL ? !ps->v : ps->kind == PS_CHOICE ? (ps->v + 1) % ps->nch : 0);
        if (ps->kind != PS_BUTTON) save_cfg();
        break;
    }
    case ACT_PLUG_SLIDE: {
        Plugin *p = &plugs[arg / 64];
        int k = arg % 64;
        if (!p->alive || k >= p->nset) break;
        pslide_v0 = p->set[k].v;
        p->set[k].v = plug_slider_v(&p->set[k], r, x);
        pslide = arg, pslide_r = r;
        if (!can_hold) end_pslide();
        break;
    }
    }
}

void plug_boot(void)
{
    plug_note = note_cb, plug_grab = grab_cb;
    plug_scan(data_dir, verbose);
    for (int i = 0; i < nplugs; i++)
        if (plugs[i].on) plug_start(&plugs[i]);
}
