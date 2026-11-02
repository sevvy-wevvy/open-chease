import os, sys
from PIL import Image

# cuts the roblox pieces out of my screenshot into tests/pieces/roblox/
# usage: rbx_sprites.py <screenshot.png>, board at 4,78 square 163, white at the bottom
here = os.path.dirname(os.path.abspath(__file__))
X0, Y0, S = 4, 78, 163
SRC = {"wK": "e1", "wQ": "d1", "wR": "h1", "wB": "c1", "wN": "b1", "wP": "a2",
       "bK": "e8", "bQ": "d8", "bR": "h8", "bB": "c8", "bN": "g8", "bP": "b7"}
EMPTY = {0: "d6", 1: "e6"}


def origin(name):
    f, r = "abcdefgh".index(name[0]), int(name[1]) - 1
    return X0 + f * S, Y0 + (7 - r) * S, (f + r) % 2


def main():
    im = Image.open(sys.argv[1]).convert("RGB")
    prof = {}
    for par, name in EMPTY.items():
        x, y, _ = origin(name)
        prof[par] = [im.getpixel((x + 40, y + k)) for k in range(S)]
    out = os.path.join(here, "pieces", "roblox")
    os.makedirs(out, exist_ok=True)
    for key, name in SRC.items():
        x, y, par = origin(name)
        sp = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        for j in range(S):
            bg = prof[par][j]
            for i in range(S):
                if j < 10 or j > 152 or i < 3 or i > (150 if name[0] == "h" else 158) or (j < 28 and (i < 28 or i > 124)):  # board rim and our tag boxes
                    continue
                p = im.getpixel((x + i, y + j))
                ks = [p[c] / max(1, bg[c]) for c in range(3)]
                k = sum(ks) / 3
                if max(ks) - min(ks) < 0.08 and k < 1.02:  # bg just darker = outline edge or drop shadow
                    a = int(round(255 * (1 - min(1, k))))
                    if a > 12:
                        sp.putpixel((i, j), (0, 0, 0, a))
                else:
                    sp.putpixel((i, j), p + (255,))
        sp.save(os.path.join(out, key.lower() + ".png"))
    print("wrote", out)


if __name__ == "__main__":
    main()
