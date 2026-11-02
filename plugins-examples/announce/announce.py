#!/usr/bin/env python3
import sys

names = {"p": "pawn", "n": "knight", "b": "bishop", "r": "rook", "q": "queen", "k": "king"}
opts = {"onboard": 1, "box": 1, "style": 0}
last = None
board = {}


def send(*lines):
    for ln in lines:
        print(ln)
    sys.stdout.flush()


def piece_on(fen, sq):
    rows = fen.split()[0].split("/")
    row = rows[8 - int(sq[1])]
    col = ord(sq[0]) - ord("a")
    x = 0
    for ch in row:
        if ch.isdigit():
            x += int(ch)
            continue
        if x == col:
            return ch
        x += 1
    return None


def words(mv):
    uci, san, pc = mv
    name = names.get((pc or "p").lower(), "piece")
    if san.startswith("O-O-O"):
        return "castled long"
    if san.startswith("O-O"):
        return "castled short"
    took = "x" in san
    return f"{name} {'takes on' if took else 'to'} {uci[2:4]}" + ("!" if "#" in san else "")


def redraw():
    if not last:
        send("clear")
        return
    uci, san, pc = last
    text = san if opts["style"] == 0 else words(last)
    out = []
    if opts["onboard"]:
        out.append(f"draw square {uci[:2]} #801fe0e0")
        out.append(f"draw square {uci[2:4]} #1fe0e0")
        out.append(f"draw arrow {uci[:2]} {uci[2:4]} #b01fe0e0")
        out.append(f"draw text {uci[2:4]} #ffffff {san}")
    if opts["box"]:
        out.append("panel Last move")
        out.append(f"line #1fe0e0 {text}")
        out.append(f"line {'white' if pc and pc.isupper() else 'black'} played it")
    out.append("show")
    send(*out)


fen = None
for line in sys.stdin:
    w = line.split()
    if not w:
        continue
    if w[0] == "hello":
        send("setting bool onboard 1 On the board | writes the move on the square it went to",
             "setting bool box 1 Show the box | a little box next to the board with the last move",
             "setting choice style 0 Short,In_words Move as | Nf3 or knight to f3",
             "setting button test Say hi | pops a note in the status line so you know its alive")
    elif w[0] == "set" and len(w) >= 3:
        opts[w[1]] = int(w[2])
        redraw()
    elif w[0] == "click" and len(w) >= 2 and w[1] == "test":
        send("note hi from the move announcer")
    elif w[0] == "position":
        fen = " ".join(w[1:])
    elif w[0] == "move" and len(w) >= 2 and fen:
        uci = w[1]
        san = w[2] if len(w) >= 3 else uci
        last = (uci, san, piece_on(fen, uci[2:4]))
        send(f"log last move {san}")
        redraw()
    elif w[0] == "board" and len(w) >= 2 and w[1] == "none":
        last = None
