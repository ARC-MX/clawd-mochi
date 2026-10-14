// The little transport glyphs shown in the animation view's corner.
//
// Each glyph is a 16x16 one-bit mask composited into a small RGB565 buffer and
// blitted in a single drawImage565(). That is deliberate: every Display
// primitive is one fillArea(), i.e. one panel-lock acquisition and one DMA wait
// (~280 us), and a 16x16 glyph of 40-odd one-pixel runs would cost ~10 ms of
// panel time per repaint — stalling the animation player every tick. One blit
// is one lock and 512 bytes.
#pragma once

#include <stdint.h>
#include "display.h"

typedef enum { TI_USB = 0, TI_BT, TI_WIFI, TI_GLYPH_COUNT } ti_glyph_t;

// 16x16. Chosen because it fits in the corner on every theme whose art is a
// subject on a field, and because 16 px is the smallest size at which a rune and
// a trident stay distinguishable.
#define TI_W 16
#define TI_H 16

// Where it sits: the top-right corner, 18 px in from the right edge. The panel's
// width is main.cpp's (DISP_W), and that is where the check that these two agree
// lives — this module has no business including it.
#define TI_X 222                       // DISP_W - 18
#define TI_Y 2

// What the corner should show — `shown` false means nothing at all. main.cpp
// decides (which link, live or lost) and sets it here; the two painters below
// read it.
//
// `colour` has to read against whatever art is behind the corner, because the
// icon has no background of its own any more (see transportIconBlit) — the pet's
// ink colour is the one that does, on every theme shipped so far.
void transportIconSet(bool shown, ti_glyph_t glyph, uint16_t colour);

// Draws what transportIconSet() last stored, straight at the panel. This is the
// housekeeping tick's path, for the times the animation player is not pushing
// frames.
//
// It writes the glyph's own pixels and nothing else — no background patch. That
// patch was the last thing in this corner that flickered: the player's bands
// carry the artwork there, the icon module redrew the patch on top of it every
// 200 ms, and the panel showed the patch appearing and disappearing at 5 Hz
// around a steady glyph. Leaves the art visible behind, which is also what
// transportIconStamp() does, so the two painters cannot disagree.
void transportIconBlit(Display& d, int16_t x, int16_t y);

// Stamps the icon into a band buffer the animation player is about to push:
// `out` is `rows` rows of `stride` pixels, the band's first panel row is `bandY`
// and its first panel column `x0`. Register it with animSetBandCallback (anim.h)
// — this is the shape that works on art which fills the panel, because the
// player pushes whole bands and would otherwise paint the corner away with
// every frame.
//
// Like transportIconBlit(), only the glyph's own pixels are written, so the icon
// sits on the artwork instead of knocking an opaque hole in it.
//
// The price of having no background: nothing here can erase a glyph that has been
// replaced, because the colour behind it is not knowable from this module. It
// does not have to while the player is running — every frame lays the artwork
// back down in that corner and only the current glyph is stamped into it — but on
// a theme whose art stops short of the corner (clawd and origin: their bands end
// at x=227 and the icon runs to 237) the right-hand pixels of a replaced glyph
// stay there until something repaints them. The fix, if it is ever wanted, is for
// the stamp to keep a copy of the corner it overwrote, so the tick can put it
// back.
void transportIconStamp(uint16_t* out, int stride, int x0, int bandY, int rows);
