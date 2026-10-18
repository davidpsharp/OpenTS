#!/usr/bin/env python3
"""Make the !OpenTS application sprites from port/riscos/icons/opents-icon.png.

Writes two RISC OS sprite files (filetype &FF9) of 32-bit sprites with a mask, each holding
"!opents" and the small "sm!opents":
  !Sprites22  34x34 and 18x18 at 90x90 dpi (square-pixel modes)
  !Sprites    34x17 and 18x9 at 90x45 dpi (rectangular-pixel modes)
OpenTS needs RISC OS 5, which plots 32-bit sprites in any mode. ImageMagick downscales.

The picture is made by make-icon.py from the OpenTS banner. Any other square picture whose
background is transparent works too (lift-subject.swift cuts the subject out of a photo).

Usage: make-sprites.py [picture.png [output directory]]
  (default: port/riscos/icons/opents-icon.png into port/riscos/app/!OpenTS)
  PREVIEW=<dir> also writes enlarged PNGs of each sprite for checking.

Adapted from the Vanilla Conquer RISC OS port's tool of the same name.
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
PICTURE = os.path.join(ROOT, "port", "riscos", "icons", "opents-icon.png")
NAME = "!opents"


def pack_rows(values, w, h, bpp):
    """Pack pixel values into RISC OS sprite rows (whole words, pixel 0 in the low bits)."""
    words = (w * bpp + 31) // 32
    out = bytearray()
    for y in range(h):
        bits = 0
        for x in range(w):
            bits |= values[y * w + x] << (x * bpp)
        out += bits.to_bytes(words * 4, "little")
    return words, bytes(out)


def sprite_file(sprites):
    body = b"".join(sprites)
    # A sprite file is a sprite area without its first (size) word.
    return struct.pack("<III", len(sprites), 16, 16 + len(body)) + body


def pixels(path, w, h):
    raw = subprocess.run(
        ["magick", path, "-filter", "Lanczos", "-resize", "%dx%d!" % (w, h), "-unsharp", "0x0.6+0.6+0",
         "-depth", "8", "rgba:-"], check=True, capture_output=True).stdout
    return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]


def sprite(name, pixels, w, h, xdpi, ydpi):
    """A 32bpp sprite (0xXXBBGGRR, red in the low byte) with a 1bpp mask."""
    image = b"".join(struct.pack("<I", (p[2] << 16) | (p[1] << 8) | p[0]) if p[3] >= 128 else b"\0\0\0\0" for p in pixels)
    mask_words, mask = pack_rows([1 if p[3] >= 128 else 0 for p in pixels], w, h, 1)
    mode = (6 << 27) | (ydpi << 14) | (xdpi << 1) | 1
    header = 44
    total = header + len(image) + len(mask)
    return struct.pack("<I12sIIIIIII", total, name.encode("latin-1"), w - 1, h - 1, 0, 31,
                       header, header + len(image), mode) + image + mask


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else PICTURE
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "port", "riscos", "app", "!OpenTS")
    files = {"!Sprites22,ff9": [(NAME, 34, 34), ("sm" + NAME, 18, 18)],
             "!Sprites,ff9": [(NAME, 34, 17), ("sm" + NAME, 18, 9)]}
    for fname, shapes in files.items():
        spr = []
        for name, w, h in shapes:
            px = pixels(source, w, h)
            spr.append(sprite(name, px, w, h, 90, 90 if h == w else 45))
            if os.environ.get("PREVIEW"):
                raw = bytes(c for p in px for c in (p[:3] if p[3] >= 128 else (192, 192, 192)))
                subprocess.run(["magick", "-size", "%dx%d" % (w, h), "-depth", "8", "rgb:-", "-scale", "800%",
                                os.path.join(os.environ["PREVIEW"], "%s-%s.png" % (fname.split(",")[0].lstrip("!"), name.lstrip("!")))],
                               input=raw, check=True)
        with open(os.path.join(out, fname), "wb") as f:
            f.write(sprite_file(spr))
        print(os.path.join(out, fname))


main()
