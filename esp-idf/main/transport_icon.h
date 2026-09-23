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

// 16x16. Chosen because the animation's bounding box never reaches the top-right
// corner on any shipped theme (see the note in main.cpp), and 16 px is the
// smallest size at which a rune and a trident stay distinguishable.
#define TI_W 16
#define TI_H 16

// Draws `glyph` at (x,y): `fg` where the mask is set, `bg` elsewhere. The
// rectangle is opaque — the panel has no alpha, and drawImage565 has no colour
// key — so `bg` must be the colour the panel currently shows behind it.
void transportIconDraw(Display& d, int16_t x, int16_t y, ti_glyph_t glyph,
                       uint16_t fg, uint16_t bg);
