#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Rect OCRect // carbon has its own Rect
#include "app.h"

typedef struct {
    CGImageRef img;
    atomic_int state; // 1 callback done, 2 we stopped waiting
} Shot;

typedef struct {
    CGDirectDisplayID id;
    CGRect r; // points, global, top left of the main display is 0,0 like everywhere else in here
    double bs; // backing scale, 2 on retina
    int pw, ph;
    uint8_t *cap;
    uint32_t *px;
    Canvas canvas;
} Mon;

static Mon mons[MAX_OUTPUTS];
static int nmons;
static NSWindow *wins[MAX_OUTPUTS];
static SCShareableContent *content;
static Backend *live_cap;
static Overlay *live_ov;
static Rect hits[256];
static int nhits, clicked, keyq[32], nkeys, wheel, kb_on;
static double click_x, click_y, wheel_x, wheel_y;
static int hot = -1;
static NSRunningApplication *before_kb;
static void track_mouse(void);
static CGColorSpaceRef rgb;

static double main_h(void) { return CGDisplayBounds(CGMainDisplayID()).size.height; } // cocoa counts y up from the bottom of this one

@interface OCWin : NSPanel
@end
@implementation OCWin
- (BOOL)canBecomeKeyWindow { return kb_on; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

@interface OCView : NSView
@property int idx;
@end
@implementation OCView
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (BOOL)acceptsFirstResponder { return YES; } // or keyDown just beeps
- (BOOL)isOpaque { return NO; }
- (void)drawRect:(NSRect)dirty
{
    Mon *m = &mons[self.idx];
    if (!m->px) return;
    CGContextRef bmp = CGBitmapContextCreate(m->px, m->pw, m->ph, 8, m->pw * 4, rgb, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = bmp ? CGBitmapContextCreateImage(bmp) : NULL;
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGContextClearRect(ctx, self.bounds);
    if (img) CGContextDrawImage(ctx, self.bounds, img);
    if (img) CGImageRelease(img);
    if (bmp) CGContextRelease(bmp);
}
- (void)mouseDown:(NSEvent *)e
{
    Mon *m = &mons[self.idx];
    NSPoint p = [e locationInWindow];
    double x = m->r.origin.x + p.x, y = m->r.origin.y + m->r.size.height - p.y;
    int over = 0;
    for (int i = 0; i < nhits && !over; i++) over = x >= hits[i].x && x < hits[i].x + hits[i].w && y >= hits[i].y && y < hits[i].y + hits[i].h;
    if (!over) { // missed the buttons, cant pass it on without accessibility access
        [[self window] setIgnoresMouseEvents:YES];
        hot = -1;
        return;
    }
    clicked = 1;
    click_x = x, click_y = y;
}
- (void)scrollWheel:(NSEvent *)e
{
    Mon *m = &mons[self.idx];
    NSPoint p = [e locationInWindow];
    if (fabs([e scrollingDeltaY]) < 0.5) return; // trackpads send tons of tiny ones
    wheel = [e scrollingDeltaY] < 0 ? 1 : -1;
    wheel_x = m->r.origin.x + p.x;
    wheel_y = m->r.origin.y + m->r.size.height - p.y;
}
- (void)keyDown:(NSEvent *)e
{
    NSString *s = [e characters];
    if (!s.length || nkeys >= 32) return;
    unichar c = [s characterAtIndex:0];
    if (c == 127) c = 8; // mac backspace is delete
    if (c < 128) keyq[nkeys++] = c;
}
@end

static int same_layout(CGDirectDisplayID *ids, int n)
{
    if (n != nmons) return 0;
    for (int i = 0; i < n; i++)
        if (ids[i] != mons[i].id || !CGRectEqualToRect(CGDisplayBounds(ids[i]), mons[i].r)) return 0;
    return 1;
}

static void drop_win(int i)
{
    if (wins[i]) [wins[i] orderOut:nil];
    wins[i] = nil;
    free(mons[i].px);
    mons[i].px = NULL;
    memset(&mons[i].canvas, 0, sizeof(Canvas));
}

static void add_win(int i)
{
    Mon *m = &mons[i];
    NSScreen *scr = nil;
    for (NSScreen *s in [NSScreen screens])
        if ([[[s deviceDescription] objectForKey:@"NSScreenNumber"] unsignedIntValue] == m->id) scr = s;
    if (!scr) return;
    OCWin *w = [[OCWin alloc] initWithContentRect:[scr frame]
                                        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    [w setBecomesKeyOnlyIfNeeded:YES];
    [w setOpaque:NO];
    [w setBackgroundColor:[NSColor clearColor]];
    [w setHasShadow:NO];
    [w setLevel:NSScreenSaverWindowLevel];
    [w setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                             NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorIgnoresCycle];
    [w setIgnoresMouseEvents:YES];
    [w setReleasedWhenClosed:NO];
    OCView *v = [[OCView alloc] initWithFrame:NSMakeRect(0, 0, scr.frame.size.width, scr.frame.size.height)];
    v.idx = i;
    [w setContentView:v];
    [w orderFrontRegardless];
    wins[i] = w;
    m->px = calloc((size_t)m->pw * m->ph, 4);
    m->canvas.px = m->px;
    m->canvas.w = m->pw;
    m->canvas.h = m->ph;
    m->canvas.ox = m->r.origin.x;
    m->canvas.oy = m->r.origin.y;
    m->canvas.scale = m->bs;
}

static void sync_outs(void)
{
    if (live_cap) {
        live_cap->nout = nmons;
        for (int i = 0; i < MAX_OUTPUTS; i++) {
            Output *o = &live_cap->out[i];
            if (i >= nmons) {
                o->have_frame = 0;
                continue;
            }
            snprintf(o->name, sizeof(o->name), "display %u", mons[i].id);
            o->logical = (Rect){mons[i].r.origin.x, mons[i].r.origin.y, mons[i].r.size.width, mons[i].r.size.height};
        }
    }
    if (live_ov) live_ov->ncanvas = nmons;
}

static void load_content(void)
{
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block SCShareableContent *got = nil;
    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *c, NSError *err) {
        if (err) fprintf(stderr, "cant list the screens: %s\n", [[err localizedDescription] UTF8String]);
        got = c;
        dispatch_semaphore_signal(done);
    }];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    content = got;
}

static int refresh(void)
{
    CGDirectDisplayID ids[MAX_OUTPUTS];
    uint32_t n = 0;
    if (CGGetActiveDisplayList(MAX_OUTPUTS, ids, &n) != kCGErrorSuccess) n = 0;
    if (same_layout(ids, (int)n)) return 0;
    for (int i = 0; i < nmons; i++) drop_win(i), free(mons[i].cap);
    memset(mons, 0, sizeof(mons));
    hot = -1;
    nmons = (int)n;
    for (int i = 0; i < nmons; i++) {
        Mon *m = &mons[i];
        m->id = ids[i];
        m->r = CGDisplayBounds(ids[i]);
        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(ids[i]);
        m->pw = mode ? (int)CGDisplayModeGetPixelWidth(mode) : (int)m->r.size.width;
        m->ph = mode ? (int)CGDisplayModeGetPixelHeight(mode) : (int)m->r.size.height;
        if (mode) CGDisplayModeRelease(mode);
        m->bs = m->r.size.width > 0 ? m->pw / m->r.size.width : 1;
        m->cap = calloc((size_t)m->pw * m->ph, 4);
    }
    if (live_ov)
        for (int i = 0; i < nmons; i++) add_win(i);
    load_content();
    sync_outs();
    if (live_cap) live_cap->out_gen++;
    return 1;
}

static int mac_cap_init(Backend *b)
{
    if (@available(macOS 14.0, *)) {
    } else {
        fprintf(stderr, "this needs macos 14 or newer to grab the screen\n");
        return 0;
    }
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]; // no dock icon
    [NSApp finishLaunching];
    rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (!CGPreflightScreenCaptureAccess()) {
        CGRequestScreenCaptureAccess();
        fprintf(stderr, "macos needs you to allow screen recording for your terminal app: System Settings > Privacy & Security > "
                        "Screen & System Audio Recording, turn it on, then quit the terminal and run this again\n");
        return 0;
    }
    live_cap = b;
    refresh();
    return nmons > 0 && content != nil;
}

static int grab(Mon *m)
{
    SCDisplay *disp = nil;
    for (SCDisplay *d in content.displays)
        if (d.displayID == m->id) disp = d;
    if (!disp) return 0;
    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:disp excludingWindows:@[]];
    SCStreamConfiguration *cfg = [[SCStreamConfiguration alloc] init];
    cfg.width = m->pw;
    cfg.height = m->ph;
    cfg.showsCursor = NO;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    // whoever finishes last frees it, a late shot would leak a whole screen
    Shot *st = calloc(1, sizeof(Shot));
    if (!st) return 0;
    if (@available(macOS 14.0, *)) {
        [SCScreenshotManager captureImageWithFilter:filter
                                      configuration:cfg
                                  completionHandler:^(CGImageRef img, NSError *err) {
                                      st->img = img ? CGImageRetain(img) : NULL;
                                      if (atomic_exchange(&st->state, 1) == 2) {
                                          if (st->img) CGImageRelease(st->img);
                                          free(st);
                                      } else
                                          dispatch_semaphore_signal(done);
                                  }];
    } else {
        free(st);
        return 0;
    }
    int waited = 0;
    while (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_MSEC))) {
        track_mouse();
        if ((waited += 5) > 2000) {
            if (atomic_exchange(&st->state, 2) == 1) break;
            return 0;
        }
    }
    CGImageRef shot = st->img;
    free(st);
    if (!shot) return 0;
    CGContextRef ctx = CGBitmapContextCreate(m->cap, m->pw, m->ph, 8, m->pw * 4, rgb, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    if (ctx) {
        CGContextDrawImage(ctx, CGRectMake(0, 0, m->pw, m->ph), shot);
        CGContextRelease(ctx);
    }
    CGImageRelease(shot);
    return ctx != NULL;
}

static int mac_capture(Backend *b)
{
    static double last;
    int got = 0;
    @autoreleasepool { // no [NSApp run] here so no pools unless we make them
        double t = [NSDate timeIntervalSinceReferenceDate];
        if (t - last > 2) {
            last = t;
            if (refresh()) fprintf(stderr, "screens changed\n");
        }
        for (int i = 0; i < nmons; i++) {
            Mon *m = &mons[i];
            Output *o = &b->out[i];
            o->have_frame = grab(m);
            if (!o->have_frame) continue;
            o->frame = (Frame){m->cap, m->pw, m->ph, m->pw * 4, 1};
            got++;
        }
    }
    return got > 0;
}

static Backend mac_backend = {.name = "screencapturekit", .init = mac_cap_init, .capture = mac_capture};
Backend *backend_mac(void) { return &mac_backend; }

static int mac_ov_init(Overlay *ov, Backend *cap)
{
    live_ov = ov;
    for (int i = 0; i < nmons; i++) add_win(i);
    sync_outs();
    return nmons > 0;
}

static Canvas *mac_canvas(Overlay *ov, int i) { return i < nmons && mons[i].px ? &mons[i].canvas : NULL; }

static void mac_commit(Overlay *ov, const Rect *h, int nh)
{
    nhits = nh < 256 ? nh : 256;
    memcpy(hits, h, sizeof(Rect) * nhits);
    @autoreleasepool {
        static int tries;
        if (++tries % 60 == 0) // a just plugged in screen can show up in CG before NSScreen knows it
            for (int i = 0; i < nmons; i++)
                if (!wins[i]) add_win(i);
        for (int i = 0; i < nmons; i++) {
            int x0, y0, x1, y1;
            if (!wins[i] || !canvas_damage(&mons[i].canvas, &x0, &y0, &x1, &y1)) continue;
            NSView *v = [wins[i] contentView];
            double bs = mons[i].bs;
            // damage is in canvas px from the top, cocoa wants points from the bottom
            [v setNeedsDisplayInRect:NSMakeRect(x0 / bs, mons[i].r.size.height - y1 / bs, (x1 - x0) / bs, (y1 - y0) / bs)];
            [wins[i] displayIfNeeded];
        }
    }
}

static int mouse_over;
static double mouse_x, mouse_y;

static void track_mouse(void)
{
    NSPoint p = [NSEvent mouseLocation];
    double lx = p.x, ly = main_h() - p.y;
    int over = 0;
    for (int i = 0; i < nhits && !over; i++)
        over = lx >= hits[i].x && lx < hits[i].x + hits[i].w && ly >= hits[i].y && ly < hits[i].y + hits[i].h;
    mouse_over = over, mouse_x = lx, mouse_y = ly;
    int want = -1;
    if (over)
        for (int i = 0; i < nmons; i++)
            if (wins[i] && CGRectContainsPoint(mons[i].r, CGPointMake(lx, ly))) want = i;
    if (want == hot) return;
    if (hot >= 0 && wins[hot]) [wins[hot] setIgnoresMouseEvents:YES];
    if (want >= 0) [wins[want] setIgnoresMouseEvents:NO];
    hot = want;
}

static int mac_wait(Overlay *ov, int timeout_ms, double *cx, double *cy)
{
    double end = [NSDate timeIntervalSinceReferenceDate] + (timeout_ms > 0 ? timeout_ms : 0) / 1000.0;
    for (;;) {
        @autoreleasepool {
            double left = end - [NSDate timeIntervalSinceReferenceDate];
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate dateWithTimeIntervalSinceNow:left > 0.03 ? 0.03 : (left > 0 ? left : 0)]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
            if (e) [NSApp sendEvent:e];
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
        if ([NSDate timeIntervalSinceReferenceDate] >= end) return 0;
    }
}

static int mac_pointer(Overlay *ov, double *x, double *y)
{
    if (!mouse_over) return 0;
    *x = mouse_x, *y = mouse_y;
    return 1;
}

static void mac_keyboard(Overlay *ov, int on)
{
    if (on && !kb_on) before_kb = [[NSWorkspace sharedWorkspace] frontmostApplication];
    kb_on = on;
    nkeys = 0;
    if (on) {
        [NSApp activateIgnoringOtherApps:YES];
        if (hot >= 0 && wins[hot]) {
            [wins[hot] makeKeyAndOrderFront:nil];
            [wins[hot] makeFirstResponder:[wins[hot] contentView]];
        }
    } else if (before_kb) {
        [before_kb activateWithOptions:0];
        before_kb = nil;
    }
}

static Overlay mac_overlay = {"nswindow", mac_ov_init, mac_canvas, 0, mac_commit, mac_wait, NULL, mac_keyboard, 0, mac_pointer};
Overlay *overlay_mac(void) { return &mac_overlay; }
