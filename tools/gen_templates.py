import ctypes, sys, os

rsvg = ctypes.CDLL("librsvg-2.so.2")
cairo = ctypes.CDLL("libcairo.so.2")

class Rect(ctypes.Structure):
    _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double), ("width", ctypes.c_double), ("height", ctypes.c_double)]

rsvg.rsvg_handle_new_from_data.restype = ctypes.c_void_p
rsvg.rsvg_handle_new_from_data.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
rsvg.rsvg_handle_render_document.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(Rect), ctypes.c_void_p]
cairo.cairo_image_surface_create.restype = ctypes.c_void_p
cairo.cairo_create.restype = ctypes.c_void_p
cairo.cairo_create.argtypes = [ctypes.c_void_p]
cairo.cairo_image_surface_get_data.restype = ctypes.POINTER(ctypes.c_uint8)
cairo.cairo_image_surface_get_data.argtypes = [ctypes.c_void_p]
cairo.cairo_surface_flush.argtypes = [ctypes.c_void_p]
cairo.cairo_destroy.argtypes = [ctypes.c_void_p]
cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]

N, S = 24, 192
src, out = sys.argv[1], sys.argv[2]

def render(path):
    data = open(path, "rb").read()
    h = rsvg.rsvg_handle_new_from_data(data, len(data), None)
    surf = cairo.cairo_image_surface_create(0, S, S)
    cr = cairo.cairo_create(surf)
    rsvg.rsvg_handle_render_document(h, cr, ctypes.byref(Rect(0, 0, S, S)), None)
    cairo.cairo_surface_flush(surf)
    buf = cairo.cairo_image_surface_get_data(surf)
    alpha = [buf[i * 4 + 3] for i in range(S * S)]
    cairo.cairo_destroy(cr)
    cairo.cairo_surface_destroy(surf)
    return alpha

SCALES = [1.0, 0.93, 0.86, 0.8, 0.74]
SETS = sys.argv[3].split(",") if len(sys.argv) > 3 else ["cburnett"]
lines = ["#ifndef OC_TEMPLATES_DATA_H", "#define OC_TEMPLATES_DATA_H", "#include <stdint.h>",
         "#define BUILTIN_SETS %d" % len(SETS),
         "#define BUILTIN_SCALES %d" % len(SCALES),
         "static const uint8_t builtin_templates[%d][%d][2][6][%d] = {" % (len(SETS), len(SCALES), N * N // 8)]
for pset in SETS:
    renders = {c + p: render(os.path.join(src, pset, c + p + ".svg")) for c in "wb" for p in "PNBRQK"}
    lines.append("{")
    for sc in SCALES:
        lines.append("{")
        for c in "wb":
            lines.append("{")
            for p in "PNBRQK":
                a = renders[c + p]
                bits = []
                for gy in range(N):
                    for gx in range(N):
                        # middle only, coords and borders leak in otherwise
                        x = (0.12 + 0.76 * (gx + 0.5) / N)
                        y = (0.12 + 0.76 * (gy + 0.5) / N)
                        px = int(((x - 0.5) / sc + 0.5) * S)
                        py = int(((y - 0.5) / sc + 0.5) * S)
                        v = a[py * S + px] if 0 <= px < S and 0 <= py < S else 0
                        bits.append(1 if v > 128 else 0)
                packed = [sum(bits[i + k] << k for k in range(8)) for i in range(0, len(bits), 8)]
                lines.append("{" + ",".join(map(str, packed)) + "},")
            lines.append("},")
        lines.append("},")
    lines.append("},")
lines += ["};", "#endif"]
open(out, "w").write("\n".join(lines) + "\n")
