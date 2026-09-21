// Minimal GFX layer over ESP-IDF's official esp_lcd ST7789 driver.
#include "display.h"
#include "font5x7.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char* TAG = "display";

// Outstanding-transfer slots for the streaming path (see streamBegin()).
// Matches the DMA queue depth configured in init().
#define STREAM_DEPTH 4
// Max rows the animation player pushes per streamRect(); must match anim.cpp's
// BAND_ROWS so max_transfer_sz covers one band.
#define STREAM_BAND_ROWS 8

// One transfer's worth of pixels for the shared scratch buffer. The animation
// player passes its own bands to streamRect(), which needs max_transfer_sz to
// cover them, so the bus is sized for the larger of the two.
#define FILL_PIXELS (240 * 8)   // pixels per transfer
#define MAX_TRANSFER_PIXELS (240 * STREAM_BAND_ROWS)

static SemaphoreHandle_t s_flush_done = nullptr;
static SemaphoreHandle_t s_dma_slots = nullptr;    // free DMA slots for streaming
static SemaphoreHandle_t s_panel_lock = nullptr;   // serialises panel access between tasks
static uint16_t          s_fill_buf[FILL_PIXELS];

// draw_bitmap() queues the pixels for DMA and returns immediately; this
// signals completion so the scratch buffer can be safely refilled.
//
// s_flush_done is a *counting* semaphore: every completed transfer gives once,
// so each waiter receives exactly the completion it queued. With a binary
// semaphore two interleaved tasks could absorb each other's give and one would
// block forever (which is why the callers below also hold s_panel_lock).
static bool IRAM_ATTR onColorTransDone(esp_lcd_panel_io_handle_t io,
                                       esp_lcd_panel_io_event_data_t* edata,
                                       void* user_ctx) {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(s_flush_done, &hp);
  xSemaphoreGiveFromISR(s_dma_slots, &hp);
  return hp == pdTRUE;
}

Display::Display(int8_t cs, int8_t dc, int8_t rst, int8_t mosi, int8_t sclk)
    : _cs(cs), _dc(dc), _rst(rst), _mosi(mosi), _sclk(sclk) {}

void Display::init(uint16_t w, uint16_t h) {
  _width = w;
  _height = h;

  s_flush_done = xSemaphoreCreateCounting(STREAM_DEPTH, 0);
  ESP_ERROR_CHECK(s_flush_done != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
  s_dma_slots = xSemaphoreCreateCounting(STREAM_DEPTH, STREAM_DEPTH);
  ESP_ERROR_CHECK(s_dma_slots != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
  s_panel_lock = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(s_panel_lock != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = _mosi;
  buscfg.miso_io_num = -1;
  buscfg.sclk_io_num = _sclk;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = MAX_TRANSFER_PIXELS * 2;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = (gpio_num_t)_cs;
  io_config.dc_gpio_num = (gpio_num_t)_dc;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 26666666; // GPIO-matrix routing limit (APB 80MHz / 3)
  io_config.trans_queue_depth = 4;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.on_color_trans_done = onColorTransDone;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                           &io_config, &_io));

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = (gpio_num_t)_rst;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;
  panel_config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE; // matches color565()
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(_io, &panel_config, &_panel));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(_panel, true));
  setRotation(0);
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(_panel, true));

  ESP_LOGI(TAG, "ST7789 %dx%d initialized (esp_lcd)", _width, _height);
}

void Display::setSPISpeed(uint32_t hz) {
  // Clock is fixed at init time; kept for API compatibility with the sketch.
  (void)hz;
}

void Display::setRotation(uint8_t m) {
  _rotation = m & 3;

  bool mx = false, my = false, mv = false;
  int  x_gap = 0, y_gap = 0;

  // Mirrors Adafruit_ST7789 for a 240x240 panel inside a 240x320 GRAM:
  // _rowstart = 80, _rowstart2 = 0, _colstart = _colstart2 = 0.
  switch (_rotation) {
    case 0:  mx = true;  my = true;  mv = false; x_gap = 0;  y_gap = 80; break;
    case 1:  mx = false; my = true;  mv = true;  x_gap = 80; y_gap = 0;  break;
    case 2:  mx = false; my = false; mv = false; x_gap = 0;  y_gap = 0;  break;
    default: mx = true;  my = false; mv = true;  x_gap = 0;  y_gap = 0;  break;
  }

  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(_panel, mv));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(_panel, mx, my));
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(_panel, x_gap, y_gap));
}

// Fills a rectangle with a solid colour by streaming it through a shared
// scratch buffer. draw_bitmap() is asynchronous, so each transfer is waited on
// before the buffer is refilled — which also keeps this call synchronous, as
// the rest of the drawing code expects.
void Display::fillArea(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (w <= 0 || h <= 0) return;

  size_t row_px   = (size_t)w;
  size_t rows_max = FILL_PIXELS / row_px;
  if (rows_max == 0) return;   // wider than the scratch buffer

  // Hold the panel for the whole transfer: every waiter takes one completion
  // off s_flush_done, so two tasks interleaving draw_bitmap/take without the
  // lock could have one steal the other's signal and block it forever.
  xSemaphoreTake(s_panel_lock, portMAX_DELAY);

  for (int16_t row = 0; row < h; row += (int16_t)rows_max) {
    size_t rows = (size_t)(h - row);
    if (rows > rows_max) rows = rows_max;

    size_t n = rows * row_px;
    for (size_t i = 0; i < n; i++) s_fill_buf[i] = color;

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(_panel, x, y + row,
                                              x + w, y + row + (int16_t)rows,
                                              s_fill_buf));
    xSemaphoreTake(s_flush_done, portMAX_DELAY);
  }

  xSemaphoreGive(s_panel_lock);
}

// Blits a raw RGB565 bitmap supplied by the caller (e.g. a boot-splash bitmap
// or the serial "img" command).
//
// Each band is staged through the shared scratch buffer rather than being handed
// to the panel directly. The bus runs with DMA (see SPI_DMA_CH_AUTO in init()),
// and this is called with artwork that lives in flash (.rodata), which the DMA
// engine cannot read: a direct pointer there fails the transfer, the
// on_color_trans_done callback never fires, and the wait below blocks for good.
//
// draw_bitmap() is asynchronous, so each band is waited on before returning —
// which also means the scratch buffer is free again once this returns.
void Display::drawImage565(int16_t x, int16_t y, int16_t w, int16_t h,
                           const uint16_t* data) {
  if (w <= 0 || h <= 0 || data == nullptr) return;

  size_t row_px   = (size_t)w;
  size_t rows_max = FILL_PIXELS / row_px;
  if (rows_max == 0) return;   // wider than the scratch buffer

  xSemaphoreTake(s_panel_lock, portMAX_DELAY);

  for (int16_t row = 0; row < h; row += (int16_t)rows_max) {
    size_t rows = (size_t)(h - row);
    if (rows > rows_max) rows = rows_max;

    size_t n = rows * row_px;
    memcpy(s_fill_buf, data + (size_t)row * row_px, n * sizeof(uint16_t));

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(_panel, x, y + row,
                                              x + w, y + row + (int16_t)rows,
                                              s_fill_buf));
    xSemaphoreTake(s_flush_done, portMAX_DELAY);
  }

  xSemaphoreGive(s_panel_lock);
}

// ── Batched streaming path (animation player) ──────────────────
//
// drawImage565() takes the panel lock and blocks on the DMA-completion
// semaphore for *every* rectangle. The animation player pushes hundreds of
// small rectangles per frame, and that per-call overhead measured ~284 us —
// far more than the bytes being moved. These take the lock once for the whole
// frame and pipeline through the DMA queue, blocking only when it is full.

void Display::streamBegin() {
  xSemaphoreTake(s_panel_lock, portMAX_DELAY);
}

void Display::streamRect(int16_t x, int16_t y, int16_t w, int16_t h,
                         const uint16_t* data) {
  if (w <= 0 || h <= 0 || data == nullptr) return;

  // Wait for a free DMA slot. The completion callback returns one, so this
  // only blocks once STREAM_DEPTH rectangles are in flight.
  xSemaphoreTake(s_dma_slots, portMAX_DELAY);

  // Unlike drawImage565() the caller's buffer is handed straight to the DMA
  // engine — it must be DRAM-resident and unchanged until streamEnd().
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(_panel, x, y, x + w, y + h, data));
}

void Display::streamEnd() {
  // Drain: every outstanding transfer returns its slot, so taking them all
  // means the panel has finished, and the caller's buffers are reusable.
  for (int i = 0; i < STREAM_DEPTH; i++) {
    xSemaphoreTake(s_dma_slots, portMAX_DELAY);
  }
  for (int i = 0; i < STREAM_DEPTH; i++) {
    xSemaphoreGive(s_dma_slots);
  }
  xSemaphoreGive(s_panel_lock);
}

void Display::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= _width || y >= _height) return;
  fillArea(x, y, 1, 1, color);
}

void Display::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (x >= _width || y >= _height) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (w <= 0 || h <= 0) return;
  if (x + w > _width) w = _width - x;
  if (y + h > _height) h = _height - y;
  fillArea(x, y, w, h, color);
}

void Display::fillScreen(uint16_t color) {
  fillRect(0, 0, _width, _height, color);
}

static inline void swap_i16(int16_t& a, int16_t& b) { int16_t t = a; a = b; b = t; }

void Display::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  int16_t steep = (y1 > y0 ? y1 - y0 : y0 - y1) > (x1 > x0 ? x1 - x0 : x0 - x1);
  if (steep) { swap_i16(x0, y0); swap_i16(x1, y1); }
  if (x0 > x1) { swap_i16(x0, x1); swap_i16(y0, y1); }
  int16_t dx = x1 - x0;
  int16_t dy = y1 > y0 ? y1 - y0 : y0 - y1;
  int16_t err = dx / 2;
  int16_t ystep = (y0 < y1) ? 1 : -1;
  for (; x0 <= x1; x0++) {
    if (steep) drawPixel(y0, x0, color);
    else drawPixel(x0, y0, color);
    err -= dy;
    if (err < 0) { y0 += ystep; err += dx; }
  }
}

void Display::fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners,
                               int16_t delta, uint16_t color) {
  int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r, px = x, py = y;
  delta++;
  while (x < y) {
    if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
    x++; ddF_x += 2; f += ddF_x;
    if (x < (y + 1)) {
      if (corners & 1) drawFastVLine(x0 + x, y0 - y, 2 * y + delta, color);
      if (corners & 2) drawFastVLine(x0 - x, y0 - y, 2 * y + delta, color);
    }
    if (y != py) {
      if (corners & 1) drawFastVLine(x0 + py, y0 - px, 2 * px + delta, color);
      if (corners & 2) drawFastVLine(x0 - py, y0 - px, 2 * px + delta, color);
      py = y; px = x;
    }
  }
}

void Display::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  drawFastVLine(x0, y0 - r, 2 * r + 1, color);
  fillCircleHelper(x0, y0, r, 3, 0, color);
}

void Display::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                           int16_t x2, int16_t y2, uint16_t color) {
  int16_t a, b, y, last;
  if (y0 > y1) { swap_i16(y0, y1); swap_i16(x0, x1); }
  if (y1 > y2) { swap_i16(y2, y1); swap_i16(x2, x1); }
  if (y0 > y1) { swap_i16(y0, y1); swap_i16(x0, x1); }
  if (y0 == y2) {
    a = b = x0;
    if (x1 < a) a = x1; else if (x1 > b) b = x1;
    if (x2 < a) a = x2; else if (x2 > b) b = x2;
    drawFastHLine(a, y0, b - a + 1, color);
    return;
  }
  int16_t dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0,
          dx12 = x2 - x1, dy12 = y2 - y1;
  int32_t sa = 0, sb = 0;
  last = (y1 == y2) ? y1 : y1 - 1;
  for (y = y0; y <= last; y++) {
    a = x0 + sa / dy01; b = x0 + sb / dy02;
    sa += dx01; sb += dx02;
    if (a > b) swap_i16(a, b);
    drawFastHLine(a, y, b - a + 1, color);
  }
  sa = (int32_t)dx12 * (y - y1);
  sb = (int32_t)dx02 * (y - y0);
  for (; y <= y2; y++) {
    a = x1 + sa / dy12; b = x0 + sb / dy02;
    sa += dx12; sb += dx02;
    if (a > b) swap_i16(a, b);
    drawFastHLine(a, y, b - a + 1, color);
  }
}

// ── Text ───────────────────────────────────────────────────────
void Display::setCursor(int16_t x, int16_t y) { _cursor_x = x; _cursor_y = y; }

void Display::drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color,
                       uint16_t bg, uint8_t size) {
  if (x >= _width || y >= _height || (x + 6 * size - 1) < 0 || (y + 8 * size - 1) < 0) return;
  for (int8_t i = 0; i < 5; i++) {
    uint8_t line = FONT5X7[c * 5 + i];
    for (int8_t j = 0; j < 8; j++, line >>= 1) {
      if (line & 1) {
        if (size == 1) drawPixel(x + i, y + j, color);
        else fillRect(x + i * size, y + j * size, size, size, color);
      } else if (bg != color) {
        if (size == 1) drawPixel(x + i, y + j, bg);
        else fillRect(x + i * size, y + j * size, size, size, bg);
      }
    }
  }
  if (bg != color) {
    if (size == 1) drawFastVLine(x + 5, y, 8, bg);
    else fillRect(x + 5 * size, y, size, 8 * size, bg);
  }
}

size_t Display::write(uint8_t c) {
  if (c == '\n') { _cursor_y += 8 * _textsize; _cursor_x = 0; }
  else if (c == '\r') { _cursor_x = 0; }
  else {
    drawChar(_cursor_x, _cursor_y, c, _textcolor, _textbg, _textsize);
    _cursor_x += 6 * _textsize;
  }
  return 1;
}

size_t Display::print(const char* s) {
  size_t n = 0;
  while (*s) { write((uint8_t)*s++); n++; }
  return n;
}
size_t Display::print(char c) { write((uint8_t)c); return 1; }
size_t Display::print(int n) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", n);
  return print(buf);
}
size_t Display::print(unsigned n) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%u", n);
  return print(buf);
}
int Display::printf(const char* fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  print(buf);
  return n;
}
