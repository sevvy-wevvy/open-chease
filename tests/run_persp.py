import json, math, os, random, shutil, subprocess, sys, tempfile
import numpy as np
from PIL import Image, ImageDraw
tmpdir = tempfile.mkdtemp(prefix="oc-persp-")
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
sys.path.insert(0, os.path.join(root, "tools"))
import make_persp
vtest = os.path.join(tmpdir, "vtest")
subprocess.run(["cc", "-O2", "-I" + os.path.join(root, "src"), "-o", vtest, os.path.join(here, "vtest.c")] + [os.path.join(root, "src", f) for f in ("vision.c", "tracker.c", "engine.c", "gpu.c")] + ["-lm", "-lpthread", "-ldl"], check=True)
# usage: run_persp.py [seed] [boards] [empty frames]
random.seed(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
nboards = int(sys.argv[2]) if len(sys.argv) > 2 else 40
nempty = int(sys.argv[3]) if len(sys.argv) > 3 else 20
sets = [s for s in os.environ.get("SETS", "cburnett,merida,alpha,maestro,staunty").split(",") if os.path.isdir(os.path.join(here, "pieces", s))]


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
 "8/5pk1/6p1/3P4/1p3P2/1P4P1/5K2/8 w - - 0 45",
 "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 30",
 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
 "8/8/4k3/8/2Q5/8/4K3/8 w - - 0 60",
]
themes = os.environ.get("THEMES", "lichess,chesscom,blue,pink,wood,purple").split(",")


def run(path, side):
    return subprocess.run([vtest, path, side], capture_output=True, text=True).stdout


total = found = geom = obs_ok = fen_ok = 0
pieces_ok = pieces_n = 0
fails = []
for i in range(nboards):
    fen = random.choice(FENS); th = random.choice(themes); ps = random.choice(sets)
    tilt = random.uniform(10, 45); yaw = random.uniform(-8, 8); roll = random.uniform(-4, 4)
    dist = random.uniform(1.6, 3.0); light = random.uniform(0, 0.25)
    W, H = random.choice([(1920, 1080), (1920, 1080), (2560, 1440)])
    width = random.randint(450, min(1100, H))
    black = random.random() < 0.4; coords = random.random() < 0.5
    hl = ",".join(random.sample([f + r for f in "abcdefgh" for r in "12345678"], random.choice([0, 2, 2])))
    path = os.path.join(tmpdir, "p.ppm")
    args = [sys.executable, os.path.join(root, "tools", "make_persp.py"), fen, path, "--pieces", os.path.join(here, "pieces", ps), "--theme", th,
            "--tilt", str(tilt), "--yaw", str(yaw), "--roll", str(roll), "--dist", str(dist), "--light", str(light), "--width", str(width),
            "--W", str(W), "--H", str(H), "--seed", str(i)]
    if black: args.append("--black")
    if coords: args.append("--coords")
    if hl: args += ["--hl", hl]
    quad = json.loads(subprocess.run(args, check=True, capture_output=True, text=True).stdout)["quad"]
    if os.environ.get("KEEP"):
        shutil.copy(path, os.path.join(os.environ["KEEP"], f"p{i}.ppm"))
        open(os.path.join(os.environ["KEEP"], f"p{i}.json"), "w").write(json.dumps({"quad": quad, "black": black, "fen": fen}))
    out = run(path, "black" if black else "white")
    total += 1
    desc = f"#{i} {th}/{ps} tilt {tilt:.0f} yaw {yaw:.0f} roll {roll:.0f} dist {dist:.1f} light {light:.2f} w{width} {'black' if black else 'white'} hl={hl} coords={coords} fen={fen.split()[0]}"
    if not out.startswith("BOARD"):
        fails.append("NOBOARD " + desc); continue
    found += 1
    pl = [l for l in out.splitlines() if l.startswith("PERSP")]
    if not pl:
        fails.append("FLAT " + out.splitlines()[0] + " " + desc); continue
    got = list(map(float, pl[0].split()[1:]))
    side = sum(math.dist(quad[k], quad[(k + 1) % 4]) for k in range(4)) / 32
    err = max(math.dist(quad[k], got[k * 2:k * 2 + 2]) for k in range(4))
    if err > side * 0.15:
        fails.append(f"GEOM off {err:.1f}px (square ~{side:.0f}) " + desc); continue
    geom += 1
    got_fen = [l for l in out.splitlines() if l.startswith("FEN")][0].split()[1]
    want = fen.split()[0]
    if occ(got_fen) == occ(want): obs_ok += 1
    else: fails.append(f"OBS got {occ(got_fen)} " + desc); continue
    for a_, b_ in zip(expand(got_fen), expand(want)):
        if b_ not in "./":
            pieces_n += 1
            pieces_ok += a_ == b_
    if got_fen == want: fen_ok += 1
    else: fails.append(f"TYPE got {got_fen} " + desc)

# frames with no board on them, nothing may come back
false_hits = 0
rng = np.random.default_rng(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
for i in range(nempty):
    kind = i % 5
    W, H = (1920, 1080)
    if kind == 0: img = make_persp.scene(W, H, rng, "game"); what = "game scene"
    elif kind == 1: img = make_persp.scene(W, H, rng, "desk"); what = "busy desktop"
    elif kind == 2:  # chess site page with the board taken away
        p = os.path.join(tmpdir, "e.ppm")
        subprocess.run([sys.executable, os.path.join(root, "tools", "make_board.py"), "8/8/8/8/8/8/8/8 w - - 0 1", p, "--theme", random.choice(themes), "--sq", "80", "--x", "300", "--y", "120", "--pieces", os.path.join(here, "pieces", "cburnett")], check=True)
        img = Image.open(p).convert("RGB"); ImageDraw.Draw(img).rectangle([300, 120, 300 + 640, 120 + 640], fill=(49, 46, 43)); what = "site without board"
    elif kind == 3:  # windows, text, a spreadsheet grid, a transparency checker, icon rows
        img = Image.new("RGB", (W, H), tuple(int(c) for c in rng.integers(30, 90, 3))); d = ImageDraw.Draw(img)
        for w in range(4):
            x, y = rng.integers(0, W - 600), rng.integers(0, H - 400)
            d.rectangle([x, y, x + 600, y + 400], fill=(245, 245, 245)); d.rectangle([x, y, x + 600, y + 28], fill=(60, 60, 70))
            for k in range(20): d.text((x + 10, y + 36 + 18 * k), "some window text, line %d" % k, fill=(20, 20, 20))
        x, y = 100, 100
        for r in range(30):
            for c in range(12): d.rectangle([x + c * 70, y + r * 22, x + c * 70 + 70, y + r * 22 + 22], outline=(200, 200, 200))
        x, y = 1100, 300
        for r in range(40):
            for c in range(60): d.rectangle([x + c * 10, y + r * 10, x + c * 10 + 9, y + r * 10 + 9], fill=(204, 204, 204) if (r + c) % 2 else (255, 255, 255))
        for k in range(10): d.rectangle([20, 20 + k * 90, 84, 84 + k * 90], fill=tuple(int(c) for c in rng.integers(0, 255, 3)))
        what = "windows + grids"
    else:  # tiled floor going off into the distance, big checker but not a board
        img = make_persp.scene(W, H, rng, "game")
        n = 24; t = Image.new("RGB", (n * 60, n * 60)); d = ImageDraw.Draw(t)
        a_, b_ = tuple(int(c) for c in rng.integers(150, 240, 3)), tuple(int(c) for c in rng.integers(30, 110, 3))
        for r in range(n):
            for c in range(n): d.rectangle([c * 60, r * 60, c * 60 + 59, r * 60 + 59], fill=a_ if (r + c) % 2 == 0 else b_)
        dst = [(-400, 500), (W + 400, 500), (W + 1500, H + 800), (-1500, H + 800)]
        co = make_persp.persp_coeffs(dst, [(0, 0), (n * 60, 0), (n * 60, n * 60), (0, n * 60)])
        img.paste(t.transform((W, H), Image.PERSPECTIVE, co, Image.BICUBIC), (0, 0), Image.new("L", t.size, 255).transform((W, H), Image.PERSPECTIVE, co))
        what = "tiled floor"
    p = os.path.join(tmpdir, "n.ppm"); img.save(p)
    if os.environ.get("KEEP"): shutil.copy(p, os.path.join(os.environ["KEEP"], f"n{i}.ppm"))
    out = run(p, "white")
    if out.startswith("BOARD"):
        false_hits += 1
        fails.append(f"FALSE #{i} {what}: " + out.splitlines()[0] + " " + " ".join(l for l in out.splitlines() if l.startswith("PERSP")))
print(f"total {total}  found {found}  geometry {geom}  colors/occupancy {obs_ok}  full-pieces {fen_ok}  piece-types {100 * pieces_ok / max(1, pieces_n):.1f}%")
print(f"empty frames {nempty}  false boards {false_hits}")
for f in fails: print(" ", f)
shutil.rmtree(tmpdir, ignore_errors=True)
