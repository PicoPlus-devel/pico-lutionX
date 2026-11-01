#!/usr/bin/env python3
"""
mkartwork.py - build the pico-bootLoader menu tiles of pico-lutionX.

    tools/mkartwork.py --bootloader ../pico-bootLoader/emu/assets

writes DIR/themes/0/diablo.png (640x480) and DIR/themes/1/diablo.png (320x240).
Run the bootloader's tools/png2raw.py on the two PNGs afterwards to build
their .444/.555 caches.

The pictures come from the screenshots in docs/screenshots (640x480, taken
with the shareware data): no game data is read.

    diablo-zoom-off.png      theme 0: the badge, the Dark Lord himself
    town-zoom-off.png        theme 1: left panel
    cathedral-zoom-off.png   theme 1: middle panel
    diablo-zoom-off.png      theme 1: right panel

The tiles follow the two themes of pico-bootLoader, with the geometry of its
tools/make_category_art.py, as pico-1943's tools/mkartwork.py does:

    theme 0   640x480: flat dark-red ground, a huge ghosted pentagram, a boxed
              badge on the left, the wordmark and a rule
    theme 1   320x240: three slanted panels of the game, white wordmark across
              the middle

Requires numpy and ffmpeg (with librsvg). Derived from pico-1943's
tools/mkartwork.py.
"""

import argparse
import base64
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SCREENSHOTS = os.path.join(HERE, "..", "docs", "screenshots")
SHOT_W, SHOT_H = 640, 480
VIEW_H = 352  # the rows above the control panel


def png_data_uri(name):
    path = os.path.join(SCREENSHOTS, name)
    if not os.path.exists(path):
        sys.exit("%s not found" % path)
    with open(path, "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode("ascii")


def cropped(name, x, y, w, h, out_w, out_h, ox=0, oy=0):
    """The part (x, y, w, h) of a screenshot, scaled to out_w x out_h at (ox, oy)."""
    if x < 0 or y < 0 or x + w > SHOT_W or y + h > VIEW_H:
        sys.exit("%s: crop %r leaves the game view" % (name, (x, y, w, h)))
    return (f'<svg x="{ox}" y="{oy}" width="{out_w}" height="{out_h}" viewBox="{x} {y} {w} {h}" '
            f'preserveAspectRatio="xMidYMid slice"><image xlink:href="{png_data_uri(name)}" '
            f'width="{SHOT_W}" height="{SHOT_H}"/></svg>')


def rasterise(svg, out_png):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "tile.svg")
        with open(path, "w") as f:
            f.write(svg)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", path, "-frames:v", "1", "-update", "1",
                        "-pix_fmt", "rgb24", "-compression_level", "9", out_png], check=True)


def decode_png(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                         check=True, capture_output=True).stdout
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                            "stream=width,height", "-of", "csv=p=0", path],
                           check=True, capture_output=True, text=True).stdout.strip().split(",")
    w, h = int(probe[0]), int(probe[1])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3)


WORDMARK = "DIABLO"

# ---------------------------------------------------------------------------
# Theme 0, geometry from pico-bootLoader's tools/make_category_art.py
# ---------------------------------------------------------------------------
T0_W, T0_H = 640, 480
T0_GROUND, T0_TINT = "#6b1212", "#842626"
BADGE_X, BADGE_Y, BADGE_W, BADGE_H = 2, 186, 90, 108
BADGE_FRAME = 8
RULE_Y, RULE_TH = 229, 2
DASH_X0, DASH_X1 = 94, 108
TEXT_X, TEXT_BASELINE, TEXT_SIZE = 114, 246, 45

# The ghosted silhouette: a pentagram in a ring, as on the title screen, in a
# 100x100 box.
def pentagram():
    pts = []
    for i in range(5):
        a = math.radians(-90 + i * 144)
        pts.append("%.2f,%.2f" % (50 + 40 * math.cos(a), 50 + 40 * math.sin(a)))
    return ('<circle cx="50" cy="50" r="45" fill="none" stroke-width="5"/>'
            f'<polygon points="{" ".join(pts)}" fill="none" stroke-width="4.5" stroke-linejoin="round"/>')


WATERMARK = (515, 345, 240, 0)  # centre x, centre y, box size, rotation

# The badge: Diablo in his lair, cropped to the aspect of the badge's inside,
# 74x92.
BADGE_SHOT = "diablo-zoom-off.png"
BADGE_CROP = (228, 58, 124, 154)
BADGE_GROUND = "#000000"


def theme0_svg(rule_x0):
    cx, cy, size, rot = WATERMARK
    s = size / 100.0
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{T0_W}" height="{T0_H}">',
        f'<rect width="{T0_W}" height="{T0_H}" fill="{T0_GROUND}"/>',
        f'<g transform="rotate({rot} {cx} {cy}) translate({cx - size / 2},{cy - size / 2}) scale({s:.5f})">'
        f'<g stroke="{T0_TINT}">{pentagram()}</g></g>',
    ]
    if rule_x0 is None:
        # measuring pass: the wordmark alone
        parts.append(
            f'<rect width="{T0_W}" height="{T0_H}" fill="#000"/>'
            f'<text x="{TEXT_X}" y="{TEXT_BASELINE}" font-family="Liberation Sans" font-weight="bold" '
            f'font-size="{TEXT_SIZE}" letter-spacing="2" fill="#ffffff">{WORDMARK}</text></svg>')
        return "".join(parts)

    ix, iy = BADGE_X + BADGE_FRAME, BADGE_Y + BADGE_FRAME
    iw, ih = BADGE_W - 2 * BADGE_FRAME, BADGE_H - 2 * BADGE_FRAME
    parts.append(
        f'<rect x="{BADGE_X}" y="{BADGE_Y}" width="{BADGE_W}" height="{BADGE_H}" fill="#ffffff"/>'
        f'<rect x="{ix}" y="{iy}" width="{iw}" height="{ih}" fill="{BADGE_GROUND}"/>'
        + cropped(BADGE_SHOT, *BADGE_CROP, iw, ih, ix, iy))
    parts.append(
        f'<rect x="{DASH_X0}" y="{RULE_Y}" width="{DASH_X1 - DASH_X0}" height="{RULE_TH}" fill="#ffffff"/>'
        f'<text x="{TEXT_X}" y="{TEXT_BASELINE}" font-family="Liberation Sans" font-weight="bold" '
        f'font-size="{TEXT_SIZE}" letter-spacing="2" fill="#ffffff">{WORDMARK}</text>'
        f'<rect x="{rule_x0}" y="{RULE_Y}" width="{637 - rule_x0}" height="{RULE_TH}" fill="#ffffff"/>')
    parts.append("</svg>")
    return "".join(parts)


def theme0_tile(out_png):
    # measure the wordmark's ink, then start the rule 12px after it
    with tempfile.TemporaryDirectory() as d:
        probe = os.path.join(d, "m.png")
        rasterise(theme0_svg(None), probe)
        ink = (decode_png(probe) > 128).any(axis=2)
        cols = np.where(ink.any(axis=0))[0]
        rule_x0 = int(cols.max()) + 12
    rasterise(theme0_svg(rule_x0), out_png)


# ---------------------------------------------------------------------------
# Theme 1: panels 8.5 degrees off vertical, 2px black seams
# ---------------------------------------------------------------------------
T1_W, T1_H = 320, 240
PANEL_CLIP = {
    "L": "0,0 120,0 84,240 0,240",
    "M": "120,0 236,0 200,240 84,240",
    "R": "236,0 320,0 320,240 200,240",
}
# Each panel shows a 400x300 part of a screenshot, centred on what it shows
# (Griswold at his forge, the Fallen on the stairs, Diablo), scaled to
# 320x240 and moved so that point lies on the panel's centre line.
PANELS = {
    "L": ("town-zoom-off.png", 335, 150, -109),
    "M": ("cathedral-zoom-off.png", 330, 165, 0),
    "R": ("diablo-zoom-off.png", 290, 150, 109),
}
PANEL_CROP_W, PANEL_CROP_H = 400, 300
T1_TEXT_Y, T1_TEXT_SIZE, T1_STROKE = 114, 33, "#200808"


def theme1_tile(out_png):
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{T1_W}" height="{T1_H}"><defs>',
    ]
    for slot, pts in PANEL_CLIP.items():
        parts.append(f'<clipPath id="p{slot}"><polygon points="{pts}"/></clipPath>')
    parts.append(f'</defs><rect width="{T1_W}" height="{T1_H}" fill="#000"/>')
    for slot, (name, cx, cy, dx) in PANELS.items():
        crop = (cx - PANEL_CROP_W // 2, cy - PANEL_CROP_H // 2, PANEL_CROP_W, PANEL_CROP_H)
        parts.append(f'<g clip-path="url(#p{slot})">{cropped(name, *crop, T1_W, T1_H, dx, 0)}</g>')
    parts.append('<g stroke="#000" stroke-width="2" fill="none"><path d="M120 0L84 240M236 0L200 240"/></g>')
    parts.append(
        f'<text x="{T1_W // 2}" y="{T1_TEXT_Y}" text-anchor="middle" font-family="Liberation Sans Narrow" '
        f'font-weight="bold" font-style="italic" font-size="{T1_TEXT_SIZE}" letter-spacing="1" '
        f'fill="#ffffff" stroke="{T1_STROKE}" stroke-width="3" paint-order="stroke fill">{WORDMARK}</text>')
    parts.append("</svg>")
    rasterise("".join(parts), out_png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bootloader", metavar="DIR", required=True, help="pico-bootLoader emu/assets directory")
    args = ap.parse_args()

    for theme, make in (("0", theme0_tile), ("1", theme1_tile)):
        path = os.path.join(args.bootloader, "themes", theme, "diablo.png")
        make(path)
        print("wrote", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
