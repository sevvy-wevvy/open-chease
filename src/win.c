#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

typedef struct {
    RECT r; // physical px, virtual screen coords
    HDC cap_dc;
    HBITMAP cap_bmp;
    uint8_t *cap_px;
    HWND win;
    HDC dc;
    HBITMAP bmp;
    uint32_t *px;
    Canvas canvas;
    int shown;
} Mon;

static Mon mons[MAX_OUTPUTS];
static int nmons;
static double scale = 1;
static Backend *live_cap;
static Overlay *live_ov;
static Rect hits[256];
static int nhits, clicked, keyq[32], nkeys, wheel, kb_on;
static double click_x, click_y, wheel_x, wheel_y;
static HWND hot, before_kb;

static void dpi_aware(void)
{
    HMODULE u = GetModuleHandleA("user32.dll");
    typedef BOOL(WINAPI * ctx_fn)(HANDLE);
    ctx_fn f = u ? (ctx_fn)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (!f || !f((HANDLE)-4)) SetProcessDPIAware(); // -4 = per monitor v2, needs win10 1703
    typedef UINT(WINAPI * dpi_fn)(void);
    dpi_fn g = u ? (dpi_fn)(void *)GetProcAddress(u, "GetDpiForSystem") : NULL;
    UINT dpi = g ? g() : 96;
    if (!g) {
        HDC dc = GetDC(NULL);
        dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    scale = dpi >= 96 ? dpi / 96.0 : 1;
}

static BOOL CALLBACK on_mon(HMONITOR m, HDC dc, LPRECT r, LPARAM p)
{
    RECT *list = (RECT *)p;
    int *n = (int *)(list + MAX_OUTPUTS);
    if (*n < MAX_OUTPUTS) list[(*n)++] = *r;
    return TRUE;
}

static int list_mons(RECT *out)
{
    struct {
        RECT r[MAX_OUTPUTS];
        int n;
    } l;
    l.n = 0;
    EnumDisplayMonitors(NULL, NULL, on_mon, (LPARAM)&l);
    memcpy(out, l.r, sizeof(RECT) * l.n);
    return l.n;
}

static HBITMAP make_dib(HDC dc, int w, int h, void **bits)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, bits, NULL, 0);
}

static void drop_cap(Mon *m)
{
    if (m->cap_dc) DeleteDC(m->cap_dc);
    if (m->cap_bmp) DeleteObject(m->cap_bmp);
    m->cap_dc = NULL, m->cap_bmp = NULL, m->cap_px = NULL;
}

static void drop_win(Mon *m)
{
    if (m->win) DestroyWindow(m->win);
    if (m->dc) DeleteDC(m->dc);
    if (m->bmp) DeleteObject(m->bmp);
    if (hot == m->win) hot = NULL;
    m->win = NULL, m->dc = NULL, m->bmp = NULL, m->px = NULL, m->shown = 0;
    memset(&m->canvas, 0, sizeof(m->canvas));
}

static void add_win(Mon *m);

static void sync(void)
{
    if (live_cap) {
        live_cap->nout = nmons;
        for (int i = 0; i < MAX_OUTPUTS; i++) {
            Output *o = &live_cap->out[i];
            if (i >= nmons) {
                o->have_frame = 0;
                continue;
            }
            snprintf(o->name, sizeof(o->name), "screen %d", i + 1);
            RECT r = mons[i].r;
            o->logical = (Rect){r.left / scale, r.top / scale, (r.right - r.left) / scale, (r.bottom - r.top) / scale};
        }
    }
    if (live_ov) live_ov->ncanvas = nmons;
}

static int refresh_mons(void)
{
    RECT now[MAX_OUTPUTS];
    int n = list_mons(now), same = n == nmons;
    for (int i = 0; same && i < n; i++) same = !memcmp(&now[i], &mons[i].r, sizeof(RECT));
    if (same) return 0;
    for (int i = 0; i < nmons; i++) drop_cap(&mons[i]), drop_win(&mons[i]);
    memset(mons, 0, sizeof(mons));
    nmons = n;
    for (int i = 0; i < n; i++) mons[i].r = now[i];
    if (live_ov)
        for (int i = 0; i < n; i++) add_win(&mons[i]);
    sync();
    if (live_cap) live_cap->out_gen++;
    return 1;
}

static int win_cap_init(Backend *b)
{
    dpi_aware();
    live_cap = b;
    refresh_mons();
    sync();
    return nmons > 0;
}

static int win_capture(Backend *b)
{
    static DWORD last_check;
    if (GetTickCount() - last_check > 2000) {
        last_check = GetTickCount();
        if (refresh_mons()) fprintf(stderr, "screens changed\n");
    }
    HDC screen = GetDC(NULL);
    int got = 0;
    for (int i = 0; i < nmons; i++) {
        Mon *m = &mons[i];
        Output *o = &b->out[i];
        int w = m->r.right - m->r.left, h = m->r.bottom - m->r.top;
        if (!m->cap_dc) {
            m->cap_dc = CreateCompatibleDC(screen);
            m->cap_bmp = make_dib(screen, w, h, (void **)&m->cap_px);
            if (m->cap_bmp) SelectObject(m->cap_dc, m->cap_bmp);
        }
        // CAPTUREBLT so our own overlay is in the grab like on linux
        if (!m->cap_px || !BitBlt(m->cap_dc, 0, 0, w, h, screen, m->r.left, m->r.top, SRCCOPY | CAPTUREBLT)) {
            o->have_frame = 0;
            continue;
        }
        GdiFlush();
        o->frame = (Frame){m->cap_px, w, h, w * 4, 1};
        o->have_frame = 1;
        got++;
    }
    ReleaseDC(NULL, screen);
    return got > 0;
}

static Backend win_backend = {.name = "gdi", .init = win_cap_init, .capture = win_capture};
Backend *backend_win(void) { return &win_backend; }

static int on_hit(double lx, double ly)
{
    for (int i = 0; i < nhits; i++)
        if (lx >= hits[i].x && lx < hits[i].x + hits[i].w && ly >= hits[i].y && ly < hits[i].y + hits[i].h) return 1;
    return 0;
}

static void set_clicky(HWND w, int on);

static Mon *mon_of(HWND w)
{
    for (int i = 0; i < nmons; i++)
        if (mons[i].win == w) return &mons[i];
    return NULL;
}

static int fwd_down;

static LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    Mon *m = mon_of(w);
    switch (msg) {
    case WM_LBUTTONDOWN:
        fwd_down = 0;
        if (m) {
            double x = (m->r.left + (short)LOWORD(lp)) / scale, y = (m->r.top + (short)HIWORD(lp)) / scale;
            if (!on_hit(x, y)) { // missed the buttons, hand the click to the game instead
                set_clicky(w, 0);
                hot = NULL;
                int sw = GetSystemMetrics(SM_SWAPBUTTON); // sendinput and the key state are physical buttons, this message is the logical left one
                INPUT in[2];
                memset(in, 0, sizeof(in));
                in[0].type = in[1].type = INPUT_MOUSE;
                in[0].mi.dwFlags = sw ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
                in[1].mi.dwFlags = sw ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
                if (GetAsyncKeyState(sw ? VK_RBUTTON : VK_LBUTTON) & 0x8000) {
                    SendInput(1, in, sizeof(INPUT));
                    fwd_down = 1; // the real release goes to the game now, unless it already got queued to us
                } else {
                    SendInput(2, in, sizeof(INPUT)); // already let go, a lone down would leave the piece stuck to the mouse
                }
                return 0;
            }
            clicked = 1;
            click_x = x, click_y = y;
        }
        return 0;
    case WM_LBUTTONUP:
        if (fwd_down) {
            fwd_down = 0;
            INPUT in;
            memset(&in, 0, sizeof(in));
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = GetSystemMetrics(SM_SWAPBUTTON) ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
            SendInput(1, &in, sizeof(in));
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
        wheel = (short)HIWORD(wp) < 0 ? 1 : -1;
        wheel_x = p.x / scale, wheel_y = p.y / scale;
        return 0;
    }
    case WM_CHAR:
        if (nkeys < 32 && wp < 128) keyq[nkeys++] = (int)wp;
        return 0;
    case WM_MOUSEACTIVATE:
        return kb_on ? MA_ACTIVATE : MA_NOACTIVATE; // clicking a button shouldnt steal focus from the game
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void add_win(Mon *m)
{
    static int registered;
    HINSTANCE inst = GetModuleHandleA(NULL);
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = "open-chease";
        RegisterClassA(&wc);
        registered = 1;
    }
    int w = m->r.right - m->r.left, h = m->r.bottom - m->r.top;
    m->win = CreateWindowExA(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "open-chease",
                             "open-chease", WS_POPUP, m->r.left, m->r.top, w, h, NULL, NULL, inst, NULL);
    HDC screen = GetDC(NULL);
    m->dc = CreateCompatibleDC(screen);
    m->bmp = make_dib(screen, w, h, (void **)&m->px);
    ReleaseDC(NULL, screen);
    if (!m->win || !m->bmp) {
        drop_win(m);
        return;
    }
    SelectObject(m->dc, m->bmp);
    memset(m->px, 0, (size_t)w * h * 4);
    m->canvas.px = m->px;
    m->canvas.w = w;
    m->canvas.h = h;
    m->canvas.ox = m->r.left / scale;
    m->canvas.oy = m->r.top / scale;
    m->canvas.scale = scale;
}

static int win_ov_init(Overlay *ov, Backend *cap)
{
    HMODULE u = GetModuleHandleA("user32.dll");
    void (WINAPI * noghost)(void) = u ? (void (WINAPI *)(void))(void *)GetProcAddress(u, "DisableProcessWindowsGhosting") : NULL;
    if (noghost) noghost();
    live_ov = ov;
    for (int i = 0; i < nmons; i++) add_win(&mons[i]);
    sync();
    return nmons > 0;
}

static Canvas *win_canvas(Overlay *ov, int i) { return i < nmons && mons[i].px ? &mons[i].canvas : NULL; }

static void win_commit(Overlay *ov, const Rect *h, int nh)
{
    nhits = nh < 256 ? nh : 256;
    memcpy(hits, h, sizeof(Rect) * nhits);
    for (int i = 0; i < nmons; i++) {
        Mon *m = &mons[i];
        if (!m->win) continue;
        int x0, y0, x1, y1;
        if (!canvas_damage(&m->canvas, &x0, &y0, &x1, &y1) && m->shown) continue;
        GdiFlush();
        POINT pos = {m->r.left, m->r.top}, zero = {0, 0};
        SIZE sz = {m->r.right - m->r.left, m->r.bottom - m->r.top};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        RECT dirty = {x0, y0, x1, y1};
        UPDATELAYEREDWINDOWINFO info = {sizeof(info), NULL, &pos, &sz, m->dc, &zero, 0, &bf, ULW_ALPHA, m->shown ? &dirty : NULL};
        if (!UpdateLayeredWindowIndirect(m->win, &info)) UpdateLayeredWindow(m->win, NULL, &pos, &sz, m->dc, &zero, 0, &bf, ULW_ALPHA);
        if (!m->shown) ShowWindow(m->win, SW_SHOWNOACTIVATE), m->shown = 1;
        SetWindowPos(m->win, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); // browsers going fullscreen push us under
    }
}

static void set_clicky(HWND w, int on)
{
    LONG_PTR ex = GetWindowLongPtrA(w, GWL_EXSTYLE);
    LONG_PTR want = on ? ex & ~WS_EX_TRANSPARENT : ex | WS_EX_TRANSPARENT;
    if (want != ex) SetWindowLongPtrA(w, GWL_EXSTYLE, want);
}

static void track_mouse(void)
{
    POINT p;
    if (!GetCursorPos(&p)) return;
    int over = on_hit(p.x / scale, p.y / scale);
    HWND want = NULL;
    if (over)
        for (int i = 0; i < nmons; i++)
            if (mons[i].win && p.x >= mons[i].r.left && p.x < mons[i].r.right && p.y >= mons[i].r.top && p.y < mons[i].r.bottom) want = mons[i].win;
    if (want == hot) return;
    if (hot) set_clicky(hot, 0);
    if (want) set_clicky(want, 1);
    hot = want;
}

static int win_pointer(Overlay *ov, double *x, double *y)
{
    POINT p;
    if (!GetCursorPos(&p) || !on_hit(p.x / scale, p.y / scale)) return 0;
    *x = p.x / scale, *y = p.y / scale;
    return 1;
}

static int win_wait(Overlay *ov, int timeout_ms, double *cx, double *cy)
{
    DWORD end = GetTickCount() + (DWORD)(timeout_ms > 0 ? timeout_ms : 0);
    for (;;) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return -1;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        track_mouse();
        if (clicked) {
            clicked = 0;
            *cx = click_x, *cy = click_y;
            return 1;
        }
        if (nkeys) {
            ov->key = keyq[0];
            memmove(keyq, keyq + 1, sizeof(int) * (size_t)--nkeys);
            return 2;
        }
        if (wheel) {
            ov->key = wheel;
            wheel = 0;
            *cx = wheel_x, *cy = wheel_y;
            return 3;
        }
        int left = (int)(end - GetTickCount());
        if (left <= 0) return 0;
        MsgWaitForMultipleObjects(0, NULL, FALSE, left < 30 ? (DWORD)left : 30, QS_ALLINPUT);
    }
}

static void win_keyboard(Overlay *ov, int on)
{
    if (on && !kb_on) before_kb = GetForegroundWindow();
    kb_on = on;
    nkeys = 0;
    for (int i = 0; i < nmons; i++) {
        if (!mons[i].win) continue;
        LONG_PTR ex = GetWindowLongPtrA(mons[i].win, GWL_EXSTYLE);
        SetWindowLongPtrA(mons[i].win, GWL_EXSTYLE, on ? ex & ~WS_EX_NOACTIVATE : ex | WS_EX_NOACTIVATE);
    }
    if (on && hot) SetForegroundWindow(hot);
    if (!on && before_kb && mon_of(GetForegroundWindow())) SetForegroundWindow(before_kb); // give the game its keys back
    if (!on) before_kb = NULL;
}

static Overlay win_overlay = {"layered window", win_ov_init, win_canvas, 0, win_commit, win_wait, NULL, win_keyboard, 0, win_pointer};
Overlay *overlay_win(void) { return &win_overlay; }
