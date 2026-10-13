#!/usr/bin/env python3
"""Re-centre a theme pack's artwork on the panel.

gif2caf positions a pack's artwork by centring it in the *source GIF's* canvas
and storing that corner as the frame's panel position (compose(), in
tools/gif2caf.py). A theme may then ask the device to magnify the art as it
pushes it — the manifest's `scale=` — and anim.cpp magnifies *from that corner*
(`renderFrame()`, outW = w * scale), so the two disagree as soon as the art is
drawn on a canvas that is not panel-sized:

  * a 128x128 sticker set at scale 2 lands tens of pixels up and to the left of
    centre, which on a small character reads as floating in the corner;
  * at scale 1 there is nothing to fix — the converter's corner is already the
    panel's centring for the art it stores;
  * a set drawn on a 240 canvas (clawd, origin) has the same complaint in theory
    and nothing on screen in practice, because the art is nearly panel-sized to
    begin with.

This rewrites the two u16s in each frame header that say where the frame goes —
`(panel - w*scale) // 2` — and touches nothing else: the artwork, the palette,
the frame count and the timing are the converter's. It is a build step, not a
repair: run it after mktheme, then bundle.

Usage:
  recenter_pack.py themes/cloudling           # in place
  recenter_pack.py themes/calico --check      # say what would move, change nothing
"""

import argparse
import os
import struct
import sys

PANEL = 240
HEADER_LEN = 14        # magic(4) + x0,y0,w,h,frames as five u16


def manifest_scale(theme):
    """The pack's magnification, as anim.cpp reads it: `scale=N`, default 1."""
    path = os.path.join(theme, "manifest.txt")
    scale = 1
    with open(path) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if line.startswith("scale"):
                _, _, value = line.partition("=")
                scale = max(1, int(value.strip() or 1))
    return scale


def recenter(theme, check):
    scale = manifest_scale(theme)
    if scale == 1:
        print(f"{theme}: scale 1 — the converter's position is already centred")
        return 0

    moved = 0
    for name in sorted(os.listdir(theme)):
        if not name.endswith(".caf"):
            continue
        path = os.path.join(theme, name)
        with open(path, "rb") as fh:
            data = bytearray(fh.read())
        if data[:4] != b"CAF3":
            sys.exit(f"error: {path} is not a .caf")
        x0, y0, w, h, _frames = struct.unpack("<HHHHH", data[4:HEADER_LEN])

        want = ((PANEL - w * scale) // 2, (PANEL - h * scale) // 2)
        if (x0, y0) == want:
            continue
        if want[0] < 0 or want[1] < 0:
            sys.exit(f"error: {name}: {w}x{h} at scale {scale} is larger than the "
                     f"panel, so there is no such thing as centred")
        moved += 1
        print(f"  {name:<28} ({x0},{y0}) -> ({want[0]},{want[1]})"
              f"   {w}x{h} at scale {scale} = {w * scale}x{h * scale} on the panel")
        if not check:
            struct.pack_into("<HH", data, 4, *want)
            with open(path, "wb") as fh:
                fh.write(bytes(data))

    verb = "would move" if check else "moved"
    print(f"{theme}: {moved} file(s) {verb} (panel {PANEL}px, scale {scale})")
    if check and moved:
        print("run again without --check to write the new positions")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("theme", help="theme pack directory (holds manifest.txt + *.caf)")
    ap.add_argument("--check", action="store_true",
                    help="report what would change instead of writing")
    args = ap.parse_args()
    if not os.path.isdir(args.theme):
        sys.exit(f"error: {args.theme} is not a directory")
    return recenter(args.theme, args.check)


if __name__ == "__main__":
    sys.exit(main())
