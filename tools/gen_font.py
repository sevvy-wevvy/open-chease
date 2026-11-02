import sys
from PIL import Image, ImageDraw, ImageFont

D = "/usr/share/fonts/truetype/"
fonts = [  # name, file or None for the pixel one
    ("normal", D + "dejavu/DejaVuSans-Bold.ttf"),
    ("pixel", None),
    ("mono", D + "dejavu/DejaVuSansMono-Bold.ttf"),
    ("serif", D + "dejavu/DejaVuSerif-Bold.ttf"),
    ("lato", D + "lato/Lato-Black.ttf"),
    ("narrow", D + "liberation/LiberationSansNarrow-Bold.ttf"),
]
sizes = [14, 20]

# own blocky glyphs, 7 rows tall plus a row under the line for g j p q y
PIX = """
! # # # # # . #
" #.# #.#
# .#.#. .#.#. ##### .#.#. ##### .#.#. .#.#.
$ ..#.. .#### #.#.. .###. ..#.# ####. ..#..
% #...# #..#. ...#. ..#.. .#... .#..# #...#
& .##.. #..#. .##.. ##..# #.##. #..#. .##.#
' # #
( ..# .#. #.. #.. #.. .#. ..#
) #.. .#. ..# ..# ..# .#. #..
* ..... ..#.. #.#.# .###. #.#.# ..#.. .....
+ ..... ..#.. ..#.. ##### ..#.. ..#.. .....
, .. .. .. .. .. .# .# #.
- .... .... .... #### .... .... ....
. . . . . . . #
/ ....# ...#. ...#. ..#.. .#... .#... #....
0 .###. #...# #..## #.#.# ##..# #...# .###.
1 ..#.. .##.. ..#.. ..#.. ..#.. ..#.. #####
2 .###. #...# ....# ..##. .#... #.... #####
3 .###. #...# ....# ..##. ....# #...# .###.
4 ...## ..#.# .#..# #...# ##### ....# ....#
5 ##### #.... ####. ....# ....# #...# .###.
6 ..##. .#... #.... ####. #...# #...# .###.
7 ##### #...# ....# ...#. ..#.. ..#.. ..#..
8 .###. #...# #...# .###. #...# #...# .###.
9 .###. #...# #...# .#### ....# ...#. .##..
: . . # . . . #
; .. .. .# .. .. .# .# #.
< ...# ..#. .#.. #... .#.. ..#. ...#
= ..... ..... ##### ..... ##### ..... .....
> #... .#.. ..#. ...# ..#. .#.. #...
? .###. #...# ....# ...#. ..#.. ..... ..#..
@ .###. #...# #.### #.#.# #.### #.... .####
A .###. #...# #...# ##### #...# #...# #...#
B ####. #...# #...# ####. #...# #...# ####.
C .###. #...# #.... #.... #.... #...# .###.
D ####. #...# #...# #...# #...# #...# ####.
E ##### #.... #.... ####. #.... #.... #####
F ##### #.... #.... ####. #.... #.... #....
G .###. #...# #.... #.### #...# #...# .###.
H #...# #...# #...# ##### #...# #...# #...#
I ### .#. .#. .#. .#. .#. ###
J ....# ....# ....# ....# #...# #...# .###.
K #...# #..#. #.#.. ##... #.#.. #..#. #...#
L #.... #.... #.... #.... #.... #.... #####
M #...# ##.## #.#.# #...# #...# #...# #...#
N #...# ##..# #.#.# #..## #...# #...# #...#
O .###. #...# #...# #...# #...# #...# .###.
P ####. #...# #...# ####. #.... #.... #....
Q .###. #...# #...# #...# #.#.# #..#. .##.#
R ####. #...# #...# ####. #.#.. #..#. #...#
S .#### #.... #.... .###. ....# ....# ####.
T ##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..
U #...# #...# #...# #...# #...# #...# .###.
V #...# #...# #...# #...# #...# .#.#. ..#..
W #...# #...# #...# #...# #.#.# ##.## #...#
X #...# #...# .#.#. ..#.. .#.#. #...# #...#
Y #...# #...# .#.#. ..#.. ..#.. ..#.. ..#..
Z ##### ....# ...#. ..#.. .#... #.... #####
[ ### #.. #.. #.. #.. #.. ###
\\ #.... .#... .#... ..#.. ...#. ...#. ....#
] ### ..# ..# ..# ..# ..# ###
^ ..#.. .#.#. #...#
_ ..... ..... ..... ..... ..... ..... ..... #####
` #. .#
a ..... ..... .###. ....# .#### #...# .####
b #.... #.... #.##. ##..# #...# #...# ####.
c ..... ..... .###. #...# #.... #...# .###.
d ....# ....# .##.# #..## #...# #...# .####
e ..... ..... .###. #...# ##### #.... .####
f ..## .#.. #### .#.. .#.. .#.. .#..
g ..... ..... .#### #...# #...# .#### ....# ####.
h #.... #.... #.##. ##..# #...# #...# #...#
i # . # # # # #
j ....# ..... ....# ....# ....# ....# #...# .###.
k #... #... #..# #.#. ##.. #.#. #..#
l #. #. #. #. #. #. .#
m ..... ..... ##.#. #.#.# #.#.# #...# #...#
n ..... ..... ####. #...# #...# #...# #...#
o ..... ..... .###. #...# #...# #...# .###.
p ..... ..... #.##. ##..# #...# ####. #.... #....
q ..... ..... .##.# #..## #...# .#### ....# ....#
r ..... ..... #.##. ##..# #.... #.... #....
s ..... ..... .#### #.... .###. ....# ####.
t .#. .#. ### .#. .#. .#. ..#
u ..... ..... #...# #...# #...# #...# .####
v ..... ..... #...# #...# #...# .#.#. ..#..
w ..... ..... #...# #...# #.#.# #.#.# .#.#.
x ..... ..... #...# .#.#. ..#.. .#.#. #...#
y ..... ..... #...# #...# #...# .#### ....# ####.
z ..... ..... ##### ...#. ..#.. .#... #####
{ ..# .#. .#. #.. .#. .#. ..#
| # # # # # # # #
} #.. .#. .#. ..# .#. .#. #..
~ ...... ...... .##..# #..##.
"""


def nib(a):
    return min(15, (a + 8) // 17)


def ttf_face(path, size, ref):
    font = ImageFont.truetype(path, size)
    hb = font.getbbox("H")
    shift = ref[0] - hb[1] if ref else 0  # capitals start where the normal font ones do
    out = []
    for c in range(32, 127):
        ch = chr(c)
        x0, y0, x1, y1 = font.getbbox(ch)
        adv = int(round(font.getlength(ch)))
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        px = []
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255)
            px = list(img.tobytes())
        out.append((x0, y0 + shift, w, h, adv, px))
    return out, (hb[1] + shift, hb[3] - hb[1])


def pixel_face(p, ref):
    g = {ln[0]: ln[2:].split() for ln in PIX.strip("\n").split("\n")}
    top = int(round(ref[0] + ref[1] / 2 - 7 * p / 2))
    out = []
    for c in range(32, 127):
        rows = g.get(chr(c))
        if not rows:
            out.append((0, top, 0, 0, 4 * p, []))
            continue
        w = max(len(r) for r in rows)
        px = [255 if ch == "#" else 0 for r in rows for ch in r.ljust(w, ".")]
        out.append((0, top, w, len(rows), (w + 1) * p, px))
    return out


out = sys.argv[1]
lines = ["#ifndef OC_FONT_DATA_H", "#define OC_FONT_DATA_H", "#include <stdint.h>", ""]
lines.append("// made by tools/gen_font.py, 4 bit alpha, two pixels a byte")
lines.append("typedef struct { int16_t x, y, w, h, adv; uint32_t off; } Glyph;")
lines.append("// px > 1 is the blocky one, each bit is a px by px block with a shadow under it")
lines.append("typedef struct { int size, px; const Glyph *glyphs; const uint8_t *bits; } FontFace;")
refs = {}
for name, path in fonts:
    for size in sizes:
        if path is None:
            p = 2 if size == 14 else 3
            gl, px = pixel_face(p, refs[size]), p
        else:
            gl, r = ttf_face(path, size, refs.get(size))
            refs.setdefault(size, r)
            px = 1
        nibs, glyphs = [], []
        for x, y, w, h, adv, bm in gl:
            glyphs.append((x, y, w, h, adv, len(nibs)))
            nibs += [nib(a) for a in bm]
        if len(nibs) & 1:
            nibs.append(0)
        bits = [nibs[i] | nibs[i + 1] << 4 for i in range(0, len(nibs), 2)]
        tag = f"{name}_{size}"
        lines.append(f"static const Glyph glyphs_{tag}[95] = {{")
        lines.append(",".join("{%d,%d,%d,%d,%d,%d}" % g for g in glyphs) + "};")
        lines.append(f"static const uint8_t bits_{tag}[{len(bits)}] = {{")
        for i in range(0, len(bits), 40):
            lines.append(",".join(str(b) for b in bits[i:i + 40]) + ",")
        lines.append("};")
        lines.append(f"static const FontFace font_{tag} = {{{size}, {px}, glyphs_{tag}, bits_{tag}}};")
lines.append(f"#define NFONTS {len(fonts)}")
lines.append("static const FontFace *const fonts[NFONTS][2] = {")
lines.append(",".join("{&font_%s_14, &font_%s_20}" % (n, n) for n, _ in fonts) + "};")
lines.append("#endif")
open(out, "w").write("\n".join(lines) + "\n")
