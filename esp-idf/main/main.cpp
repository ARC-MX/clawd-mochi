/*
 * ╔══════════════════════════════════════════════════════════════╗
 *   CLAWD MOCHI — ESP32-C3 + ST7789 240×240
 *   ESP-IDF port (C++)
 *
 *   Wiring:
 *     SDA → GPIO 10 (hardware SPI MOSI)
 *     SCL → GPIO 8  (hardware SPI SCK)   — strapping pin, see README
 *     RST → GPIO 2                       — strapping pin, needs a pull-up
 *     DC  → GPIO 1
 *     CS  → GPIO 4
 *     BL  → GPIO 3
 *     VCC → 3V3
 *     GND → GND
 *
 *   WiFi: "ClaWD-Mochi"  pw: clawd1234  → http://192.168.4.1
 *   (factory defaults — the web UI's device card changes both, and the same
 *    name is used for the BLE advertisement)
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

#include "display.h"
#include "cmd_queue.h"
#include "settings.h"
#include "transport_icon.h"
#include "logo_data.h"
#include "splash_text.h"
#include "splash_credit.h"
#include "esp_littlefs.h"
#include "ble_cli.h"
#include "anim.h"

static const char* TAG = "clawd_mochi";

// ── Pins ──────────────────────────────────────────────────────
// ESP32-C3. Note GPIO2 (RES) and GPIO8 (SCK) are strapping pins on this chip —
// see the wiring notes in README.md before changing them.
#define TFT_CS   4
#define TFT_DC   1
#define TFT_RST  2
#define TFT_BLK  3
#define TFT_MOSI 10
#define TFT_SCLK 8

// Non-static: anim.cpp draws through this same instance.
Display tft(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK);

// ── WiFi ──────────────────────────────────────────────────────
// The AP's identity is not a constant here: settings.c holds the stored device
// name (shared with the BLE name) and password, and the web UI changes both. The
// factory defaults live in settings.c beside the storage code.

// ── Radio ─────────────────────────────────────────────────────
// Historical note, because this bit the project hard: the first boot after a
// PHY config change (or an NVS wipe) runs a *full* RF calibration, and its
// inrush used to sag the 3V3 rail below the BOD threshold — already the most
// permissive setting, CONFIG_ESP_BROWNOUT_DET_LVL_SEL_0 ~= 2.43 V — so the chip
// boot-looped. That is self-locking: the calibration is only cached in NVS
// (CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE) once it *completes*, so a failed
// attempt guarantees the next boot pays the full-calibration current again.
//
// The fix was a stiffer supply (see the brownout notes in the repo history). The
// workaround that unblocked development — bringing the radio up before the
// backlight, while the panel was dark — has since been removed, because with
// adequate power the full calibration survives with the panel lit. Verified by
// wiping NVS and booting: full calibration with the backlight on, clean.
//
// If this ever boot-loops again on a weak supply, moving the wifiInitSoftAP()
// call ahead of backlightInit() in app_main() restores the extra headroom.
#define ENABLE_WIFI 1
#define ENABLE_BLE  1

// ── Display ───────────────────────────────────────────────────
#define DISP_W 240
#define DISP_H 240

// ── Colours ───────────────────────────────────────────────────
static uint16_t C_ORANGE, C_DARKBG, C_MUTED, C_GREEN;
#define C_WHITE ST77XX_WHITE
#define C_BLACK ST77XX_BLACK

// ── State ─────────────────────────────────────────────────────
// The pet's expressions are themed animations (see anim.cpp); these views are
// the screens that are not animations.
#define VIEW_ANIM 0        // the animation player owns the panel
#define VIEW_CODE 1
#define VIEW_DRAW 2

static uint8_t  currentView  = VIEW_ANIM;
// Set once anything asks for a specific state. The boot sequence switches to
// `idle` after the WiFi screen, and must not clobber an explicit request (a hook
// fires within seconds of boot).
static bool     stateRequested = false;
// What the non-animation screens are cleared to, and — since the player treats
// the artwork's field index as transparent — the pet's own background too. Set
// in initColours(); the default lives there so it can go through color565(),
// which is not a constant expression.
static uint16_t animSurround = 0xFFFF;
// Set whenever the pet's background changes, and consumed by the housekeeping
// loop below. The animation player repaints only the artwork's bounding box each
// frame, so without this the margin around the artwork keeps the old colour until
// the next state change — visible as a ring that does not follow the picker. Done
// here rather than at the call sites because the web picker fires continuously
// while dragging: this coalesces a drag into one full-screen repaint per tick
// (the last one is never dropped) and cannot repaint over another view.
static bool     bgDirty      = false;
static bool     busy         = false;
static bool     backlightOn  = true;
// Terminal mode, a mode *within* VIEW_CODE (the rest of its state lives with
// the TERM_* constants below). Declared up here because requestState() clears it
// when a state request takes the panel back from the user's views.
static bool     termMode     = false;

// ── Sleeping when nothing is driving the pet ──────────────────
// The pet only moves when something drives it — Claude Code hooks over serial
// or BLE, a web command, the CLI. With no agent running nothing arrives, so it
// would otherwise sit on `idle` indefinitely. After this many seconds of quiet
// it drops to the theme's `sleep` state instead.
//
// Any command restarts the timer, but only a *state* request wakes it: a
// status line or a brightness change should not rouse it from a nap.
#define SLEEP_AFTER_DEFAULT 120
#define SLEEP_AFTER_MAX     86400        // clamp, so *=1000 cannot overflow
static uint16_t sleepAfterSec = SLEEP_AFTER_DEFAULT;   // 0 disables
static TickType_t lastActivity = 0;
static bool     autoAsleep    = false;  // we napped it, the host did not ask

// Called by every command path. Cheap enough to call unconditionally.
static void noteActivity() { lastActivity = xTaskGetTickCount(); }

// The one way to change state: cancels any auto-sleep and restarts the timer,
// then hands off to the player. Explicitly asking for a state is itself
// activity, so callers need not call noteActivity() as well.
//
// A state request is also the pet's normal mode announcing itself: Claude Code
// drives the device through here (a hook sends `state <n>`), so if one of the
// user's views owns the panel — the code view, the canvas, an image push — it
// gets handed back. That is what makes the canvas safe to draw on while an agent
// is working: the drawing survives until the agent next says something, and then
// the pet takes over rather than fighting it for the screen.
static void requestState(const char* state) {
  autoAsleep = false;
  noteActivity();
  if (currentView != VIEW_ANIM) {
    currentView = VIEW_ANIM;
    termMode = false;
  }
  animPlayState(state);
}

// Backlight PWM: 8-bit duty, duty==0 → off.
#define BL_DUTY_MIN  64    // ~25% — dim but clearly visible
#define BL_DUTY_FULL 255   // 100% (never used by default)

static void backlightInit() {
  ledc_timer_config_t tim = {};
  tim.speed_mode      = LEDC_LOW_SPEED_MODE;
  tim.duty_resolution = LEDC_TIMER_8_BIT;
  tim.timer_num       = LEDC_TIMER_0;
  tim.freq_hz         = 1000;               // 1 kHz PWM (flicker-free)
  tim.clk_cfg         = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&tim));

  ledc_channel_config_t ch = {};
  ch.gpio_num   = TFT_BLK;
  ch.speed_mode = LEDC_LOW_SPEED_MODE;
  ch.channel    = LEDC_CHANNEL_0;
  ch.timer_sel  = LEDC_TIMER_0;
  ch.duty       = 0;                        // start OFF
  ch.hpoint     = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

// Brightness as a duty in [0,255]; 0 == off.
static void setBacklightDuty(uint32_t duty) {
  if (duty > 255) duty = 255;
  backlightOn = (duty > 0);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void setBacklight(bool on) {
  setBacklightDuty(on ? BL_DUTY_MIN : 0);   // "on" = 3% brightness
}
static uint16_t drawBgColor  = 0;

// Status line, shown on the static views (set over serial / HTTP).
#define STATUS_MAX 96
static char     statusText[STATUS_MAX] = "";
static uint8_t  statusSize  = 2;
static uint16_t statusColor = 0;   // 0 = fall back to C_BLACK

// ── Terminal ──────────────────────────────────────────────────
#define TERM_COLS      15
#define TERM_ROWS       8
#define TERM_CHAR_W    12
#define TERM_CHAR_H    20
#define TERM_PAD_X      8
#define TERM_PAD_Y     18

// termMode is declared with the other view state, above requestState().
static char     termLines[TERM_ROWS][TERM_COLS + 1];
static uint8_t  termRow     = 0;
static uint8_t  termCol     = 0;

// ── Boot logo (Jaguar Micro) ──────────────────────────────────
// LOGO_BITMAP is a wide ~3.6:1 mark streamed from flash by drawImage565. Its
// black "Jaguar" lettering is illegible over the panel's orange / dark themes,
// so it is mounted on a rounded white card that supplies the light background
// it needs — see the bg treatment discussion in the repo history.
#define LOGO_PAD     12
#define CARD_W       (LOGO_W + LOGO_PAD * 2)
#define CARD_H       (LOGO_H + LOGO_PAD * 2)
#define CARD_X       ((DISP_W - CARD_W) / 2)
#define CARD_RADIUS  8

#define LOGO_X       (CARD_X + LOGO_PAD)

#define BRAND_TEXT   "Jaguar Micro"
#define BRAND_SIZE   2
#define BRAND_CHAR_W (6 * BRAND_SIZE)          // 6 px glyph cell at size 1
#define BRAND_TEXT_H (8 * BRAND_SIZE)
#define BRAND_GAP    16

// The card + caption block is centred as a unit in the panel.
#define CARD_Y       ((DISP_H - (CARD_H + BRAND_GAP + BRAND_TEXT_H)) / 2)
#define LOGO_Y       (CARD_Y + LOGO_PAD)
#define BRAND_Y      (CARD_Y + CARD_H + BRAND_GAP)

// ── Boot splash ───────────────────────────────────────────────
// The panel's only font is the ASCII 5x7 in font5x7.h, so the Chinese line
// cannot be drawn as text at all — both lines ship as pre-rendered RGB565
// bitmaps made by tools/text2header.py. The background is baked into those
// bitmaps, so this value and their --bg must be regenerated together.
#define SPLASH_BG       Display::color565(255, 202, 1)     // the logo's yellow
#define SPLASH_GAP      26
#define SPLASH_Y        ((DISP_H - (SPLASH_TITLE_H + SPLASH_GAP + SPLASH_CREDIT_H)) / 2)
#define SPLASH_TITLE_Y  SPLASH_Y
#define SPLASH_CREDIT_Y (SPLASH_Y + SPLASH_TITLE_H + SPLASH_GAP)

static httpd_handle_t server = nullptr;

// ═════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════

static void delayMs(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static uint16_t hexToRgb565(const char* hex) {
  while (*hex == '#') hex++;
  if (strlen(hex) != 6) return C_WHITE;
  char tmp[7];
  strncpy(tmp, hex, 6);
  tmp[6] = 0;
  long v = strtol(tmp, nullptr, 16);
  return Display::color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// Inverse of hexToRgb565(), so the web UI can load the current colours from the
// device instead of keeping its own copy of the defaults — one source of truth,
// and a colour set over serial/BLE shows up in the pickers too.
static void rgb565ToHex(uint16_t c, char* out, size_t outLen) {
  // Scale back to 8 bits by replicating the high bits into the low ones, which
  // is what makes 0x1F -> 0xFF rather than 0xF8.
  uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
  uint8_t g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
  uint8_t b = (uint8_t)((c & 0x1F) * 255 / 31);
  snprintf(out, outLen, "#%02x%02x%02x", r, g, b);
}

static void initColours() {
  C_ORANGE = Display::color565(218, 17, 0);
  C_DARKBG = Display::color565(10,  12,  16);
  C_MUTED  = Display::color565(90,  88,  86);
  C_GREEN  = Display::color565(80, 220, 130);
  // Pale mint. Not pure white: the crab is warm orange, and a white field reads
  // as blown-out glare next to it. Change it here, from the web UI's colour
  // picker, or with `bg#RRGGBB` over serial/BLE.
  animSurround = Display::color565(232, 242, 238);
  drawBgColor = C_ORANGE;
}

// ═════════════════════════════════════════════════════════════
//  LOGO
// ═════════════════════════════════════════════════════════════

// Horizontal inset carved from each side of card row `dy`, measured from the
// nearest edge, to fake an r-radius corner. The panel has no alpha, so rounded
// corners are punched back out in the background colour instead of masked.
static int16_t cornerInset(int16_t dy, int16_t r) {
  if (dy >= r) return 0;
  int16_t d = r - 1 - dy;                 // distance from the corner centre
  int16_t span = r - (int16_t)(sqrtf((float)(r * r - d * d)) + 0.5f);
  return span > 0 ? span : 0;
}

// Draws the "Jaguar Micro" caption, printed twice one pixel apart to fake a
// bold weight (the same trick the old "Anthropic" caption used).
static void drawBrandText() {
  tft.setTextColor(C_WHITE);
  tft.setTextSize(BRAND_SIZE);
  int16_t x = (DISP_W - (int16_t)strlen(BRAND_TEXT) * BRAND_CHAR_W) / 2;
  tft.setCursor(x, BRAND_Y);
  tft.print(BRAND_TEXT);
  tft.setCursor(x + 1, BRAND_Y);
  tft.print(BRAND_TEXT);
}

// Paints the boot artwork for screen rows [y0, y1), clearing the band to the
// background first.
//
// That clear is not optional: the reveal lip is drawn full width, but the card,
// the mark and the caption are all narrower than the panel, so a band repaint
// that only touched those rectangles would leave the lip's outer ends and any
// lip above the card on screen for good.
//
// Working a row band at a time keeps this off a 115 KB full-frame buffer, which
// this board cannot afford — it has no PSRAM.
//
// `rowsBeyondAreArtwork` says the rows outside this band already hold the right
// pixels — true when the band is a repair made while a lip sweeps *down* over a
// finished card. It only matters for the caption, which is one unclippable glyph
// run: on the way up it may not go down before the band has reached its top row,
// or its glyphs would spill into the still-dark rows above the reveal lip; on the
// way down every row below the band is already painted, so any band that touches
// the caption can repaint all of it and leave no seam.
static void drawLogoRows(int16_t y0, int16_t y1, bool rowsBeyondAreArtwork = false) {
  if (y1 <= y0) return;
  tft.fillRect(0, y0, DISP_W, y1 - y0, C_DARKBG);

  int16_t cy0 = y0 > CARD_Y ? y0 : CARD_Y;
  int16_t cy1 = y1 < CARD_Y + CARD_H ? y1 : CARD_Y + CARD_H;
  if (cy0 < cy1) {
    tft.fillRect(CARD_X, cy0, CARD_W, cy1 - cy0, C_WHITE);
    for (int16_t y = cy0; y < cy1; y++) {
      int16_t fromTop = y - CARD_Y;
      int16_t fromBot = (CARD_Y + CARD_H - 1) - y;
      int16_t inset = cornerInset(fromTop < fromBot ? fromTop : fromBot,
                                  CARD_RADIUS);
      if (inset > 0) {                    // carve the corner back to the screen
        tft.fillRect(CARD_X, y, inset, 1, C_DARKBG);
        tft.fillRect(CARD_X + CARD_W - inset, y, inset, 1, C_DARKBG);
      }
    }
  }

  int16_t ly0 = y0 > LOGO_Y ? y0 : LOGO_Y;
  int16_t ly1 = y1 < LOGO_Y + LOGO_H ? y1 : LOGO_Y + LOGO_H;
  if (ly0 < ly1) {
    tft.drawImage565(LOGO_X, ly0, LOGO_W, ly1 - ly0,
                     LOGO_BITMAP + (size_t)(ly0 - LOGO_Y) * LOGO_W);
  }

  // The caption is a single unclippable glyph run, so it goes down whole on the
  // first band that can carry it — see the note on rowsBeyondAreArtwork above.
  if (y0 < BRAND_Y + BRAND_TEXT_H && y1 > BRAND_Y &&
      (rowsBeyondAreArtwork || y0 <= BRAND_Y)) drawBrandText();
}

// ═════════════════════════════════════════════════════════════
//  VIEWS
// ═════════════════════════════════════════════════════════════

// Draws the status line centred near the bottom of the panel, wrapping onto
// extra lines if it is wider than the panel. Fixed buffer — no dynamic
// allocation.
static void drawStatusText() {
  if (statusText[0] == 0) return;

  const int16_t charW = 6 * statusSize;
  const int16_t lineH = 8 * statusSize + 2;
  int16_t maxChars = DISP_W / charW;
  if (maxChars < 1) maxChars = 1;

  const int16_t len    = (int16_t)strlen(statusText);
  const int16_t lines  = (len + maxChars - 1) / maxChars;
  const int16_t startY = DISP_H - lines * lineH - 8;

  tft.setTextSize(statusSize);
  // White by default: the only static view left is the dark Claude Code screen,
  // where the old black default would be invisible.
  tft.setTextColor(statusColor ? statusColor : C_WHITE);

  char line[STATUS_MAX + 1];
  for (int16_t i = 0; i < lines; i++) {
    int16_t n = len - i * maxChars;
    if (n > maxChars) n = maxChars;
    memcpy(line, statusText + i * maxChars, (size_t)n);
    line[n] = 0;

    int16_t x = (DISP_W - n * charW) / 2;
    if (x < 0) x = 0;
    tft.setCursor(x, startY + i * lineH);
    tft.print(line);
  }
}

static void drawCodeView() {
  termMode = false;
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0,          DISP_W, 4, C_ORANGE);
  tft.fillRect(0, DISP_H - 4, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_ORANGE);
  tft.setTextSize(4);
  tft.setCursor((DISP_W - 144) / 2, DISP_H / 2 - 52);
  tft.print("Claude");
  tft.setTextColor(C_WHITE);
  tft.setTextSize(4);
  tft.setCursor((DISP_W - 96) / 2,  DISP_H / 2 + 8);
  tft.print("Code");
  tft.fillRect((DISP_W - 96) / 2, DISP_H / 2 + 52, 96, 3, C_ORANGE);
  drawStatusText();
}

// ═════════════════════════════════════════════════════════════
//  TERMINAL
// ═════════════════════════════════════════════════════════════

static void termClear() {
  for (uint8_t i = 0; i < TERM_ROWS; i++) termLines[i][0] = 0;
  termRow = 0;
  termCol = 0;
}

static void termDrawHeader() {
  tft.fillRect(0, 0, DISP_W, TERM_PAD_Y + 1, C_DARKBG);
  tft.setTextColor(C_ORANGE);
  tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, 4);
  tft.print("clawd@mochi terminal");
  tft.drawFastHLine(0, TERM_PAD_Y, DISP_W, C_ORANGE);
}

#define PREFIX_PX 54

static void termDrawPrefix(int16_t yy) {
  tft.setTextColor(C_GREEN);
  tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, yy + 6);
  tft.print("clawd:~$ ");
}

static void termDrawLine(uint8_t r) {
  const int16_t yy = TERM_PAD_Y + 4 + r * TERM_CHAR_H;
  tft.fillRect(0, yy, DISP_W, TERM_CHAR_H, C_DARKBG);
  if (r == termRow) termDrawPrefix(yy);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(TERM_PAD_X + PREFIX_PX, yy + 1);
  tft.print(termLines[r]);
  if (r == termRow) {
    const int16_t cx = TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W;
    tft.fillRect(cx, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  }
}

static void termDrawLastChar() {
  if (termCol == 0) return;
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  const uint8_t prev  = termCol - 1;
  tft.fillRect(baseX + prev * TERM_CHAR_W, yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(baseX + prev * TERM_CHAR_W, yy + 1);
  tft.print(termLines[termRow][prev]);
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
}

static void termDrawBackspace() {
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W * 2, TERM_CHAR_H - 1, C_DARKBG);
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  if (termLines[termRow][0] == 0) {
    tft.fillRect(0, yy, TERM_PAD_X + PREFIX_PX, TERM_CHAR_H, C_DARKBG);
  }
}

static void termFullRedraw() {
  tft.fillScreen(C_DARKBG);
  termDrawHeader();
  for (uint8_t r = 0; r < TERM_ROWS; r++) termDrawLine(r);
}

static void termScroll() {
  for (uint8_t i = 0; i < TERM_ROWS - 1; i++)
    strcpy(termLines[i], termLines[i + 1]);
  termLines[TERM_ROWS - 1][0] = 0;
  termRow = TERM_ROWS - 1;
  termFullRedraw();
}

static void termAddChar(char c) {
  if (c == '\n' || c == '\r') {
    const int16_t yy = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
    tft.fillRect(TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W,
                 yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
    termRow++;
    termCol = 0;
    if (termRow >= TERM_ROWS) { termScroll(); return; }
    termDrawLine(termRow);
  } else if (c == '\b' || c == 127) {
    if (termCol > 0) {
      termCol--;
      termLines[termRow][termCol] = 0;
      termDrawBackspace();
    }
  } else if (c >= 32 && c < 127) {
    if (termCol >= TERM_COLS) {
      termRow++;
      termCol = 0;
      if (termRow >= TERM_ROWS) { termScroll(); return; }
    }
    if (termCol == 0) termDrawPrefix(TERM_PAD_Y + 4 + termRow * TERM_CHAR_H);
    termLines[termRow][termCol] = c;
    termLines[termRow][termCol + 1] = 0;
    termCol++;
    termDrawLastChar();
  }
}

// ═════════════════════════════════════════════════════════════
//  ANIMATIONS
// ═════════════════════════════════════════════════════════════

// ── Boot card ─────────────────────────────────────────────────
// Fixed pacing, not the pet's speed setting: this is a one-shot boot flourish,
// and tying it to animSpeed made the UI's speed slider look like it controlled
// the pet when it only ever changed this.
#define BOOT_STEP_MS 24
#define BOOT_STEPS   30

// The sweeping lip's two tones. The reveal can afford a white leading row because
// it only ever travels over dark background, but the return sweep crosses the
// white card too, where white-on-white would vanish. These two read as a lit edge
// over the dark surround and as a shadow over the card, so the sweep stays one
// unbroken line the whole way down.
#define LIP_DIM    Display::color565(150, 152, 158)
#define LIP_SHADOW Display::color565(70, 72, 78)

// Ease-in-out on [0,1]: the edge pulls away slowly, accelerates, then settles.
static float easeInOut(float p) {
  return (p < 0.5f) ? 2.0f * p * p : 1.0f - 2.0f * (1.0f - p) * (1.0f - p);
}

// Second pass over the same artwork: the lit edge runs back down the finished
// card, as if the page had settled and the light swept across it once more.
//
// Nothing is exposed this time, so this is a repair rather than a reveal — and
// the panel cannot be read back, so a lip left behind would stay there for good.
// Each step therefore repaints the band the previous lip is sitting in, which is
// rows [trail, y) — the pair it covers and no more — and only then lays the new
// lip below. Total traffic is one more screenful, and unlike clearing the screen
// and re-revealing there is no black frame at the turn.
static void animLogoReturnSweep() {
  int16_t trail = 0;                    // rows above this still hold the last lip

  for (int s = 1; s <= BOOT_STEPS; s++) {
    // Stop two rows short of the bottom so the lip always fits on the panel.
    int16_t y = (int16_t)(easeInOut((float)s / BOOT_STEPS) * (DISP_H - 2) + 0.5f);
    if (y < trail + 2) continue;        // sub-pixel step, nothing to repaint

    drawLogoRows(trail, y, true);       // also erases the previous lip
    tft.fillRect(0, y, DISP_W, 1, LIP_DIM);
    tft.fillRect(0, y + 1, DISP_W, 1, LIP_SHADOW);
    trail = y;
    delayMs(BOOT_STEP_MS);
  }

  drawLogoRows(trail, DISP_H, true);    // run the last lip off the bottom edge
}

// Boot animation: the artwork rolls up from the bottom edge, its leading edge
// catching the light like the lip of a turning page.
//
// A page-flip is a wipe, not a fade, so this needs no per-pixel blending — just
// a reveal line sweeping bottom to top. Each step paints the newly exposed band
// and lays a lit lip along the new edge; the next band paints over that lip, so
// the total SPI traffic stays near one screenful.
static void animLogoReveal() {
  busy = true;
  tft.fillScreen(C_DARKBG);

  int16_t cursor = DISP_H;              // content is painted from cursor down

  for (int s = 1; s <= BOOT_STEPS; s++) {
    int16_t y = (int16_t)(DISP_H - easeInOut((float)s / BOOT_STEPS) * DISP_H + 0.5f);
    if (y >= cursor) continue;          // sub-pixel step, nothing new to expose

    drawLogoRows(y, cursor);            // also repaints the previous lip
    if (y > 0) {
      // Only when the topmost row is reached does the lip's second row fall off
      // the panel — fillArea does not clip, so guard it here.
      tft.fillRect(0, y, DISP_W, 1, C_WHITE);
      if (y + 1 < DISP_H) tft.fillRect(0, y + 1, DISP_W, 1, LIP_DIM);
      cursor = (int16_t)(y + 2 > DISP_H ? DISP_H : y + 2);
    } else {
      cursor = 0;                       // don't paint a lip over the finished top
    }
    delayMs(BOOT_STEP_MS);
  }

  drawLogoRows(0, cursor);              // clear any leftover lip
  animLogoReturnSweep();                // and run the light back down over it
  delayMs(1500);
  busy = false;
}

// ═════════════════════════════════════════════════════════════
//  WEB ROUTES
// ═════════════════════════════════════════════════════════════

static void sendJson(httpd_req_t* req, const char* body) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

// esp_http_server hands query strings back exactly as they arrived — its own
// header says the parts are not URL-decoded — while a browser's fetch()
// percent-encodes whatever it is given. Nothing here decoded them, so the page's
// encodings arrived intact and wrong: "%23aabbcc" reached hexToRgb565 (which
// wants six hex digits, so the pet background stayed white) and
// "12%2C34%3B56%2C78" reached the stroke parser (which splits on ';', so the web
// canvas drew nothing). Decode in place: a decoded string is never longer than
// its encoding, so the caller's own buffer is a safe destination.
static void urlDecode(char* s) {
  char* w = s;
  for (const char* r = s; *r; r++) {
    if (*r == '+') {                       // form encoding's space
      *w++ = ' ';
      continue;
    }
    if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
      const int hi = isdigit((unsigned char)r[1]) ? r[1] - '0' : (r[1] | 0x20) - 'a' + 10;
      const int lo = isdigit((unsigned char)r[2]) ? r[2] - '0' : (r[2] | 0x20) - 'a' + 10;
      *w++ = (char)((hi << 4) | lo);
      r += 2;
      continue;
    }
    *w++ = *r;
  }
  *w = 0;
}

static bool getQueryArg(httpd_req_t* req, const char* key, char* out, size_t outlen) {
  size_t len = httpd_req_get_url_query_len(req) + 1;
  if (len <= 1) return false;
  char* buf = (char*)malloc(len);
  if (!buf) return false;
  if (httpd_req_get_url_query_str(req, buf, len) != ESP_OK) { free(buf); return false; }
  bool found = (httpd_query_key_value(buf, key, out, outlen) == ESP_OK);
  free(buf);
  if (found) urlDecode(out);
  return found;
}

// Pulls one field out of a form-urlencoded body that the caller has already
// read. httpd_req_recv consumes the body, so it is read once — see routeDevice —
// and every field has to come out of that one buffer. Same key=value scanner as
// the query string above, and the same decoding.
static bool bodyArg(const char* body, const char* key, char* out, size_t outlen) {
  const bool found = (httpd_query_key_value(body, key, out, outlen) == ESP_OK);
  if (found) urlDecode(out);
  return found;
}

// Serves the web UI from the LittleFS "storage" partition (see web/index.html,
// bundled by littlefs_create_partition_image in the top-level CMakeLists).
static esp_err_t routeRoot(httpd_req_t* req) {
  static const char* PATH = "/littlefs/index.html";

  FILE* fd = fopen(PATH, "r");
  if (!fd) {
    ESP_LOGE(TAG, "cannot open %s", PATH);
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "index.html not found");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "text/html");

  char chunk[1024];
  size_t n;
  while ((n = fread(chunk, 1, sizeof(chunk), fd)) > 0) {
    if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
      fclose(fd);
      httpd_resp_sendstr_chunk(req, NULL);   // terminate the chunked response
      return ESP_FAIL;
    }
  }

  fclose(fd);
  return httpd_resp_send_chunk(req, NULL, 0);   // empty chunk ends the response
}

// Single-character view commands. Shared by the HTTP /cmd route and the serial
// CLI so the two paths can never drift apart.
//
// 'w' and 's' used to select the two code-drawn eye expressions; the pet is now
// driven entirely by themed animations, so they play a state instead. That keeps
// the existing hooks, scripts and web UI working unchanged.
static void applyCommand(char c) {
  noteActivity();
  if (termMode) {
    if (c == 'q') { termMode = false; drawCodeView(); }
    return;
  }
  switch (c) {
    case 'w': currentView = VIEW_ANIM; stateRequested = true; requestState("idle"); break;
    case 's': currentView = VIEW_ANIM; stateRequested = true; requestState("done"); break;
    case 'd':
      requestState("");                   // stop the player before painting over it
      currentView = VIEW_CODE; drawCodeView();
      termMode = true; termClear(); termFullRedraw(); break;
    case 'a':
      requestState("");
      currentView = VIEW_ANIM;
      animLogoReveal();
      requestState("idle");
      break;
  }
}

static esp_err_t routeCmd(httpd_req_t* req) {
  char k[8] = {0};
  if (!getQueryArg(req, "k", k, sizeof(k)) || k[0] == 0) {
    sendJson(req, "{\"e\":1}");
    return ESP_OK;
  }
  applyCommand(k[0]);
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeChar(httpd_req_t* req) {
  noteActivity();
  if (!termMode) { sendJson(req, "{\"ok\":1}"); return ESP_OK; }
  char c[8] = {0};
  if (getQueryArg(req, "c", c, sizeof(c)) && c[0] != 0) {
    // %08 (backspace) arrives URL-decoded as a single 0x08 byte
    if ((unsigned char)c[0] == 0x08) termAddChar('\b');
    else if ((unsigned char)c[0] == 0x0A) termAddChar('\n');
    else termAddChar(c[0]);
  }
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeRedraw(httpd_req_t* req) {
  noteActivity();
  char bg[16] = {0};
  if (getQueryArg(req, "bg", bg, sizeof(bg))) {
    // Pet background only. This used to also set drawBgColor, which conflated
    // the two: the canvas has its own colour, set through /draw/clear, and the
    // web UI now exposes them as separate pickers.
    animSurround = hexToRgb565(bg);
    animSetBackground(animSurround);
    bgDirty = true;
  }
  switch (currentView) {
    case VIEW_CODE: drawCodeView();   break;
    case VIEW_DRAW: tft.fillScreen(drawBgColor); break;
    default: break;   // the animation player owns the panel
  }
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeCanvas(httpd_req_t* req) {
  noteActivity();
  char on[8] = {0};
  if (getQueryArg(req, "on", on, sizeof(on)) && strcmp(on, "1") == 0) {
    // Stop the player before the canvas owns the panel: it otherwise keeps
    // pushing frames and repaints its artwork over the strokes every ~70 ms.
    requestState("");
    currentView = VIEW_DRAW;
    tft.fillScreen(drawBgColor);
  }
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeDrawClear(httpd_req_t* req) {
  noteActivity();
  char bg[16] = {0};
  if (getQueryArg(req, "bg", bg, sizeof(bg))) {
    drawBgColor = hexToRgb565(bg);
  } else {
    drawBgColor = hexToRgb565("#aa4818");
  }
  currentView = VIEW_DRAW;
  termMode = false;
  tft.fillScreen(drawBgColor);
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeDrawStroke(httpd_req_t* req) {
  noteActivity();
  char pen[16] = {0};
  char pts[2048] = {0};
  // Silence here was the wrong default: a stroke that never arrives looked
  // exactly like one that was drawn, so the page had nothing to report.
  if (!getQueryArg(req, "pen", pen, sizeof(pen))) {
    sendJson(req, "{\"e\":\"no pen\"}");
    return ESP_OK;
  }
  if (!getQueryArg(req, "pts", pts, sizeof(pts))) {
    sendJson(req, "{\"e\":\"stroke too long\"}");
    return ESP_OK;
  }
  const uint16_t color = hexToRgb565(pen);
  currentView = VIEW_DRAW;

  int16_t prevX = -1, prevY = -1;
  char* saveptr = nullptr;
  char* tok = strtok_r(pts, ";", &saveptr);
  while (tok) {
    const char* comma = strchr(tok, ',');
    if (comma) {
      int16_t x = (int16_t)atoi(tok);
      int16_t y = (int16_t)atoi(comma + 1);
      if (prevX >= 0) {
        tft.drawLine(prevX, prevY, x, y, color);
        tft.drawLine(prevX + 1, prevY, x + 1, y, color);
        tft.drawLine(prevX, prevY + 1, x, y + 1, color);
      } else {
        tft.fillCircle(x, y, 2, color);
      }
      prevX = x;
      prevY = y;
    }
    tok = strtok_r(nullptr, ";", &saveptr);
  }
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeBacklight(httpd_req_t* req) {
  noteActivity();
  char on[8] = {0};
  bool enable = getQueryArg(req, "on", on, sizeof(on)) && strcmp(on, "1") == 0;
  setBacklight(enable);
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

// ── Device identity ───────────────────────────────────────────
// One name for both radios — the AP SSID and the BLE advertisement are the same
// string by design — plus the AP password, stored in NVS by settings.c.
//
// Applying a change means restarting. The AP has to come up with a different
// identity and the BLE advertisement is built once at init, so a restart is both
// simpler than a live reconfigure of two radios and exactly what the page warns
// the user about ("the device will restart, reconnect to the new name").
//
// POST, not GET: this writes flash and reboots the SoC, and a GET lives on in
// browser history where a reload or a prefetch would reboot the device again.
static esp_err_t routeDevice(httpd_req_t* req) {
  noteActivity();

  char body[513];
  const int got = (req->content_len > 0 && req->content_len < (int)sizeof(body))
                      ? httpd_req_recv(req, body, req->content_len)
                      : 0;
  if (got <= 0) {
    sendJson(req, "{\"e\":\"empty request\"}");
    return ESP_OK;
  }
  body[got] = 0;

  // Buffers deliberately longer than the limits settings.c enforces: a too-long
  // value should come back as a clear error, not be silently truncated into
  // something that looks valid.
  char def[8] = {0};
  char name[96] = {0};
  char pass[96] = {0};
  const char* err = nullptr;

  if (bodyArg(body, "default", def, sizeof(def)) && def[0] == '1') {
    if (!settingsReset(&err)) {
      char j[96];
      snprintf(j, sizeof(j), "{\"e\":\"%s\"}", err);
      sendJson(req, j);
      return ESP_OK;
    }
  } else {
    bodyArg(body, "name", name, sizeof(name));
    bodyArg(body, "pass", pass, sizeof(pass));
    if (!settingsSave(name, pass, &err)) {
      char j[96];
      snprintf(j, sizeof(j), "{\"e\":\"%s\"}", err);
      sendJson(req, j);
      return ESP_OK;
    }
  }

  char j[160];
  snprintf(j, sizeof(j), "{\"ok\":1,\"name\":\"%s\"}", settingsName());
  sendJson(req, j);

  // Let that reply reach the socket before the radio goes down: the restart is
  // about to drop this connection, and the page needs the answer to explain why.
  delayMs(300);
  esp_restart();
  return ESP_OK;                       // not reached
}

// ── Theme replacement ─────────────────────────────────────────
// Uploading a pack is a three-step replacement — begin, put per file, commit —
// rather than one request, because the partition cannot hold two themes at
// once. See routeThemeBegin for why.
#define THEME_NAME_MAX 64
#define THEME_PATH_MAX 96
#define THEME_CHUNK    2048

// Frees the partition before an upload starts.
//
// This step exists because the two themes cannot coexist: clawd's pack alone
// leaves ~380 KB of the 2.44 MB partition free, which is less than any pack
// worth installing. Staging the new theme beside the old one — the obvious way
// to make the swap atomic — is simply not possible here, so the old animations
// are dropped first and the manifest is written last. That leaves the manifest
// pointing at files that are gone for the duration of the upload; the player is
// stopped here so it does not spend the transfer logging that.
static esp_err_t routeThemeBegin(httpd_req_t* req) {
  noteActivity();
  animPlayState("");         // stop the player while its files are missing

  unsigned freed = 0;
  DIR* dir = opendir(ANIM_THEME_DIR);
  if (dir) {
    struct dirent* de;
    char victim[THEME_PATH_MAX];
    while ((de = readdir(dir)) != nullptr) {
      const char* n = de->d_name;
      if (n[0] == '.' || strcmp(n, "manifest.txt") == 0) continue;
      if (strlen(n) >= THEME_NAME_MAX) continue;
      snprintf(victim, THEME_PATH_MAX, "%s/%.*s", ANIM_THEME_DIR,
               THEME_NAME_MAX - 1, n);
      if (unlink(victim) == 0) freed++;
    }
    closedir(dir);
  }
  ESP_LOGI(TAG, "theme: cleared %u file(s) for a new upload", freed);

  char j[48];
  snprintf(j, sizeof(j), "{\"ok\":1,\"cleared\":%u}", freed);
  sendJson(req, j);
  return ESP_OK;
}

// ── Theme upload ──────────────────────────────────────────────
// The partition holds exactly one theme, so "switching" is replacement rather
// than a choice among installed themes — there is no room for a second, let
// alone a staging copy to build beside the current one. The pack is posted one
// file at a time and the manifest goes last: until it lands, the old mapping is
// still in force, which is as close to an atomic swap as a full partition
// allows. A failure partway leaves a half-written theme, so the commit step is
// separate and explicit.
static esp_err_t routeThemePut(httpd_req_t* req) {
  noteActivity();
  char name[THEME_NAME_MAX] = {0};
  if (!getQueryArg(req, "name", name, sizeof(name)) || name[0] == 0) {
    sendJson(req, "{\"e\":\"name\"}");
    return ESP_OK;
  }
  // The name becomes a path component and arrives from the network.
  if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) {
    ESP_LOGW(TAG, "theme: rejected name '%s'", name);
    sendJson(req, "{\"e\":\"name\"}");
    return ESP_OK;
  }

  char path[THEME_PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", ANIM_THEME_DIR, name);

  FILE* fd = fopen(path, "wb");
  if (!fd) {
    ESP_LOGE(TAG, "theme: cannot write %s", path);
    sendJson(req, "{\"e\":\"open\"}");
    return ESP_OK;
  }

  char* buf = (char*)malloc(THEME_CHUNK);
  if (!buf) {
    fclose(fd);
    sendJson(req, "{\"e\":\"mem\"}");
    return ESP_OK;
  }

  int left = req->content_len;
  while (left > 0) {
    int want = left > THEME_CHUNK ? THEME_CHUNK : left;
    int got = httpd_req_recv(req, buf, want);
    if (got <= 0) break;
    if (fwrite(buf, 1, (size_t)got, fd) != (size_t)got) break;
    left -= got;
  }
  free(buf);
  fclose(fd);

  if (left > 0) {
    ESP_LOGE(TAG, "theme: %s truncated, %d byte(s) missing", name, left);
    sendJson(req, "{\"e\":\"short\"}");
    return ESP_OK;
  }
  ESP_LOGI(TAG, "theme: wrote %s (%d bytes)", name, req->content_len);
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

// Drops animations the new manifest does not reference, then restarts the
// player. Without the sweep the previous set's files would still be occupying
// the partition — and there is no room for both.
static esp_err_t routeThemeCommit(httpd_req_t* req) {
  noteActivity();

  char manifest[2048];
  size_t mLen = 0;
  FILE* mf = fopen(ANIM_MANIFEST, "r");
  if (mf) {
    mLen = fread(manifest, 1, sizeof(manifest) - 1, mf);
    fclose(mf);
  }
  manifest[mLen] = 0;

  // Collect first, delete after: unlinking while a directory is being walked
  // is asking for trouble on a filesystem this small.
  char doomed[16][THEME_PATH_MAX];
  unsigned nDoomed = 0;

  if (mLen > 0) {
    DIR* dir = opendir(ANIM_THEME_DIR);
    if (dir) {
      struct dirent* de;
      while ((de = readdir(dir)) != nullptr && nDoomed < 16) {
        const char* n = de->d_name;
        if (n[0] == '.' || strcmp(n, "manifest.txt") == 0) continue;
        // d_name is 255 bytes wide; nothing a theme ships is anywhere near
        // that, and the length check keeps the path snprintf below provably
        // in bounds.
        if (strlen(n) >= THEME_NAME_MAX) continue;
        // A substring test can only under-delete, never over-delete: a name
        // that IS referenced always matches itself. Worst case a stale file
        // survives and wastes space, which is the benign direction.
        if (strstr(manifest, n) == nullptr) {
          snprintf(doomed[nDoomed], THEME_PATH_MAX, "%s/%.*s",
                   ANIM_THEME_DIR, THEME_NAME_MAX - 1, n);
          nDoomed++;
        }
      }
      closedir(dir);
    }
  }

  unsigned dropped = 0;
  for (unsigned i = 0; i < nDoomed; i++) {
    if (unlink(doomed[i]) == 0) dropped++;
  }

  animReloadTheme();
  ESP_LOGI(TAG, "theme: committed (%u stale file(s) dropped)", dropped);

  char j[48];
  snprintf(j, sizeof(j), "{\"ok\":1,\"dropped\":%u}", dropped);
  sendJson(req, j);
  return ESP_OK;
}

// Switches which animation plays. Separate from /cmd, which only carries the
// single-character verbs the original sketch defined.
static esp_err_t routeThemeState(httpd_req_t* req) {
  noteActivity();
  char name[THEME_NAME_MAX] = {0};
  if (!getQueryArg(req, "name", name, sizeof(name))) {
    sendJson(req, "{\"e\":\"name\"}");
    return ESP_OK;
  }
  stateRequested = true;
  currentView = VIEW_ANIM;
  requestState(name);            // empty name stops the player
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeState(httpd_req_t* req) {
  char bg[8];
  rgb565ToHex(animSurround, bg, sizeof(bg));
  // The state list rides along so the web UI can offer a picker without a
  // second round trip, and without duplicating the manifest's contents here.
  char states[192];
  animListStates(states, sizeof(states));
  // 512: the state list is the bulk of it, plus the identity strings — the name
  // and password are validated at the door to exclude quotes and backslashes, so
  // they go into the JSON verbatim.
  char j[512];
  snprintf(j, sizeof(j),
           "{\"view\":%u,\"busy\":%s,\"term\":%s,\"bl\":%s,"
           "\"bg\":\"%s\",\"states\":\"%s\",\"name\":\"%s\",\"pass\":\"%s\"}",
           currentView,
           busy ? "true" : "false",
           termMode ? "true" : "false",
           backlightOn ? "true" : "false",
           bg, states, settingsName(), settingsPass());
  sendJson(req, j);
  return ESP_OK;
}

static esp_err_t routeNotFound(httpd_req_t* req, httpd_err_code_t code) {
  httpd_resp_send_err(req, code, "not found");
  return ESP_FAIL;
}

// ═════════════════════════════════════════════════════════════
//  WIFI + HTTP SERVER
// ═════════════════════════════════════════════════════════════

// Stations currently associated with our SoftAP — the web controller's link.
// Event-driven rather than polled: esp_wifi_ap_get_sta_list() is a synchronous
// call into the WiFi library under its lock, while this is a counter increment
// on the default event task. volatile because the reader is a loop that never
// writes it, and would otherwise be free to hoist the load out. (Read-modify-
// write rather than ++/--, which C++20 deprecated on volatile.)
static volatile uint8_t s_wifiStas = 0;

static void wifiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data) {
  (void)arg; (void)data;
  if (base != WIFI_EVENT) return;
  uint8_t n = s_wifiStas;
  if (id == WIFI_EVENT_AP_STACONNECTED) {
    if (n < 0xFF) s_wifiStas = (uint8_t)(n + 1);
  } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
    if (n) s_wifiStas = (uint8_t)(n - 1);
  } else {
    return;
  }
  ESP_LOGI(TAG, "wifi: %u station(s) up", (unsigned)s_wifiStas);
}

static uint8_t wifiStaCount() { return s_wifiStas; }

static void wifiInitSoftAP() {
  esp_netif_t* ap = esp_netif_create_default_wifi_ap();
  (void)ap;

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Registered before esp_wifi_start(), so a station cannot associate in the gap.
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED,
                                             &wifiEventHandler, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED,
                                             &wifiEventHandler, nullptr));

  wifi_config_t wifi_config = {};
  const char* name = settingsName();
  const char* pass = settingsPass();
  strncpy((char*)wifi_config.ap.ssid, name, sizeof(wifi_config.ap.ssid));
  strncpy((char*)wifi_config.ap.password, pass, sizeof(wifi_config.ap.password));
  // ssid_len is what the driver copies, so the SSID need not be terminated —
  // and a name at the length limit would not be, in a zero-filled 32-byte field.
  wifi_config.ap.ssid_len = strlen(name);
  wifi_config.ap.max_connection = 4;
  wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  // settingsLoad() has already dropped anything WPA2 would reject back to the
  // default, so this check cannot abort for a stored value — only a hardware or
  // driver failure gets here.
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "SoftAP started: %s", name);
}

static void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // 13 routes are registered below; keep a little headroom. The upload routes
  // also need a bigger stack than 4 KB while writing to LittleFS.
  config.max_uri_handlers = 20;
  // A canvas stroke is one query string, and a finger's scribble generates a
  // point per pointer event — half a second of drawing already passed the 512-byte
  // default, and the server rejected it before the handler ever saw it. 2 KB is
  // about 250 points, i.e. several seconds of continuous drawing.
  config.max_uri_len     = 2048;
  config.stack_size       = 8192;
  config.recv_wait_timeout = 30;    // a 2 MB pack over WiFi is not fast
  config.send_wait_timeout = 30;
  config.uri_match_fn = httpd_uri_match_wildcard;

  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "failed to start http server");
    return;
  }

  httpd_uri_t r = {};
  r.method = HTTP_GET;
  r.user_ctx = nullptr;

  r.uri = "/";            r.handler = routeRoot;       httpd_register_uri_handler(server, &r);
  r.uri = "/cmd";         r.handler = routeCmd;        httpd_register_uri_handler(server, &r);
  r.uri = "/char";        r.handler = routeChar;       httpd_register_uri_handler(server, &r);
  r.uri = "/redraw";      r.handler = routeRedraw;     httpd_register_uri_handler(server, &r);
  r.uri = "/canvas";      r.handler = routeCanvas;     httpd_register_uri_handler(server, &r);
  r.uri = "/draw/clear";  r.handler = routeDrawClear;  httpd_register_uri_handler(server, &r);
  r.uri = "/draw/stroke"; r.handler = routeDrawStroke; httpd_register_uri_handler(server, &r);
  r.uri = "/backlight";   r.handler = routeBacklight;  httpd_register_uri_handler(server, &r);
  r.uri = "/state";       r.handler = routeState;      httpd_register_uri_handler(server, &r);
  r.uri = "/theme/state"; r.handler = routeThemeState; httpd_register_uri_handler(server, &r);

  // Posts with a body, so they cannot share the GET handlers above: the theme
  // upload streams file bytes, and /device writes flash and reboots the device.
  r.method = HTTP_POST;
  r.uri = "/device";       r.handler = routeDevice;
  httpd_register_uri_handler(server, &r);
  r.uri = "/theme/begin";  r.handler = routeThemeBegin;
  httpd_register_uri_handler(server, &r);
  r.uri = "/theme/put";    r.handler = routeThemePut;
  httpd_register_uri_handler(server, &r);
  r.uri = "/theme/commit"; r.handler = routeThemeCommit;
  httpd_register_uri_handler(server, &r);

  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, routeNotFound);
}

// ═════════════════════════════════════════════════════════════
//  APP
// ═════════════════════════════════════════════════════════════

static void mountStorage() {
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = "/littlefs";
  conf.partition_label = "storage";
  conf.format_if_mount_failed = true;

  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
    return;
  }

  size_t total = 0, used = 0;
  if (esp_littlefs_info(conf.partition_label, &total, &used) == ESP_OK) {
    ESP_LOGI(TAG, "LittleFS mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
  }
}

// ═════════════════════════════════════════════════════════════
//  SERIAL CLI — USB serial via the chip's native USB-Serial-JTAG.
//  Lets Claude Code hooks drive the screen. See CLAUDE-CODE-BRIDGE.md.
//
//  Not UART0. The C3 Super Mini's USB socket is wired to the USB-Serial-JTAG
//  peripheral, and the console's *secondary* sink is what puts our printf
//  output on /dev/ttyACM0. Reading UART0 instead — which is what this used to
//  do — meant host writes filled the USB peripheral's RX FIFO, NAK'd, and
//  blocked the host's write() for good: the transport was dead in one
//  direction while looking alive in the other.
// ═════════════════════════════════════════════════════════════

#define SERIAL_RX_RING  4096     // USB-Serial-JTAG's ring: >64 required, and
                                 // one image band is 3840 bytes
#define IMG_BAND_ROWS      8

static void serialReply(const char* s) {
  ESP_LOGD(TAG, "reply '%s'", s);
  printf("%s\n", s);
}

// Receives DISP_W x DISP_H raw RGB565 (little-endian) after answering "ready".
// Bands of rows go straight to the panel, so no full-screen buffer is needed.
static void serialReceiveImage() {
  requestState("");                 // the image owns the panel until a state request
  currentView = VIEW_DRAW;
  termMode = false;
  serialReply("ready");

  static uint16_t band[DISP_W * IMG_BAND_ROWS];
  uint8_t* raw = (uint8_t*)band;

  const uint32_t bandPx   = (uint32_t)DISP_W * IMG_BAND_ROWS;
  const uint32_t bandLen  = bandPx * 2;
  const uint32_t totalPx  = (uint32_t)DISP_W * DISP_H;

  uint32_t gotPx = 0, filled = 0;
  int16_t  bandY = 0;

  while (gotPx < totalPx) {
    int n = usb_serial_jtag_read_bytes(raw + filled, bandLen - filled,
                                       pdMS_TO_TICKS(1000));
    if (n <= 0) break;                       // stalled — give up
    filled += (uint32_t)n;
    if (filled == bandLen) {
      tft.drawImage565(0, bandY, DISP_W, IMG_BAND_ROWS, band);
      bandY += IMG_BAND_ROWS;
      gotPx += bandPx;
      filled = 0;
    }
  }
  if (filled >= (uint32_t)DISP_W * 2) {       // flush a partial band
    int16_t rows = (int16_t)(filled / 2 / DISP_W);
    tft.drawImage565(0, bandY, DISP_W, rows, band);
    gotPx += (uint32_t)rows * DISP_W;
  }

  char msg[32];
  snprintf(msg, sizeof(msg), "done %lu", (unsigned long)(gotPx * 2));
  serialReply(msg);
}

static void serialHandleLine(char* line) {
  while (*line == ' ' || *line == '\t') line++;
  size_t n = strlen(line);
  while (n && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
  if (n == 0) return;

  // Every serial/BLE command counts as activity, so the pet does not nod off
  // while someone is talking to it. Note this is *before* the dispatch below,
  // which is also reached by the Claude Code hooks.
  noteActivity();

  // Single-character view commands — same set as the web UI's /cmd.
  if (n == 1) { applyCommand(line[0]); serialReply("ok"); return; }

  if (line[0] == 't') {                      // t<text> — type into the terminal
    if (!termMode) {
      currentView = VIEW_CODE; drawCodeView();
      termMode = true; termClear(); termFullRedraw();
    }
    for (size_t i = 1; i < n; i++) termAddChar(line[i]);
    serialReply("ok");
    return;
  }

  if (strncmp(line, "bg", 2) == 0) {         // bg#RRGGBB
    animSurround = hexToRgb565(line + 2);
    animSetBackground(animSurround);
    bgDirty = true;
    // The draw canvas keeps its own colour, but if it is what is on screen then
    // this command should change what you are looking at, not something hidden.
    if (currentView == VIEW_DRAW) drawBgColor = animSurround;
    switch (currentView) {
      case VIEW_CODE: drawCodeView();              break;
      case VIEW_DRAW: tft.fillScreen(drawBgColor); break;
      default: break;   // the animation player owns the panel
    }
    serialReply("ok");
    return;
  }

  if (strcmp(line, "logo") == 0) {
    requestState(""); currentView = VIEW_ANIM;
    animLogoReveal();
    requestState("idle");
    serialReply("ok"); return;
  }
  if (strcmp(line, "canvas") == 0) {
    requestState("");                 // stop the player, or it repaints over the canvas
    currentView = VIEW_DRAW; termMode = false;
    tft.fillScreen(drawBgColor); serialReply("ok"); return;
  }

  if (strncmp(line, "line ", 5) == 0) {      // line x1,y1,x2,y2,#RRGGBB
    int x1, y1, x2, y2; char col[16] = {0};
    if (sscanf(line + 5, "%d,%d,%d,%d,%15s", &x1, &y1, &x2, &y2, col) == 5) {
      uint16_t c = hexToRgb565(col);
      tft.drawLine(x1, y1, x2, y2, c);
      tft.drawLine(x1 + 1, y1, x2 + 1, y2, c);   // 2px, as in the upstream PR
    }
    serialReply("ok");
    return;
  }

  if (strncmp(line, "status", 6) == 0) {     // status[N] [-c#RRGGBB] <text>
    char* p = line + 6;
    if (*p >= '1' && *p <= '4') { statusSize = (uint8_t)(*p - '0'); p++; }
    while (*p == ' ') p++;

    if (p[0] == '-' && p[1] == 'c') {
      char hexbuf[16] = {0};
      char*  sp = strchr(p + 2, ' ');
      size_t hl = sp ? (size_t)(sp - (p + 2)) : strlen(p + 2);
      if (hl > sizeof(hexbuf) - 1) hl = sizeof(hexbuf) - 1;
      memcpy(hexbuf, p + 2, hl);
      statusColor = hexToRgb565(hexbuf);
      p = sp ? sp + 1 : p + 2 + hl;
    }
    while (*p == ' ') p++;

    strncpy(statusText, p, STATUS_MAX - 1);
    statusText[STATUS_MAX - 1] = 0;

    // The animations repaint their own region every frame, so the status line
    // can only be shown on the static views.
    if (currentView == VIEW_CODE)      drawCodeView();
    else if (currentView == VIEW_DRAW) tft.fillScreen(drawBgColor);
    serialReply("ok");
    return;
  }

  // state <name> — play a themed clawd animation; "state off" returns to the
  // static views. "states" lists what the mounted theme provides.
  if (strcmp(line, "states") == 0) {
    char list[256];
    animListStates(list, sizeof(list));
    serialReply(list[0] ? list : "(theme has no manifest)");
    return;
  }

  if (strncmp(line, "state", 5) == 0 && (line[5] == 0 || line[5] == ' ')) {
    char* arg = line + 5;
    while (*arg == ' ') arg++;

    stateRequested = true;
    if (*arg == 0 || strcmp(arg, "off") == 0) {
      requestState("");
      serialReply("ok");
      return;
    }
    requestState(arg);
    serialReply("ok");
    return;
  }

  // device [<name> [<password>]] — the same identity the web card saves, from the
  // CLI. Both go through settings.c and both restart to apply, because the AP has
  // to come up with the new SSID and the BLE advertisement is built at init. A
  // bare `device` reports what is in force.
  //
  // The name cannot contain a space here; the web UI is how to set one that does
  // (its form encoding carries the space through).
  if (strncmp(line, "device", 6) == 0 && (line[6] == 0 || line[6] == ' ')) {
    char* arg = line + 6;
    while (*arg == ' ') arg++;
    if (*arg == 0) {
      char reply[128];
      snprintf(reply, sizeof(reply), "name=%s pass=%s", settingsName(), settingsPass());
      serialReply(reply);
      return;
    }
    char* pass = strchr(arg, ' ');
    if (pass) { *pass++ = 0; while (*pass == ' ') pass++; }

    const char* err = nullptr;
    if (!settingsSave(arg, pass ? pass : "", &err)) {
      serialReply(err);
      return;
    }
    serialReply("ok - restarting");
    delayMs(300);
    esp_restart();
    return;
  }

  // sleepafter <seconds> — how long the pet waits with nothing driving it
  // before napping. 0 disables. Reports the current value with no argument.
  if (strncmp(line, "sleepafter", 10) == 0 &&
      (line[10] == 0 || line[10] == ' ')) {
    char* arg = line + 10;
    while (*arg == ' ') arg++;
    if (*arg == 0) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%u", (unsigned)sleepAfterSec);
      serialReply(buf);
      return;
    }
    long v = strtol(arg, nullptr, 10);
    if (v < 0) v = 0;
    if (v > SLEEP_AFTER_MAX) v = SLEEP_AFTER_MAX;
    sleepAfterSec = (uint16_t)v;
    noteActivity();                    // start the new interval from now
    serialReply("ok");
    return;
  }

  if (strcmp(line, "img") == 0) {
    // Only reachable over BLE: the serial reader intercepts "img" itself,
    // since the pixel stream has to be read off its own UART.
    serialReply("img: usb serial only");
    return;
  }

  serialReply("unknown cmd");
}

// Commands from every transport — USB serial, BLE, and the HTTP routes, which
// stamp themselves instead of queueing — are run by a single worker task, so a
// slow screen animation can never stall the NimBLE host task or the httpd task.
static QueueHandle_t s_cmdQueue = nullptr;

static void cmdWorkerTask(void* arg) {
  cmd_item_t item;
  while (true) {
    if (xQueueReceive(s_cmdQueue, &item, portMAX_DELAY) == pdTRUE) {
      ESP_LOGI(TAG, "cmd[%s]: %s", cmdSrcName(item.src), item.line);
      serialHandleLine(item.line);
    }
  }
}

static void serialTask(void* arg) {
  static cmd_item_t item;
  item.src = CMD_SRC_SERIAL;
  size_t len = 0;
  uint8_t ch;

  // Read one byte at a time: a bulk read could swallow the first bytes of an
  // "img" payload before the line was dispatched. The driver returns as soon as
  // one byte lands, so this does not spin.
  while (true) {
    int n = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100));
    if (n <= 0) continue;
    if (ch == '\n' || ch == '\r') {
      if (len) {
        item.line[len] = 0;
        // "img" streams raw pixels off this port, so it must run here, where
        // we own it — not on the worker task.
        if (strcmp(item.line, "img") == 0) serialReceiveImage();
        else xQueueSend(s_cmdQueue, &item, 0);
        len = 0;
      }
      continue;
    }
    if (len < CMD_LINE_MAX - 1) item.line[len++] = (char)ch;
  }
}

static void serialInit() {
  s_cmdQueue = xQueueCreate(8, sizeof(cmd_item_t));
  ESP_ERROR_CHECK(s_cmdQueue != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
  xTaskCreate(cmdWorkerTask, "cmd_worker", 6144, nullptr, 5, nullptr);

  // The host link. No UART0 driver: the console writes its own TX FIFO through
  // esp_stdio's VFS without one, and nothing here would read UART0's.
  // USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() sizes both rings at 256; the TX ring
  // only has to be non-zero for the install to pass, since nothing here writes
  // through it — replies go out via printf on the console path.
  usb_serial_jtag_driver_config_t usj = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  usj.rx_buffer_size = SERIAL_RX_RING;
  esp_err_t usj_err = usb_serial_jtag_driver_install(&usj);
  ESP_LOGI(TAG, "USB-Serial-JTAG driver install -> %s", esp_err_to_name(usj_err));
  if (usj_err == ESP_OK) {
    xTaskCreate(serialTask, "serial_cli", 4096, nullptr, 5, nullptr);
  } else {
    // Without the driver usb_serial_jtag_read_bytes() dereferences its NULL
    // object, so the reader must not start. BLE and WiFi still drive the pet.
    ESP_LOGE(TAG, "no USB-serial input on this boot");
  }

#if ENABLE_BLE
  // The BLE advertisement carries the same name as the SoftAP by design.
  bleCliInit(s_cmdQueue, settingsName());
#endif
}

// ── Corner transport icon (VIEW_ANIM only) ────────────────────
// Visibility is the link's own state, not command traffic: a transport that is
// connected shows, and the link whose state changed most recently holds the
// corner for ICON_GRACE_MS. The colour carries the same distinction — accent
// while the link is up, muted grey during the grace window after it goes.
//
// That window is what makes BLE visible at all: the hook connects, writes and
// disconnects inside 0.4 s, so "connected" alone would be a flicker. Letting a
// still-connected link outrank the window would hide it just as completely, since
// the cable is usually attached — hence changedAt, not priority, decides. With
// the cable in you get accent USB at rest, a grey BT rune for 5 s after each hook
// event, and 5 s of WiFi when a phone joins or leaves the AP. s_iconPriority only
// breaks ties.
//
// Redrawn unconditionally on every housekeeping tick rather than tracked to a
// change. Five independent paths paint over that corner — the player's
// fillScreen on every state change, the boot card, the WiFi info screen,
// drawCodeView(), and the canvas/img fillScreen — and each would have to
// remember to notify us. A 16x16 blit is 512 bytes, about a tenth of a
// millisecond on the bus, so self-healing by brute force is cheaper than that
// coupling. The cost is that the icon is absent for up to one tick (200 ms)
// after a state change wipes it.
//
// (222,2)-(237,17) clears every shipped theme's bounding box: the packs that
// reach the right edge all start at y>=18, and the ones that start at y=12 all
// stop at x<=221. That is a property of this pack set, not of the format — the
// converter's 12 px default margin only guarantees x<=227, and `--no-fit` can
// produce full-bleed art, which would overwrite the icon every frame.
#define ICON_GRACE_MS 5000
#define ICON_X (DISP_W - 18)     // 222
#define ICON_Y 2

// Which link gets the corner when two changed inside the same window, or when
// none has changed recently and several are up.
static const uint8_t s_iconPriority[] = {
    CMD_SRC_SERIAL, CMD_SRC_BLE, CMD_SRC_HTTP,
};

typedef struct {
  bool       live;       // up as of the last tick
  TickType_t changedAt;  // last transition, either way; 0 = never seen
} link_t;

static link_t  s_links[CMD_SRC_COUNT];
static bool    s_iconShown  = false;
static uint8_t s_iconLogged = CMD_SRC_NONE;   // src, | 0x80 while live

static bool linkConnected(uint8_t src) {
  switch (src) {
    case CMD_SRC_SERIAL: return usb_serial_jtag_is_connected();
    case CMD_SRC_BLE:    return bleCliConnCount() > 0;
    case CMD_SRC_HTTP:   return wifiStaCount() > 0;
    default:             return false;
  }
}

static ti_glyph_t linkGlyph(uint8_t src) {
  switch (src) {
    case CMD_SRC_SERIAL: return TI_USB;
    case CMD_SRC_BLE:    return TI_BT;
    default:             return TI_WIFI;
  }
}

static void transportIconTick() {
  const TickType_t now = xTaskGetTickCount();
  const size_t n = sizeof(s_iconPriority) / sizeof(s_iconPriority[0]);

  // Every link, every tick — including while another view owns the panel, so a
  // grace window cannot stall behind a view change and reappear stale.
  for (size_t i = 0; i < n; i++) {
    const uint8_t src = s_iconPriority[i];
    link_t& l = s_links[src];
    const bool up = linkConnected(src);
    if (up != l.live) {
      l.live      = up;
      l.changedAt = now;
    }
  }

  if (currentView != VIEW_ANIM) {
    // Another view owns the panel and its own paint covered the corner. Erasing
    // here would punch a 16x16 animSurround hole in the code view's dark
    // background or in an image pushed with `img` — just forget it.
    s_iconShown  = false;
    s_iconLogged = CMD_SRC_NONE;
    return;
  }

  // Whoever changed last holds the corner for the grace window; otherwise the
  // highest-priority link that is up. The style is always the link's own state.
  uint8_t pick = CMD_SRC_NONE;
  for (size_t i = 0; i < n && pick == CMD_SRC_NONE; i++) {
    const link_t& l = s_links[s_iconPriority[i]];
    if (l.changedAt != 0 && (now - l.changedAt) < pdMS_TO_TICKS(ICON_GRACE_MS)) {
      pick = s_iconPriority[i];
    }
  }
  for (size_t i = 0; i < n && pick == CMD_SRC_NONE; i++) {
    if (s_links[s_iconPriority[i]].live) pick = s_iconPriority[i];
  }

  if (pick == CMD_SRC_NONE) {
    if (s_iconShown) {
      tft.fillRect(ICON_X, ICON_Y, TI_W, TI_H, animSurround);
      s_iconShown = false;
      ESP_LOGI(TAG, "icon: hidden");
    }
    s_iconLogged = CMD_SRC_NONE;
    return;
  }

  const bool live = s_links[pick].live;
  transportIconDraw(tft, ICON_X, ICON_Y, linkGlyph(pick),
                    live ? C_ORANGE : C_MUTED, animSurround);
  s_iconShown = true;

  // One line per visible change. The panel has no read-back path, so this is
  // how the icon's logic gets checked without a camera pointed at it.
  const uint8_t state = (uint8_t)(pick | (live ? 0x80 : 0x00));
  if (state != s_iconLogged) {
    s_iconLogged = state;
    ESP_LOGI(TAG, "icon: %s %s", cmdSrcName(pick), live ? "live" : "lost");
  }
}

// Prints `s` from (x,y) at size 1, wrapping every `perLine` characters. Needed
// because Display::write() never wraps and drawChar() only bounds-checks where a
// glyph *starts*: a line longer than the 38 characters that fit across the panel
// simply loses its tail off the edge, and the AP password is up to 63.
static void printWrapped(int16_t x, int16_t y, uint8_t perLine, const char* s) {
  uint8_t col = 0;
  tft.setCursor(x, y);
  for (; *s; s++) {
    if (col == perLine) {
      y += 12;                            // size 1 is a 12 px line here
      tft.setCursor(x, y);
      col = 0;
    }
    tft.print(*s);
    col++;
  }
}

extern "C" void app_main() {
  esp_err_t nvs = nvs_flash_init();
  if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // The device's identity lives in NVS, and the way back from a bad identity
    // is the web page — which a boot loop never reaches. So a full or
    // version-mismatched partition is erased rather than fatal. (The RF
    // calibration is cached there too; it is simply redone on the next boot.)
    ESP_LOGW(TAG, "NVS unusable (%s); erasing it", esp_err_to_name(nvs));
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs);
  // Before the radios: the AP's SSID and the BLE name both come from here.
  settingsLoad();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  mountStorage();

  gpio_set_direction((gpio_num_t)TFT_BLK, GPIO_MODE_OUTPUT);
  backlightInit();          // PWM, starts at 0%
  setBacklight(true);       // light the panel before the boot animation

  tft.init(240, 240);
  // Rotation 1 — the original sketch's setting. Note that "rotation 3 plus
  // setFlipVertical(true)" is *not* the same orientation: it equals rotation 1
  // with the row-order bit inverted, which with swap_xy on is a left-right
  // mirror of the whole image. Plain rotation 1 is the one that reads correctly.
  tft.setRotation(1);
  initColours();

  // Boot splash — commemorative card, pale yellow with black lettering.
  tft.fillScreen(SPLASH_BG);
  tft.drawImage565((DISP_W - SPLASH_TITLE_W) / 2, SPLASH_TITLE_Y,
                   SPLASH_TITLE_W, SPLASH_TITLE_H, SPLASH_TITLE_BITMAP);
  tft.drawImage565((DISP_W - SPLASH_CREDIT_W) / 2, SPLASH_CREDIT_Y,
                   SPLASH_CREDIT_W, SPLASH_CREDIT_H, SPLASH_CREDIT_BITMAP);
  delayMs(1600);

  animLogoReveal();

#if ENABLE_WIFI
  wifiInitSoftAP();

  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);
  // Both strings are whatever the settings hold, so neither fits next to its
  // label: the panel is 240 px and size 1 is 6 px per character. The name gets
  // its own line, and the password wraps rather than truncating — this screen is
  // how a phone gets onto the AP, and a 63-character password has to be readable.
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(12, 16);
  tft.print("WiFi");
  tft.setTextSize(1);
  tft.setCursor(12, 36);
  tft.print(settingsName());
  tft.setTextColor(C_MUTED);
  char passLine[80];
  snprintf(passLine, sizeof(passLine), "password: %s", settingsPass());
  printWrapped(12, 50, 38, passLine);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(12, 90);
  tft.print("Open browser:");
  tft.setTextColor(C_ORANGE);
  tft.setTextSize(2);
  tft.setCursor(12, 116);
  tft.print("192.168.4.1");
  tft.setTextColor(C_MUTED);
  tft.setTextSize(1);
  tft.setCursor(12, 146);
  tft.print("press any button to start");

  startWebServer();
#endif

  serialInit();             // also starts BLE, when ENABLE_BLE
  animInit();
  animSetBackground(animSurround);
  // A stored background beats initColours()'s default: the colour picked last
  // time is what the pet comes back with. After animInit(), so the player holds
  // the value before its first frame.
  uint16_t storedBg = 0;
  if (settingsGetBg(&storedBg)) {
    animSurround = storedBg;
    animSetBackground(animSurround);
    ESP_LOGI(TAG, "background restored from storage (0x%04x)", (unsigned)storedBg);
  }

  // The corner icon waits for the pet to own the panel: currentView is already
  // VIEW_ANIM through the boot card and the WiFi info screen, so it needs a
  // separate flag rather than a view test.
  bool petOnScreen = false;
#if ENABLE_WIFI
  // Leave the WiFi info screen up for a few seconds, then start the idle
  // animation — unless something already asked for a specific state.
  // Timed non-blocking so the serial/web handlers keep running throughout.
  const TickType_t wifiScreenUntil = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
#else
  currentView = VIEW_ANIM;
  requestState("idle");
  petOnScreen = true;
#endif

  // Start the nap timer from the moment the pet is actually on screen. A hook
  // that fired during boot may have already set a state, in which case the
  // branch above was skipped — so this cannot be left to requestState().
  noteActivity();

  while (true) {
#if ENABLE_WIFI
    if (!petOnScreen && xTaskGetTickCount() >= wifiScreenUntil) {
      petOnScreen = true;
      if (!stateRequested) {            // don't clobber an explicit request
        currentView = VIEW_ANIM;
        requestState("idle");
      }
    }
#endif
    // Nothing has driven the pet for a while: no agent is running, or it is
    // sitting on a prompt. Nap until something asks for a state again.
    if (!autoAsleep && currentView == VIEW_ANIM && sleepAfterSec > 0 &&
        (xTaskGetTickCount() - lastActivity) >=
            pdMS_TO_TICKS((uint32_t)sleepAfterSec * 1000)) {
      autoAsleep = true;
      animPlayState("sleep");
    }
    if (petOnScreen) transportIconTick();

    // The pet's background changed while the player owns the panel: only its
    // bounding box gets repainted per frame, so the margin needs this one clear.
    // Skipped for the other views — they already repainted themselves with the
    // new colour, and clearing here would wipe what they drew.
    if (bgDirty && currentView == VIEW_ANIM) {
      bgDirty = false;
      tft.fillScreen(animSurround);
    }

    // Store a new background, throttled. The picker fires continuously while
    // dragging and this is flash, so writes are at most one per two seconds — and
    // the value a drag ends on is not dropped, only delayed until the throttle
    // allows it.
    static uint16_t   persistedBg = 0;
    static TickType_t bgStoredAt  = 0;
    static bool       bgAdopted   = false;
    if (!bgAdopted) {                   // whatever boot settled on is what is stored
      persistedBg = animSurround;
      bgAdopted = true;
    }
    if (animSurround != persistedBg &&
        (bgStoredAt == 0 || (xTaskGetTickCount() - bgStoredAt) >= pdMS_TO_TICKS(2000))) {
      if (settingsSetBg(animSurround)) persistedBg = animSurround;
      bgStoredAt = xTaskGetTickCount();
    }
    delayMs(200);
  }
}
