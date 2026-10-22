#!/usr/bin/env python3
"""Generates the Uzume app icon and the MSI bitmaps. Run manually with `python3 -I scripts/make-icons.py`; the outputs
are checked in. Needs Pillow and fontTools (with brotli, to read the WOFF2 font of the design system).

Geometry: the components Mark, AppIcon and ServerGlyph in docs/design/source/uzume/ds/uzume/components/bundle.js;
spec docs/design/uzume/frame.md (0-4 A) and docs/design/uzume/runs-and-server.md (8l-0, board S8l0).

Outputs:
  client/app/icons/uzume.svg            AppIcon, tone sumi, vector Mark (source of truth for the big sizes)
  client/app/icons/uzume-<size>.png     16..1024; 16, 24 and 32 are pixel tiles (no anti-aliasing, stepped corners),
                                        48 uses the same grid-snapped mark smoothed, 64 and up render the vector geometry
  client/app/icons/uzume.ico            16, 24, 32, 48, 64, 128, 256 (each its own rendering)
  packaging/windows/bitmaps/dialog.bmp  WixUIDialogBmp 493 x 312, 24-bit
  packaging/windows/bitmaps/banner.bmp  WixUIBannerBmp 493 x 58, 24-bit
"""
import io
import math
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ICONS = os.path.join(ROOT, "client", "app", "icons")
BITMAPS = os.path.join(ROOT, "packaging", "windows", "bitmaps")
FONT = os.path.join(ROOT, "docs", "design", "source", "uzume", "ds", "uzume", "fonts", "IBMPlexMono-Regular.woff2")
PNG_SIZES = [16, 24, 32, 48, 64, 128, 256, 512, 1024]
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]

# Tokens (docs/design/uzume/design-system.md, ds/uzume/tokens.css).
SUMI = (0x1C, 0x1B, 0x19)            # ground (sumi), ink (kinari), on-accent
KINARI = (0xFB, 0xFA, 0xF5)          # ground (kinari)
KINARI_SURFACE = (0xFF, 0xFF, 0xFF)  # surface (kinari): the dialog text area and the banner (board S8l0)
SUMI_INK = (0xEF, 0xEC, 0xE4)        # ink (sumi)
SUMI_DIM = (0x8A, 0x86, 0x7C)        # dim (sumi): register marks
KINARI_EDGE = (0xE2, 0xDF, 0xD5)     # AppIcon kinari tile edge (bundle.js)
YAMABUKI = (0xF8, 0xB5, 0x00)        # accent
TILE_RADIUS = 0.225                  # radius-app-icon
MARK_SCALE = 0.66                    # AppIcon: mark at 66 % of the tile
REGISTER_MARK = 12                   # --register-mark

# Mark (viewBox 96): M14 42 L30 38 L30 56 A18 18 0 0 0 66 56 L66 16 L82 10 L82 56 A34 34 0 0 1 14 56 Z,
# light point circle (48, 30) r 9.
MARK_PATH = "M14 42 L30 38 L30 56 A18 18 0 0 0 66 56 L66 16 L82 10 L82 56 A34 34 0 0 1 14 56 Z"


def mark_polygon(steps=96):
    pts = [(14, 42), (30, 38), (30, 56)]
    pts += [(48 + 18 * math.cos(t), 56 + 18 * math.sin(t)) for t in (math.pi * (1 - i / steps) for i in range(1, steps))]
    pts += [(66, 56), (66, 16), (82, 10), (82, 56)]
    pts += [(48 + 34 * math.cos(t), 56 + 34 * math.sin(t)) for t in (math.pi * i / steps for i in range(1, steps))]
    pts += [(14, 56)]
    return pts


MARK_POLY = mark_polygon()


# ---------- vector drawing on a supersampled canvas (k = canvas pixels per output pixel) ----------

def rrect(d, x, y, w, h, r, fill, k):
    d.rounded_rectangle([x * k, y * k, (x + w) * k - 1, (y + h) * k - 1], radius=max(r * k, 0), fill=fill)


def draw_mark(d, x, y, size, ink, light, k):
    s = size / 96.0
    d.polygon([((x + px * s) * k, (y + py * s) * k) for px, py in MARK_POLY], fill=ink)
    cx, cy, r = (x + 48 * s) * k, (y + 30 * s) * k, 9 * s * k
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=light)


def draw_glyph(d, x, y, b, box, line, k, ring=0.0, ring_fill=None):
    """glyphParts of bundle.js: box with two stacked units (stroked), the top one lit by a Yamabuki square."""
    bR, uH, uB, uR, gap, pad, dS, dP = b * .24, b * .22, b * .06, b * .05, b * .09, b * .2, b * .09, b * .05
    top, uw = y + (b - (2 * uH + gap)) / 2, b - 2 * pad
    if ring:
        rrect(d, x - ring, y - ring, b + 2 * ring, b + 2 * ring, bR + ring, ring_fill, k)
    rrect(d, x, y, b, b, bR, box, k)
    for i in range(2):
        uy = top + i * (uH + gap)
        rrect(d, x + pad, uy, uw, uH, uR, line, k)  # outer edge of the stroke
        rrect(d, x + pad + uB, uy + uB, uw - 2 * uB, uH - 2 * uB, max(uR - uB, 0), box, k)
    rrect(d, x + pad + uw - uB - dP - dS, top + (uH - dS) / 2, dS, dS, 0, YAMABUKI, k)


def draw_app_icon(d, x, y, s, tone, k, server=False):
    bg, fg = (KINARI, SUMI) if tone == "kinari" else (SUMI, SUMI_INK)
    if tone == "kinari":
        bw = s / 220.0
        rrect(d, x, y, s, s, s * TILE_RADIUS, KINARI_EDGE, k)
        rrect(d, x + bw, y + bw, s - 2 * bw, s - 2 * bw, s * TILE_RADIUS - bw, bg, k)
    else:
        rrect(d, x, y, s, s, s * TILE_RADIUS, bg, k)
    ms = s * MARK_SCALE
    draw_mark(d, x + (s - ms) / 2, y + (s - ms) / 2, ms, fg, YAMABUKI, k)
    if server:
        box, line = (SUMI, SUMI_INK) if tone == "kinari" else (SUMI_INK, SUMI)
        draw_glyph(d, x + s * .6, y + s * .6, s * .4, box, line, k, ring=s * .03, ring_fill=bg)


def supersampled(w, h, bg, paint, ss):
    img = Image.new("RGBA", (w * ss, h * ss), bg)
    paint(ImageDraw.Draw(img), ss)
    return img.reduce(ss)


def render_vector_icon(size):
    ss = max(2, min(16, 4096 // size))
    return supersampled(size, size, (0, 0, 0, 0), lambda d, k: draw_app_icon(d, 0, 0, size, "sumi", k), ss)


# ---------- pixel-fitted tiles below 64 px ----------
# The mark is re-fitted to the pixel grid: arm edges, arc centre and radii on whole pixels, the slanted arm tops keep the
# vector slopes (left 0.25, right 0.375). ss = 1 samples each pixel centre once (hard pixels, stepped corners);
# larger values average subsamples of the same snapped geometry.
FIT = {
    #     arms L / R (x0, x1)  cx  cy  r_in r_out  left top (y at L x0)  right top (y at R x0)  light (x, y, r)  ss
    16: dict(L=(4, 6), R=(10, 12), cx=8, cy=9, rin=2, rout=4, tl=7.0, tr=4.0, light=(8, 6, 1.0), ss=1),
    24: dict(L=(6, 9), R=(15, 18), cx=12, cy=13, rin=3, rout=6, tl=11.0, tr=6.7, light=(12, 9, 1.6), ss=1),
    32: dict(L=(8, 12), R=(20, 24), cx=16, cy=18, rin=4, rout=8, tl=14.9, tr=9.2, light=(16, 12, 2.0), ss=1),
    48: dict(L=(13, 18), R=(30, 35), cx=24, cy=27, rin=6, rout=11, tl=22.0, tr=13.4, light=(24, 18, 3.0), ss=8),
}


def fitted_class(n, f, x, y):
    """0 outside, 1 tile, 2 mark ink, 3 light point, for the sample point (x, y) in pixel units."""
    r = n * TILE_RADIUS
    cx_, cy_ = min(max(x, r), n - r), min(max(y, r), n - r)
    if not (0 <= x < n and 0 <= y < n) or (x - cx_) ** 2 + (y - cy_) ** 2 > r * r:
        return 0
    lx, ly, lr = f["light"]
    if (x - lx) ** 2 + (y - ly) ** 2 < lr * lr:
        return 3
    if y < f["cy"]:
        (l0, l1), (r0, r1) = f["L"], f["R"]
        if l0 <= x < l1 and y >= f["tl"] - .25 * (x - l0):
            return 2
        if r0 <= x < r1 and y >= f["tr"] - .375 * (x - r0):
            return 2
        return 1
    dist = math.hypot(x - f["cx"], y - f["cy"])
    return 2 if f["rin"] < dist < f["rout"] else 1


def render_fitted_icon(n):
    f = FIT[n]
    ss, colors = f["ss"], {1: SUMI, 2: SUMI_INK, 3: YAMABUKI}
    img = Image.new("RGBA", (n, n))
    px = img.load()
    for py in range(n):
        for pxx in range(n):
            acc, inside = [0, 0, 0], 0
            for sy in range(ss):
                for sx in range(ss):
                    c = fitted_class(n, f, pxx + (sx + .5) / ss, py + (sy + .5) / ss)
                    if c:
                        inside += 1
                        for i in range(3):
                            acc[i] += colors[c][i]
            px[pxx, py] = (0, 0, 0, 0) if not inside else \
                tuple(round(a / inside) for a in acc) + (round(255 * inside / (ss * ss)),)
    return img


def render_icon(size):
    return render_fitted_icon(size) if size in FIT else render_vector_icon(size)


def write_svg(path):
    ms = 96 * MARK_SCALE
    off = (96 - ms) / 2
    hexc = lambda c: "#%02x%02x%02x" % c
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 96 96" width="96" height="96">
  <!-- Uzume app icon: AppIcon tile (radius 22.5 %, tone sumi) with Mark 03a "Rise" (docs/design/uzume/frame.md 0-4 A).
       Generated by scripts/make-icons.py together with the PNG and ICO files; edit the script, not this file. -->
  <rect width="96" height="96" rx="{96 * TILE_RADIUS:g}" fill="{hexc(SUMI)}"/>
  <g transform="translate({off:g} {off:g}) scale({MARK_SCALE:g})">
    <path d="{MARK_PATH}" fill="{hexc(SUMI_INK)}"/>
    <circle cx="48" cy="30" r="9" fill="{hexc(YAMABUKI)}"/>
  </g>
</svg>
"""
    with open(path, "w", newline="\n") as fh:
        fh.write(svg)


# ---------- MSI bitmaps (board S8l0) ----------

def mono_font(size):
    from fontTools.ttLib import TTFont
    font = TTFont(FONT)
    font.flavor = None
    buf = io.BytesIO()
    font.save(buf)
    buf.seek(0)
    return ImageFont.truetype(buf, size)


def draw_spaced(d, x, baseline, text, font, fill, spacing):
    for ch in text:
        d.text((x, baseline), ch, font=font, fill=fill, anchor="ls")
        x += font.getlength(ch) + spacing


def register_marks(d, x0, y0, x1, y1, inset, size, fill):
    """RegisterMarks: four 1 px corner marks (size x size) at inset from the corners of the box [x0, x1) x [y0, y1)."""
    for cx, sx in ((x0 + inset, 1), (x1 - inset - 1, -1)):
        for cy, sy in ((y0 + inset, 1), (y1 - inset - 1, -1)):
            d.line([(cx, cy), (cx + sx * (size - 1), cy)], fill=fill)
            d.line([(cx, cy), (cx, cy + sy * (size - 1))], fill=fill)


def render_dialog():
    w, h, strip = 493, 312, 164

    def paint(d, k):
        d.rectangle([0, 0, w * k - 1, h * k - 1], fill=KINARI_SURFACE)
        d.rectangle([0, 0, strip * k - 1, h * k - 1], fill=SUMI)
        draw_app_icon(d, 24, 28, 96, "kinari", k, server=True)  # strip padding 28 / 24, AppIcon 96 kinari + server

    img = supersampled(w, h, KINARI_SURFACE, paint, 8).convert("RGB")
    d = ImageDraw.Draw(img)
    register_marks(d, 0, 0, strip, h, 12, REGISTER_MARK, SUMI_DIM)
    # Foot label (Mono 10/16, letter-spacing .08em, dim), bottom padding 20. The version line of the board is left out:
    # a bitmap would carry a stale version, Windows shows the real one.
    font = mono_font(10)
    asc, desc = font.getmetrics()
    draw_spaced(d, 24, h - 20 - 16 + (16 - asc - desc) / 2 + asc, "UZUME · SETUP", font, SUMI_DIM, 0.8)
    return img


def render_banner():
    w, h = 493, 58

    def paint(d, k):
        d.rectangle([0, 0, w * k - 1, h * k - 1], fill=KINARI_SURFACE)
        gx = w - 14 - 20                         # ServerGlyph 20, 14 px from the right edge
        draw_mark(d, gx - 10 - 36, (h - 36) / 2, 36, SUMI, YAMABUKI, k)  # Mark 36, gap 10, centred
        draw_glyph(d, gx, h - 10 - 20, 20, SUMI, KINARI, k)              # bottom-aligned, 10 px from the bottom

    return supersampled(w, h, KINARI_SURFACE, paint, 8).convert("RGB")


def main():
    os.makedirs(ICONS, exist_ok=True)
    os.makedirs(BITMAPS, exist_ok=True)
    write_svg(os.path.join(ICONS, "uzume.svg"))
    rendered = {s: render_icon(s) for s in sorted(set(PNG_SIZES) | set(ICO_SIZES))}
    for s in PNG_SIZES:
        rendered[s].save(os.path.join(ICONS, f"uzume-{s}.png"), optimize=True)
    # Every ICO entry is its own rendering (append_images), not a downscale of the 256 px one.
    frames = [rendered[s] for s in ICO_SIZES]
    frames[-1].save(os.path.join(ICONS, "uzume.ico"), format="ICO", sizes=[(s, s) for s in ICO_SIZES],
                    append_images=frames[:-1])
    render_dialog().save(os.path.join(BITMAPS, "dialog.bmp"))
    render_banner().save(os.path.join(BITMAPS, "banner.bmp"))


if __name__ == "__main__":
    main()
