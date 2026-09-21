#!/usr/bin/env python3
"""mochi-theme — build, check and preview Clawd Mochi animation packs.

One entry point for the whole sticker pipeline, which used to be three scripts
you had to know to run in the right order:

  convert   a GIF (or a directory of them) into the device's .caf format
  pack      a sticker set into a theme directory the device can hold
  verify    that a .caf replays to exactly the frames it was built from
  preview   render what the panel will actually show

preview exists because the rest of this pipeline can be checked by numbers —
sizes, frame counts, fit against the partition — but "is the art legible, is it
magnified sensibly, is the background right" cannot. That was the gap: the
firmware's rendering rules had to be reproduced by hand, in another language,
every time the question came up.

It composites the way anim.cpp does, so the image is what the panel shows:

  * palette index 0 is transparent, and the background shows through
  * the manifest's scale= magnifies the stored art on push, from (x0, y0)
  * the frame is pushed at its stored position, not scaled to fit

Usage:
  mochi_theme.py pack ../stickers/128/calico -o /tmp/calico --scale 2 --no-upscale
  mochi_theme.py preview /tmp/calico -o /tmp/calico.png
  mochi_theme.py preview /tmp/calico -o /tmp/calico.png --bg '#e8f2ee' --cols 3
"""

import argparse
import os
import struct
import subprocess
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
GIF2CAF = os.path.join(HERE, "gif2caf.py")
MKTHEME = os.path.join(HERE, "mktheme.py")
VERIFY = os.path.join(HERE, "verify_caf.py")

PANEL = 240


# ── .caf reading ──────────────────────────────────────────────
# Mirrors anim.cpp. Kept deliberately literal: if the firmware's decode changes,
# this must change with it, and being a second implementation is the point — it
# is how a mismatch gets noticed.

CAF_MAGIC = b"CAF3"
CAF_HEADER_LEN = 14
CAF_PALETTE_LEN = 512


def read_caf(path):
    """-> (x0, y0, w, h, palette, frame_offset_table, raw_bytes)"""
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) < CAF_HEADER_LEN + CAF_PALETTE_LEN or data[:4] != CAF_MAGIC:
        raise ValueError(f"{path}: not a CAF3 file")
    x0, y0, w, h, frames = struct.unpack("<HHHHH", data[4:14])
    pal_off = CAF_HEADER_LEN
    palette = struct.unpack("<256H", data[pal_off:pal_off + CAF_PALETTE_LEN])
    table = pal_off + CAF_PALETTE_LEN
    offsets = struct.unpack(f"<{frames}I", data[table:table + 4 * frames])
    return x0, y0, w, h, palette, offsets, data


def decode_frame(data, offset, w, h, palette, bg565):
    """One frame as RGB, with palette index 0 resolved to the background.

    The device skips index-0 runs and leaves the cleared surround alone, so the
    field is whatever the background is — not the colour the file happens to
    store there.
    """
    raw = bytearray()
    i = offset
    need = w * h * 3          # bytes, not pixels — runs are expanded to RGB
    while len(raw) < need:
        run, idx = data[i], data[i + 1]
        i += 2
        if run == 0:
            raise ValueError("zero-length run")
        v = palette[idx]
        if idx == 0:
            v = bg565
        r = ((v >> 11) & 0x1F) * 255 // 31
        g = ((v >> 5) & 0x3F) * 255 // 63
        b = (v & 0x1F) * 255 // 31
        raw.extend(bytes((r, g, b)) * run)
    im = Image.new("RGB", (w, h))
    im.frombytes(bytes(raw[:need]))
    return im


def hex565(s):
    s = s.lstrip("#")
    return ((int(s[0:2], 16) & 0xF8) << 8) | ((int(s[2:4], 16) & 0xFC) << 3) | \
           (int(s[4:6], 16) >> 3)


def read_manifest(theme_dir):
    """-> (states in file order, scale)"""
    states, scale = [], 1
    path = os.path.join(theme_dir, "manifest.txt")
    if not os.path.exists(path):
        return states, scale
    with open(path) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line or "=" not in line:
                continue
            k, v = (p.strip() for p in line.split("=", 1))
            if k == "scale":
                scale = max(1, int(v))
            elif v:
                states.append((k, v))
    return states, scale


# ── preview ───────────────────────────────────────────────────

def preview(theme_dir, out_path, bg="#e8f2ee", cols=3, cell=200):
    states, scale = read_manifest(theme_dir)
    if not states:
        sys.exit(f"error: no states in {theme_dir}/manifest.txt")

    bg_rgb = tuple(int(bg.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))
    bg565 = hex565(bg)

    rows = (len(states) + cols - 1) // cols
    pad, label_h = 8, 18
    sheet = Image.new("RGB",
                      (cols * (cell + pad) + pad,
                       rows * (cell + label_h + pad) + pad),
                      (24, 24, 28))
    draw = ImageDraw.Draw(sheet)

    for n, (state, caf) in enumerate(states):
        path = os.path.join(theme_dir, caf)
        if not os.path.exists(path):
            print(f"  ! {state}: {caf} is missing", file=sys.stderr)
            continue
        x0, y0, w, h, palette, offsets, data = read_caf(path)

        # Middle frame: the first is often a rest pose and says little about
        # whether the animation reads.
        im = decode_frame(data, offsets[len(offsets) // 2], w, h, palette, bg565)

        out_w, out_h = w * scale, h * scale
        if out_w > PANEL or h % (8 // scale if scale <= 8 else 1):
            print(f"  ! {state}: {w}x{h} at scale {scale} would be rejected "
                  f"by the firmware", file=sys.stderr)
        screen = Image.new("RGB", (PANEL, PANEL), bg_rgb)
        # Nearest neighbour, matching the firmware's integer magnification.
        big = im.resize((out_w, out_h), Image.NEAREST)
        screen.paste(big, (x0, y0))
        screen = screen.resize((cell, cell), Image.LANCZOS)

        cx = pad + (n % cols) * (cell + pad)
        cy = pad + (n // cols) * (cell + label_h + pad)
        sheet.paste(screen, (cx, cy))
        draw.text((cx + 2, cy + cell + 3),
                  f"{state}  {w}x{h}@{scale}x -> {out_w}x{out_h}", fill=(200, 200, 205))

    sheet.save(out_path)
    print(f"{len(states)} state(s); background {bg}; scale {scale}")
    print(f"wrote {out_path}")


# ── dispatch ──────────────────────────────────────────────────

def run(script, argv):
    """Delegate to the existing scripts, which stay usable on their own."""
    return subprocess.call([sys.executable, script] + argv)


def main():
    ap = argparse.ArgumentParser(
        prog="mochi-theme", description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("convert", help="GIF -> .caf")
    p.add_argument("rest", nargs=argparse.REMAINDER)

    p = sub.add_parser("pack", help="sticker set -> theme directory")
    p.add_argument("rest", nargs=argparse.REMAINDER)

    p = sub.add_parser("verify", help="check a .caf replays losslessly")
    p.add_argument("rest", nargs=argparse.REMAINDER)

    p = sub.add_parser("preview", help="render what the panel will show")
    p.add_argument("theme", help="theme directory")
    p.add_argument("-o", "--out", required=True, help="PNG to write")
    p.add_argument("--bg", default="#e8f2ee",
                   help="pet background, as the firmware is set (default the "
                        "shipped pale mint)")
    p.add_argument("--cols", type=int, default=3)
    p.add_argument("--cell", type=int, default=200)

    args = ap.parse_args()
    if args.cmd == "preview":
        preview(args.theme, args.out, args.bg, args.cols, args.cell)
        return 0
    script = {"convert": GIF2CAF, "pack": MKTHEME, "verify": VERIFY}[args.cmd]
    return run(script, args.rest)


if __name__ == "__main__":
    sys.exit(main())
