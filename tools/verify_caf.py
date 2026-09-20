#!/usr/bin/env python3
"""Verify that a .caf file replays to exactly the frames it was built from.

Decodes the container the same way anim.cpp does — palette-index RLE over the
artwork bounding box — and compares every frame against the converter's own
output. A mismatch means the device would show a corrupted frame.

Usage:
  verify_caf.py THEME_DIR [GIF_DIR]
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gif2caf

HEADER_LEN = 14        # magic(4) + x0,y0,w,h,frames (5 x u16)
PALETTE_LEN = 512
TABLE_OFF = HEADER_LEN + PALETTE_LEN


def decode(path, w, h):
    """Replay the container and return the list of reconstructed frames."""
    d = open(path, "rb").read()
    if d[:4] != gif2caf.MAGIC:
        raise SystemExit(f"{path}: bad magic {d[:4]!r}")

    x0, y0, fw, fh, frames = struct.unpack("<HHHHH", d[4:14])
    if (fw, fh) != (w, h):
        raise SystemExit(f"{path}: stored {fw}x{fh}, expected {w}x{h}")

    total = w * h
    out = []
    for f in range(frames):
        off = struct.unpack("<I", d[TABLE_OFF + f * 4:TABLE_OFF + f * 4 + 4])[0]
        buf = bytearray()
        i = off
        while len(buf) < total:
            run, val = d[i], d[i + 1]
            i += 2
            buf += bytes([val]) * run
        if len(buf) != total:
            raise SystemExit(f"{path}: frame {f} overran ({len(buf)} != {total})")
        out.append(bytes(buf))
    return out, (x0, y0, fw, fh)


def main():
    theme = sys.argv[1] if len(sys.argv) > 1 else "esp-idf/data/theme"
    gifs = sys.argv[2] if len(sys.argv) > 2 else "../stickers/240/clawd"

    manifest = {}
    with open(os.path.join(theme, "manifest.txt")) as fh:
        for line in fh:
            line = line.split("#")[0].strip()
            if "=" in line:
                k, v = line.split("=", 1)
                manifest[k.strip()] = v.strip()

    bad = 0
    for state, caf in sorted(manifest.items()):
        caf_path = os.path.join(theme, caf)
        gif_path = os.path.join(gifs, os.path.splitext(caf)[0] + ".gif")
        if not os.path.exists(gif_path):
            print(f"{state:14} SKIP (no source gif {gif_path})")
            continue

        # Reproduce exactly what convert() does.
        idx, _delays, size, palette = gif2caf.load_frames(gif_path)
        cw, ch = size
        idx, size, rect = gif2caf.compose(idx, size, palette, 12)
        x0, y0, w, h = rect
        h2 = min(ch, ((h + gif2caf.BAND_ROWS - 1) // gif2caf.BAND_ROWS)
                 * gif2caf.BAND_ROWS)
        y0 = max(0, min(y0, ch - h2))
        rect = (x0, y0, w, h2)
        idx = gif2caf.crop_frames(idx, rect, cw)

        got, stored = decode(caf_path, w, h2)
        if stored != rect:
            print(f"{state:14} FAIL rect {stored} != {rect}")
            bad += 1
            continue
        if got == idx:
            print(f"{state:14} OK   {len(idx):3} frames, bbox "
                  f"{w}x{h2} at ({x0},{y0})")
        else:
            where = next(i for i, (a, b) in enumerate(zip(got, idx)) if a != b)
            print(f"{state:14} FAIL at frame {where}")
            bad += 1

    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
