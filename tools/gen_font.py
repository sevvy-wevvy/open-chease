import sys
from PIL import Image, ImageDraw, ImageFont

path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
out = sys.argv[1]
sizes = [14, 20]
lines = ["#ifndef OC_FONT_DATA_H", "#define OC_FONT_DATA_H", "#include <stdint.h>", ""]
lines.append("typedef struct { int16_t x, y, w, h, adv; uint32_t off; } Glyph;")
lines.append("typedef struct { int size, ascent, height; const Glyph *glyphs; const uint8_t *bits; } FontFace;")
for size in sizes:
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    glyphs, bits = [], []
    for c in range(32, 127):
        ch = chr(c)
        x0, y0, x1, y1 = font.getbbox(ch)
        adv = int(round(font.getlength(ch)))
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        off = len(bits)
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255)
            bits.extend(img.tobytes())
        glyphs.append((x0, y0, w, h, adv, off))
    lines.append(f"static const Glyph glyphs_{size}[95] = {{")
    lines.append(",".join("{%d,%d,%d,%d,%d,%d}" % g for g in glyphs) + "};")
    lines.append(f"static const uint8_t bits_{size}[{len(bits)}] = {{")
    for i in range(0, len(bits), 40):
        lines.append(",".join(str(b) for b in bits[i:i + 40]) + ",")
    lines.append("};")
    lines.append(f"static const FontFace font_{size} = {{{size}, {ascent}, {ascent + descent}, glyphs_{size}, bits_{size}}};")
lines.append("#endif")
open(out, "w").write("\n".join(lines) + "\n")
