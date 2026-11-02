#!/usr/bin/env python3
import sys

spot = 0
size = 0
waiting = False


def send(*lines):
    for ln in lines:
        print(ln)
    sys.stdout.flush()


def ask():
    global waiting
    if waiting or not size:
        return
    s = size / 8
    if spot == 0:
        send(f"frame 0 0 {size} {size}")
    elif spot == 1:
        send(f"frame 0 0 {s} {s}")
    else:
        send(f"frame 0 -60 {size} 50")
    waiting = True


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    px = parts[4][: w * h * 3]
    return w, h, px


def show(w, h, px):
    n = w * h
    if not n:
        return
    step = max(1, n // 20000) * 3
    r = g = b = c = 0
    for i in range(0, len(px) - 2, step):
        r += px[i]
        g += px[i + 1]
        b += px[i + 2]
        c += 1
    hexc = "#%02x%02x%02x" % (r // c, g // c, b // c)
    send("draw rect -34 0 26 26 " + hexc,
         "draw box -34 0 26 26 #ffffff",
         "panel Average color",
         f"line {hexc} {hexc} over {w}x{h} px",
         "show")


for line in sys.stdin:
    w = line.split()
    if not w:
        continue
    if w[0] == "hello":
        send("setting choice spot 0 Board,Corner,Above_board Read from | the whole board, the top left square or the strip above the board",
             "setting slider every 2 1 10 Every (sec) | how often it looks again")
    elif w[0] == "set" and len(w) >= 3 and w[1] == "spot":
        spot = int(w[2])
        waiting = False
        ask()
    elif w[0] == "set" and len(w) >= 3 and w[1] == "every":
        send(f"tick {int(w[2]) * 1000}")
    elif w[0] == "board":
        if w[1] == "none":
            size = 0
            send("clear")
        else:
            size = float(w[3])
            ask()
    elif w[0] == "tick":
        ask()
    elif w[0] == "frame":
        waiting = False
        if w[1] == "none":
            if w[2:3] != ["busy"]:
                send("log no frame, " + " ".join(w[2:]))
        else:
            try:
                show(*read_ppm(" ".join(w[3:])))
            except OSError as e:
                send(f"log cant read the frame: {e}")
