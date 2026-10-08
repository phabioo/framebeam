#!/usr/bin/env python3
"""Generates client/app/icons/player-<size>.png and player.ico from the geometry of player.svg
(docs/design/logo.md, concept 4a, tone "color"). Run manually; the outputs are checked in.
Needs Pillow. Each size is drawn at 8x supersampling and downscaled so 16/32 px stay crisp."""
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "client", "app", "icons")
PNG_SIZES = [16, 24, 32, 48, 64, 128, 256, 512, 1024]
ICO_SIZES = [16, 24, 32, 48, 64, 256]
INK, PAPER, BEAM = "#1b1b1d", "#f6f5f2", "#3cbfd8"
SS = 8


def render(size):
    n = size * SS
    k = n / 100.0
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, n - 1, n - 1], radius=21.5 * k, fill=INK)
    # Frame: rect 24,24 52x52 radius 6, stroke 8 (centered on the path): outer 20..80 r10, inner 28..72 r2
    d.rounded_rectangle([20 * k, 20 * k, 80 * k - 1, 80 * k - 1], radius=10 * k, fill=PAPER)
    d.rounded_rectangle([28 * k, 28 * k, 72 * k - 1, 72 * k - 1], radius=2 * k, fill=INK)
    d.rounded_rectangle([18 * k, 45.5 * k, 46 * k, 54.5 * k], radius=2 * k, fill=BEAM)
    d.polygon([(42 * k, 35 * k), (64 * k, 50 * k), (42 * k, 65 * k)], fill=BEAM)
    return img.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    for s in PNG_SIZES:
        render(s).save(os.path.join(OUT, f"player-{s}.png"))
    # Every ICO entry is its own supersampled rendering (append_images), not a downscale of the 256 px one.
    frames = [render(s) for s in ICO_SIZES]
    frames[-1].save(os.path.join(OUT, "player.ico"), format="ICO", sizes=[(s, s) for s in ICO_SIZES],
                    append_images=frames[:-1])


if __name__ == "__main__":
    main()
