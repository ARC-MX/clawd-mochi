#!/usr/bin/env python3
"""Draw the "origin" theme's sticker set — the orange cube pet, in code.

Every other pack in this repo is converted from a sticker set someone else drew
(`../stickers/<set>/`). This character has no such source: it exists as a
product render, so its artwork is a script. What comes out is an ordinary
sticker set, laid out the way `mktheme.py` expects, so the rest of the pipeline
(and its checks) applies unchanged:

  origin_art.py -o /tmp/origin-gifs
  origin_art.py --pack themes/origin          # ...and hand it to mktheme

`--shell` picks how much of the character to draw, which is how the `face` theme
is made from the same code and the same faces:

  full    the whole pet (default) — legs, body, ears, screen, face
  frame   the screen and what is on it: the black bezel, the lit panel, the face
  bare    the face alone, on the field — no bezel and no panel behind it
  panel   the screen filling the panel, its bezel along the panel's own edge

The faces are identical in every shell; only what surrounds them changes, so a
pose, a blink or a scroll reads the same wherever it is drawn. `panel` is the
one that also changes the *size*: its face is drawn at twice the references's
unit (16 px instead of 8), which takes 13 units of face across most of the
panel's width — and its bezel is a PANEL_BEZEL-px border at the panel's edge,
not a frame drawn in cells.

Four choices here are forced by the pipeline, not by taste:

  * **The art is authored at panel size, and the pack uses scale 1.** A theme
    may store its art small and have the device magnify it on push, which is
    what `--scale 2 --no-upscale` does for the other sets. It cannot be used
    here: gif2caf centres the art in the *unmagnified* canvas and stores that
    as the panel position (compose(), gif2caf.py), while anim.cpp pushes at
    that point with width w*scale. A 216-wide subject on a 240 canvas therefore
    lands at x0=12 with outW=432, and renderFrame() rejects the frame outright
    (outW > 240) — the state would be dropped, not merely misplaced.

  * **The union bounding box is exactly 216 px wide** — panel minus the 12 px
    margin gif2caf keeps on each side. At exactly 216 the converter's fit
    scale is 1.0, which makes `--no-upscale` a no-op *and* makes
    tools/verify_caf.py agree with the pack (it recomputes the fit without
    knowing about --no-upscale, so any other width makes it disagree and fail).
    It also means the pet fills the panel, as the reference does. Everything
    the character does horizontally — nothing, as it turns out — has to happen
    inside that box, which is why the error state vibrates instead of shaking.

    A shell that draws less than the whole pet is *narrower* than that, and
    cannot avoid it: the face keeps its size and its 8 px unit grid, so `frame`
    comes to about 130 px across and `bare` to about 100. At fit scale 1 that
    is allowed — the converter centres the box and the device draws it there —
    so the rule below is `at most` 216 for those, and `exactly` only for `full`.
    The catch is that verify_caf.py, which re-derives the fit, only agrees with
    the packs whose art fills the box.

    `panel` is the third case and the opposite one: its art is the panel, 240 px
    across, and it is packed with `--margin 0` so the converter has no box to
    fit it into and leaves it exactly where it was drawn. A margin at all would
    make the fit 240/216 and scale the art by 0.9 — not an integer, so every
    edge in it would land between pixels. The check below holds it to the panel
    exactly, and pack() is where the margin is passed.

  * **The GIF is written by hand, full frames on one palette.** Pillow's
    encoder writes every frame after the first as a delta carrying its own
    trimmed colour table, which gif2caf cannot read — it wants 256 entries per
    frame — and it silently folds a frame that repeats its predecessor into it.
    See write_gif().
  * **The palette is the 6x6x6 web-safe cube.** Pillow hands every frame after
    the first back as RGB, and gif2caf dithered it onto that cube to get indices
    again; this art sits exactly on the cube, so the round trip is exact. See
    the palette comment. Flat colour on a 4 px grid also keeps the RLE runs
    long, which is most of why this pack is small.

Frame delays are multiples of 10 ms (GIF stores centiseconds) and stay inside
10..500 ms, which is what the player clamps to.
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
MKTHEME = os.path.join(HERE, "mktheme.py")

PANEL = 240        # the panel, and the GIF canvas, are the same thing
GRID = 4           # every edge lands on this grid: crisp pixels, long runs
MARGIN = 12        # gif2caf's default, and what the geometry below is built to
PANEL_BEZEL = 2    # px of black at the panel's edge in the `panel` shell — the one
                   # edge in this file that is not on the grid, on purpose: a
                   # 4 px border already reads as a frame, and this is meant to
                   # read as the panel's own edge

# Palette, in the order the .caf stores it. Index 0 is the field: the firmware
# never draws it, it lets the runtime background show through, so it is the
# colour the art sits on rather than a colour the character owns. (It is also
# why nothing in the art may be pure white — gif2caf is handed white as the
# field colour and would swallow matching pixels.)
#
# Every entry is a multiple of 51 per channel — the 6x6x6 web-safe cube — and
# that is not an aesthetic choice. Pillow opens a GIF's first frame as palette
# data but every frame after it as RGB (LOADING_STRATEGY defaults to
# RGB_AFTER_FIRST), and gif2caf then pulls each frame back to indices with
# convert("P"), which dithers onto the default web palette. For flat pixel art
# that is speckle: orange that is not on the cube comes back as a checkerboard
# of the two nearest cube colours. On the cube, every pixel matches a palette
# entry, the propagated error is zero, and the round trip is exact — which is
# what check_gif() below asserts rather than assumes.
BG, BODY, SHADE, SCREEN, INK, BLUSH = range(6)
PALETTE = [
    (255, 255, 204),       # 0 cream — the field; the reference's backdrop is
                           #   warmer, and the previews pass it explicitly
    (255, 102, 0),         # 1 body orange, lit front face
    (204, 102, 0),         # 2 the shadow under the body: legs, undersides
    (255, 153, 51),        # 3 the screen, a shade lighter than the body
    (0, 0, 0),             # 4 ink: bezel and every face pixel
    (255, 153, 153),       # 5 blush
]
PAL_FLAT = [c for rgb in PALETTE for c in rgb]
PAL_FLAT += [0] * (768 - len(PAL_FLAT))
UNUSED = len(PALETTE)      # a palette entry the art never draws with

# ── geometry, in cells (1 cell = GRID px) ─────────────────────
# The character is 54 cells wide because that is exactly what the margin
# allows: 3..56 lands on px 12..227, and 227 - 12 + 1 = 216.
#
# The proportions are off the reference render: the body is 1.35x wider than it
# is tall, the screen takes about two thirds of that width and five sixths of
# the height, and the legs are a quarter of the body's height. Getting the
# screen wrong is what makes the pet read as a monitor with orange trim instead
# of a cube with a face on it.
EAR_X0, EAR_X1 = 3, 56
EAR_Y0, EAR_Y1 = 19, 24
BODY_X0, BODY_X1 = 7, 52
BODY_Y0, BODY_Y1 = 9, 42
BEZEL_X0, BEZEL_X1, BEZEL_Y0, BEZEL_Y1 = 15, 44, 12, 39
SCR_X0, SCR_X1, SCR_Y0, SCR_Y1 = 17, 42, 14, 37
LEG_Y0, LEG_Y1 = 43, 50
LEG_COLS = ((7, 15), (23, 36), (44, 52))    # three legs, two notches between

# The face lives inside the screen on its own coarser grid: 8 px units, which
# is the resolution the reference's pixel faces are drawn at. UNIT and the
# screen's inset are the two numbers a shell changes — see set_shell().
UNIT = 8
SCR_INSET = 1                                    # cells of bezel before the screen
FACE_U = (SCR_X1 - SCR_X0 + 1) * GRID // UNIT    # 13 units across
FACE_V = (SCR_Y1 - SCR_Y0 + 1) * GRID // UNIT    # 12 down

# How much of the character to draw — see --shell. Set once in main(); every
# coordinate below is shared by the shells, so a face sits in the same place
# whether or not there is a pet under it.
SHELL = "full"


def set_shell(name):
    """Point the drawing at one composition's screen.

    `full`, `frame` and `bare` share the pet's screen. `panel` is the screen
    filling the panel: its bezel is PANEL_BEZEL px at the panel's own edge and
    the lit screen runs out to it, so SCR_* here is the *face's* area rather
    than the screen's — 13 units of 16 px across and 12 down, centred on the
    panel. Growing the unit is what grows the face, because the faces are drawn
    in units; nothing about a face has to change for that, it comes out twice
    the size.
    """
    global SHELL, UNIT, SCR_INSET
    global BEZEL_X0, BEZEL_X1, BEZEL_Y0, BEZEL_Y1, SCR_X0, SCR_X1, SCR_Y0, SCR_Y1
    global FACE_U, FACE_V

    SHELL = name
    if name == "panel":
        UNIT, SCR_INSET = 16, 0
        BEZEL_X0, BEZEL_X1, BEZEL_Y0, BEZEL_Y1 = 0, 59, 0, 59
        SCR_X0, SCR_X1 = 4, 55       # 52 cells = 13 units of 16 px: (240-208)/2 = 16 px in
        SCR_Y0, SCR_Y1 = 6, 53       # 48 cells = 12 units: (240-192)/2 = 24 px down
    else:
        UNIT, SCR_INSET = 8, 1
        BEZEL_X0, BEZEL_X1, BEZEL_Y0, BEZEL_Y1 = 15, 44, 12, 39
        SCR_X0, SCR_X1, SCR_Y0, SCR_Y1 = 17, 42, 14, 37

    FACE_U = (SCR_X1 - SCR_X0 + 1) * GRID // UNIT
    FACE_V = (SCR_Y1 - SCR_Y0 + 1) * GRID // UNIT
    # Every face is drawn on a 13x12 grid of units, so a screen that is not that
    # many units across would clip them. The geometry above has to add up, and
    # this is where it is made to: a shell whose numbers are off stops here
    # rather than producing art with a mouth cut off.
    if (FACE_U, FACE_V) != (13, 12):
        sys.exit(f"error: the {name} screen is {FACE_U}x{FACE_V} face units, "
                 f"but every face is drawn on a 13x12 one")


def surface():
    """The colour that stands in for "the panel showing through" in a face.

    On `bare` there is no panel, so the pixels two faces draw in SCREEN —
    sparkle corners in face_done, the sunglasses glint in face_slack — become
    the field instead, which reads as the same hole either way.
    """
    return BG if SHELL == "bare" else SCREEN


# ── drawing ───────────────────────────────────────────────────

def cell(d, x0, y0, x1, y1, fill):
    """Fill an inclusive cell rectangle."""
    d.rectangle((x0 * GRID, y0 * GRID, (x1 + 1) * GRID - 1, (y1 + 1) * GRID - 1),
                fill=fill)


def unit(d, ox, oy, u, v, w, h, fill):
    """Fill a face-grid rectangle, in 8 px units, at the screen's origin."""
    d.rectangle((ox + u * UNIT, oy + v * UNIT,
                 ox + (u + w) * UNIT - 1, oy + (v + h) * UNIT - 1), fill=fill)


def glyph(d, ox, oy, u, v, rows, fill=INK):
    """Stamp a '#'/'.' bitmap onto the face grid."""
    for dv, row in enumerate(rows):
        for du, ch in enumerate(row):
            if ch == "#":
                unit(d, ox, oy, u + du, v + dv, 1, 1, fill)


def smile(d, ox, oy):
    """Three units wide: the middle one a row lower than the corners."""
    unit(d, ox, oy, 5, 7, 1, 1, INK)
    unit(d, ox, oy, 7, 7, 1, 1, INK)
    unit(d, ox, oy, 6, 8, 1, 1, INK)


def frown(d, ox, oy):
    """The same shape upside down."""
    unit(d, ox, oy, 6, 7, 1, 1, INK)
    unit(d, ox, oy, 5, 8, 1, 1, INK)
    unit(d, ox, oy, 7, 8, 1, 1, INK)


def draw_pet(d, p, face, i, n):
    """One frame: legs, body, ears, screen, face — as much of that as the shell
    asks for.

    Poses are vertical on purpose. A hop moves the whole pet (the feet leave
    the ground); `squash` compresses the body from the top down, which is
    breathing; `squeeze` pulls the sides in, which is the error state's
    vibrate. Nothing translates sideways, so every frame's artwork stays inside
    the 216 px box the block above depends on.

    The poses all move the *screen* as well as the body, so a shell with no
    body still breathes and hops: that is what keeps the face alive.
    """
    hop = p.get("hop", 0)
    squash = p.get("squash", 0)
    squeeze = p.get("squeeze", 0)

    if SHELL == "panel":
        # The bezel is the panel's own edge, so a pose has nowhere to move it:
        # the hop and the breath move the *face* inside the screen instead,
        # which is what they look like with no body to carry them. The face
        # sits 24 px in from the top and bottom, which is room for the pet's
        # own amplitudes and then some, so they stay as authored — twice the
        # face, same motion.
        #
        # Drawn in pixels, not cells: PANEL_BEZEL is the one measure in this
        # file that is finer than the grid.
        d.rectangle((0, 0, PANEL - 1, PANEL - 1), fill=INK)
        d.rectangle((PANEL_BEZEL, PANEL_BEZEL,
                     PANEL - 1 - PANEL_BEZEL, PANEL - 1 - PANEL_BEZEL),
                    fill=surface())
        # squeeze has no sides left to pull in, so it shakes the face a cell
        # down on alternate frames: the same flinch, from the other end.
        dy = (squash - hop) * GRID - (GRID if squeeze else 0)
        face(d, SCR_X0 * GRID, SCR_Y0 * GRID + dy, i, n, p)
        return

    if SHELL != "full":
        # These shells are the screen alone, so its bob is the artwork's — and
        # the device pushes the panel eight rows at a time (BAND_ROWS, anim.cpp:
        # a frame whose height is not a multiple of 8 is refused, not clipped).
        # The bob is therefore quantised to 8 px steps. Rounding down rather
        # than up keeps every pose inside the amplitude the pet itself had, and
        # a state that loses its 4 px breath still moves: the code scrolls, the
        # sunglasses glint, the eyes blink.
        hop -= hop % 2
        squash -= squash % 2

    top = BODY_Y0 - hop + squash
    bot = BODY_Y1 - hop

    if SHELL == "full":
        # Legs first, so the body covers their tops. They are in the body's
        # shadow in the render, and painting them darker is what makes a flat
        # front view read as an object standing up.
        for x0, x1 in LEG_COLS:
            cell(d, x0 + squeeze, LEG_Y0 - hop, x1 - squeeze, LEG_Y1 - hop, SHADE)

        cell(d, BODY_X0 + squeeze, top, BODY_X1 - squeeze, bot, BODY)

        for x0, x1 in ((EAR_X0 + squeeze, BODY_X0 - 1 + squeeze),
                       (BODY_X1 + 1 - squeeze, EAR_X1 - squeeze)):
            cell(d, x0, EAR_Y0 - hop + squash, x1, EAR_Y1 - hop + squash, BODY)
            cell(d, x0, EAR_Y1 - hop + squash, x1, EAR_Y1 - hop + squash, SHADE)

    sy0, sy1 = BEZEL_Y0 - hop + squash, BEZEL_Y1 - hop + squash
    if SHELL != "bare":
        # With a body under it, the squeeze pulls the body's sides in and the
        # frame rides inside untouched. With no body, flexing the frame itself
        # is the only thing left for the error state to do — and it flexes the
        # bezel, never the screen, so the face is never cut into.
        flex = squeeze if SHELL != "full" else 0
        cell(d, BEZEL_X0 + flex, sy0, BEZEL_X1 - flex, sy1, INK)
        cell(d, SCR_X0, sy0 + SCR_INSET, SCR_X1, sy1 - SCR_INSET, surface())

    face(d, SCR_X0 * GRID, (SCR_Y0 - hop + squash) * GRID, i, n, p)


# ── the faces ─────────────────────────────────────────────────
# The face grid is 13x12 units, which is odd across on purpose: a pair of 2-wide
# eyes at u=3 and u=8 leaves 3 units of margin on either side, so every pair
# below is symmetric without fudging. The mouths lean on the same trick — for a
# smile the corners sit a unit *above* the middle, for a frown a unit below.

def face_standby(d, ox, oy, i, n, p):
    """Two upright bars, which collapse into a line to blink."""
    if p.get("blink"):
        unit(d, ox, oy, 3, 5, 3, 1, INK)
        unit(d, ox, oy, 7, 5, 3, 1, INK)
    else:
        unit(d, ox, oy, 4, 4, 1, 3, INK)
        unit(d, ox, oy, 8, 4, 1, 3, INK)


def face_thinking(d, ox, oy, i, n, p):
    dx, dy = p.get("eyes", (0, 0))
    for u in (3, 8):
        unit(d, ox, oy, u + dx, 4 + dy, 2, 2, INK)
    unit(d, ox, oy, 6, 7, 1, 1, INK)
    for u in (1, 11):
        unit(d, ox, oy, u, 6, 1, 1, BLUSH)
    for k in range(p.get("dots", 0)):
        unit(d, ox, oy, 10 + k, 9, 1, 1, INK)


def face_coding(d, ox, oy, i, n, p):
    # One unit of gap between the glyphs, or they run together into a zigzag:
    # 3 + 1 + 3 + 1 + 3 = 11 of the 13 units the screen is wide. Tall and thin
    # rather than short and fat: three units is not enough width for a chevron
    # to have thick arms *and* read as a chevron — twice now it has come out
    # looking like a brace.
    glyph(d, ox, oy, 1, 2, ("..#", "..#", ".#.", "#..", ".#.", "..#", "..#"))
    glyph(d, ox, oy, 5, 2, ("..#", "..#", ".#.", ".#.", "#..", "#..", "#.."))
    glyph(d, ox, oy, 9, 2, ("#..", "#..", ".#.", "..#", ".#.", "#..", "#.."))
    # A line of "code" scrolling under it. Words are dropped as they cross the
    # screen edge rather than wrapped, so nothing pops mid-screen.
    for k in range(-1, 5):
        u = 1 + k * 3 - p.get("scroll", 0) % 3
        if 0 <= u and u + 2 <= FACE_U:
            unit(d, ox, oy, u, 10, 2, 1, INK)
    if p.get("cursor"):
        unit(d, ox, oy, 11, 9, 1, 2, INK)


def face_focus(d, ox, oy, i, n, p):
    """Glasses, and eyes reading behind them — the `face` theme's working face.

    At panel size the reference's `</>` is a wall of chevrons rather than a
    face, so that shell works in glasses instead (see pack()). The frames are
    drawn in the pet's shadow orange, not ink: a pupil is one unit, and a black
    pupil against a black frame is a thicker corner, not an eye. Reading is the
    animation — the pupil goes along one line of the lens and down to the next,
    the way eyes go over code.
    """
    px, py = p.get("read", (0, 0))
    for u in (1, 8):
        unit(d, ox, oy, u, 2, 4, 1, SHADE)
        unit(d, ox, oy, u, 5, 4, 1, SHADE)
        unit(d, ox, oy, u, 3, 1, 2, SHADE)
        unit(d, ox, oy, u + 3, 3, 1, 2, SHADE)
    unit(d, ox, oy, 5, 3, 3, 1, SHADE)              # the bridge
    if not p.get("blink"):
        for u in (2, 9):
            unit(d, ox, oy, u + px, 3 + py, 1, 1, INK)
    unit(d, ox, oy, 5, 8, 3, 1, INK)                # a mouth set in concentration
    if p.get("cursor"):
        unit(d, ox, oy, 11, 9, 1, 2, INK)


def face_done(d, ox, oy, i, n, p):
    """Sparkle eyes: a plus of ink with the screen colour left in the corners."""
    for u in (2, 8):
        unit(d, ox, oy, u, 3, 3, 3, INK)
        for du, dv in ((0, 0), (2, 0), (0, 2), (2, 2)):
            unit(d, ox, oy, u + du, 3 + dv, 1, 1, surface())
    for u in (1, 11):
        unit(d, ox, oy, u, 6, 1, 1, BLUSH)
    smile(d, ox, oy)
    if p.get("sparkle"):
        unit(d, ox, oy, 0, 0, 1, 1, INK)
        unit(d, ox, oy, 12, 10, 1, 1, INK)


def face_error(d, ox, oy, i, n, p):
    """The one expression the reference sheet does not have: a frown and eyes
    crossed out, which is what a failed turn should look like."""
    for u in (2, 8):
        glyph(d, ox, oy, u, 3, ("#.#", ".#.", "#.#"))
    frown(d, ox, oy)


def face_sleeping(d, ox, oy, i, n, p):
    # Eyes sit left of centre to leave the right-hand column free for the z,
    # which rises the height of the screen without ever crossing them.
    for u in (1, 5):
        unit(d, ox, oy, u, 5, 3, 1, INK)
    glyph(d, ox, oy, 9, 6 - (p.get("zed", 0) // 2), ("###", "..#", ".#.", "#..", "###"))


def face_happy(d, ox, oy, i, n, p):
    for u in (2, 8):
        glyph(d, ox, oy, u, 4, (".#.", "#.#"))
    for u in (1, 11):
        unit(d, ox, oy, u, 6, 1, 1, BLUSH)
    smile(d, ox, oy)


def face_slack(d, ox, oy, i, n, p):
    """Sunglasses, with a glint that crosses the lenses once a loop."""
    for u in (1, 8):
        unit(d, ox, oy, u, 3, 4, 3, INK)
    unit(d, ox, oy, 5, 4, 3, 1, INK)             # the bridge
    g = p.get("glint")
    if g is not None:
        for u in (1, 8):
            unit(d, ox, oy, u + 1 + g, 3, 1, 1, surface())
    unit(d, ox, oy, 5, 8, 3, 1, INK)


def face_grin(d, ox, oy, i, n, p):
    """The sheet's closed-eye laugh: the eyes curve the other way from happy's,
    and the mouth drops open at the top of the hop."""
    for u in (2, 8):
        glyph(d, ox, oy, u, 4, ("#.#", ".#."))
    for u in (1, 11):
        unit(d, ox, oy, u, 6, 1, 1, BLUSH)
    unit(d, ox, oy, 4, 7, 1, 1, INK)
    unit(d, ox, oy, 8, 7, 1, 1, INK)
    unit(d, ox, oy, 5, 9 if p.get("open") else 8, 3, 1, INK)


def face_shine(d, ox, oy, i, n, p):
    """Diamond sparkles for eyes, drawn hollow so the screen shows through as a
    glint, filling in solid for a couple of frames as if catching the light."""
    for u in (1, 8):
        for dv, row in enumerate(("..#..", ".#.#.", "#...#", ".#.#.", "..#..")):
            for du, ch in enumerate(row):
                if ch == "#":
                    unit(d, ox, oy, u + du, 1 + dv, 1, 1, INK)
        if p.get("twinkle"):
            for dv in range(3):
                unit(d, ox, oy, u + 1, 2 + dv, 3, 1, INK)
    for u in (1, 11):
        unit(d, ox, oy, u, 6, 1, 1, BLUSH)
    smile(d, ox, oy)


def face_blank(d, ox, oy, i, n, p):
    """Two dots and a flat line — the last face on the sheet, and the only one
    that says nothing at all."""
    if not p.get("blink"):
        unit(d, ox, oy, 4, 4, 1, 1, INK)
        unit(d, ox, oy, 8, 4, 1, 1, INK)
    unit(d, ox, oy, 5, 7, 3, 1, INK)


FACE = {
    "standby": face_standby,
    "thinking": face_thinking,
    "coding": face_coding,
    "focus": face_focus,
    "done": face_done,
    "error": face_error,
    "sleeping": face_sleeping,
    "happy": face_happy,
    "slack": face_slack,
    "grin": face_grin,
    "shine": face_shine,
    "blank": face_blank,
}


# ── the animations ────────────────────────────────────────────
# (suffix, face, frames, delay ms). The suffix is the sticker name mktheme
# matches on; --pack below is where those names get pointed at device states.

STATES = [
    ("standby", "standby", 24, 120),     # idle: the pose the panel rests on
    ("thinking", "thinking", 16, 100),
    ("coding", "coding", 20, 80),        # working
    ("focus", "focus", 20, 80),          # working, in the panel shell — see pack()
    ("done", "done", 16, 80),
    ("error", "error", 12, 90),
    ("sleeping", "sleeping", 24, 150),   # the nap
    # Spares, in the order the reference sheet lists its "more expressions":
    # the first of those four is happy, which is already above.
    ("happy", "happy", 16, 70),
    ("slack", "slack", 16, 120),
    ("grin", "grin", 16, 70),
    ("shine", "shine", 16, 90),
    ("blank", "blank", 24, 140),
]


def pose_standby(i, n):
    # Two slow breaths a loop, and one blink: the pet spends hours here, so the
    # motion is small and the blink is what keeps it looking awake.
    return {"squash": (0, 0, 1, 2, 2, 1, 0, 0)[i % 8], "blink": i in (19, 20)}


def pose_thinking(i, n):
    # The gaze goes first, then the dots arrive one at a time.
    gaze = ((0, 0), (0, -1), (0, -1), (1, -1), (1, 0), (0, 0), (-1, 0), (-1, -1))
    return {"eyes": gaze[i * len(gaze) // n], "dots": (i % 12) // 4}


def pose_coding(i, n):
    return {"scroll": i, "cursor": (i // 5) % 2, "squash": 1 if i % 6 == 5 else 0}


def pose_focus(i, n):
    # Two frames a glance, along one line of the lens and down to the next, the
    # way eyes read; a blink before the loop comes round; the cursor on a beat
    # of its own; and a nod every fourth frame, which is the typing.
    scan = ((0, 0), (1, 0), (0, 1), (1, 1))
    return {"read": scan[(i // 2) % 4], "cursor": (i // 4) % 2,
            "blink": i in (16, 17), "squash": 1 if i % 4 == 3 else 0}


def pose_done(i, n):
    # Two small hops, and the sparkles swap corners on alternate frames.
    return {"hop": (0, 1, 2, 2, 1, 0, 0, 0)[i % 8], "sparkle": i % 2}


def pose_error(i, n):
    # A vibrate, not a shake — see the 216 px note in the docstring. It reads as
    # a flinch: the whole pet stiffens and lets go.
    return {"squeeze": (0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0)[i % 12]}


def pose_sleeping(i, n):
    return {"squash": (0, 1, 2, 3, 3, 2, 1, 0)[i % 8], "zed": i % 12}


def pose_happy(i, n):
    return {"hop": (0, 1, 2, 2, 1, 0, 1, 0)[i % 8],
            "squash": 1 if i % 4 == 3 else 0}


def pose_slack(i, n):
    return {"squash": (0, 0, 1, 1, 0, 0, 0, 0)[i % 8],
            "glint": i if i < 3 else None}


def pose_grin(i, n):
    # The mouth opens at the top of the hop, which is the whole animation.
    hop = (0, 1, 2, 2, 1, 0, 0, 0)[i % 8]
    return {"hop": hop, "open": hop == 2}


def pose_shine(i, n):
    # A slow breath under a sparkle that catches the light every sixth frame.
    return {"squash": (0, 0, 1, 1, 0, 0, 0, 0)[i % 8], "twinkle": (i % 6) < 2}


def pose_blank(i, n):
    # The most patient loop here: one breath and one blink in three and a half
    # seconds, which is what "no expression at all" has to look like.
    return {"squash": (0, 0, 1, 1, 0, 0)[i % 6], "blink": i in (19, 20)}


POSE = {
    "standby": pose_standby,
    "thinking": pose_thinking,
    "coding": pose_coding,
    "focus": pose_focus,
    "done": pose_done,
    "error": pose_error,
    "sleeping": pose_sleeping,
    "happy": pose_happy,
    "slack": pose_slack,
    "grin": pose_grin,
    "shine": pose_shine,
    "blank": pose_blank,
}


def render(suffix, i, n):
    im = Image.new("P", (PANEL, PANEL), BG)
    im.putpalette(PAL_FLAT)
    d = ImageDraw.Draw(im)
    draw_pet(d, POSE[suffix](i, n), FACE[suffix], i, n)
    return im


# ── checks ────────────────────────────────────────────────────

def check(suffix, frames):
    """Refuse to emit art the device or the pipeline would choke on.

    getbbox() is exact here: index 0 is the field, so "non-zero pixel" means
    "art" and the box it returns is the artwork's own.
    """
    xs0 = ys0 = 10 ** 9
    xs1 = ys1 = -1
    for im in frames:
        box = im.getbbox()
        if box is None:
            sys.exit(f"error: {suffix}: frame is empty")
        xs0, ys0 = min(xs0, box[0]), min(ys0, box[1])
        xs1, ys1 = max(xs1, box[2] - 1), max(ys1, box[3] - 1)

    w, h = xs1 - xs0 + 1, ys1 - ys0 + 1
    if SHELL == "panel":
        # The bezel is the panel's edge, so the art *is* the panel: same size,
        # same place, and nothing for the converter to fit or centre — which is
        # why pack() hands this shell `--margin 0`.
        if (xs0, ys0, w, h) != (0, 0, PANEL, PANEL):
            sys.exit(f"error: {suffix}: the panel shell draws {w}x{h} at "
                     f"({xs0},{ys0}); it has to be the whole panel at (0,0)")
        return (xs0, ys0, w, h)
    # The whole pet has to fill the box exactly — see the docstring. A shell
    # that draws only the screen, or only the face, is narrower by construction
    # (it keeps the face's own size and grid), and at fit scale 1 a smaller box
    # is fine: the converter centres it on the panel.
    if SHELL == "full":
        if w != PANEL - 2 * MARGIN:
            sys.exit(f"error: {suffix}: artwork is {w} px wide, must be exactly "
                     f"{PANEL - 2 * MARGIN} — the converter's fit scale has to be 1")
    elif w > PANEL - 2 * MARGIN:
        sys.exit(f"error: {suffix}: artwork is {w} px wide, must be at most "
                 f"{PANEL - 2 * MARGIN}")
    if h > PANEL - 2 * MARGIN or h % 8:
        sys.exit(f"error: {suffix}: artwork is {h} px tall, must be a multiple "
                 f"of 8 and at most {PANEL - 2 * MARGIN}")
    return (xs0, ys0, w, h)


# ── output ────────────────────────────────────────────────────

def write_gif(path, frames, delays):
    """Write the frames out as GIF89a, by hand.

    Pillow is the obvious tool and the wrong one here: its encoder writes every
    frame after the first as a delta carrying its *own* colour table, trimmed to
    the colours that frame uses (a 24-frame animation comes back with 226-entry
    tables). gif2caf reads each frame through a 256-entry palette and dies with
    an IndexError on anything shorter, so a pack built from Pillow-written GIFs
    cannot be converted at all. Pillow is also free to fold a frame that repeats
    its predecessor into it, which silently turns 24 authored frames into 13.

    Writing the container instead costs one small LZW encoder and buys exactness:
    full-size frames on one global table, the frame count that was authored, and
    the per-frame delays as written.
    """
    out = bytearray(b"GIF89a")
    out += struct.pack("<HH", PANEL, PANEL)
    out += bytes((0xF7, BG, 0))          # global table, 256 entries, 8 bits/px
    out += bytes(PAL_FLAT)               # ...padded, so the reader keeps all 256
    out += b"\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00"     # loop forever

    for im, ms in zip(frames, delays):
        cs = max(1, ms // 10)            # GIF stores delays in centiseconds
        # The transparency flag is set, on an index the palette never uses, and
        # nothing here is actually transparent. It is there because Pillow reads
        # a GIF's first frame as palette data but every later frame as RGB
        # (LOADING_STRATEGY defaults to RGB_AFTER_FIRST), and gif2caf pulls each
        # frame back to indices with convert("P") — a 256-entry palette when the
        # frame is RGBA, a trimmed 226-entry one when it is RGB, and the latter
        # is an IndexError in the converter. The field is transparent in the
        # .caf, by index 0, not in the GIF.
        out += b"\x21\xf9\x04\x05" + struct.pack("<H", cs) + bytes((UNUSED, 0))
        out += b"\x2c" + struct.pack("<HHHH", 0, 0, PANEL, PANEL) + bytes((0,))
        out += bytes((8,))               # LZW minimum code size
        blob = _lzw_literals(im.tobytes())
        for i in range(0, len(blob), 255):
            chunk = blob[i:i + 255]
            out += bytes((len(chunk),)) + chunk
        out += b"\x00"                   # end of this frame's data blocks

    out += b"\x3b"                       # trailer
    with open(path, "wb") as fh:
        fh.write(out)


# LZW codes that are not pixel values. 8-bit pixels start the code space at 9
# bits wide.
_CLEAR, _END = 256, 257
_CLEAR_EVERY = 200   # literals between clear codes — see _lzw_literals


def _lzw_literals(indices):
    """GIF LZW, literals only.

    A real encoder builds a dictionary and emits back-references to it. This one
    emits every pixel as a literal and drops a clear code often enough that the
    decoder's table never reaches the 512 entries where its codes would widen
    past 9 bits, so the width is fixed and the packing stays trivial. The result
    is valid GIF, several times larger than a compressed stream — and it is a
    build intermediate that never reaches the device, so being able to say
    exactly which bytes the converter will read is worth more than the size.
    """
    out = bytearray()
    bitbuf = nbits = 0

    def emit(code):
        nonlocal bitbuf, nbits
        bitbuf |= code << nbits
        nbits += 9
        while nbits >= 8:
            out.append(bitbuf & 0xFF)
            bitbuf >>= 8
            nbits -= 8

    emit(_CLEAR)
    run = 0
    for v in indices:
        emit(v)
        run += 1
        if run == _CLEAR_EVERY:
            emit(_CLEAR)
            run = 0
    emit(_END)
    if nbits:
        out.append(bitbuf & 0xFF)
    return bytes(out)


def check_gif(path, frames):
    """Read the file back the way gif2caf does, and insist it reads the same.

    gif2caf opens the GIF, seeks each frame and calls convert("P"), so what it
    sees is what this checks: a 256-entry palette (it indexes all of them, and a
    shorter one is an IndexError), one index that stands for the field, and the
    artwork's colours unchanged. Pillow answers convert("P") with a dither onto
    the web palette, so a colour off the cube would come back as speckle — that
    is the failure this exists to catch, before a pack is built from it.
    """
    im = Image.open(path)
    if im.n_frames != len(frames):
        sys.exit(f"error: {path}: {im.n_frames} frames in the file, "
                 f"{len(frames)} written")
    for i, want in enumerate(frames):
        im.seek(i)
        p = im.convert("P")
        pal, got = p.getpalette(), p.tobytes()
        if len(pal) < 768:
            sys.exit(f"error: {path}: frame {i} reads back a {len(pal)}-byte "
                     f"palette; gif2caf indexes all 256 entries")
        # Which index the field lands on is Pillow's business — a GIF with a
        # transparent entry can hand it back on any one of them. What gif2caf
        # needs is that it is *one* index, and that no artwork pixel shares it,
        # or that pixel is read as background and swallows the art.
        #
        # It is found by colour rather than read off the corner, which is how
        # this used to do it: the corner is field in every shell that leaves one
        # empty, but `panel` draws its bezel there. The field's *colour* is the
        # one thing that holds either way — load_frames pins it to index 0.
        #
        # `panel` has no field at all: its art covers every pixel of the frame,
        # and Pillow hands a covered frame back with a palette that need not
        # contain the field colour. There is nothing to look for, and the check
        # below turns into the opposite one — an artwork pixel on the field
        # would mean the shell failed to cover the panel.
        field = None
        if SHELL != "panel":
            for k in range(len(pal) // 3):
                if tuple(pal[k * 3:k * 3 + 3]) == PALETTE[BG]:
                    field = k
                    break
            if field is None:
                sys.exit(f"error: {path}: frame {i}: the field colour "
                         f"{PALETTE[BG]} is not in the palette Pillow handed back")
        for src, dst in zip(want.tobytes(), got):
            if src == BG:
                if field is None:
                    sys.exit(f"error: {path}: frame {i}: the art of the {SHELL} "
                             f"shell draws field pixels; it has to cover the "
                             f"panel")
                if dst != field:
                    sys.exit(f"error: {path}: frame {i}: field pixels came back "
                             f"on index {dst} as well as {field}")
            elif field is not None and dst == field:
                sys.exit(f"error: {path}: frame {i}: an artwork pixel shares the "
                         f"field index {field} and would be read as background")
            elif tuple(pal[dst * 3:dst * 3 + 3]) != PALETTE[src]:
                sys.exit(f"error: {path}: frame {i}: colour {PALETTE[src]} came "
                         f"back as {tuple(pal[dst * 3:dst * 3 + 3])} — is it off "
                         f"the 6x6x6 web-safe cube?")


def write_sheet(path, frames):
    """Every frame of one state in a grid — for looking at, not for shipping.

    A GIF viewer shows the first frame and nothing else, which is the one frame
    that says least about whether a motion works.
    """
    cols, cell = 6, PANEL
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * cell), (28, 24, 20))
    for k, im in enumerate(frames):
        sheet.paste(im.convert("RGB"), ((k % cols) * cell, (k // cols) * cell))
    sheet.save(path)


def pack(gif_dir, out_dir, prefix):
    """Hand the set to mktheme, which is the tool that knows how to make a pack.

    The mapping is explicit because the art here is named after the *face*
    (standby, coding, done, slack), not after the upstream sticker sets' names.

    `working` is the one state whose face depends on the shell. The pet's is
    the reference sheet's `</>`, which on the pet's screen is a glyph it shows
    you; blown up to the panel it is a wall of chevrons and not a face at all,
    so the `panel` shell works in glasses instead (face_focus).

    `panel` also needs `--margin 0` and `--opaque`: its art is the whole panel,
    so any margin would leave the converter fitting a 240 px picture into a
    216 px box (a scale of 0.9, which puts every edge in the art between two
    pixels), and its bezel reaches the frame's own border, where the converter
    looks for a background to replace.
    """
    working = "focus" if SHELL == "panel" else "coding"
    cmd = [sys.executable, MKTHEME, gif_dir, "-o", out_dir, "--prefix", prefix,
           "--no-upscale", "--budget", "2400",
           "--map", "idle=standby", "--map", f"working={working}",
           "--map", "done=done",
           "--spares", "happy,slack,grin,shine,blank"]
    if SHELL == "panel":
        cmd += ["--margin", "0", "--opaque"]
    print(f"\n$ {' '.join(cmd[1:])}")
    return subprocess.call(cmd)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", help="directory to write the GIFs into")
    ap.add_argument("--pack", metavar="DIR",
                    help="also build a theme pack there, via tools/mktheme.py")
    ap.add_argument("--prefix", default="origin",
                    help="sticker filename prefix (default: origin)")
    ap.add_argument("--states", default="",
                    help="comma-separated subset to draw (default: all)")
    ap.add_argument("--shell", choices=("full", "frame", "bare", "panel"),
                    default="full",
                    help="how much of the character to draw: the whole pet "
                         "(default), just the screen it has a face on, just the "
                         "face, or that screen filling the panel")
    ap.add_argument("--sheet", metavar="DIR",
                    help="write one contact sheet PNG per state here")
    args = ap.parse_args()

    set_shell(args.shell)

    if not args.out and not args.pack:
        ap.error("give -o (where the GIFs go), --pack (a theme to build), or both")
    wanted = [s.strip() for s in args.states.split(",") if s.strip()]
    out = args.out
    if not out:
        # Deliberately not inside the pack: a theme directory is what gets
        # zipped and pushed to the device, so stray GIFs there would eat
        # partition space the animations need.
        out = tempfile.mkdtemp(prefix="origin-gifs-")

    name_of = {s[0]: s for s in STATES}
    for bad in [w for w in wanted if w not in name_of]:
        ap.error(f"unknown state {bad!r} — choose from {', '.join(name_of)}")
    todo = [s for s in STATES if not wanted or s[0] in wanted]

    os.makedirs(out, exist_ok=True)
    if args.sheet:
        os.makedirs(args.sheet, exist_ok=True)

    total = 0
    for suffix, face, n, ms in todo:
        frames = [render(suffix, i, n) for i in range(n)]
        check(suffix, frames)
        path = os.path.join(out, f"{args.prefix}-{suffix}.gif")
        write_gif(path, frames, [ms] * n)
        check_gif(path, frames)
        size = os.path.getsize(path)
        total += size
        print(f"  {suffix:<9} {n:>3} frames  {ms} ms  {n * ms / 1000:>4.1f}s  "
              f"{size / 1024:>5.0f} KB")
        if args.sheet:
            write_sheet(os.path.join(args.sheet, f"{suffix}.png"), frames)

    print(f"{len(todo)} state(s), {total / 1024:.0f} KB, written to {out}")

    if args.pack:
        return pack(out, args.pack, args.prefix)
    return 0


if __name__ == "__main__":
    sys.exit(main())
