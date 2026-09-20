// Minimal GFX layer for the ST7789 panel.
// Reimplements just the subset of Adafruit_GFX that the Clawd Mochi sketch
// uses, on top of ESP-IDF's official esp_lcd driver stack
// (esp_lcd_panel_io_spi + esp_lcd_panel_st7789).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#define ST77XX_BLACK 0x0000
#define ST77XX_WHITE 0xFFFF

class Display {
public:
  Display(int8_t cs, int8_t dc, int8_t rst, int8_t mosi, int8_t sclk);

  // lifecycle
  void init(uint16_t w, uint16_t h);
  void setRotation(uint8_t m);
  void setSPISpeed(uint32_t hz);

  // colour
  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  // primitives
  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void fillScreen(uint16_t color);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    fillRect(x, y, w, 1, color);
  }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    fillRect(x, y, 1, h, color);
  }
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                    int16_t x2, int16_t y2, uint16_t color);

  // Blit a raw RGB565 bitmap. The data is little-endian, matching both the
  // panel's configured RAMCTRL endianness and the serial "img" wire format.
  void drawImage565(int16_t x, int16_t y, int16_t w, int16_t h,
                    const uint16_t* data);

  // Batched blit for the animation player, which pushes hundreds of small
  // rectangles per frame. drawImage565() blocks on every one of them (lock +
  // DMA-completion wait), which measured ~284 us per call — the dominant cost.
  // This takes the panel once for the whole frame and pipelines through the DMA
  // queue, waiting only when the queue is full.
  //
  // `data` must be DMA-capable and stay valid until streamEnd(), so callers
  // must pass their own rotating buffers rather than a shared scratch buffer.
  void streamBegin();
  void streamRect(int16_t x, int16_t y, int16_t w, int16_t h,
                  const uint16_t* data);
  void streamEnd();

  // text
  void setCursor(int16_t x, int16_t y);
  void setTextColor(uint16_t c) { _textcolor = _textbg = c; }
  void setTextSize(uint8_t s) { _textsize = s; }
  size_t write(uint8_t c);
  size_t print(const char* s);
  size_t print(char c);
  size_t print(int n);
  size_t print(unsigned n);
  int printf(const char* fmt, ...);

  int16_t width() const { return _width; }
  int16_t height() const { return _height; }

private:
  // Every primitive decomposes into a solid-colour rectangle fill.
  void fillArea(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color,
                uint16_t bg, uint8_t size);
  void fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners,
                        int16_t delta, uint16_t color);

  int8_t _cs, _dc, _rst, _mosi, _sclk;
  esp_lcd_panel_io_handle_t _io = nullptr;
  esp_lcd_panel_handle_t _panel = nullptr;

  uint16_t _width = 240, _height = 240;
  uint8_t _rotation = 0;

  int16_t _cursor_x = 0, _cursor_y = 0;
  uint16_t _textcolor = ST77XX_WHITE, _textbg = ST77XX_WHITE;
  uint8_t _textsize = 1;
};
