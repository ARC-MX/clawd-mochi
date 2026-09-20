#!/usr/bin/env python3
"""Render a text string into an RGB565 C header.

The firmware's only font is `font5x7.h` — the classic 256-glyph Adafruit ASCII
set. It has no CJK coverage at all, so any Chinese on screen has to arrive as a
pre-rendered bitmap instead of as glyph data. This tool bakes one line of text
into a panel-ready RGB565 array.

Anti-aliasing matters more than the byte count here: PIL rasterises the string
at 4x and it is downsampled with LANCZOS, and because the background colour is
baked in, the smooth edge pixels survive to the panel. A 1-bit mask would be
8x smaller but would look jagged at this size.

Usage:
  text2header.py "超凡AI论坛纪念" --width 208 --bg fff6be --fg 000000 \\
                 -o main/splash_text.h --name SPLASH_TITLE

Requires: Pillow, and a TrueType/OpenType font with the needed coverage.
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

# Shared with the logo converter — see logo2header.py.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from logo2header import composite, emit_header, to_rgb565_le  # noqa: E402

DEFAULT_FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc"
SUPERSAMPLE = 4


def render_mask(text, font_path, font_index, width):
    """Rasterise `text` and return a grayscale coverage mask `width` px wide.

    Rendering oversized and downscaling is what produces the smooth edges; PIL
    only anti-aliases within the rendered size, so rasterising straight to 208 px
    would leave the stroke edges visibly stepped.
    """
    # Pick a size that lands near the target so the downscale stays mild. CJK
    # glyphs are about square, so width/size is a decent first guess.
    probe = max(8, int(width / max(1, len(text)) * SUPERSAMPLE))
    font = ImageFont.truetype(font_path, probe, index=font_index)

    canvas = Image.new("L", (probe * (len(text) + 2), probe * 3), 0)
    ImageDraw.Draw(canvas).text((probe, probe), text, fill=255, font=font)

    bbox = canvas.getbbox()
    if bbox is None:
        sys.exit("error: nothing was rendered — is the font missing these glyphs?")
    canvas = canvas.crop(bbox)

    height = max(1, round(canvas.height * width / canvas.width))
    return canvas.resize((width, height), Image.LANCZOS)


def tint(mask, fg_hex, bg_hex):
    """Blend the mask between background and foreground, keeping the AA edges."""
    def rgb(h):
        h = h.lstrip("#")
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))

    bg = Image.new("RGB", mask.size, rgb(bg_hex))
    fg = Image.new("RGB", mask.size, rgb(fg_hex))
    return Image.composite(fg, bg, mask)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("text")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--width", type=int, default=208)
    ap.add_argument("--bg", default="fff6be", help="baked background colour")
    ap.add_argument("--fg", default="000000", help="text colour")
    ap.add_argument("--font", default=DEFAULT_FONT)
    ap.add_argument("--font-index", type=int, default=0,
                    help="face index inside a .ttc collection")
    ap.add_argument("--name", default="SPLASH_TITLE", help="C symbol prefix")
    args = ap.parse_args()

    if not os.path.exists(args.font):
        sys.exit("error: font not found: %s" % args.font)

    mask = render_mask(args.text, args.font, args.font_index, args.width)
    img = tint(mask, args.fg, args.bg)
    emit_header(args.name, img.width, img.height, to_rgb565_le(img),
                args.output,
                "%r rendered at %s" % (args.text, os.path.basename(args.font)))

    print("%s -> %s" % (args.text, args.output))
    print("  %dx%d, %d bytes" % (img.width, img.height, img.width * img.height * 2))


if __name__ == "__main__":
    main()
