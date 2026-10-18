#!/usr/bin/env python3
"""Make the !OpenTS icon picture, port/riscos/icons/opents-icon.png, from the OpenTS banner.

The banner is too wide for a 34x34 icon, so the icon keeps two of its parts: the fiery "TS",
cut out of the banner by colour, on a gold-rimmed bronze chevron drawn in the banner's
colours. make-sprites.py then turns the picture into the application's sprites.

Usage: make-icon.py <opents-logo.png> [output.png]
  The banner is https://raw.githubusercontent.com/OpenTS-Developers/.github/main/assets/opents-logo.png
  (1280x389). Needs Pillow and NumPy.
"""
import os
import sys
from collections import deque

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
N = 1024
TS_BOX = (772, 88, 1062, 248)   # where "TS" is in the 1280x389 banner


def letter_mask(logo):
    """The banner's letters: very saturated orange to yellow, unlike the duller gold chevron."""
    a = np.asarray(logo).astype(float) / 255
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    mx, mn = a[..., :3].max(-1), a[..., :3].min(-1)
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    d = np.maximum(mx - mn, 1e-6)
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) / 6
    m = Image.fromarray((((s > 0.70) & (h < 0.13) & (al > 0.5) & (mx > 0.35)) * 255).astype(np.uint8))
    m = m.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))
    m = m.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
    return np.asarray(m) > 127


def components(b):
    """Label the 4-connected regions of a boolean array; returns the labels and each one's size."""
    H, W = b.shape
    lab = np.zeros(b.shape, int)
    sizes = []
    for y in range(H):
        for x in range(W):
            if b[y, x] and not lab[y, x]:
                n = len(sizes) + 1
                q = deque([(y, x)])
                lab[y, x] = n
                count = 0
                while q:
                    cy, cx = q.popleft()
                    count += 1
                    for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                        if 0 <= ny < H and 0 <= nx < W and b[ny, nx] and not lab[ny, nx]:
                            lab[ny, nx] = n
                            q.append((ny, nx))
                sizes.append(count)
    return lab, sizes


def letters(logo):
    """The banner with everything but the letters made transparent."""
    m = letter_mask(logo)
    lab, sizes = components(m)
    keep = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s > 3000])   # drop specks of chevron
    lab, sizes = components(~keep)
    for i, s in enumerate(sizes):
        if s < 600:                                                        # fill specks in the letters
            keep |= lab == i + 1
    out = logo.copy()
    out.putalpha(Image.fromarray((keep * 255).astype(np.uint8)))
    return out


def gradient(top, bottom):
    t = np.linspace(0, 1, N)[:, None, None]
    a = np.array(top)[None, None, :] * (1 - t) + np.array(bottom)[None, None, :] * t
    return Image.fromarray(np.broadcast_to(a, (N, N, 3)).astype(np.uint8), "RGB").convert("RGBA")


def polygon(points):
    m = Image.new("L", (N, N), 0)
    ImageDraw.Draw(m).polygon([(x * N, y * N) for x, y in points], fill=255)
    return m


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "port", "riscos", "icons", "opents-icon.png")
    logo = Image.open(sys.argv[1]).convert("RGBA")
    if logo.size != (1280, 389):
        sys.exit("expected the 1280x389 banner, got %dx%d" % logo.size)

    icon = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    # The chevron fills the square; its tip goes just past the bottom so the last row stays solid.
    icon.paste(gradient((255, 236, 150), (150, 110, 40)), (0, 0), polygon([(0.0, 0.0), (1.0, 0.0), (0.5, 1.05)]))
    icon.paste(gradient((105, 86, 50), (45, 36, 18)), (0, 0), polygon([(0.075, 0.06), (0.925, 0.06), (0.5, 0.955)]))
    icon.paste(gradient((240, 200, 110), (190, 140, 50)), (0, 0),
               polygon([(0.22, 0.63), (0.31, 0.63), (0.5, 0.81), (0.69, 0.63), (0.78, 0.63), (0.5, 0.885)]))

    ts = letters(logo).crop(TS_BOX)
    w = int(N * 0.95)
    ts = ts.resize((w, int(ts.height * w / ts.width)), Image.LANCZOS)
    alpha = ts.getchannel("A")
    shadow = Image.new("RGBA", ts.size, (0, 0, 0, 0))
    shadow.putalpha(alpha.point(lambda v: int(v * 0.7)))
    shadow = shadow.filter(ImageFilter.GaussianBlur(10))
    outline = Image.new("RGBA", ts.size, (30, 18, 6, 0))
    outline.putalpha(alpha.filter(ImageFilter.MaxFilter(17)))   # a dark edge so "TS" reads at 18x18
    x, y = (N - w) // 2, int(N * 0.135)
    icon.alpha_composite(shadow, (x + 14, y + 18))
    icon.alpha_composite(outline, (x, y))
    icon.alpha_composite(ts, (x, y))
    icon.save(out)
    print(out)


main()
