#!/usr/bin/env python3
"""Render each state of a theme pack as an animated GIF, for the READMEs.

The GIFs come from the .caf files rather than the source stickers on purpose: a
.caf is what the panel actually plays, so the preview carries the pack's scale,
the field resolved to the background colour (palette index 0 is transparent, not a
colour) and the per-frame delays the player uses — the clamped ones included.

Usage:
  theme_gifs.py esp-idf/data/theme pics/states

Writes <out-dir>/<state>.gif at 2x, nearest-neighbour, which is how the firmware
magnifies and what keeps pixel art crisp in a README. Regenerate after changing a
theme, and keep the result in the repository: the README embeds these files.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image                                    # noqa: E402
from mochi_theme import (PANEL, decode_frame, hex565,    # noqa: E402
                         read_caf, read_manifest)

# anim.cpp clamps every authored delay before it waits; a preview that ignored
# that would play at a speed the device never does.
DELAY_MIN_MS, DELAY_MAX_MS = 10, 500
UPSCALE = 2


def state_frames(theme_dir, caf, scale, bg_rgb, bg565):
    """Every frame of one state, composed onto a panel-sized canvas."""
    x0, y0, w, h, palette, offsets, data = read_caf(os.path.join(theme_dir, caf))
    out_w, out_h = w * scale, h * scale
    if out_w > PANEL or out_h > PANEL:
        sys.exit(f"error: {caf} at scale {scale} is {out_w}x{out_h}, larger than the panel")

    # The delays sit after the frame-offset table, two bytes per frame.
    table = 14 + 512 + len(offsets) * 4
    delays = [struct.unpack("<H", data[table + f * 2: table + f * 2 + 2])[0]
              for f in range(len(offsets))]

    frames = []
    for offset in offsets:
        im = decode_frame(data, offset, w, h, palette, bg565)
        screen = Image.new("RGB", (PANEL, PANEL), bg_rgb)
        # Nearest neighbour, matching the firmware's integer magnification.
        screen.paste(im.resize((out_w, out_h), Image.NEAREST), (x0, y0))
        frames.append(screen.resize((PANEL * UPSCALE, PANEL * UPSCALE), Image.NEAREST))

    ms = [min(DELAY_MAX_MS, max(DELAY_MIN_MS, d)) for d in delays]
    return frames, ms


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    theme_dir, out_dir = sys.argv[1], sys.argv[2]
    bg = sys.argv[3] if len(sys.argv) > 3 else "#e8f2ee"
    os.makedirs(out_dir, exist_ok=True)

    states, scale = read_manifest(theme_dir)
    bg_rgb = tuple(int(bg.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))
    bg565 = hex565(bg)

    total = 0
    for state, caf in states:
        frames, ms = state_frames(theme_dir, caf, scale, bg_rgb, bg565)
        path = os.path.join(out_dir, f"{state}.gif")
        frames[0].save(path, save_all=True, append_images=frames[1:],
                       duration=ms, loop=0, optimize=True)
        kb = os.path.getsize(path) / 1024
        total += kb
        print(f"  {state:<14} {len(frames):>3} frames  {sum(ms)/1000:>4.1f}s  {kb:>6.0f} KB")
    print(f"{len(states)} state(s), {total:.0f} KB total, {UPSCALE}x, background {bg}")


if __name__ == "__main__":
    main()
