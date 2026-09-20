// Plays .caf animations from the theme directory on LittleFS.
//
// Why the format is what it is — this board is a classic ESP32 with no PSRAM:
//   * a decoded 240x240 frame is 115 KB, and a palette canvas is 57 KB, either
//     of which would not survive alongside WiFi + BLE in ~150 KB of heap;
//   * so each frame is stored as palette-index run-length pairs and cropped to
//     the artwork's bounding box, and the player walks those runs once per
//     frame, pushing a band of rows at a time straight to the panel.
//
// Cropping to the bbox is the important part for speed: with the official
// esp_lcd driver a rectangle's address window costs ~230 us regardless of its
// payload, and the bus moves only ~2 bytes/us here, so the panel-sized canvas
// (mostly empty margin) would cost ~35 ms a frame on its own. The artwork's own
// box is what fits in a 33 ms frame at 30 fps.
//
// Nothing here allocates more than a band, and the palette is already packed as
// RGB565, so playback costs the heap nothing and the inner loop is a copy.

#include "anim.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "display.h"

static const char* TAG = "anim";

#define THEME_DIR     "/littlefs/theme"
#define MANIFEST_PATH THEME_DIR "/manifest.txt"

#define STATE_MAX     24     // longest state name we accept
// Not PATH_MAX: that is a standard <limits.h> macro and redefining it clashes.
#define ANIM_PATH_MAX 128    // theme dir + "/" + filename, comfortably
#define FILE_NAME_MAX 64
#define BAND_ROWS     8      // rows per panel transfer (matches FILL_PIXELS)

// Pacing comes from the pack, not from a constant here. Each .caf carries a
// per-frame duration (the converter writes the GIF's own delay, and folds in
// the time of any frames dropped by --stride), so a theme authored at 8 fps and
// one authored at 17 fps each play at the rate they were drawn for. Picking one
// rate for every theme is what made calico run ~1.8x fast.
//
// animSetSpeed() scales that authored timing rather than replacing it: 2 is the
// pack's own rate, 1 is 1.5x slower, 3 is 0.67x.
#define ANIM_DELAY_MIN_MS 10        // guard against a zero/absurd table entry
#define ANIM_DELAY_MAX_MS 500
static volatile uint16_t s_speedNum = 1;
static volatile uint16_t s_speedDen = 1;

// Log the achieved frame rate every N frames. Handy when tuning ANIM_FPS or the
// .caf encoding against this board's SPI ceiling; off by default to keep the
// serial console quiet.
#define ANIM_LOG_FPS  0
#define ANIM_LOG_EVERY 120

// Must match display.cpp's STREAM_DEPTH: streamRect() keeps that many
// transfers in flight, so this many pixel buffers must stay valid.
#define STREAM_BUFS   4

// .caf layout (see tools/gif2caf.py): magic(4) then x0,y0,w,h,frames as u16
// (14 bytes total), then 256 RGB565 palette entries, then the offset and delay
// tables. Getting this wrong shifts everything and the frame count reads as zero.
#define CAF_HEADER_LEN 14
#define CAF_PALETTE_LEN 512
#define CAF_TABLE_OFF (CAF_HEADER_LEN + CAF_PALETTE_LEN)

// The player owns the panel while an animation runs; main.cpp hands it over by
// parking its own drawing (setState() is the only entry point).
extern Display tft;

static SemaphoreHandle_t s_lock = nullptr;
static char              s_state[STATE_MAX] = "";   // requested state, "" = idle
static uint32_t          s_gen = 0;                 // bumped on every request
static uint16_t          s_bg = 0xFFFF;             // clear colour (theme white)
// Magnification the pack asks the device to apply when pushing a frame, from
// the manifest's `scale=` line. 1 means the .caf is already panel-sized.
#define ANIM_SCALE_MAX 4        // must stay a divisor of BAND_ROWS
static uint16_t          s_scale = 1;

#if ANIM_LOG_FPS
// Playback stats, accumulated across animation loops so short animations are
// still measured. `render` is the cost of pushing one frame (the number to
// watch); `wait` is the pacing sleep and should make up the difference.
static uint32_t s_logFrames = 0;
static int64_t  s_logT0 = 0;
static int64_t  s_renderUs = 0;
static int64_t  s_waitUs = 0;
#endif

// ── manifest ──────────────────────────────────────────────────
// One "state=file" pair per line; '#' starts a comment. Deliberately plain text
// so a theme can be edited on the device without a JSON parser.
static bool manifestLookup(const char* state, char* file, size_t fileLen) {
  FILE* fd = fopen(MANIFEST_PATH, "r");
  if (!fd) {
    ESP_LOGW(TAG, "no manifest at %s", MANIFEST_PATH);
    return false;
  }

  char line[ANIM_PATH_MAX];
  bool found = false;
  while (fgets(line, sizeof(line), fd)) {
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == '\n' || *p == 0) continue;

    char* eq = strchr(p, '=');
    if (!eq) continue;
    *eq = 0;
    char* key = p;
    char* val = eq + 1;

    // trim key and value
    char* e = key + strlen(key);
    while (e > key && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    while (*val == ' ' || *val == '\t') val++;
    char* ve = val + strlen(val);
    while (ve > val && (ve[-1] == '\n' || ve[-1] == '\r' || ve[-1] == ' ')) *--ve = 0;

    // `scale` is the pack's magnification for the device to apply, not a state.
    // Refresh it on every lookup so a theme swapped in at runtime takes effect
    // without a reboot.
    if (strcmp(key, "scale") == 0) {
      long v = strtol(val, nullptr, 10);
      if (v < 1) v = 1;
      if (v > ANIM_SCALE_MAX) v = ANIM_SCALE_MAX;
      s_scale = (uint16_t)v;
      continue;
    }

    if (strcmp(key, state) == 0) {
      strncpy(file, val, fileLen - 1);
      file[fileLen - 1] = 0;
      found = true;
      break;
    }
  }
  fclose(fd);
  return found;
}

void animListStates(char* out, unsigned outLen) {
  out[0] = 0;
  FILE* fd = fopen(MANIFEST_PATH, "r");
  if (!fd) return;

  char line[ANIM_PATH_MAX];
  while (fgets(line, sizeof(line), fd)) {
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == '\n' || *p == 0) continue;
    char* eq = strchr(p, '=');
    if (!eq) continue;
    *eq = 0;
    char* e = p + strlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    if (strcmp(p, "scale") == 0) continue;    // pack config, not a state

    unsigned used = strlen(out);
    if (used + strlen(p) + 2 >= outLen) break;
    if (used) strcat(out, " ");
    strcat(out, p);
  }
  fclose(fd);
}

// ── .caf decoding ─────────────────────────────────────────────

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// Renders one frame's runs into row bands and pushes them to the panel.
// Runs are consumed with a tiny state machine, so no frame-sized buffer is
// needed anywhere. Bands go through Display's batched streaming path: with a
// full frame costing ~27 window sets, drawImage565()'s per-call lock and DMA
// wait would add several milliseconds for nothing.
// A pack may store its artwork below panel size and ask the device to magnify it
// on push (the theme manifest's scale= line). That is worth doing for a set
// drawn small — cloudling's crab is natively 113x114 — because RLE runs stay
// long only while pixels stay crisp: scaling the art up at conversion time
// costs bytes as well as sharpness. Magnification here is integer and
// nearest-neighbour, so a magnified frame is what the converter would have
// written anyway, minus the file size.
static bool renderFrame(FILE* fd, uint32_t blobOffset, uint16_t x0, uint16_t y0,
                        uint16_t w, uint16_t h, const uint16_t* palette) {
  // streamRect() hands each buffer to the DMA engine and returns immediately,
  // so a buffer must not be reused until its transfer completes — hence several.
  static uint16_t band[STREAM_BUFS][240 * BAND_ROWS];

  const uint16_t scale = s_scale ? s_scale : 1;
  const uint16_t rowsPerBand = (uint16_t)(BAND_ROWS / scale);
  const uint16_t outW = (uint16_t)(w * scale);
  const uint32_t bandPixels = (uint32_t)outW * BAND_ROWS;
  const uint32_t totalPixels = (uint32_t)w * h;

  // Bands are a whole number of output rows, so the scale has to divide
  // BAND_ROWS; and a band can never exceed the scratch buffer or the panel.
  if (rowsPerBand == 0 || (BAND_ROWS % scale) || (h % rowsPerBand)) {
    ESP_LOGW(TAG, "%ux%u at scale %u does not band evenly", w, h, scale);
    return false;
  }
  if (outW > 240 || bandPixels > 240 * BAND_ROWS) {
    ESP_LOGW(TAG, "frame %ux%u at scale %u does not fit the panel", w, h, scale);
    return false;
  }
  if (fseek(fd, (long)blobOffset, SEEK_SET) != 0) return false;

  // Snapshot the surround: animSetBackground() can be called from another task
  // between bands, and a frame drawn half in one colour and half in another
  // would show as a seam.
  const uint16_t bg = s_bg;

  tft.streamBegin();

  uint32_t done = 0, slot = 0;
  uint16_t bandIdx = 0, bandEndRow = rowsPerBand;
  uint16_t sr = 0, sc = 0;
  int remaining = 0;
  uint16_t color = 0;
  bool field = false;         // run is palette index 0 — leave the surround be

  // Palette index 0 is the .caf's field, not a colour to draw. Pre-fill each
  // band with the surround and skip index-0 runs, so the field shows through.
  // That is what makes the background a runtime setting: the colour baked into
  // the file is never actually displayed.
  uint16_t* out = band[slot];
  for (uint32_t i = 0; i < bandPixels; i++) out[i] = bg;

  while (done < totalPixels) {
    if (remaining == 0) {
      uint8_t run[2];
      if (fread(run, 1, 2, fd) != 2) {
        tft.streamEnd();
        return false;
      }
      remaining = run[0];
      field = (run[1] == 0);
      color = palette[run[1]];
      if (remaining == 0) continue;
    }

    // Pixel at a time, not run at a time: with scale > 1 each source pixel
    // becomes a square rather than a single entry, so a run is no longer a
    // contiguous stretch of the band buffer.
    while (remaining > 0) {
      if (sr >= bandEndRow) {
        tft.streamRect(x0, (uint16_t)(y0 + bandIdx * BAND_ROWS),
                       outW, BAND_ROWS, out);
        slot = (slot + 1) % STREAM_BUFS;
        out = band[slot];
        for (uint32_t i = 0; i < bandPixels; i++) out[i] = bg;
        bandIdx++;
        bandEndRow = (uint16_t)(bandEndRow + rowsPerBand);
        continue;               // this pixel belongs to the band just started
      }

      if (!field) {
        uint16_t* p = out + (uint32_t)((sr - (uint16_t)(bandIdx * rowsPerBand)) * scale)
                          * outW + (uint32_t)sc * scale;
        for (uint16_t dy = 0; dy < scale; dy++) {
          uint16_t* q = p + (uint32_t)dy * outW;
          for (uint16_t dx = 0; dx < scale; dx++) q[dx] = color;
        }
      }

      if (++sc == w) { sc = 0; sr++; }
      done++;
      remaining--;
    }
  }

  // The final band is never closed by the loop above — it is only pushed when
  // the *next* band starts.
  tft.streamRect(x0, (uint16_t)(y0 + bandIdx * BAND_ROWS), outW, BAND_ROWS, out);
  tft.streamEnd();
  return true;
}

// Plays one animation until its frame count elapses, the state changes, or the
// file turns out to be unusable. Returns false only on a hard failure.
static bool playOnce(const char* state, uint32_t gen) {
  char file[FILE_NAME_MAX];
  if (!manifestLookup(state, file, sizeof(file))) {
    ESP_LOGW(TAG, "state '%s' not in manifest", state);
    return false;
  }

  char path[ANIM_PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", THEME_DIR, file);

  FILE* fd = fopen(path, "r");
  if (!fd) {
    ESP_LOGW(TAG, "cannot open %s", path);
    return false;
  }
  ESP_LOGI(TAG, "playing %s", path);

  uint8_t head[CAF_HEADER_LEN];
  if (fread(head, 1, sizeof(head), fd) != sizeof(head) ||
      memcmp(head, "CAF3", 4) != 0) {
    ESP_LOGW(TAG, "%s: bad header", path);
    fclose(fd);
    return false;
  }
  const uint16_t x0 = rd16(head + 4);
  const uint16_t y0 = rd16(head + 6);
  const uint16_t w = rd16(head + 8);
  const uint16_t h = rd16(head + 10);
  const uint16_t frames = rd16(head + 12);
  if (frames == 0 || w == 0 || h == 0) {
    ESP_LOGW(TAG, "%s: bad dimensions (%ux%u, %u frames)", path, w, h, frames);
    fclose(fd);
    return false;
  }

  uint16_t palette[256];
  uint8_t pal[CAF_PALETTE_LEN];
  if (fread(pal, 1, sizeof(pal), fd) != sizeof(pal)) {
    fclose(fd);
    return false;
  }
  for (int i = 0; i < 256; i++) palette[i] = rd16(pal + i * 2);

  uint8_t table[4];
  uint8_t delayBuf[2];
  // The delay table follows the offset table, both sized by the frame count.
  const long delayTable = CAF_TABLE_OFF + (long)frames * 4;

  for (uint16_t f = 0; f < frames; f++) {
    if (s_gen != gen) {                 // another state was requested
      fclose(fd);
      return true;
    }

    const int64_t t0 = esp_timer_get_time();

    // Seek per frame rather than reading the offset table straight through: the
    // duration lookup below would otherwise have to rewind past it anyway.
    if (fseek(fd, CAF_TABLE_OFF + (long)f * 4, SEEK_SET) != 0) break;
    if (fread(table, 1, 4, fd) != 4) break;
    const uint32_t offset = rd32(table);

    if (fseek(fd, delayTable + (long)f * 2, SEEK_SET) != 0) break;
    if (fread(delayBuf, 1, 2, fd) != 2) break;
    uint16_t authoredMs = rd16(delayBuf);

    if (!renderFrame(fd, offset, x0, y0, w, h, palette)) break;

    // The pack's own timing, scaled by the speed setting. Rendering is now much
    // cheaper than the SPI-bound full frame, so this wait (not the bus) sets the
    // frame rate; subtract the work already done so a slow frame does not add
    // to the interval.
    if (authoredMs < ANIM_DELAY_MIN_MS) authoredMs = ANIM_DELAY_MIN_MS;
    if (authoredMs > ANIM_DELAY_MAX_MS) authoredMs = ANIM_DELAY_MAX_MS;

    const int64_t renderUs = esp_timer_get_time() - t0;
    uint32_t wantMs = (uint32_t)authoredMs * s_speedNum / s_speedDen;
    if (wantMs < ANIM_DELAY_MIN_MS) wantMs = ANIM_DELAY_MIN_MS;
    const int waitMs = (int)wantMs - (int)(renderUs / 1000);
    if (waitMs > 0) vTaskDelay(pdMS_TO_TICKS(waitMs));

#if ANIM_LOG_FPS
    if (s_logT0 == 0) s_logT0 = t0;
    s_renderUs += renderUs;
    s_waitUs += esp_timer_get_time() - t0 - renderUs;
    if (++s_logFrames >= ANIM_LOG_EVERY) {
      const int64_t span = esp_timer_get_time() - s_logT0;
      ESP_LOGI(TAG, "%.1f fps; render %.1f ms, wait %.1f ms",
               s_logFrames * 1e6 / (double)span,
               s_renderUs / 1000.0 / s_logFrames,
               s_waitUs / 1000.0 / s_logFrames);
      s_logFrames = 0;
      s_renderUs = 0;
      s_waitUs = 0;
      s_logT0 = esp_timer_get_time();
    }
#endif
  }

  fclose(fd);
  return true;
}

// ── task ──────────────────────────────────────────────────────

static void animTask(void* arg) {
  char cur[STATE_MAX];
  char prev[STATE_MAX] = "";        // what is currently on the panel
  uint32_t gen = 0;

  while (true) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strncpy(cur, s_state, sizeof(cur) - 1);
    cur[sizeof(cur) - 1] = 0;
    gen = s_gen;
    xSemaphoreGive(s_lock);

    // Clear once per switch, before the first frame of the new state. Frames are
    // stored cropped to their own bounding box, so the previous state's pixels
    // outside the new box would otherwise stay on the panel — very visible when
    // going from a large animation to a small one.
    //
    // Deliberately not done when stopping (empty state): a command like 'd' stops
    // the player and then paints a full-screen view, and clearing here would race
    // that paint. Stopping therefore leaves the last frame up, and any view
    // drawn afterwards repaints the whole panel anyway.
    if (cur[0] != 0 && strcmp(cur, prev) != 0) {
      tft.fillScreen(s_bg);
    }
    strncpy(prev, cur, sizeof(prev) - 1);
    prev[sizeof(prev) - 1] = 0;

    if (cur[0] == 0) {                  // idle: nothing to draw
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!playOnce(cur, gen)) {
      // Missing or broken animation — drop the request so we do not spin.
      xSemaphoreTake(s_lock, portMAX_DELAY);
      if (s_gen == gen) s_state[0] = 0;
      xSemaphoreGive(s_lock);
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

void animSetBackground(unsigned short colour) { s_bg = (uint16_t)colour; }

// 1 = slow, 2 = the pack's own rate, 3 = fast. Anything else falls back to 2.
void animSetSpeed(unsigned level) {
  switch (level) {
    case 1:  s_speedNum = 3; s_speedDen = 2; break;   // 1.5x the authored delay
    case 3:  s_speedNum = 2; s_speedDen = 3; break;   // 0.67x
    default: s_speedNum = 1; s_speedDen = 1; break;   // as authored
  }
}

void animPlayState(const char* state) {
  if (!s_lock) {
    // The player is not up yet — a command can arrive during the boot splash,
    // since serialInit() runs before animInit(). Remember the request; animTask
    // reads s_state as soon as it starts. Safe unsynchronised: the task does not
    // exist yet, so there is no concurrent reader.
    strncpy(s_state, state ? state : "", sizeof(s_state) - 1);
    s_state[sizeof(s_state) - 1] = 0;
    return;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  strncpy(s_state, state ? state : "", sizeof(s_state) - 1);
  s_state[sizeof(s_state) - 1] = 0;
  s_gen++;
  xSemaphoreGive(s_lock);
}

void animInit(void) {
  s_lock = xSemaphoreCreateMutex();
  if (!s_lock) {
    ESP_LOGE(TAG, "no memory for anim lock");
    return;
  }
  xTaskCreate(animTask, "anim", 4096, nullptr, 4, nullptr);
}
