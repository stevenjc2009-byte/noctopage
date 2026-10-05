#!/usr/bin/env python3
"""Draws Noctopage's LiveArea art in code (re-runnable, no source images).

  sce_sys/icon0.png                        128x128
  sce_sys/livearea/contents/bg.png         840x500
  sce_sys/livearea/contents/startup.png    280x158

Look: pure black ground, a white open book, a thin crescent moon, the word
"Noctopage" (bg/startup) in white, accent royal blue (65,105,226).
LiveArea / the package installer reject truecolour PNGs for these three files,
so everything is written as 8-bit indexed (colour type 3) PNG, and the script
verifies that by reading each file's IHDR back.

Needs Pillow. Usage: python tools/make_art.py
"""
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLUE = (65, 105, 226)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
GREY = (96, 96, 96)
SS = 4  # supersampling factor for smooth edges

FONT_CANDIDATES = [
    "C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]


def load_font(px):
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return ImageFont.truetype(path, px)
    return ImageFont.load_default(px)


def pts(cx, cy, s, coords):
    return [(cx + x * s, cy + y * s) for x, y in coords]


def draw_mark(d, cx, cy, s):
    """Open book + crescent moon, centred on (cx, cy), 's' = half-width in px."""
    # Cover edge under the pages, in accent blue.
    left_cover = [(-1.0, -0.22), (-0.04, 0.0), (-0.04, 0.68), (-1.0, 0.46)]
    right_cover = [(-x, y) for x, y in left_cover]
    d.polygon(pts(cx, cy, s, left_cover), fill=BLUE)
    d.polygon(pts(cx, cy, s, right_cover), fill=BLUE)
    # Pages.
    left_page = [(-0.94, -0.30), (-0.05, -0.08), (-0.05, 0.58), (-0.94, 0.36)]
    right_page = [(-x, y) for x, y in left_page]
    d.polygon(pts(cx, cy, s, left_page), fill=WHITE)
    d.polygon(pts(cx, cy, s, right_page), fill=WHITE)
    # Faint text lines on the pages.
    for i in range(3):
        y0 = -0.10 + i * 0.17
        w = max(1, int(s * 0.03))
        d.line(pts(cx, cy, s, [(-0.80, y0 - 0.02), (-0.16, y0 + 0.13)]), fill=GREY, width=w)
        d.line(pts(cx, cy, s, [(0.16, y0 + 0.13), (0.80, y0 - 0.02)]), fill=GREY, width=w)
    # Thin crescent moon above the book: white disc, black disc biting it.
    mx, my, mr = 0.45, -0.78, 0.30
    d.ellipse([cx + (mx - mr) * s, cy + (my - mr) * s, cx + (mx + mr) * s, cy + (my + mr) * s], fill=WHITE)
    br = mr * 0.88
    bx, by = mx + 0.13, my - 0.07
    d.ellipse([cx + (bx - br) * s, cy + (by - br) * s, cx + (bx + br) * s, cy + (by + br) * s], fill=BLACK)


def finish(img, path):
    """Downsample, quantise to 8-bit indexed, save, verify the IHDR."""
    out = img.resize((img.width // SS, img.height // SS), Image.LANCZOS)
    pal = out.quantize(colors=32, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pal.save(path, format="PNG", bits=8)
    with open(path, "rb") as f:
        head = f.read(26)
    w, h, depth, ctype = struct.unpack(">IIBB", head[16:26])
    assert (depth, ctype) == (8, 3), f"{path}: not 8-bit indexed (depth {depth}, type {ctype})"
    print(f"wrote {os.path.relpath(path, ROOT)}  {w}x{h}  8-bit indexed")


def canvas(w, h):
    img = Image.new("RGB", (w * SS, h * SS), BLACK)
    return img, ImageDraw.Draw(img)


def text_centered(d, cx, cy, text, px, fill):
    font = load_font(px * SS)
    d.text((cx * SS, cy * SS), text, font=font, fill=fill, anchor="mm")


def make_icon():
    img, d = canvas(128, 128)
    draw_mark(d, 64 * SS, 70 * SS, 48 * SS)
    finish(img, os.path.join(ROOT, "sce_sys", "icon0.png"))


def make_bg():
    img, d = canvas(840, 500)
    draw_mark(d, 250 * SS, 235 * SS, 150 * SS)
    text_centered(d, 640, 225, "Noctopage", 64, WHITE)
    d.rectangle([520 * SS, 275 * SS, 760 * SS, 278 * SS], fill=BLUE)
    text_centered(d, 640, 305, "manga & e-book reader", 22, GREY)
    finish(img, os.path.join(ROOT, "sce_sys", "livearea", "contents", "bg.png"))


def make_startup():
    img, d = canvas(280, 158)
    draw_mark(d, 56 * SS, 84 * SS, 40 * SS)
    text_centered(d, 195, 76, "Noctopage", 28, WHITE)
    d.rectangle([138 * SS, 98 * SS, 252 * SS, 100 * SS], fill=BLUE)
    finish(img, os.path.join(ROOT, "sce_sys", "livearea", "contents", "startup.png"))


if __name__ == "__main__":
    try:
        make_icon()
        make_bg()
        make_startup()
    except AssertionError as e:
        sys.exit(str(e))
