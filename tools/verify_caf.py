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
PANEL = 240            # the device's panel: a .caf stores its corner in its pixels


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


def header_size(path):
    """The artwork rect a .caf stores, without decoding any of its frames."""
    d = open(path, "rb").read(HEADER_LEN)
    if d[:4] != gif2caf.MAGIC:
        raise SystemExit(f"{path}: bad magic {d[:4]!r}")
    _x0, _y0, w, h, _frames = struct.unpack("<HHHHH", d[4:14])
    return w, h


def main():
    theme = sys.argv[1] if len(sys.argv) > 1 else "themes/clawd"
    gifs = sys.argv[2] if len(sys.argv) > 2 else "../stickers/240/clawd"

    manifest = {}
    scale = 1
    with open(os.path.join(theme, "manifest.txt")) as fh:
        for line in fh:
            line = line.split("#")[0].strip()
            if "=" in line:
                k, v = line.split("=", 1)
                # `scale` is the pack's magnification for the device to apply,
                # not a state with a file of its own.
                if k.strip() == "scale":
                    scale = max(1, int(v.strip()))
                else:
                    manifest[k.strip()] = v.strip()

    bad = 0
    for state, caf in sorted(manifest.items()):
        caf_path = os.path.join(theme, caf)
        gif_path = os.path.join(gifs, os.path.splitext(caf)[0] + ".gif")
        if not os.path.exists(gif_path):
            print(f"{state:14} SKIP (no source gif {gif_path})")
            continue

        # Three knobs change what the converter produces and the .caf records
        # none of them: the margin it fitted into, whether the art had a
        # background to find (--opaque), and whether it was allowed to scale up.
        # Rather than guess from the file, run the combinations a pack here is
        # built with and require one of them to reproduce it exactly — the
        # geometry *and* every frame, which is the check.
        recipes = ((12, False, True), (12, False, False),
                   (0, True, True), (0, True, False))
        want_w, want_h = header_size(caf_path)
        got, stored = decode(caf_path, want_w, want_h)

        for margin, opaque, no_upscale in recipes:
            idx, _delays, size, palette = gif2caf.load_frames(gif_path, 0xFFFF, 1,
                                                             opaque)
            cw, ch = size
            fitted, _, rect = gif2caf.compose(idx, size, palette, margin, no_upscale)
            x0, y0, w, h = rect
            h2 = min(ch, ((h + gif2caf.BAND_ROWS - 1) // gif2caf.BAND_ROWS)
                     * gif2caf.BAND_ROWS)
            y0 = max(0, min(y0, ch - h2))
            if (w, h2) != (want_w, want_h):
                continue
            # The corner the file stores is one of two, and both are correct:
            #
            #   * the converter's, which centres the art in the *source GIF's*
            #     canvas — what a scale=1 pack has, and where the device, which
            #     pushes at that corner with the art's own size, draws it centred;
            #   * the panel's, `(PANEL - w*scale)//2` — what a pack whose art is
            #     stored small and magnified on push has to store instead, since
            #     the device magnifies from that corner and would otherwise land
            #     the art up and to the left. tools/recenter_pack.py does this,
            #     and the packs it was run on are the ones this accepts.
            #
            # The two coincide at scale 1, so this is not a tolerance: for every
            # pack that does not ask the device to magnify, the check below is
            # exactly the one it always was.
            centred = ((PANEL - want_w * scale) // 2, (PANEL - want_h * scale) // 2)
            if (stored[0], stored[1]) not in ((x0, y0), centred):
                print(f"{state:14} FAIL stored at ({stored[0]},{stored[1]}), which "
                      f"is neither the converter's ({x0},{y0}) nor the panel's "
                      f"{centred}")
                bad += 1
                break
            expect = gif2caf.crop_frames(fitted, (x0, y0, w, h2), cw)
            if expect == got:
                where = (" (recentred)" if (stored[0], stored[1]) == centred
                         and (x0, y0) != centred else "")
                print(f"{state:14} OK   {len(expect):3} frames, bbox "
                      f"{w}x{h2} at ({stored[0]},{stored[1]}){where}")
                break
        else:
            print(f"{state:14} FAIL no recipe reproduces {caf_path} "
                  f"({want_w}x{want_h}, {len(got)} frames)")
            bad += 1

    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
