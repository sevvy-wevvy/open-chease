#!/usr/bin/env python3
# open-chease plugin template. copy this folder into your plugins folder,
# rename it, then rip out whatever you dont need.
#
# open-chease starts this program and talks to it like a uci engine.
# it writes lines to our stdin, we print lines to stdout, one thing per line,
# words split by spaces. full list is in docs/PLUGINS.md on github
#
# stdlib only, any python3 on any OS

import sys
import traceback


# settings
# these get sent on hello. open-chease draws them in the settings and saves them,
# then sends "set <key> <value>" right after each one (with the saved value)
# and again whenever the user changes it. the default here only counts till theres
# a saved value, always go by what came in through set
#
# keys: letters, numbers, - _ . and 31 chars max. label and tip split by |
# 24 settings max per plugin, values are always whole numbers

TAB = "Template"  # own tab in the settings, 23 chars max. drop the tab line and they go in the Plugins tab

SETTINGS = [
    # bool <key> <default 0/1> <label> | <tip>
    "setting bool arrow 1 Best move arrow | arrow and a colored square for the engines move",
    "setting bool box 1 Show the box | the little box next to the board",
    # choice <key> <default index> <options split by commas, _ is a space> <label> | <tip>
    "setting choice color 0 Cyan,Green,Pink,Yellow Color | what color the drawings are",
    "setting choice big 0 Best_move,Score,Nothing Big text | what the big text above the board says",
    # slider <key> <default> <min> <max> <label> | <tip>
    "setting slider every 2 0 10 Every (sec) | how often it reads the screen for the color swatch, 0 is never",
    # button <key> <label> | <tip>  (no default, you get "click <key>" when its pressed)
    "setting button hi Say hi | pops a note in the status line so you know its alive",
    "setting button peek Look now | reads the screen right now",
    "setting button wipe Wipe it | clears the drawings till the next move",
]

opts = {}


# menu
# items in the ... menu. "menu <key> <label> | <tip>", label 23 chars max, 4 items per plugin
# (and 6 from all plugins together, first come first served, so dont hog them).
# clicking one sends "click <key>", same as a settings button, one key can be both.
# send the same key again to change the label, "menu <key>" with nothing after removes it.
# they go away by themselves when the plugin stops.
# older open-chease versions dont know menu and just skip the line, no harm

def menu():
    send("menu hi Say hi | same as the Say hi button in the settings",
         f"menu pause {'Resume drawing' if paused else 'Pause drawing'} | stops drawing on the board till you hit it again")

COLORS = ["1fe0e0", "40e070", "ff5fb0", "ffd23f"]


# state

board = None     # (x, y, size, bottom) once its found, None when theres no board
team = "white"   # side the user plays
fen = None
last = None      # (uci, san) of the last move played
best = None      # (uci, san, kind, score, depth) from the engine
over = None      # "checkmate win" etc when the game ended
swatch = None    # "#rrggbb" average color from the last screen read
waiting = False  # asked for a frame and its not back yet
wiped = False
paused = False   # the Pause drawing menu item


def send(*lines):
    # flush or open-chease never sees it, a pipe holds on to output otherwise.
    # a line it cant read gets skipped without a word, not even with -v.
    # if something doesnt show up check the spelling and the number of words first
    for ln in lines:
        print(ln)
    sys.stdout.flush()


def say(*bits):
    # stderr only shows up when open-chease runs with -v, handy for debugging
    print(*bits, file=sys.stderr, flush=True)


# events
# one small function per message. each gets w, the line split into words (w[0] is the message name)

def on_hello(w):
    # "hello 1 template": 1 is the protocol version, then our folder name
    send(*SETTINGS)
    send(f"tab {TAB}")
    menu()
    send(f"log running, settings are in the {TAB} tab")


def on_set(w):
    # "set every 2"
    if len(w) < 3:
        return
    opts[w[1]] = int(w[2])
    if w[1] == "every":
        # tick 1000 = a tick every second, 100 is the fastest, 0 turns it off
        send(f"tick {opts['every'] * 1000}")
    redraw()


def on_click(w):
    # "click hi", one of our buttons or menu items got pressed
    global wiped, paused
    key = w[1] if len(w) > 1 else ""
    if key == "hi":
        send("note hi from the plugin template")
    elif key == "peek":
        look()
    elif key == "wipe":
        wiped = True
        send("clear")  # clear works right away, no show needed
    elif key == "pause":
        paused = not paused
        menu()  # same key again, so the label flips
        if paused:
            send("clear")
        redraw()


def on_board(w):
    # "board 220 140 576 white" = x y on screen, size in px, side at the bottom
    # "board none" = lost it. comes again whenever the board moves or flips
    global board, waiting
    if w[1] == "none":
        board = None
        waiting = False
        send("clear")
        return
    board = (float(w[1]), float(w[2]), float(w[3]), w[4])
    redraw()


def on_team(w):
    # "team white", comes right after board
    global team
    team = w[1]
    redraw()


def on_position(w):
    # "position <fen>" after every move and when the board is found
    global fen, best, over
    fen = " ".join(w[1:])
    best = None  # old best move is for the old position
    over = None
    redraw()


def on_move(w):
    # "move e7e5 e5" = uci then san. san can be missing if it couldnt work it out
    global last, wiped
    last = (w[1], w[2] if len(w) > 2 else w[1])
    wiped = False
    redraw()


def on_best(w):
    # "best g1f3 Nf3 cp 36 11" = uci, san, cp or mate, score for the side to move, depth
    # comes again every time the engine gets deeper, so dont do anything slow here
    global best
    best = (w[1], w[2], w[3], int(w[4]), int(w[5]))
    redraw()


def on_gameover(w):
    # "gameover checkmate win", then win, loss or draw for the user
    global over
    over = " ".join(w[1:])
    send(f"note game over, {w[2] if len(w) > 2 else ''}")
    send(f"log last game: {over}")
    redraw()


def on_tick(w):
    # "tick 12345", only comes after we sent tick. the number is a clock in ms
    look()


def on_frame(w):
    # "frame 576 576 /path/to/frame.ppm" = the screen bit we asked for is ready
    # "frame none busy" (or "no board", "too small", "bad numbers") = no luck
    global waiting, swatch
    waiting = False
    if w[1] == "none":
        if w[2:3] != ["busy"]:
            say("no frame:", " ".join(w[2:]))
        return
    path = " ".join(w[3:])  # the path can have spaces in it
    try:
        swatch = average(*read_ppm(path))
    except (OSError, ValueError) as e:
        send(f"log cant read the frame: {e}")
        return
    redraw()


HANDLERS = {
    "hello": on_hello,
    "set": on_set,
    "click": on_click,
    "board": on_board,
    "team": on_team,
    "position": on_position,
    "move": on_move,
    "best": on_best,
    "gameover": on_gameover,
    "tick": on_tick,
    "frame": on_frame,
}


# screen reading

def look():
    # frame x y w h, from the top left of the board in board px.
    # negative works too, "frame 0 -60 576 50" is the strip above the board.
    # 1024x1024 max and once every 100 ms, ask again sooner and you get "frame none busy"
    global waiting
    if not board or waiting:
        return
    s = board[2]
    send(f"frame 0 0 {s:.0f} {s:.0f}")
    waiting = True


def read_ppm(path):
    # binary P6: "P6\n<w> <h>\n255\n" then rgb bytes. its real screen pixels,
    # so on a 2x screen its twice the size you asked for.
    # read it right away, the next frame overwrites the same file
    with open(path, "rb") as f:
        data = f.read()
    i, head = 0, []
    while len(head) < 4:
        while data[i:i + 1].isspace():
            i += 1
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        head.append(data[i:j])
        i = j
    if head[0] != b"P6":
        raise ValueError("not a P6 ppm")
    w, h = int(head[1]), int(head[2])
    return w, h, data[i + 1:i + 1 + w * h * 3]  # one whitespace byte after 255, then pixels


def average(w, h, px):
    n = len(px) // 3
    if not n:
        raise ValueError("empty frame")
    step = max(1, n // 20000) * 3  # dont walk every pixel, python is slow
    r = g = b = c = 0
    for i in range(0, n * 3 - 2, step):
        r += px[i]
        g += px[i + 1]
        b += px[i + 2]
        c += 1
    return "#%02x%02x%02x" % (r // c, g // c, b // c)


# drawing
# draw/panel/line lines pile up and nothing changes on screen till "show",
# then it swaps in everything at once. means you build the whole picture every time
#
# positions: a square name (e4) or "x y" in px from the top left of the board.
# square names follow the board when black is at the bottom, x y dont, theyre plain px.
# colors: #rgb, #rrggbb or #aarrggbb, aa is how see-through (00 is invisible, ff solid)
# 160 drawings and 10 box lines max, text is cut at 79 chars

def col(alpha=""):
    return "#" + alpha + COLORS[opts.get("color", 0) % len(COLORS)]


def score_text(kind, score):
    # score is for the side to move, flip it so + is always good for white
    if fen and fen.split()[1] == "b":
        score = -score
    if kind == "mate":
        return ("+M" if score > 0 else "-M") + str(abs(score))
    return "%+.2f" % (score / 100)


def redraw():
    if not board or wiped or paused:
        return
    size = board[2]
    out = []

    if last:
        # rect works on a square too, it starts at the squares top left corner.
        # one square is size / 8 px. this tints the last move see-through white
        sq = size / 8
        out.append(f"draw rect {last[0][:2]} {sq:.0f} {sq:.0f} #40ffffff")
        out.append(f"draw rect {last[0][2:4]} {sq:.0f} {sq:.0f} #40ffffff")

    if best and opts.get("arrow", 1):
        uci, san = best[0], best[1]
        # square draws a thick frame around the square, the aa part of the color gets ignored here
        out.append(f"draw square {uci[2:4]} {col()}")
        # arrows go square to square, same thickness as the apps own arrows
        out.append(f"draw arrow {uci[:2]} {uci[2:4]} {col('c0')}")
        # small text on a square lands in its top left corner
        out.append(f"draw text {uci[2:4]} #ffffff {san}")
        # or at x y
        out.append(f"draw text {size - 70:.0f} {size + 8:.0f} #c0ffffff depth {best[4]}")

    big = opts.get("big", 0)
    if best and big < 2:
        # big text just under the board. above it works too (negative y) but the apps own box often sits there
        out.append(f"draw big 0 {size + 6:.0f} {col()} {best[1] if big == 0 else score_text(best[2], best[3])}")

    if swatch:
        # rect is filled, box is just the outline. x y w h then the color
        out.append(f"draw rect -34 0 26 26 {swatch}")
        out.append("draw box -34 0 26 26 #ffffff")

    if opts.get("box", 1):
        # the box goes next to the board, stacked with other plugins boxes
        out.append("panel Plugin template")
        out.append(f"line you play {team}")  # no color = the normal text color
        if last:
            out.append(f"line #ffffff last move {last[1]}")
        if best:
            out.append(f"line {col()} best {best[1]}  {score_text(best[2], best[3])}  d{best[4]}")
        if swatch:
            out.append(f"line {swatch} board color {swatch}")
        if over:
            out.append(f"line #ff7a5a game over, {over}")

    out.append("show")  # nothing above shows up without this
    send(*out)


# main loop
# stdin closing means open-chease quit or turned us off, the loop just ends.
# lines we dont know get skipped, newer versions of open-chease might send more

def main():
    # open-chease talks utf-8. windows python picks some other encoding for pipes,
    # and one bad byte would throw outside the try below and kill us, so replace those
    sys.stdin.reconfigure(encoding="utf-8", errors="replace")
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    for line in sys.stdin:
        w = line.split()
        if not w:
            continue
        fn = HANDLERS.get(w[0])
        if not fn:
            continue
        try:
            fn(w)
        except Exception as e:
            # a crash gets us turned off, better to log it and keep going
            send(f"log oops: {type(e).__name__} {e}")
            traceback.print_exc()


if __name__ == "__main__":
    main()
