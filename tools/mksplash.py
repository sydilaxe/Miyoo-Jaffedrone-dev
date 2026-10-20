#!/usr/bin/env python3
"""The start-up screen's background: the Jaffe house's roofline (two shed
roofs meeting in a valley against the sky), scaled to 640x480 and stored
with a 256-colour palette.

File: "JDI1", width and height (uint16 little-endian), 256 RGB palette
entries, then one palette index per pixel, row by row.

usage: mksplash.py PHOTO OUT.jdi [PREVIEW.png]
"""
import struct
import sys
from PIL import Image

W, H = 640, 480


def main():
    im = Image.open(sys.argv[1]).convert("RGB")
    s = W / im.width
    im = im.resize((W, round(im.height * s)), Image.LANCZOS).crop((0, 0, W, H))
    q = im.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = (q.getpalette() + [0] * 768)[:768]
    with open(sys.argv[2], "wb") as f:
        f.write(b"JDI1" + struct.pack("<HH", W, H) + bytes(pal) + q.tobytes())
    if len(sys.argv) > 3:
        q.convert("RGB").save(sys.argv[3])


if __name__ == "__main__":
    main()
