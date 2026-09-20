/*
 * ╔══════════════════════════════════════════════════════════════╗
 *   CLAWD MOCHI — ESP32 + ST7789 240×240
 *   ESP-IDF port (C++)
 *
 *   Wiring:
 *     SDA → GPIO 21  (hardware SPI MOSI)
 *     SCL → GPIO 18  (hardware SPI SCK)
 *     RST → GPIO 23
 *     DC  → GPIO 5
 *     CS  → GPIO 32
 *     BL  → GPIO 14
 *     VCC → 3V3
 *     GND → GND
 *
 *   WiFi: "ClaWD-Mochi"  pw: clawd1234  → http://192.168.4.1
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

#include "display.h"
#include "logo_data.h"
#include "esp_littlefs.h"

static const char* TAG = "clawd_mochi";

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS  32
#define TFT_DC  5
#define TFT_RST 23
#define TFT_BLK 14
#define TFT_MOSI 21
#define TFT_SCLK 18

static Display tft(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK);

// ── WiFi ──────────────────────────────────────────────────────
static const char* AP_SSID = "ClaWD-Mochi";
static const char* AP_PASS = "clawd1234";

// ── Display ───────────────────────────────────────────────────
#define DISP_W 240
#define DISP_H 240

// ── Eye constants (shared by both eye views) ──────────────────
#define EYE_W   30
#define EYE_H   60
#define EYE_GAP 120
#define EYE_OX  0
#define EYE_OY  40

// ── Colours ───────────────────────────────────────────────────
static uint16_t C_ORANGE, C_DARKBG, C_MUTED, C_GREEN;
#define C_WHITE ST77XX_WHITE
#define C_BLACK ST77XX_BLACK

// ── State ─────────────────────────────────────────────────────
#define VIEW_EYES_NORMAL 0
#define VIEW_EYES_SQUISH 1
#define VIEW_CODE        2
#define VIEW_DRAW        3

static uint8_t  currentView  = VIEW_EYES_NORMAL;
static bool     busy         = false;
static bool     backlightOn  = true;

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
static uint8_t  animSpeed    = 1;

static uint16_t animBgColor  = 0;
static uint16_t drawBgColor  = 0;

// ── Terminal ──────────────────────────────────────────────────
#define TERM_COLS      15
#define TERM_ROWS       8
#define TERM_CHAR_W    12
#define TERM_CHAR_H    20
#define TERM_PAD_X      8
#define TERM_PAD_Y     18

static bool     termMode    = false;
static char     termLines[TERM_ROWS][TERM_COLS + 1];
static uint8_t  termRow     = 0;
static uint8_t  termCol     = 0;

// ── Logo ──────────────────────────────────────────────────────
#define LOGO_CX 120
#define LOGO_CY 105

static httpd_handle_t server = nullptr;

// ═════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════

static void delayMs(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static int speedMs(int ms) {
  if (animSpeed == 3) return ms / 2;
  if (animSpeed == 1) return ms * 2;
  return ms;
}

static uint16_t hexToRgb565(const char* hex) {
  while (*hex == '#') hex++;
  if (strlen(hex) != 6) return C_WHITE;
  char tmp[7];
  strncpy(tmp, hex, 6);
  tmp[6] = 0;
  long v = strtol(tmp, nullptr, 16);
  return Display::color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

static void initColours() {
  C_ORANGE = Display::color565(218, 17, 0);
  C_DARKBG = Display::color565(10,  12,  16);
  C_MUTED  = Display::color565(90,  88,  86);
  C_GREEN  = Display::color565(80, 220, 130);
  animBgColor = C_ORANGE;
  drawBgColor = C_ORANGE;
}

// ═════════════════════════════════════════════════════════════
//  LOGO
// ═════════════════════════════════════════════════════════════

static void drawLogoFilled(uint16_t bg, uint16_t fg) {
  tft.fillScreen(bg);
  for (uint16_t i = 0; i < LOGO_TRI_COUNT; i++) {
    tft.fillTriangle(LOGO_TRIS[i][0], LOGO_TRIS[i][1], LOGO_TRIS[i][2],
                     LOGO_TRIS[i][3], LOGO_TRIS[i][4], LOGO_TRIS[i][5], fg);
  }
  tft.setTextColor(fg);
  tft.setTextSize(2);
  tft.setCursor(LOGO_CX - 54, 210);
  tft.print("Anthropic");
  tft.setCursor(LOGO_CX - 53, 210);
  tft.print("Anthropic");
}

// ═════════════════════════════════════════════════════════════
//  VIEWS
// ═════════════════════════════════════════════════════════════

static inline int16_t eyeLX(int16_t ox) {
  return (DISP_W - (EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox;
}
static inline int16_t eyeRX(int16_t ox) { return eyeLX(ox) + EYE_W + EYE_GAP; }
static inline int16_t eyeY()            { return (DISP_H - EYE_H) / 2 - EYE_OY; }
static inline int16_t eyeCY()           { return eyeY() + EYE_H / 2; }

static void drawNormalEyes(int16_t ox = 0, bool blink = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeY();
  if (!blink) {
    tft.fillRect(lx, ey, EYE_W, EYE_H, C_BLACK);
    tft.fillRect(rx, ey, EYE_W, EYE_H, C_BLACK);
  } else {
    tft.fillRect(lx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
    tft.fillRect(rx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
  }
}

static void drawChevron(int16_t cx, int16_t cy, int16_t arm, int16_t reach,
                        uint8_t thk, bool rightFacing, uint16_t col) {
  for (int8_t t = -(int8_t)thk; t <= (int8_t)thk; t++) {
    if (rightFacing) {
      tft.drawLine(cx - reach / 2, cy - arm + t, cx + reach / 2, cy + t, col);
      tft.drawLine(cx + reach / 2, cy + t, cx - reach / 2, cy + arm + t, col);
    } else {
      tft.drawLine(cx + reach / 2, cy - arm + t, cx - reach / 2, cy + t, col);
      tft.drawLine(cx - reach / 2, cy + t, cx + reach / 2, cy + arm + t, col);
    }
  }
}

static void drawSquishEyes(bool closed = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  const int16_t arm   = EYE_H / 2;
  const int16_t reach = EYE_W / 2;
  const int16_t lcx   = lx + EYE_W / 2;
  const int16_t rcx   = rx + EYE_W / 2;
  if (!closed) {
    drawChevron(lcx, cy, arm, reach, 10, true,  C_BLACK);
    drawChevron(rcx, cy, arm, reach, 10, false, C_BLACK);
  } else {
    tft.fillRect(lx, cy - 5, EYE_W, 10, C_BLACK);
    tft.fillRect(rx, cy - 5, EYE_W, 10, C_BLACK);
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

static void animNormalEyes() {
  busy = true;
  const int16_t offs[] = {-16, 16, -16, 16, 0};
  for (uint8_t i = 0; i < 5; i++) { drawNormalEyes(offs[i]); delayMs(speedMs(80)); }
  drawNormalEyes(0, true);  delayMs(speedMs(100));
  drawNormalEyes(0, false); delayMs(speedMs(70));
  drawNormalEyes(0, true);  delayMs(speedMs(70));
  drawNormalEyes(0, false);
  busy = false;
}

static void animSquishEyes() {
  busy = true;
  for (uint8_t i = 0; i < 3; i++) {
    drawSquishEyes(false); delayMs(speedMs(160));
    drawSquishEyes(true);  delayMs(speedMs(100));
  }
  drawSquishEyes(false);
  busy = false;
}

static void animLogoReveal() {
  busy = true;
  tft.fillScreen(animBgColor);
  for (uint16_t i = 0; i < LOGO_SEG_COUNT; i++) {
    tft.drawLine(LOGO_SEGS[i][0], LOGO_SEGS[i][1], LOGO_SEGS[i][2], LOGO_SEGS[i][3], C_WHITE);
    tft.drawLine(LOGO_SEGS[i][0] + 1, LOGO_SEGS[i][1], LOGO_SEGS[i][2] + 1, LOGO_SEGS[i][3], C_WHITE);
    if (i % 4 == 0) delayMs(speedMs(8));
  }
  drawLogoFilled(animBgColor, C_WHITE);
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

static bool getQueryArg(httpd_req_t* req, const char* key, char* out, size_t outlen) {
  size_t len = httpd_req_get_url_query_len(req) + 1;
  if (len <= 1) return false;
  char* buf = (char*)malloc(len);
  if (!buf) return false;
  if (httpd_req_get_url_query_str(req, buf, len) != ESP_OK) { free(buf); return false; }
  bool found = (httpd_query_key_value(buf, key, out, outlen) == ESP_OK);
  free(buf);
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

static esp_err_t routeCmd(httpd_req_t* req) {
  char k[8] = {0};
  if (!getQueryArg(req, "k", k, sizeof(k)) || k[0] == 0) {
    sendJson(req, "{\"e\":1}");
    return ESP_OK;
  }
  const char c = k[0];

  if (termMode) {
    if (c == 'q') { termMode = false; drawCodeView(); }
    sendJson(req, "{\"ok\":1}");
    return ESP_OK;
  }

  sendJson(req, "{\"ok\":1}");
  switch (c) {
    case 'w': currentView = VIEW_EYES_NORMAL; animNormalEyes(); break;
    case 's': currentView = VIEW_EYES_SQUISH; animSquishEyes(); break;
    case 'd':
      currentView = VIEW_CODE; drawCodeView();
      termMode = true; termClear(); termFullRedraw(); break;
    case 'a':
      currentView = VIEW_EYES_NORMAL;
      animLogoReveal();
      break;
  }
  return ESP_OK;
}

static esp_err_t routeChar(httpd_req_t* req) {
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

static esp_err_t routeSpeed(httpd_req_t* req) {
  char v[8] = {0};
  if (getQueryArg(req, "v", v, sizeof(v))) {
    int s = atoi(v);
    if (s < 1) s = 1;
    if (s > 3) s = 3;
    animSpeed = (uint8_t)s;
  }
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeRedraw(httpd_req_t* req) {
  char bg[16] = {0};
  if (getQueryArg(req, "bg", bg, sizeof(bg))) {
    animBgColor = hexToRgb565(bg);
    drawBgColor = animBgColor;
  }
  switch (currentView) {
    case VIEW_EYES_NORMAL: drawNormalEyes(); break;
    case VIEW_EYES_SQUISH: drawSquishEyes(); break;
    case VIEW_CODE:        drawCodeView();   break;
    case VIEW_DRAW:        tft.fillScreen(drawBgColor); break;
  }
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeCanvas(httpd_req_t* req) {
  char on[8] = {0};
  if (getQueryArg(req, "on", on, sizeof(on)) && strcmp(on, "1") == 0) {
    currentView = VIEW_DRAW;
    tft.fillScreen(drawBgColor);
  }
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeDrawClear(httpd_req_t* req) {
  char bg[16] = {0};
  if (getQueryArg(req, "bg", bg, sizeof(bg))) {
    drawBgColor = hexToRgb565(bg);
    animBgColor = drawBgColor;
  } else {
    drawBgColor = hexToRgb565("#aa4818");
    animBgColor = drawBgColor;
  }
  currentView = VIEW_DRAW;
  termMode = false;
  tft.fillScreen(drawBgColor);
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeDrawStroke(httpd_req_t* req) {
  char pen[16] = {0};
  char pts[512] = {0};
  if (!getQueryArg(req, "pen", pen, sizeof(pen)) ||
      !getQueryArg(req, "pts", pts, sizeof(pts))) {
    sendJson(req, "{\"ok\":1}");
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
  char on[8] = {0};
  bool enable = getQueryArg(req, "on", on, sizeof(on)) && strcmp(on, "1") == 0;
  setBacklight(enable);
  sendJson(req, "{\"ok\":1}");
  return ESP_OK;
}

static esp_err_t routeState(httpd_req_t* req) {
  char j[128];
  snprintf(j, sizeof(j),
           "{\"view\":%u,\"busy\":%s,\"term\":%s,\"bl\":%s,\"speed\":%u}",
           currentView,
           busy ? "true" : "false",
           termMode ? "true" : "false",
           backlightOn ? "true" : "false",
           animSpeed);
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

static void wifiInitSoftAP() {
  esp_netif_t* ap = esp_netif_create_default_wifi_ap();
  (void)ap;

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_config = {};
  strncpy((char*)wifi_config.ap.ssid, AP_SSID, sizeof(wifi_config.ap.ssid));
  strncpy((char*)wifi_config.ap.password, AP_PASS, sizeof(wifi_config.ap.password));
  wifi_config.ap.ssid_len = strlen(AP_SSID);
  wifi_config.ap.max_connection = 4;
  wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "SoftAP started: %s (pw: %s)", AP_SSID, AP_PASS);
}

static void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;
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
  r.uri = "/speed";       r.handler = routeSpeed;      httpd_register_uri_handler(server, &r);
  r.uri = "/redraw";      r.handler = routeRedraw;     httpd_register_uri_handler(server, &r);
  r.uri = "/canvas";      r.handler = routeCanvas;     httpd_register_uri_handler(server, &r);
  r.uri = "/draw/clear";  r.handler = routeDrawClear;  httpd_register_uri_handler(server, &r);
  r.uri = "/draw/stroke"; r.handler = routeDrawStroke; httpd_register_uri_handler(server, &r);
  r.uri = "/backlight";   r.handler = routeBacklight;  httpd_register_uri_handler(server, &r);
  r.uri = "/state";       r.handler = routeState;      httpd_register_uri_handler(server, &r);

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

extern "C" void app_main() {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  mountStorage();

  gpio_set_direction((gpio_num_t)TFT_BLK, GPIO_MODE_OUTPUT);
  backlightInit();          // PWM, starts at 0%
  setBacklight(true);       // light the panel before the boot animation

  tft.init(240, 240);
  // Rotation 3 = landscape rotated 180° vs. rotation 1. Chosen to match how the
  // panel is physically mounted here (the original sketch used rotation 1).
  tft.setRotation(3);
  initColours();

  // Boot splash
  tft.fillScreen(animBgColor);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(3);
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 - 22);
  tft.print("Clawd");
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 + 14);
  tft.print("Mochi");
  delayMs(1200);

  animLogoReveal();

  wifiInitSoftAP();

  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(12, 16);
  tft.print("WiFi: ClaWD-Mochi");
  tft.setTextColor(C_MUTED);
  tft.setTextSize(1);
  tft.setCursor(12, 44);
  tft.print("password: clawd1234");
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(12, 68);
  tft.print("Open browser:");
  tft.setTextColor(C_ORANGE);
  tft.setTextSize(2);
  tft.setCursor(12, 94);
  tft.print("192.168.4.1");
  tft.setTextColor(C_MUTED);
  tft.setTextSize(1);
  tft.setCursor(12, 124);
  tft.print("press any button to start");

  startWebServer();

  // Nothing to poll — the http server task handles requests internally.
  while (true) {
    delayMs(1000);
  }
}
