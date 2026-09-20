#!/usr/bin/env python3
"""Convert sticker GIFs into the device's compact .caf animation format.

The panel is 240x240 with no PSRAM, so the firmware cannot hold a full frame
(115 KB) or a frame canvas in RAM. .caf therefore stores each frame as
palette-index runs, and the firmware walks those runs once per frame, pushing
the panel a band of rows at a time.

Colours are quantised in RGB565 space up front, so the palette the device
stores is exactly what it writes to the panel — no per-pixel conversion at
playback time. These sticker sets use <256 distinct RGB565 values, so the
quantisation is lossless in practice; the script warns if a GIF exceeds that.

Usage:
  gif2caf.py INPUT_DIR -o OUT_DIR [--names idle=clawd-idle ...]
  gif2caf.py clawd-idle.gif -o out/          # single file

Format (little-endian throughout):
  "CAF3"                4 B
  x0, y0                u16, u16  (where the artwork sits on the panel)
  width, height         u16, u16  (of the artwork bounding box)
  frameCount            u16
  palette[256]          512 B   (RGB565)
  frameOffset[]         frameCount x u32  (absolute, from file start)
  frameDelay[]          frameCount x u16  (milliseconds; kept, playback ignores)
  frame blobs           per frame: (runLen u8, paletteIndex u8) ...

Frames are stored as palette-index RLE, cropped to the artwork's bounding box
rather than the full panel.

Why one full-bbox push per frame instead of a sparse delta: with the official
esp_lcd driver, setting a rectangle's address window costs ~232 us on this
board (measured) regardless of how few pixels it carries, so the number of
window sets — not the bytes — dominates. A sparse delta needs 135-178 windows
per frame for the busier animations (~40 ms), while pushing the cropped bbox is
~18-27 windows and its data costs 18-26 ms. Cropping to the bbox is what makes
that affordable: the panel-sized canvas is mostly empty margin.
"""

import argparse
import os
import struct
import sys

from PIL import Image, ImageSequence

MAGIC = b"CAF3"
BAND_ROWS = 8          # rows per panel transfer; must match anim.cpp's BAND_ROWS


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def bg_index(idx, w, h):
    """The index that is the background: the one appearing on the frame border
    that also covers the most of the frame.

    The transparent index is not a reliable signal here — GIF frames disagree
    about it (in these stickers frame 0 declares index 1 while later frames use
    index 0 for the same background colour), and an index can also appear in the
    artwork. Border + area together are unambiguous for a centred subject.
    """
    border = list(idx[:w]) + list(idx[-w:])
    border += [idx[y * w] for y in range(h)]
    border += [idx[y * w + w - 1] for y in range(h)]
    candidates = set(border)
    counts = {}
    for v in idx:
        if v in candidates:
            counts[v] = counts.get(v, 0) + 1
    return max(counts, key=counts.get)


def nearest_map(colors, palette):
    """Map each colour in `colors` to the index of the closest palette entry."""
    return [min(range(len(palette)),
                key=lambda k: dist565(c, palette[k]))
            for c in colors]


def dist565(a, b):
    """Squared distance in RGB565, expanded back to 8-bit per channel."""
    ar, ag, ab = (a >> 11) & 0x1F, (a >> 5) & 0x3F, a & 0x1F
    br, bg, bb = (b >> 11) & 0x1F, (b >> 5) & 0x3F, b & 0x1F
    return (ar - br) ** 2 * 4 + (ag - bg) ** 2 + (ab - bb) ** 2 * 4


def load_frames(path):
    """Return (index-byte arrays, delays, size, palette).

    Reads each frame through the GIF's OWN palette (`seek` + `convert("P")`)
    rather than compositing to RGB: these stickers are palettised pixel art, and
    going through RGB invents thousands of interpolated colours, which both
    loses fidelity and shatters the run-length structure (~3x larger output).

    Frames may carry local colour tables, so the palette union can exceed 256.
    When it does, the *palette* is quantised — never the pixels — so pixel
    indices are only remapped through a small lookup table and every run
    boundary is preserved.
    """
    im = Image.open(path)

    # The stickers are square-padded with a transparent background. The panel has
    # no alpha, so that index has to become a real colour — and whatever the GIF
    # happens to store there is arbitrary (one set's idle art lands on a salmon
    # pink), which is what made the panel look colour-shifted. Force it to white,
    # and pin white to index 0 so quantisation can never dilute it.
    im.seek(0)
    trans = im.info.get("transparency")

    raw_frames = []       # per frame: (index bytes, per-frame rgb565 palette)
    delays = []
    union = {}            # rgb565 -> colour list position (excluding white)
    for i in range(im.n_frames):
        im.seek(i)
        p = im.convert("P")
        pal = p.getpalette()
        if not pal:
            continue
        delays.append(im.info.get("duration") or 70)
        pal565 = [rgb565(pal[k * 3], pal[k * 3 + 1], pal[k * 3 + 2])
                  for k in range(256)]
        idx = p.tobytes()
        bg = bg_index(idx, im.size[0], im.size[1])
        pal565[bg] = 0xFFFF     # background -> white
        for u in set(idx):
            v = pal565[u]
            if v != 0xFFFF and v not in union:
                union[v] = len(union)
        raw_frames.append((idx, pal565))

    size = im.size
    colors = list(union.keys())

    # Index 0 is always white (the background); artwork colours start at 1.
    if len(colors) <= 255:
        body = colors
    else:
        print(f"  note: {os.path.basename(path)}: {len(colors)} colours across "
              f"frames -> quantising to 255")
        strip = Image.new("RGB", (len(colors), 1))
        strip.putdata([(c >> 11 << 3, (c >> 5 & 0x3F) << 2, (c & 0x1F) << 3)
                       for c in colors])
        ref = strip.quantize(colors=255, method=Image.MEDIANCUT)
        rp = ref.getpalette()
        body = [rgb565(rp[k * 3], rp[k * 3 + 1], rp[k * 3 + 2]) for k in range(255)]
        # never emit a duplicate white in the body
        body = [c for c in body if c != 0xFFFF]
        mapped = [min(range(len(body)), key=lambda k: dist565(c, body[k]))
                  for c in colors]
        union = {c: mapped[i] for i, c in enumerate(colors)}

    palette = [0xFFFF] + body          # index 0 is white, 1..N the artwork
    palette += [0] * (256 - len(palette))
    # colour -> final index: white is 0, everything else is 1 + its body slot
    final = {c: 1 + union[c] for c in colors}
    final[0xFFFF] = 0

    idx_frames = [bytes(final[pal565[u]] for u in idx)
                  for idx, pal565 in raw_frames]
    return idx_frames, delays, size, palette


def rle_encode(indices):
    """(runLength u8, value u8) pairs. Runs longer than 255 are split."""
    out = bytearray()
    i, n = 0, len(indices)
    while i < n:
        v = indices[i]
        run = 1
        while i + run < n and indices[i + run] == v and run < 255:
            run += 1
        out.append(run)
        out.append(v)
        i += run
    return bytes(out)


def crop_frames(idx_frames, rect, canvas_w):
    """Crop every frame to `rect` = (x0, y0, w, h)."""
    x0, y0, w, h = rect
    out = []
    for idx in idx_frames:
        buf = bytearray(w * h)
        for y in range(h):
            src = (y0 + y) * canvas_w + x0
            buf[y * w:(y + 1) * w] = idx[src:src + w]
        out.append(bytes(buf))
    return out


def build(idx_frames, delays, rect, palette):
    """Serialise bbox-cropped frames as palette-index RLE."""
    x0, y0, w, h = rect
    assert h % BAND_ROWS == 0, f"bbox height {h} not a multiple of {BAND_ROWS}"

    blobs = [rle_encode(f) for f in idx_frames]

    # magic(4) + x0,y0,w,h,frames as five u16 (10) + palette(512)
    header_len = 4 + 10 + 512 + len(idx_frames) * 4 + len(idx_frames) * 2

    out = bytearray()
    out += MAGIC
    out += struct.pack("<HHHHH", x0, y0, w, h, len(idx_frames))
    out += struct.pack("<256H", *palette)

    off = header_len
    for b in blobs:
        out += struct.pack("<I", off)
        off += len(b)
    for d in delays:
        out += struct.pack("<H", min(int(d), 65535))
    for b in blobs:
        out += b
    return bytes(out)


def scale_idx(idx, sw, sh, dw, dh):
    """Nearest-neighbour upscale of a palette-index plane (pixel art, so no
    smoothing wanted)."""
    out = bytearray(dw * dh)
    for y in range(dh):
        src = (y * sh // dh) * sw
        row = y * dw
        for x in range(dw):
            out[row + x] = idx[src + x * sw // dw]
    return bytes(out)


def compose(idx_frames, size, palette, margin):
    """Crop every frame to the artwork's union bounding box, upscale it to fill
    the panel, and centre it on a white field.

    The sticker GIFs are drawn with the character occupying only ~33% x 21% of
    the canvas — the desktop app scales them itself. On a 240x240 panel that
    reads as a tiny crab adrift in white, so the artwork is re-framed here.
    """
    sw, sh = size
    white = 0
    xs0 = ys0 = 10 ** 9
    xs1 = ys1 = -1
    for idx in idx_frames:
        for i, v in enumerate(idx):
            if v != white:
                x, y = i % sw, i // sw
                if x < xs0: xs0 = x
                if x > xs1: xs1 = x
                if y < ys0: ys0 = y
                if y > ys1: ys1 = y
    if xs1 < 0:
        return idx_frames, size, (0, 0, sw, sh)       # nothing but background

    bw, bh = xs1 - xs0 + 1, ys1 - ys0 + 1
    avail_w = sw - margin * 2
    avail_h = sh - margin * 2
    scale = min(avail_w / bw, avail_h / bh)
    dw = max(1, int(bw * scale))
    dh = max(1, int(bh * scale))

    ox = (sw - dw) // 2
    oy = (sh - dh) // 2
    print(f"    artwork {bw}x{bh} at ({xs0},{ys0}) -> {dw}x{dh} "
          f"(x{scale:.2f})")

    out_frames = []
    for idx in idx_frames:
        cropped = bytearray(bw * bh)
        for y in range(bh):
            src = (ys0 + y) * sw + xs0
            cropped[y * bw:(y + 1) * bw] = idx[src:src + bw]
        big = scale_idx(bytes(cropped), bw, bh, dw, dh)
        canvas = bytearray([0]) * (sw * sh)           # index 0 is white
        for y in range(dh):
            dst = (oy + y) * sw + ox
            canvas[dst:dst + dw] = big[y * dw:(y + 1) * dw]
        out_frames.append(bytes(canvas))
    return out_frames, size, (ox, oy, dw, dh)


def convert(src, dst, margin=12, fit=True):
    idx_frames, delays, size, palette = load_frames(src)
    cw, ch = size
    if fit:
        idx_frames, size, rect = compose(idx_frames, size, palette, margin)
        x0, y0, w, h = rect
        # The player pushes whole bands, so the height must be a multiple of
        # BAND_ROWS. The artwork is centred with `margin` to spare, so rounding
        # down into that margin keeps the subject fully inside.
        h2 = min(ch, ((h + BAND_ROWS - 1) // BAND_ROWS) * BAND_ROWS)
        y0 = max(0, min(y0, ch - h2))
        rect = (x0, y0, w, h2)
        idx_frames = crop_frames(idx_frames, rect, cw)
    else:
        rect = (0, 0, cw, ch)
    data = build(idx_frames, delays, rect, palette)
    with open(dst, "wb") as fh:
        fh.write(data)
    return len(data), len(idx_frames)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="a .gif file, or a directory of them")
    ap.add_argument("-o", "--out", required=True, help="output directory")
    ap.add_argument("--margin", type=int, default=12,
                    help="white border kept around the artwork (default 12)")
    ap.add_argument("--no-fit", action="store_true",
                    help="keep the GIF's own framing instead of cropping to the "
                         "artwork and scaling it up to fill the panel")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    if os.path.isdir(args.input):
        gifs = sorted(f for f in os.listdir(args.input) if f.lower().endswith(".gif"))
        if not gifs:
            raise SystemExit(f"no .gif files in {args.input}")
    else:
        gifs = [os.path.basename(args.input)]
        args.input = os.path.dirname(args.input) or "."

    total = 0
    for name in gifs:
        src = os.path.join(args.input, name)
        dst = os.path.join(args.out, os.path.splitext(name)[0] + ".caf")
        size, frames = convert(src, dst, args.margin, not args.no_fit)
        total += size
        orig = os.path.getsize(src)
        print(f"  {name:32} {frames:>3} frames  {orig/1024:>6.0f}K -> {size/1024:>5.0f}K")
    print(f"\n共 {len(gifs)} 个文件，合计 {total/1024/1024:.2f} MB")


if __name__ == "__main__":
    sys.exit(main())
