import os, random, shutil, subprocess, sys, tempfile
tmpdir = tempfile.mkdtemp(prefix="oc-vision-")
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
vtest = os.path.join(tmpdir, "vtest")
subprocess.run(["cc", "-O2", "-I" + os.path.join(root, "src"), "-o", vtest, os.path.join(here, "vtest.c")] + [os.path.join(root, "src", f) for f in ("vision.c", "tracker.c", "engine.c")] + ["-lm", "-lpthread"], check=True)
# usage: run_vision.py [seed] [boards], PIECES=dir to try another piece set
random.seed(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
pieces = os.environ.get("PIECES", os.path.join(here, "pieces", "cburnett"))


def expand(f):
    out = ""
    for row in f.split("/"):
        for ch in row: out += "." * int(ch) if ch.isdigit() else ch
        out += "/"
    return out


def occ(f):
    return "".join(ch if ch in "./" else "W" if ch.isupper() else "B" for ch in expand(f))


FENS = [
 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
 "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
 "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 8",
 "2r2rk1/1bqnbppp/p2ppn2/1p6/3NP3/1BN1BP2/PPPQ2PP/2KR3R w - - 0 14",
 "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
 "8/5pk1/6p1/3P4/1p3P2/1P4P1/5K2/8 w - - 0 45",
 "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 30",
 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
 "3q1rk1/p4pp1/1p2p2p/3pP3/1PnP4/P1R2N2/5PPP/3Q2K1 w - - 0 25",
 "8/8/4k3/8/2Q5/8/4K3/8 w - - 0 60",
 "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3",
 "r2q1rk1/ppp2ppp/2n1bn2/2bpp3/4P3/2PP1N2/PPBN1PPP/R1BQ1RK1 w - - 0 9",
]
themes = os.environ.get("THEMES", "lichess,chesscom,blue,pink").split(",")
textures = os.environ.get("TEXTURES", "none").split(",")
tints = os.environ.get("TINTS", "").split(";") if os.environ.get("TINTS") else [""]
total = det = obs_ok = fen_ok = 0
pieces_ok = pieces_n = 0
confusion = {}
fails = []
for i in range(int(sys.argv[2]) if len(sys.argv) > 2 else 60):
    fen = random.choice(FENS); th = random.choice(themes)
    sq = random.choice([32, 40, 48, 56, 64, 72, 80, 96, 110, 140, 180, 230])
    W, H = (1920, 1080) if sq <= 110 else (5120, 1440) if sq <= 160 else (7680, 2160)
    x = random.randint(0, W - sq * 8 - 1); y = random.randint(0, H - sq * 8 - 1) if H > sq * 8 else 0
    black = random.random() < 0.4
    hl = ",".join(random.sample([f + r for f in "abcdefgh" for r in "12345678"], random.choice([0, 2, 2, 1])))
    coords = random.random() < 0.6
    scale = random.choice([1.0, 0.95, 0.9, 0.85])
    args = ["python3", os.path.join(root, "tools", "make_board.py"), fen, os.path.join(tmpdir, "t.ppm"), "--theme", th, "--sq", str(sq), "--x", str(x), "--y", str(y), "--scale", str(scale), "--pieces", pieces, "--W", str(W), "--H", str(H)]
    if black: args.append("--black")
    if hl: args += ["--hl", hl]
    if coords: args.append("--coords")
    tx = random.choice(textures) if len(textures) > 1 else textures[0]; tn = random.choice(tints) if len(tints) > 1 else tints[0]
    if tx != "none": args += ["--texture", tx, "--seed", str(i)]
    if tn: args += ["--tint", tn]
    subprocess.run(args, check=True)
    out = subprocess.run([vtest, os.path.join(tmpdir, "t.ppm"), "black" if black else "white"], capture_output=True, text=True).stdout
    total += 1
    desc = f"{th}/{tx}{'/' + tn if tn else ''} sq{sq} at {x},{y} {'black' if black else 'white'} hl={hl} coords={coords} scale={scale} fen={fen.split()[0]}"
    if not out.startswith("BOARD"):
        fails.append("NOBOARD " + desc); continue
    bx, by, bs = map(float, out.split()[1:4])
    gt = max(1.5, sq * 0.04) if tx != "none" else 1.5
    if abs(bx - x) > gt or abs(by - y) > gt or abs(bs - sq) > (gt / 5 if tx != "none" else 0.3):
        fails.append(f"GEOM got {bx},{by},{bs} " + desc); continue
    det += 1
    got_fen = [l for l in out.splitlines() if l.startswith("FEN")][0].split()[1]
    want = fen.split()[0]
    # colors first, if those are off the piece types dont matter
    if occ(got_fen) == occ(want): obs_ok += 1
    else: fails.append(f"OBS got {occ(got_fen)} " + desc); continue
    for a_, b_ in zip(expand(got_fen), expand(want)):
        if b_ not in "./":
            pieces_n += 1
            pieces_ok += a_ == b_
            if a_ != b_: confusion[b_ + ">" + a_] = confusion.get(b_ + ">" + a_, 0) + 1
    if got_fen == want: fen_ok += 1
    else: fails.append(f"TYPE got {got_fen} " + desc)
print(f"total {total}  board-geometry {det}  colors/occupancy {obs_ok}  full-pieces {fen_ok}  piece-types {100 * pieces_ok / max(1, pieces_n):.1f}%")
print("  confusions:", sorted(confusion.items(), key=lambda x: -x[1])[:10])
for f in fails: print(" ", f)
shutil.rmtree(tmpdir, ignore_errors=True)
