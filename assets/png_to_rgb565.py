#!/usr/bin/env python3
"""Convert a PNG to a TFT_eSPI-ready RGB565 C header.
Usage: png_to_rgb565.py <in.png> <symbol> <out.h>
"""
import sys
from PIL import Image

src, name, out = sys.argv[1], sys.argv[2], sys.argv[3]
img = Image.open(src).convert("RGB")
w, h = img.size
px = img.load()

vals = []
for y in range(h):
    for x in range(w):
        r, g, b = px[x, y]
        vals.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

with open(out, "w") as f:
    f.write("// Auto-generated from %s by assets/png_to_rgb565.py — do not edit.\n" % src)
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write("#define %s_W %d\n#define %s_H %d\n" % (name.upper(), w, name.upper(), h))
    f.write("static const uint16_t %s[%d] = {\n" % (name, w * h))
    for i in range(0, len(vals), 12):
        f.write("  " + ",".join("0x%04X" % v for v in vals[i:i + 12]) + ",\n")
    f.write("};\n")
print("wrote %s (%dx%d, %d px)" % (out, w, h, w * h))
