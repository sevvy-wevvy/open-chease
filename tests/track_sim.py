import argparse, ctypes, os, random, shutil, subprocess, sys, tempfile
import chess
from PIL import Image, ImageDraw, ImageFont

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


def pkey(p):
    return ("w" if p.color == chess.WHITE else "b") + p.symbol().upper()


class Scene:
    def __init__(self, rnd):
        self.theme = rnd.choice(list(THEMES))
        self.pset = rnd.choice(["cburnett"] * 3 + ["staunty", "alpha", "maestro"])
        self.sq = rnd.choice([44, 52, 60, 64, 72, 80])
        self.W, self.H = 1000, 760
        self.x = rnd.randint(10, self.W - self.sq * 8 - 220)
        self.y = rnd.randint(70, self.H - self.sq * 8 - 10)
        self.flip = rnd.random() < 0.45
        self.coords = rnd.random() < 0.6
        self.promo_style = rnd.choice(["chesscom", "lichess"])
        self.font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", max(9, self.sq // 6))

    def xy(self, sq):
        f, r = chess.square_file(sq), chess.square_rank(sq)
        dc, dr = (7 - f, r) if self.flip else (f, 7 - r)
        return self.x + dc * self.sq, self.y + dr * self.sq

    def render(self, board, last=None, extra=None):
        light, dark, hll, hld = THEMES[self.theme]
        img = Image.new("RGB", (self.W, self.H), (38, 36, 33))
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
            d.rectangle([x0, y0, x0 + self.sq - 1, y0 + self.sq - 1], fill=col)
            if self.coords:
                oc = dark if is_light else light
                f, r = chess.square_file(sq), chess.square_rank(sq)
                if (x0 - self.x) == 0:
                    d.text((x0 + 3, y0 + 2), str(r + 1), fill=oc, font=self.font)
                if (y0 - self.y) == self.sq * 7:
                    d.text((x0 + self.sq - self.sq // 6, y0 + self.sq - self.sq // 4), "abcdefgh"[f], fill=oc, font=self.font)
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
    frames = []
    checks = []

    def emit(img, n=1):
        for _ in range(n):
            frames.append(img)

    def settle(last, n):
        emit(sc.render(board, last), n)
        checks.append((len(frames) - 1, board.fen(), board.move_stack[-1].promotion if board.move_stack else None))

    last = [board.move_stack[-1].from_square, board.move_stack[-1].to_square] if board.move_stack else None
    settle(last, 3)
    plies = 0
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
                emit(sc.render(board, last, {"selected": mv.from_square, "dots": dots}), rnd.randint(1, 5))
                stats["dots"] += 1
            if rnd.random() < 0.25:  # mid drag ghost
                t = rnd.uniform(0.3, 0.8)
                fx, fy = fx0 + (tx0 - fx0) * t + rnd.randint(-8, 8), fy0 + (ty0 - fy0) * t + rnd.randint(-8, 8)
                emit(sc.render(board, last, {"ghost": mv.from_square, "float": (key, fx, fy)}), rnd.randint(1, 5))
                stats["drags"] += 1
            if mv.promotion:
                b2 = board.copy()
                b2.remove_piece_at(mv.from_square)
                b2.set_piece_at(mv.to_square, chess.Piece(chess.PAWN, mover))
                emit(sc.render(b2, last, {"promo": (mover, mv.to_square)}), rnd.randint(1, 9))
                stats["dialogs"] += 1
        elif rnd.random() < 0.6:
            t = rnd.uniform(0.3, 0.7)
            emit(sc.render(board, last, {"hide": [mv.from_square], "float": (key, fx0 + (tx0 - fx0) * t, fy0 + (ty0 - fy0) * t)}))
        board.push(mv)
        plies += 1
        if mv.promotion:
            stats["promos"] += 1
        last = [mv.from_square, mv.to_square]
        # two moves in one frame
        if mover != user and rnd.random() < 0.08 and not board.is_game_over():
            stats["skips"] += 1
            continue
        settle(last, rnd.choice([2, 2, 3]))
    d = tempfile.mkdtemp(prefix="oc-sim-", dir=out)
    try:
        for i, im in enumerate(frames):
            im.save(os.path.join(d, "%06d.ppm" % i))
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
    final_ok = checks and got.get(checks[-1][0], ("?", ""))[1].split()[:2] == checks[-1][1].split()[:2]
    desc = f"seed {seed}: {sc.theme}/{sc.pset}/{sc.sq}px/{'flip' if sc.flip else 'norm'}/{sc.promo_style}{'/midgame' if midgame else ''} user={'w' if user else 'b'}"
    return dict(ok=ok, bad=bad, promo_ok=promo_ok, promo_bad=promo_bad, final_ok=bool(final_ok), first_bad=first_bad,
                desc=desc, frames=len(frames), stats=stats)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=10)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--bin", default=os.path.join(root, "bin", "open-chease"))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="oc-track-", dir="/tmp" if a.keep else None)
    tot = dict(ok=0, bad=0, promo_ok=0, promo_bad=0, final=0)
    try:
        for g in range(a.games):
            r = sim_game(a.seed + g, tmp, a.bin, a.keep)
            for k in ("ok", "bad", "promo_ok", "promo_bad"):
                tot[k] += r[k]
            tot["final"] += r["final_ok"]
            flag = "ok " if not r["bad"] else "BAD"
            print(f"{flag} {r['desc']}  moves {r['ok']}/{r['ok'] + r['bad']}  promos {r['promo_ok']}/{r['promo_ok'] + r['promo_bad']}  final {'ok' if r['final_ok'] else 'WRONG'}  {r['stats']}")
            if r["first_bad"]:
                print(f"      first wrong at frame {r['first_bad'][0]}: want {r['first_bad'][1]}  got {r['first_bad'][2]}")
            sys.stdout.flush()
    finally:
        if not a.keep:
            shutil.rmtree(tmp, ignore_errors=True)
    n = tot["ok"] + tot["bad"]
    pn = tot["promo_ok"] + tot["promo_bad"]
    print(f"\nmoves right {tot['ok']}/{n} ({100 * tot['ok'] / max(1, n):.1f}%)  promotions right {tot['promo_ok']}/{pn}  games ending right {tot['final']}/{a.games}")


if __name__ == "__main__":
    main()
