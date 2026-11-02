import os, random, shutil, subprocess, sys, tempfile
import chess
import track_sim as ts
# still frames off the animated sim boards: run_anim.py [seed] [boards], SIM_ANIM / SIM_THEME / SIM_ANIM_STR like track_sim
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
tmpdir = tempfile.mkdtemp(prefix="oc-anim-")
vtest = os.path.join(tmpdir, "vtest")
subprocess.run(["cc", "-O2", "-I" + os.path.join(root, "src"), "-o", vtest, os.path.join(here, "vtest.c")] + [os.path.join(root, "src", f) for f in ("vision.c", "tracker.c", "engine.c", "gpu.c")] + ["-lm", "-lpthread", "-ldl"], check=True)
seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
rnd = random.Random(seed)
total = det = obs_ok = fen_ok = 0
fails = []
for i in range(int(sys.argv[2]) if len(sys.argv) > 2 else 40):
    sc = ts.Scene(rnd)
    b = chess.Board()
    for _ in range(rnd.randint(0, 40)):
        if b.is_game_over():
            break
        b.push(ts.pick_move(b, rnd))
    last = [b.move_stack[-1].from_square, b.move_stack[-1].to_square] if b.move_stack else None
    t = rnd.randint(0, 500)
    p = os.path.join(tmpdir, "a.ppm")
    sc.render(b, last, None, t).save(p)
    out = subprocess.run([vtest, p, "black" if sc.flip else "white"], capture_output=True, text=True).stdout
    total += 1
    desc = f"{sc.theme}/{sc.pset}/{sc.sq}px t={t} {b.fen().split()[0]}"
    if not out.startswith("BOARD"):
        fails.append("NOBOARD " + desc)
        continue
    bx, by, bs = map(float, out.split()[1:4])
    if abs(bx - sc.x) > 2 or abs(by - sc.y) > 2 or abs(bs - sc.sq) > 0.5:
        fails.append(f"GEOM got {bx},{by},{bs} " + desc)
        continue
    det += 1
    got = [l for l in out.splitlines() if l.startswith("OBS")][0].split()[1]
    want = "/".join("".join("." if not b.piece_at(chess.square(f, r)) else "W" if b.piece_at(chess.square(f, r)).color else "B" for f in range(8)) for r in range(7, -1, -1))
    if got != want:
        fails.append(f"OBS got {got} " + desc)
        continue
    obs_ok += 1
    gf = [l for l in out.splitlines() if l.startswith("FEN")][0].split()[1]
    if gf == b.fen().split()[0]:
        fen_ok += 1
    else:
        fails.append(f"TYPE got {gf} " + desc)
print(f"total {total}  board-geometry {det}  colors/occupancy {obs_ok}  full-pieces {fen_ok}")
for f in fails:
    print(" ", f)
shutil.rmtree(tmpdir, ignore_errors=True)
