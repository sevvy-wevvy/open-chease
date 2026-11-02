import argparse, ctypes, math, os, random, shutil, subprocess, sys, tempfile
import chess
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)

rsvg = ctypes.CDLL("librsvg-2.so.2")
cairo = ctypes.CDLL("libcairo.so.2")


class R(ctypes.Structure):
    _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double), ("w", ctypes.c_double), ("h", ctypes.c_double)]


rsvg.rsvg_handle_new_from_data.restype = ctypes.c_void_p
rsvg.rsvg_handle_new_from_data.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
rsvg.rsvg_handle_render_document.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(R), ctypes.c_void_p]
cairo.cairo_image_surface_create.restype = ctypes.c_void_p
cairo.cairo_create.restype = ctypes.c_void_p
cairo.cairo_create.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_data.restype = ctypes.POINTER(ctypes.c_uint8)
cairo.cairo_image_surface_get_data.argtypes = [ctypes.c_void_p]
cairo.cairo_surface_flush.argtypes = [ctypes.c_void_p]

_cache = {}


def piece(pset, key, s):
    k = (pset, key, s)
    if k not in _cache:
        png = os.path.join(here, "pieces", pset, key.lower() + ".png")
        if os.path.exists(png):  # sets we only have as pictures, like chess.com ones
            _cache[k] = Image.open(png).convert("RGBA").resize((s, s), Image.LANCZOS)
            return _cache[k]
        data = open(os.path.join(here, "pieces", pset, key + ".svg"), "rb").read()
        h = rsvg.rsvg_handle_new_from_data(data, len(data), None)
        surf = cairo.cairo_image_surface_create(0, s, s)
        cr = cairo.cairo_create(surf)
        rsvg.rsvg_handle_render_document(h, cr, ctypes.byref(R(0, 0, s, s)), None)
        cairo.cairo_surface_flush(surf)
        buf = bytes(ctypes.cast(cairo.cairo_image_surface_get_data(surf), ctypes.POINTER(ctypes.c_uint8 * (s * s * 4))).contents)
        _cache[k] = Image.frombuffer("RGBA", (s, s), buf, "raw", "BGRA", 0, 1).copy()
    return _cache[k]


THEMES = {
    "lichess": ((240, 217, 181), (181, 136, 99), (205, 210, 106), (170, 162, 58)),
    "chesscom": ((235, 236, 208), (115, 149, 82), (245, 246, 130), (185, 202, 67)),
    "blue": ((222, 227, 230), (140, 162, 173), (195, 216, 135), (150, 183, 99)),
}
PINK = ((245, 241, 242), (235, 148, 164), (236, 208, 188), (226, 156, 140))
# roblox board: every square is lighter on top, darker on the bottom, soft step in the middle
RBX = (((160, 170, 208), (153, 163, 199), (137, 145, 178), (133, 141, 173)),
       ((86, 98, 150), (82, 94, 144), (74, 84, 128), (71, 81, 125)),
       ((164, 158, 232), (161, 154,227), (153, 146, 217), (151, 144, 214)),
       ((127, 122, 203), (125, 120, 200), (121, 115, 192), (120, 114, 190)))
_grad = {}


def grad_square(cols, s):
    k = (cols, s)
    if k not in _grad:
        im = Image.new("RGB", (s, s))
        d = ImageDraw.Draw(im)
        for j in range(s):
            t = (j + 0.5) / s
            if t < 0.45:
                a, b, u = cols[0], cols[1], t / 0.45
            elif t < 0.55:
                u = (t - 0.45) / 0.1
                a, b, u = cols[1], cols[2], u * u * (3 - 2 * u)
            else:
                a, b, u = cols[2], cols[3], (t - 0.55) / 0.45
            d.line([0, j, s, j], fill=tuple(int(round(a[c] + (b[c] - a[c]) * u)) for c in range(3)))
        _grad[k] = im
    return _grad[k]


# SIM_ANIM=shimmer,pulse,hue,scroll,hlpulse,sparkle,bob,scale,spin,frames,glow (or all), SIM_ANIM_STR=1 how hard
# t is the frame number, so the board keeps changing while nobody moves
ANIM = set(os.environ.get("SIM_ANIM", "").replace("all", "shimmer,pulse,hue,scroll,hlpulse,sparkle,bob,scale,spin,frames,glow").split(",")) - {""}
AS = float(os.environ.get("SIM_ANIM_STR", "1"))
_noise = {}


def noise_tile(s):
    if s not in _noise:
        r = np.random.default_rng(s)
        n = Image.fromarray((r.random((s, s)) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(max(1, s / 18)))
        a = np.asarray(n).astype(np.float32)
        a = (a - a.mean()) / max(1, a.std())
        _noise[s] = np.tile(a, (2, 2))
    return _noise[s]


def anim_board(img, x, y, n, t):  # whole board area, under the pieces
    a = np.asarray(img.crop((x, y, x + n, y + n))).astype(np.float32)
    if "hue" in ANIM:  # swing the hue a bit, keeps light and dark apart
        hsv = np.asarray(Image.fromarray(a.astype(np.uint8)).convert("HSV")).astype(np.float32)
        hsv[..., 0] = (hsv[..., 0] + AS * 18 * math.sin(t * 0.7)) % 256
        a = np.asarray(Image.fromarray(hsv.astype(np.uint8), "HSV").convert("RGB")).astype(np.float32)
    if "pulse" in ANIM:
        a *= 1 + AS * 0.1 * math.sin(t * 1.3)
    if "scroll" in ANIM:  # moving cloudy texture
        nt = noise_tile(max(16, n // 6))
        k = nt.shape[0] // 2
        o = int(t * 7) % k
        sub = nt[o:o + k, (o * 2) % k:(o * 2) % k + k]
        rep = np.tile(sub, (n // k + 1, n // k + 1))[:n, :n]
        a += (AS * 9 * rep)[..., None]
    if "shimmer" in ANIM:  # bright band sweeping across
        yy, xx = np.mgrid[0:n, 0:n]
        ph = ((t * 0.37) % 1.6 - 0.3) * 2 * n
        band = np.clip(1 - np.abs(xx + yy - ph) / (n * 0.18), 0, 1)
        a += (AS * 55 * band)[..., None]
    img.paste(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)), (x, y))


def anim_piece(im, sq, s, t):  # sprite and where it goes, relative to the square
    ph = sq * 0.9
    dx = dy = 0
    if "frames" in ANIM:
        im = im.rotate(AS * 7 * (-1, 0, 1, 0)[(int(t) + sq) % 4], resample=Image.BICUBIC)
    if "spin" in ANIM:  # 3d-ish set turning round
        w = max(4, int(s * (1 - AS * 0.4 * abs(math.sin(t * 0.9 + ph)))))
        im, dx = im.resize((w, s), Image.LANCZOS), (s - w) // 2
    if "scale" in ANIM:
        k = 1 + AS * 0.07 * math.sin(t * 1.3 + ph)
        w, h = max(4, int(im.width * k)), max(4, int(s * k))
        dx, dy = dx + (im.width - w) // 2, dy + (s - h) // 2
        im = im.resize((w, h), Image.LANCZOS)
    if "bob" in ANIM:
        dy += int(round(AS * s * 0.06 * math.sin(t * 1.7 + ph)))
    return im, dx, dy


_glow = {}


def glow_of(im, key):
    if key not in _glow:
        r = max(2, im.width // 14)
        _glow[key] = im.getchannel("A").filter(ImageFilter.MaxFilter(2 * (r // 2) + 1)).filter(ImageFilter.GaussianBlur(r))
    return _glow[key]


def sparkles(img, x, y, n, s, t, seed):
    r = random.Random(seed * 7919 + int(t))
    d = ImageDraw.Draw(img, "RGBA")
    for _ in range(int(AS * 26)):
        cx, cy, z = x + r.random() * n, y + r.random() * n, max(2, r.random() * s * 0.09)
        c = (255, 255, r.randint(150, 255), r.randint(160, 255))
        d.line([cx - z, cy, cx + z, cy], fill=c, width=max(1, int(z / 3)))
        d.line([cx, cy - z, cx, cy + z], fill=c, width=max(1, int(z / 3)))


def pkey(p):
    return ("w" if p.color == chess.WHITE else "b") + p.symbol().upper()


class Scene:
    def __init__(self, rnd):
        self.theme = rnd.choice(list(THEMES))
        self.pset = rnd.choice(["cburnett"] * 3 + ["staunty", "alpha", "maestro"])
        if os.environ.get("SIM_PSET"):  # pin one look, for chasing a bug on a single theme
            self.theme, self.pset = os.environ.get("SIM_THEME", self.theme), os.environ["SIM_PSET"]
        if os.environ.get("SIM_THEME") == "roblox":
            self.theme, self.pset = "roblox", os.environ.get("SIM_PSET", "roblox")
        self.sq = rnd.choice([44, 52, 60, 64, 72, 80])
        self.W, self.H = 1000, 760
        self.skip = float(os.environ.get("SIM_SKIP", "0.08"))
        if self.theme == "roblox":
            self.sq = rnd.choice([56, 72, 90, 110, 140])
            self.W, self.H = self.sq * 8 + 420, self.sq * 8 + 120
            self.skip = float(os.environ.get("SIM_SKIP", "0.4"))  # pebble answers before the next frame a lot
        self.x = rnd.randint(10, self.W - self.sq * 8 - 220)
        self.y = rnd.randint(70, self.H - self.sq * 8 - 10)
        self.flip = rnd.random() < 0.45
        self.coords = rnd.random() < 0.6 and self.theme != "roblox"
        self.promo_style = rnd.choice(["chesscom", "lichess"])
        self.font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", max(9, self.sq // 6))

    def xy(self, sq):
        f, r = chess.square_file(sq), chess.square_rank(sq)
        dc, dr = (7 - f, r) if self.flip else (f, 7 - r)
        return self.x + dc * self.sq, self.y + dr * self.sq

    def render(self, board, last=None, extra=None, t=0):
        light, dark, hll, hld = THEMES.get(self.theme, PINK)
        if "hlpulse" in ANIM:  # last move squares fading in and out
            k = min(1, AS) * (0.5 + 0.5 * math.sin(t * 1.1))
            hll, hld = (tuple(int(light[c] + (hll[c] - light[c]) * (1 - 0.6 * k)) for c in range(3)),
                        tuple(int(dark[c] + (hld[c] - dark[c]) * (1 - 0.6 * k)) for c in range(3)))
        rbx = self.theme == "roblox"
        img = Image.new("RGB", (self.W, self.H), (25, 37, 28) if rbx else (38, 36, 33))
        d = ImageDraw.Draw(img, "RGBA")
        d.rectangle([0, 0, self.W, 50], fill=(25, 25, 25))
        d.text((16, 16), "play  puzzles  learn  watch", fill=(190, 190, 190))
        d.rectangle([self.x + self.sq * 8 + 20, self.y, self.W - 10, self.y + 300], fill=(48, 46, 43))
        extra = extra or {}
        hidden = set(extra.get("hide", []))
        for sq in chess.SQUARES:
            x0, y0 = self.xy(sq)
            is_light = (chess.square_file(sq) + chess.square_rank(sq)) % 2 == 1
            col = light if is_light else dark
            if last and sq in last:
                col = hll if is_light else hld
            if extra.get("selected") == sq:
                col = hll if is_light else hld
            if rbx:
                hl = bool(last and sq in last) or extra.get("selected") == sq
                img.paste(grad_square(RBX[(0 if is_light else 1) + (2 if hl else 0)], self.sq), (x0, y0))
            else:
                d.rectangle([x0, y0, x0 + self.sq - 1, y0 + self.sq - 1], fill=col)
            if self.coords:
                oc = dark if is_light else light
                f, r = chess.square_file(sq), chess.square_rank(sq)
                if (x0 - self.x) == 0:
                    d.text((x0 + 3, y0 + 2), str(r + 1), fill=oc, font=self.font)
                if (y0 - self.y) == self.sq * 7:
                    d.text((x0 + self.sq - self.sq // 6, y0 + self.sq - self.sq // 4), "abcdefgh"[f], fill=oc, font=self.font)
        if rbx:  # rounded rim round the board
            m, rr, bw = max(2, self.sq // 30), self.sq // 8, self.sq * 8
            rim = Image.new("L", (bw + 2 * m, bw + 2 * m), 255)
            ImageDraw.Draw(rim).rounded_rectangle([m, m, bw + m - 1, bw + m - 1], radius=rr, fill=0)
            img.paste((52, 55, 63), (self.x - m, self.y - m), rim)
        if ANIM & {"hue", "pulse", "scroll", "shimmer"}:
            anim_board(img, self.x, self.y, self.sq * 8, t)
        if board.is_check():
            k = board.king(board.turn)
            x0, y0 = self.xy(k)
            for i in range(10, 0, -1):
                rr = self.sq * 0.5 * i / 10
                a = int(150 * (1 - i / 10) + 40)
                d.ellipse([x0 + self.sq / 2 - rr, y0 + self.sq / 2 - rr, x0 + self.sq / 2 + rr, y0 + self.sq / 2 + rr], fill=(230, 30, 30, a))
        for sq, p in board.piece_map().items():
            if sq in hidden:
                continue
            x0, y0 = self.xy(sq)
            im = piece(self.pset, pkey(p), self.sq)
            if extra.get("ghost") == sq:
                im = im.copy()
                im.putalpha(im.getchannel("A").point(lambda v: v * 3 // 10))
            elif ANIM:
                if "glow" in ANIM:
                    g = glow_of(im, (self.pset, pkey(p), self.sq))
                    ga = AS * (0.35 + 0.35 * math.sin(t * 1.5 + sq))
                    img.paste((255, 214, 90) if p.color == chess.WHITE else (120, 200, 255), (x0, y0), g.point(lambda v: int(min(255, v * ga))))
                im, dx, dy = anim_piece(im, sq, self.sq, t)
                x0, y0 = x0 + dx, y0 + dy
            img.paste(im, (x0, y0), im)
        for sq in extra.get("dots", []):
            x0, y0 = self.xy(sq)
            cx, cy = x0 + self.sq / 2, y0 + self.sq / 2
            if board.piece_at(sq):
                rr, w = self.sq * 0.47, max(3, self.sq // 12)
                d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=(0, 0, 0, 36), width=w)
            else:
                rr = self.sq * (0.16 if self.theme == "chesscom" else 0.13)
                d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=(0, 0, 0, 36) if self.theme == "chesscom" else (20, 85, 30, 128))
        if "sparkle" in ANIM:
            sparkles(img, self.x, self.y, self.sq * 8, self.sq, t, self.sq)
        if "float" in extra:
            key, fx, fy = extra["float"]
            im = piece(self.pset, key, self.sq)
            img.paste(im, (int(fx), int(fy)), im)
        if "promo" in extra:
            color, to = extra["promo"]
            keys = [("w" if color == chess.WHITE else "b") + k for k in "QNRB"]
            x0, y0 = self.xy(to)
            step = self.sq if y0 == self.y else -self.sq
            if self.promo_style == "chesscom":
                for i, k in enumerate(keys):
                    yy = y0 + i * step
                    d.rectangle([x0, yy, x0 + self.sq - 1, yy + self.sq - 1], fill=(255, 255, 255))
                    im = piece(self.pset, k, self.sq)
                    img.paste(im, (x0, yy), im)
            else:
                d.rectangle([self.x, self.y, self.x + self.sq * 8, self.y + self.sq * 8], fill=(250, 250, 250, 120))
                for i, k in enumerate(keys):
                    yy = y0 + i * step
                    d.ellipse([x0, yy, x0 + self.sq - 1, yy + self.sq - 1], fill=(176, 176, 176))
                    im = piece(self.pset, k, int(self.sq * 0.8))
                    img.paste(im, (x0 + self.sq // 10, yy + self.sq // 10), im)
        return img


def pick_move(board, rnd):
    moves = list(board.legal_moves)
    promos = [m for m in moves if m.promotion]
    if promos:
        want = rnd.choices([chess.QUEEN, chess.KNIGHT, chess.ROOK, chess.BISHOP], [70, 15, 10, 5])[0]
        same = [m for m in promos if m.promotion == want]
        return rnd.choice(same or promos)
    pawns = [m for m in moves if board.piece_type_at(m.from_square) == chess.PAWN]
    if pawns and rnd.random() < 0.45:
        far = max(pawns, key=lambda m: chess.square_rank(m.to_square) if board.turn else 7 - chess.square_rank(m.to_square))
        return far if rnd.random() < 0.5 else rnd.choice(pawns)
    caps = [m for m in moves if board.is_capture(m)]
    if caps and rnd.random() < 0.4:
        return rnd.choice(caps)
    return rnd.choice(moves)


def sim_game(seed, out, binary, keep=False):
    rnd = random.Random(seed)
    sc = Scene(rnd)
    board = chess.Board()
    midgame = rnd.random() < 0.25
    if midgame:
        for _ in range(rnd.randint(8, 30)):
            if board.is_game_over():
                break
            board.push(pick_move(board, rnd))
    user = rnd.choice([chess.WHITE, chess.BLACK])
    frames = [0]
    checks = []
    truth = set()
    d = tempfile.mkdtemp(prefix="oc-sim-", dir=out)

    def emit(b, last, extra=None, n=1):  # animated frames all differ, so draw and save each one now
        im = None
        for _ in range(n):
            if im is None or ANIM:
                im = sc.render(b, last, extra, frames[0])
            im.save(os.path.join(d, "%06d.ppm" % frames[0]))
            frames[0] += 1

    def settle(last, n):
        emit(board, last, None, n)
        truth.add(" ".join(board.fen().split()[:2]))
        checks.append((frames[0] - 1, board.fen(), board.move_stack[-1].promotion if board.move_stack else None))

    last = [board.move_stack[-1].from_square, board.move_stack[-1].to_square] if board.move_stack else None
    settle(last, 3)
    plies = 0
    skipped = False
    stats = {"promos": 0, "dialogs": 0, "drags": 0, "dots": 0, "skips": 0}
    while not board.is_game_over() and plies < 140:
        mv = pick_move(board, rnd)
        mover = board.turn
        p = board.piece_at(mv.from_square)
        key = pkey(p)
        fx0, fy0 = sc.xy(mv.from_square)
        tx0, ty0 = sc.xy(mv.to_square)
        if mover == user:
            if rnd.random() < 0.3:
                dots = [m.to_square for m in board.legal_moves if m.from_square == mv.from_square]
                emit(board, last, {"selected": mv.from_square, "dots": dots}, rnd.randint(1, 5))
                stats["dots"] += 1
            if rnd.random() < 0.25:  # mid drag ghost
                t = rnd.uniform(0.3, 0.8)
                fx, fy = fx0 + (tx0 - fx0) * t + rnd.randint(-8, 8), fy0 + (ty0 - fy0) * t + rnd.randint(-8, 8)
                emit(board, last, {"ghost": mv.from_square, "float": (key, fx, fy)}, rnd.randint(1, 5))
                stats["drags"] += 1
            if mv.promotion:
                b2 = board.copy()
                b2.remove_piece_at(mv.from_square)
                b2.set_piece_at(mv.to_square, chess.Piece(chess.PAWN, mover))
                emit(b2, last, {"promo": (mover, mv.to_square)}, rnd.randint(1, 9))
                stats["dialogs"] += 1
        elif rnd.random() < 0.6:
            t = rnd.uniform(0.3, 0.7)
            emit(board, last, {"hide": [mv.from_square], "float": (key, fx0 + (tx0 - fx0) * t, fy0 + (ty0 - fy0) * t)})
        board.push(mv)
        plies += 1
        if mv.promotion:
            stats["promos"] += 1
        last = [mv.from_square, mv.to_square]
        # two moves in one frame, roblox: your move and the bot answer land together, never three
        if (not skipped if sc.theme == "roblox" else mover != user) and rnd.random() < sc.skip and not board.is_game_over():
            stats["skips"] += 1
            skipped = True
            continue
        skipped = False
        settle(last, rnd.choice([2, 2, 3]))
    try:
        env = dict(os.environ, OPEN_CHEASE_DATA=os.path.join(d, "data"))
        res = subprocess.run([binary, "--replay", d, "--team", "white" if user == chess.WHITE else "black"],
                             capture_output=True, text=True, env=env)
        got = {}
        for line in res.stdout.splitlines():
            parts = line.split(" ", 2)
            if len(parts) == 3:
                got[int(parts[0])] = (parts[1], parts[2])
    finally:
        if keep:
            print("frames kept in", d)
        else:
            shutil.rmtree(d, ignore_errors=True)
    ok = bad = promo_ok = promo_bad = 0
    first_bad = None
    for idx, fen, promo in checks:
        state, gf = got.get(idx, ("?", ""))
        want = fen.split()[:2]
        have = gf.split()[:2]
        good = state == "T" and want == have
        if good:
            ok += 1
        else:
            bad += 1
            if first_bad is None:
                first_bad = (idx, " ".join(want), state + " " + " ".join(have))
        if promo:
            if good:
                promo_ok += 1
            else:
                promo_bad += 1
    # false moves: we showed a position the game never had. lost: dropped the board while it was still there
    false = lost = 0
    prev = None
    for i in sorted(got):
        st, gf = got[i]
        pl = " ".join(gf.split()[:2])
        if st == "T" and pl != prev and pl not in truth:
            false += 1
        if st != "T" and prev is not None and prev != "S":
            lost += 1
        prev = pl if st == "T" else "S"
    final_ok = checks and got.get(checks[-1][0], ("?", ""))[1].split()[:2] == checks[-1][1].split()[:2]
    desc = f"seed {seed}: {sc.theme}/{sc.pset}/{sc.sq}px/{'flip' if sc.flip else 'norm'}/{sc.promo_style}{'/midgame' if midgame else ''} user={'w' if user else 'b'}"
    return dict(ok=ok, bad=bad, promo_ok=promo_ok, promo_bad=promo_bad, final_ok=bool(final_ok), first_bad=first_bad,
                desc=desc, frames=frames[0], stats=stats, false=false, lost=lost)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=10)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--bin", default=os.path.join(root, "bin", "open-chease"))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="oc-track-")
    tot = dict(ok=0, bad=0, promo_ok=0, promo_bad=0, final=0, false=0, lost=0)
    try:
        for g in range(a.games):
            r = sim_game(a.seed + g, tmp, a.bin, a.keep)
            for k in ("ok", "bad", "promo_ok", "promo_bad", "false", "lost"):
                tot[k] += r[k]
            tot["final"] += r["final_ok"]
            flag = "ok " if not r["bad"] else "BAD"
            print(f"{flag} {r['desc']}  moves {r['ok']}/{r['ok'] + r['bad']}  promos {r['promo_ok']}/{r['promo_ok'] + r['promo_bad']}  final {'ok' if r['final_ok'] else 'WRONG'}  false {r['false']} lost {r['lost']}  {r['stats']}")
            if r["first_bad"]:
                print(f"      first wrong at frame {r['first_bad'][0]}: want {r['first_bad'][1]}  got {r['first_bad'][2]}")
            sys.stdout.flush()
    finally:
        if not a.keep:
            shutil.rmtree(tmp, ignore_errors=True)
    n = tot["ok"] + tot["bad"]
    pn = tot["promo_ok"] + tot["promo_bad"]
    print(f"\nmoves right {tot['ok']}/{n} ({100 * tot['ok'] / max(1, n):.1f}%)  promotions right {tot['promo_ok']}/{pn}  games ending right {tot['final']}/{a.games}  false moves {tot['false']}  board lost {tot['lost']}")


if __name__ == "__main__":
    main()
