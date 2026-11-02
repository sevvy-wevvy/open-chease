# renders a board seen at an angle, like a 3d game camera looking at it, prints the real corners
import argparse, json, math, os, subprocess, sys, tempfile
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

here = os.path.dirname(os.path.abspath(__file__))


def persp_coeffs(dst, src):
    # PIL wants output px -> input px
    A, b = [], []
    for (x, y), (u, v) in zip(dst, src):
        A.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); b.append(u)
        A.append([0, 0, 0, x, y, 1, -v * x, -v * y]); b.append(v)
    return np.linalg.solve(np.array(A, float), np.array(b, float)).tolist()


def project(pts, tilt, yaw, roll, dist):
    t, w, r = math.radians(tilt), math.radians(yaw), math.radians(roll)
    out = []
    for X, Y in pts:
        X, Y = X * math.cos(w) - Y * math.sin(w), X * math.sin(w) + Y * math.cos(w)
        Yp, Zp = Y * math.cos(t), -Y * math.sin(t)
        x, y = X / (dist + Zp), Yp / (dist + Zp)
        out.append((x * math.cos(r) - y * math.sin(r), x * math.sin(r) + y * math.cos(r)))
    return out


def scene(W, H, rng, kind="game"):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    if kind == "game":
        sky = np.array(rng.uniform(80, 200, 3), np.float32); ground = np.array(rng.uniform(40, 140, 3), np.float32)
        hz = H * rng.uniform(0.25, 0.5)
        k = np.clip((yy - hz) / 40, 0, 1)[..., None]
        arr = sky * (1 - k) + ground * k + rng.normal(0, 1, (H, W, 1)).astype(np.float32) * 4
    else:
        arr = np.zeros((H, W, 3), np.float32) + np.array(rng.uniform(20, 70, 3), np.float32)
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    for _ in range(int(rng.integers(15, 40))):
        x, y = rng.uniform(0, W), rng.uniform(0, H); s = rng.uniform(20, 260)
        col = tuple(int(c) for c in rng.integers(0, 255, 3))
        if rng.random() < 0.5: d.ellipse([x, y, x + s, y + s * rng.uniform(0.3, 1.5)], fill=col)
        else: d.polygon([(x + rng.uniform(-s, s), y + rng.uniform(-s, s)) for _ in range(int(rng.integers(3, 6)))], fill=col)
    img = img.filter(ImageFilter.GaussianBlur(float(rng.uniform(0, 2.5))))
    d = ImageDraw.Draw(img)
    for _ in range(int(rng.integers(1, 5))):  # hud boxes with text
        x, y = rng.uniform(0, W - 300), rng.uniform(0, H - 120)
        d.rectangle([x, y, x + rng.uniform(120, 300), y + rng.uniform(40, 120)], fill=(20, 20, 24))
        for i in range(3): d.text((x + 10, y + 8 + 14 * i), "Player%d  score %d" % (rng.integers(1, 99), rng.integers(0, 9999)), fill=(230, 230, 230))
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fen"); ap.add_argument("out")
    ap.add_argument("--pieces", required=True); ap.add_argument("--theme", default="lichess")
    ap.add_argument("--black", action="store_true"); ap.add_argument("--hl", default=""); ap.add_argument("--coords", action="store_true")
    ap.add_argument("--tilt", type=float, default=30); ap.add_argument("--yaw", type=float, default=0); ap.add_argument("--roll", type=float, default=0)
    ap.add_argument("--dist", type=float, default=2.2); ap.add_argument("--light", type=float, default=0.15)
    ap.add_argument("--width", type=int, default=700); ap.add_argument("--cx", type=float, default=-1); ap.add_argument("--cy", type=float, default=-1)
    ap.add_argument("--W", type=int, default=1920); ap.add_argument("--H", type=int, default=1080); ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--bg", default="game"); ap.add_argument("--noise", type=float, default=4)
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)
    sq = 100
    tmp = tempfile.mkdtemp(prefix="mkp-", dir=os.environ.get("TMPDIR"))
    flat = os.path.join(tmp, "flat.ppm")
    args = [sys.executable, os.path.join(here, "make_board.py"), a.fen, flat, "--theme", a.theme, "--sq", str(sq), "--x", "0", "--y", "0",
            "--W", str(sq * 8), "--H", str(sq * 8), "--pieces", a.pieces, "--scale", "0.9"]
    if a.black: args.append("--black")
    if a.hl: args += ["--hl", a.hl]
    if a.coords: args.append("--coords")
    subprocess.run(args, check=True)
    board = Image.open(flat).convert("RGB")
    os.remove(flat); os.rmdir(tmp)
    rim = int(sq * rng.uniform(0.25, 0.6))
    n = sq * 8 + 2 * rim
    rimc = tuple(int(c) for c in rng.integers(40, 120, 3))
    full = Image.new("RGB", (n, n), rimc)
    full.paste(board, (rim, rim))
    # light on the board plane, a ramp plus a soft spot
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n - 0.5
    ang = rng.uniform(0, 2 * math.pi); sx, sy = rng.uniform(-0.4, 0.4, 2)
    g = 1 + a.light * (xx * math.cos(ang) + yy * math.sin(ang)) * 2 + a.light * 0.8 * (np.exp(-((xx - sx) ** 2 + (yy - sy) ** 2) / 0.08) - 0.4)
    arr = np.asarray(full, np.float32) * g[..., None]
    full = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
    half = 0.5 * n / (sq * 8)
    outer = project([(-half, -half), (half, -half), (half, half), (-half, half)], a.tilt, a.yaw, a.roll, a.dist)
    inner = project([(-0.5, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5)], a.tilt, a.yaw, a.roll, a.dist)
    xs = [p[0] for p in outer]; ys = [p[1] for p in outer]
    k = a.width / (max(xs) - min(xs))
    bw, bh = (max(xs) - min(xs)) * k, (max(ys) - min(ys)) * k
    cx = a.cx if a.cx >= 0 else rng.uniform(bw / 2 + 5, a.W - bw / 2 - 5)
    cy = a.cy if a.cy >= 0 else rng.uniform(bh / 2 + 5, a.H - bh / 2 - 5)
    mx, my = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    place = lambda p: ((p[0] - mx) * k + cx, (p[1] - my) * k + cy)
    outer = [place(p) for p in outer]; inner = [place(p) for p in inner]
    co = persp_coeffs(outer, [(0, 0), (n, 0), (n, n), (0, n)])
    warped = full.transform((a.W, a.H), Image.PERSPECTIVE, co, Image.BICUBIC)
    mask = Image.new("L", (n, n), 255).transform((a.W, a.H), Image.PERSPECTIVE, co, Image.BILINEAR)
    img = scene(a.W, a.H, rng, a.bg)
    img.paste(warped, (0, 0), mask)
    arr = np.asarray(img, np.float32) + rng.normal(0, a.noise, (a.H, a.W, 1)).astype(np.float32)
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).save(a.out)
    print(json.dumps({"quad": [[round(x, 2), round(y, 2)] for x, y in inner]}))


if __name__ == "__main__":
    main()
