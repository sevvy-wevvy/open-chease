import argparse, ctypes, os
from PIL import Image, ImageDraw, ImageFont

rsvg = ctypes.CDLL("librsvg-2.so.2"); cairo = ctypes.CDLL("libcairo.so.2")
class R(ctypes.Structure):
    _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double), ("w", ctypes.c_double), ("h", ctypes.c_double)]
rsvg.rsvg_handle_new_from_data.restype = ctypes.c_void_p
rsvg.rsvg_handle_new_from_data.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
rsvg.rsvg_handle_render_document.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(R), ctypes.c_void_p]
cairo.cairo_image_surface_create.restype = ctypes.c_void_p
cairo.cairo_create.restype = ctypes.c_void_p; cairo.cairo_create.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_data.restype = ctypes.POINTER(ctypes.c_uint8)
cairo.cairo_image_surface_get_data.argtypes = [ctypes.c_void_p]
cairo.cairo_surface_flush.argtypes = [ctypes.c_void_p]

def piece_img(path, s):
    data = open(path, "rb").read()
    h = rsvg.rsvg_handle_new_from_data(data, len(data), None)
    surf = cairo.cairo_image_surface_create(0, s, s); cr = cairo.cairo_create(surf)
    rsvg.rsvg_handle_render_document(h, cr, ctypes.byref(R(0, 0, s, s)), None)
    cairo.cairo_surface_flush(surf)
    buf = bytes(ctypes.cast(cairo.cairo_image_surface_get_data(surf), ctypes.POINTER(ctypes.c_uint8 * (s * s * 4))).contents)
    im = Image.frombuffer("RGBA", (s, s), buf, "raw", "BGRA", 0, 1)
    return im.copy()

THEMES = {"lichess": ((240, 217, 181), (181, 136, 99), (205, 210, 106)),
          "chesscom": ((235, 236, 208), (115, 149, 82), (245, 246, 130)),
          "blue": ((222, 227, 230), (140, 162, 173), (155, 199, 0)),
          "pink": ((245, 241, 242), (235, 148, 164), (239, 176, 145)),
          "purple": ((200, 182, 230), (112, 82, 160), (230, 200, 120)),
          "night": ((120, 124, 134), (62, 66, 76), (110, 140, 90)),
          "wood": ((222, 184, 135), (160, 110, 70), (200, 200, 110)),
          "red": ((240, 220, 220), (190, 60, 60), (240, 200, 120))}

ap = argparse.ArgumentParser()
ap.add_argument("fen"); ap.add_argument("out")
ap.add_argument("--theme", default="lichess"); ap.add_argument("--sq", type=int, default=80)
ap.add_argument("--x", type=int, default=300); ap.add_argument("--y", type=int, default=120)
ap.add_argument("--black", action="store_true"); ap.add_argument("--hl", default="")
ap.add_argument("--coords", action="store_true"); ap.add_argument("--W", type=int, default=1920); ap.add_argument("--H", type=int, default=1080)
ap.add_argument("--pieces", required=True); ap.add_argument("--scale", type=float, default=1.0)
ap.add_argument("--texture", default="none"); ap.add_argument("--tint", default=""); ap.add_argument("--seed", type=int, default=0)
a = ap.parse_args()
light, dark, hlc = THEMES[a.theme]
img = Image.new("RGB", (a.W, a.H), (49, 46, 43))
d = ImageDraw.Draw(img)
d.rectangle([0, 0, a.W, 60], fill=(30, 30, 30)); d.text((20, 20), "lichess.org  play  puzzles  learn", fill=(200, 200, 200))
d.rectangle([a.x + a.sq * 8 + 40, a.y, a.x + a.sq * 8 + 400, a.y + 500], fill=(38, 36, 33))
for i in range(12): d.text((a.x + a.sq * 8 + 60, a.y + 20 + 30 * i), f"{i+1}. e4 e5 Nf3 Nc6", fill=(220, 220, 220))
hl = set(a.hl.split(",")) if a.hl else set()
rows = a.fen.split()[0].split("/")
board = {}
for r, row in enumerate(rows):
    f = 0
    for ch in row:
        if ch.isdigit(): f += int(ch)
        else: board[(7 - r, f)] = ch; f += 1
cache = {}
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", max(9, a.sq // 6))
for dr in range(8):
    for dc in range(8):
        rank, file = (7 - dr, dc) if not a.black else (dr, 7 - dc)
        x0, y0 = a.x + dc * a.sq, a.y + dr * a.sq
        col = light if (dr + dc) % 2 == 0 else dark
        name = "abcdefgh"[file] + str(rank + 1)
        if name in hl:
            base = col; col = tuple(int(base[i] * 0.5 + hlc[i] * 0.5) for i in range(3))
        d.rectangle([x0, y0, x0 + a.sq - 1, y0 + a.sq - 1], fill=col)
if a.texture != "none":
    import numpy as np
    rng = np.random.default_rng(a.seed)
    n = a.sq * 8
    arr = np.asarray(img, dtype=np.float32).copy()
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    if a.texture == "wood":
        warp = 6 * np.sin(yy * 0.013 + rng.uniform(0, 6)) + 3 * np.sin(yy * 0.05 + rng.uniform(0, 6))
        grain = 16 * np.sin((xx + warp * 4) * 0.09 * 80 / a.sq * 2) + rng.normal(0, 5, (n, n))
        flip = ((xx // a.sq + yy // a.sq) % 2 == 1)
        grain2 = 16 * np.sin((yy + warp * 4) * 0.09 * 80 / a.sq * 2) + rng.normal(0, 5, (n, n))
        delta = np.where(flip, grain2, grain)[..., None]
    elif a.texture == "marble":
        small = rng.normal(0, 1, (max(4, n // 40), max(4, n // 40))).astype(np.float32)
        big = np.asarray(Image.fromarray(small).resize((n, n), Image.BICUBIC), dtype=np.float32)
        delta = (22 * big / (np.abs(big).max() + 1e-6) + rng.normal(0, 3, (n, n)))[..., None]
    elif a.texture == "gradient":
        delta = None
        f = (0.82 + 0.3 * (xx + yy) / (2 * n))[..., None]
        arr[a.y:a.y + n, a.x:a.x + n] *= f
    else:
        delta = rng.normal(0, 7, (n, n, 3))
    if delta is not None: arr[a.y:a.y + n, a.x:a.x + n] += delta
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
tint = {}
if a.tint:
    for t in a.tint.split(","):
        tint[t[0]] = tuple(int(t[1:][i:i + 2], 16) for i in (0, 2, 4))
for dr in range(8):
    for dc in range(8):
        rank, file = (7 - dr, dc) if not a.black else (dr, 7 - dc)
        x0, y0 = a.x + dc * a.sq, a.y + dr * a.sq
        name = "abcdefgh"[file] + str(rank + 1)
        if a.coords:
            oc = dark if (dr + dc) % 2 == 0 else light
            if dc == 0: d.text((x0 + 3, y0 + 2), str(rank + 1), fill=oc, font=font)
            if dr == 7: d.text((x0 + a.sq - a.sq // 6, y0 + a.sq - a.sq // 4), "abcdefgh"[file], fill=oc, font=font)
        p = board.get((rank, file))
        if p:
            key = ("w" if p.isupper() else "b") + p.upper()
            ps = int(a.sq * a.scale)
            if key not in cache:  # png if the set has one
                png = f"{a.pieces}/{key}.png"
                cache[key] = Image.open(png).convert("RGBA").resize((ps, ps), Image.LANCZOS) if os.path.exists(png) else piece_img(f"{a.pieces}/{key}.svg", ps)
                if key[0] in tint:
                    from PIL import ImageOps
                    gray = ImageOps.grayscale(cache[key])
                    col = ImageOps.colorize(gray, (20, 20, 20), tint[key[0]]).convert("RGBA")
                    col.putalpha(cache[key].getchannel("A"))
                    cache[key] = col
            off = (a.sq - ps) // 2
            img.paste(cache[key], (x0 + off, y0 + off), cache[key])
img.save(a.out)
