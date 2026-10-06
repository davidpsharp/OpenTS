#!/usr/bin/env python3
"""Make the !OpenTS application sprites from code/resources/app-icon/opents-flat.svg.

Writes two RISC OS sprite files (filetype &FF9), each holding a 34x34 "!opents" and an
18x18 "sm!opents" icon with a mask:
  !Sprites22  mode 28, 8bpp, default 256-colour palette (square-pixel modes)
  !Sprites    mode 27, 4bpp, with its own 16-colour palette (the fallback; the Wimp
              scales it for rectangular-pixel modes such as 12)

The flat version of the icon is the one the project uses at small sizes (see
build-ico.sh). ImageMagick renders and downscales it.

Usage: make-sprites.py [output directory]   (default: port/riscos/app/!OpenTS)
       make-sprites.py --photo <image.png> [output directory]
  PREVIEW=<dir> also writes enlarged PNGs of each sprite for checking.

--photo makes true-colour sprites instead, for RISC OS 5, from a square picture whose
background is transparent: 32-bit sprites with a mask, 34x34 and 18x18 at 90x90 dpi in
!Sprites22 and 34x17 and 18x9 at 90x45 dpi in !Sprites (rectangular-pixel modes).

Adapted from the Vanilla Conquer RISC OS port's tool of the same name.
"""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SVG = os.path.join(ROOT, "code", "resources", "app-icon", "opents-flat.svg")
NAME = "!opents"


def render(size, tmp):
    """Return size*size (r, g, b, a) tuples."""
    big = os.path.join(tmp, "icon.png")
    if not os.path.exists(big):
        subprocess.run(["magick", "-background", "none", "-density", "384", SVG, "-resize", "1024x1024!", big], check=True)
    raw = subprocess.run(
        ["magick", big, "-filter", "Lanczos", "-resize", "%dx%d" % (size, size), "-depth", "8", "rgba:-"],
        check=True, capture_output=True).stdout
    return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]


def default_256():
    """The standard RISC OS 256-colour palette: two bits of tint shared by
    all three guns, plus two more bits per gun."""
    pal = []
    for p in range(256):
        t = p & 3
        r = ((p >> 4) & 1) << 3 | ((p >> 2) & 1) << 2 | t
        g = ((p >> 6) & 1) << 3 | ((p >> 5) & 1) << 2 | t
        b = ((p >> 7) & 1) << 3 | ((p >> 3) & 1) << 2 | t
        pal.append((r * 17, g * 17, b * 17))
    return pal


def nearest(pal, c):
    return min(range(len(pal)), key=lambda i: sum((pal[i][k] - c[k]) ** 2 for k in range(3)))


def kmeans_16(pixels):
    """Pick 16 colours for the opaque pixels (simple k-means)."""
    pts = [p[:3] for p in pixels if p[3] >= 128]
    uniq = sorted(set(pts))
    pal = [uniq[i * len(uniq) // 16] for i in range(16)] if len(uniq) >= 16 else uniq + [(0, 0, 0)] * (16 - len(uniq))
    for _ in range(20):
        sums = [[0, 0, 0, 0] for _ in pal]
        for c in pts:
            s = sums[nearest(pal, c)]
            s[0] += c[0]; s[1] += c[1]; s[2] += c[2]; s[3] += 1
        pal = [(s[0] // s[3], s[1] // s[3], s[2] // s[3]) if s[3] else pal[i] for i, s in enumerate(sums)]
    return pal


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


def sprite(name, pixels, size, mode, bpp, pal, store_palette):
    idx = [nearest(pal, p[:3]) if p[3] >= 128 else 0 for p in pixels]
    mask = [(1 << bpp) - 1 if p[3] >= 128 else 0 for p in pixels]
    words, image = pack_rows(idx, size, size, bpp)
    _, maskdata = pack_rows(mask, size, size, bpp)
    palette = b"".join(struct.pack("<II", *(2 * [b << 24 | g << 16 | r << 8])) for r, g, b in pal) if store_palette else b""
    header = 44 + len(palette)
    total = header + len(image) + len(maskdata)
    return struct.pack("<I12sIIIIIII", total, name.encode("latin-1"), words - 1, size - 1, 0,
                       (size * bpp - 1) % 32, header, header + len(image), mode) + palette + image + maskdata, idx


def sprite_file(sprites):
    body = b"".join(sprites)
    # A sprite file is a sprite area without its first (size) word.
    return struct.pack("<III", len(sprites), 16, 16 + len(body)) + body


def preview(path, pixels_idx, pal, size):
    """Write a PNG of what the sprite looks like, for checking."""
    raw = bytes(c for i in pixels_idx for c in pal[i])
    subprocess.run(["magick", "-size", "%dx%d" % (size, size), "-depth", "8", "rgb:-", "-scale", "400%", path],
                   input=raw, check=True)


def photo_pixels(path, w, h):
    raw = subprocess.run(
        ["magick", path, "-filter", "Lanczos", "-resize", "%dx%d!" % (w, h), "-unsharp", "0x0.6+0.6+0",
         "-depth", "8", "rgba:-"], check=True, capture_output=True).stdout
    return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]


def photo_sprite(name, pixels, w, h, xdpi, ydpi):
    """A 32bpp sprite (0xXXBBGGRR, red in the low byte) with a 1bpp mask."""
    image = b"".join(struct.pack("<I", (p[2] << 16) | (p[1] << 8) | p[0]) if p[3] >= 128 else b"\0\0\0\0" for p in pixels)
    mask_words, mask = pack_rows([1 if p[3] >= 128 else 0 for p in pixels], w, h, 1)
    mode = (6 << 27) | (ydpi << 14) | (xdpi << 1) | 1
    header = 44
    total = header + len(image) + len(mask)
    return struct.pack("<I12sIIIIIII", total, name.encode("latin-1"), w - 1, h - 1, 0, 31,
                       header, header + len(image), mode) + image + mask


def photo_main(source, out):
    files = {"!Sprites22,ff9": [(NAME, 34, 34), ("sm" + NAME, 18, 18)],
             "!Sprites,ff9": [(NAME, 34, 17), ("sm" + NAME, 18, 9)]}
    for fname, shapes in files.items():
        spr = []
        for name, w, h in shapes:
            pixels = photo_pixels(source, w, h)
            spr.append(photo_sprite(name, pixels, w, h, 90, 90 if h == w else 45))
            if os.environ.get("PREVIEW"):
                raw = bytes(c for p in pixels for c in (p[:3] if p[3] >= 128 else (192, 192, 192)))
                subprocess.run(["magick", "-size", "%dx%d" % (w, h), "-depth", "8", "rgb:-", "-scale", "800%",
                                os.path.join(os.environ["PREVIEW"], "%s-%s.png" % (fname.split(",")[0].lstrip("!"), name.lstrip("!")))],
                               input=raw, check=True)
        with open(os.path.join(out, fname), "wb") as f:
            f.write(sprite_file(spr))
        print(os.path.join(out, fname))


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--photo":
        photo_main(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else os.path.join(ROOT, "port", "riscos", "app", "!OpenTS"))
        return
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "port", "riscos", "app", "!OpenTS")
    with tempfile.TemporaryDirectory() as tmp:
        big, small = render(34, tmp), render(18, tmp)
    pal256 = default_256()
    pal16 = kmeans_16(big + small)
    files = {"!Sprites22,ff9": (28, 8, pal256, False), "!Sprites,ff9": (27, 4, pal16, True)}
    for fname, (mode, bpp, pal, store) in files.items():
        spr = []
        for name, pixels, size in ((NAME, big, 34), ("sm" + NAME, small, 18)):
            data, idx = sprite(name, pixels, size, mode, bpp, pal, store)
            spr.append(data)
            if os.environ.get("PREVIEW"):
                preview(os.path.join(os.environ["PREVIEW"], "%s-%s.png" % (fname.split(",")[0].lstrip("!"), name.lstrip("!"))), idx, pal, size)
        with open(os.path.join(out, fname), "wb") as f:
            f.write(sprite_file(spr))
        print(os.path.join(out, fname))


main()
