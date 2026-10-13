// Theme packs: see theme_pack.h.
//
// A pack is a ZIP of flat entries. It is read once, straight from wherever it
// comes — off the socket for an upload, out of flash for the built-in one — and
// each entry is expanded onto the storage partition as it arrives. Nothing is
// ever held whole: the largest animation is 287 KB expanded and this board has
// no PSRAM, so the reader works in 2 KB bites and a 32 KB ring.
//
// The deflate streams are decompressed by the miniz that lives in this chip's
// mask ROM (tinfl_decompress), so none of this costs flash. That ring is not a
// scratch buffer — it is miniz's LZ dictionary, and the note on flushing it is
// the part worth reading twice.

#include "theme_pack.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "miniz.h"

#include "anim.h"
#include "settings.h"

static const char* TAG = "theme";

#define THEME_NAME_MAX 64      // littlefs' object name limit, name + NUL
#define THEME_PATH_MAX 128
#define IN_CHUNK       2048    // one bite of the pack, and the reader's buffer
#define MANIFEST_MAX   2048    // plenty: the shipped one is ~1.2 KB
#define RESERVE_BYTES  (64 * 1024)
#define STORAGE_LABEL  "storage"   // must agree with mountStorage()

// The pack built into the firmware, added to the image by
// target_add_binary_data() in main/CMakeLists.txt. It is the six states the
// hooks drive, deflated: 168 KB of flash that expands to 892 KB.
extern const uint8_t default_pack_zip_start[] asm("_binary_default_pack_zip_start");
extern const uint8_t default_pack_zip_end[]   asm("_binary_default_pack_zip_end");

// ── The partition ─────────────────────────────────────────────

static long freeBytes(void) {
  size_t total = 0, used = 0;
  if (esp_littlefs_info(STORAGE_LABEL, &total, &used) != ESP_OK) return -1;
  return (long)total - (long)used;
}

// Deletes the theme, manifest first.
//
// The order is the point: "no manifest" is what the boot path reads as "this
// device has no theme, put one back", and that has to be true for the whole time
// the partition is in an intermediate state. Deleting the animations first would
// leave a manifest pointing at files that are gone, which reads as a theme and
// plays as a blank panel.
//
// The directory itself may not exist — it is carved out of the image at build
// time, and nothing ships a theme in there any more.
static void clearTheme(void) {
  unlink(ANIM_MANIFEST);

  mkdir(ANIM_THEME_DIR, 0777);     // EEXIST is the normal case

  DIR* dir = opendir(ANIM_THEME_DIR);
  if (!dir) return;
  struct dirent* de;
  char path[THEME_PATH_MAX];
  while ((de = readdir(dir)) != nullptr) {
    if (de->d_name[0] == '.') continue;
    snprintf(path, sizeof(path), "%s/%.*s", ANIM_THEME_DIR,
             THEME_NAME_MAX - 1, de->d_name);
    unlink(path);
  }
  closedir(dir);
}

// ── Reading the pack ──────────────────────────────────────────
// A buffered pull over the source, so the ZIP parser and the decompressor can
// both ask for exactly what they want without either knowing where the bytes
// came from.

typedef struct {
  const ThemePackSource* src;
  uint8_t buf[IN_CHUNK];
  size_t  len, pos;
  bool    bad;                    // the source reported an error, not the end
} ZipReader;

static bool refill(ZipReader* r) {
  if (r->pos < r->len) return true;
  const int got = r->src->read(r->src->ctx, r->buf, sizeof(r->buf));
  if (got <= 0) {
    r->bad = (got < 0);
    return false;
  }
  r->len = (size_t)got;
  r->pos = 0;
  return true;
}

// Why the pack stopped early, for the log and the web UI.
static const char* endReason(const ZipReader* r) {
  return r->bad ? "transfer failed" : "truncated pack";
}

static bool readExact(ZipReader* r, void* dst, size_t n) {
  uint8_t* p = (uint8_t*)dst;
  while (n) {
    if (!refill(r)) return false;
    size_t take = r->len - r->pos;
    if (take > n) take = n;
    memcpy(p, r->buf + r->pos, take);
    p += take;
    r->pos += take;
    n -= take;
  }
  return true;
}

static size_t readSome(ZipReader* r, void* dst, size_t n) {
  if (!refill(r)) return 0;
  size_t take = r->len - r->pos;
  if (take > n) take = n;
  memcpy(dst, r->buf + r->pos, take);
  r->pos += take;
  return take;
}

static bool skipBytes(ZipReader* r, size_t n) {
  while (n) {
    if (!refill(r)) return false;
    size_t take = r->len - r->pos;
    if (take > n) take = n;
    r->pos += take;
    n -= take;
  }
  return true;
}

// ── Writing an entry ──────────────────────────────────────────
// Entries go either to a file on the partition or — for the manifest, which has
// to be written last — into RAM. One sink type keeps the expansion below from
// caring which.

typedef struct {
  FILE*    fd;                    // set for a file, NULL for the manifest
  uint8_t* mem;
  size_t   cap, len;              // `len` counts what has been written either way
  bool     spilled;               // the manifest did not fit
} Sink;

static bool sinkWrite(Sink* s, const uint8_t* p, size_t n) {
  if (s->fd) {
    if (fwrite(p, 1, n, s->fd) != n) return false;
    s->len += n;
    return true;
  }
  if (s->len + n > s->cap) {
    s->spilled = true;
    return false;
  }
  memcpy(s->mem + s->len, p, n);
  s->len += n;
  return true;
}

static const char* sinkReason(const Sink* s) {
  if (s->mem) return s->spilled ? "manifest too long" : "out of memory";
  return "no room on the partition";
}

// ── Expanding one entry ───────────────────────────────────────
// Both expanders leave the reader exactly at the end of the entry's data and
// hand back a reason on failure, or NULL. `crc` is running: it starts at 0 for
// the entry and is compared with the header's value once the data is in.

// A stored entry is a straight copy, but it still gets the CRC treatment: the
// check is what catches a body that arrived short.
static const char* expandStored(ZipReader* r, Sink* sink, uint32_t compSize,
                                uint32_t* crc) {
  uint8_t bite[IN_CHUNK];
  uint32_t left = compSize;
  while (left) {
    const size_t want = left < sizeof(bite) ? left : sizeof(bite);
    const size_t got = readSome(r, bite, want);
    if (got == 0) return endReason(r);
    *crc = esp_rom_crc32_le(*crc, bite, (uint32_t)got);
    if (!sinkWrite(sink, bite, got)) return sinkReason(sink);
    left -= (uint32_t)got;
  }
  return nullptr;
}

// One deflate stream, expanded through miniz's ring.
static const char* expandDeflated(ZipReader* r, Sink* sink, uint32_t compSize,
                                  uint32_t* crc) {
  uint8_t bite[IN_CHUNK];
  uint8_t* ring = (uint8_t*)malloc(TINFL_LZ_DICT_SIZE);
  tinfl_decompressor* d = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
  if (!ring || !d) {
    free(ring);
    free(d);
    return "out of memory";
  }

  tinfl_init(d);
  uint32_t left = compSize;         // bytes of this entry still unread
  size_t avail = 0;                 // bytes of `bite` not yet handed over
  const uint8_t* next_in = bite;
  uint8_t* next = ring;             // where the next output byte goes
  size_t outTotal = 0;
  const char* why = nullptr;

  for (;;) {
    if (avail == 0 && left > 0) {
      const size_t want = left < sizeof(bite) ? left : sizeof(bite);
      const size_t got = readSome(r, bite, want);
      if (got == 0) { why = endReason(r); break; }
      left -= (uint32_t)got;
      avail = got;
      next_in = bite;
    }

    // The room left at the write head. miniz treats the output buffer as a
    // wrapping dictionary, so this is the distance to the end of the ring.
    const size_t room = (size_t)(ring + TINFL_LZ_DICT_SIZE - next);
    // Both of miniz's sizes are in/out: what is offered going in, what it took
    // coming back — not what is left over. Reading either one as a remainder
    // gets a number in the tens of thousands where a few hundred was meant.
    size_t in = avail;
    size_t out = room;
    const uint32_t flags = left > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0;
    const tinfl_status st =
        tinfl_decompress(d, next_in, &in, ring, next, &out, flags);
    next_in += in;
    avail -= in;
    next += out;
    outTotal += out;

    if (out) {
      *crc = esp_rom_crc32_le(*crc, next - out, (uint32_t)out);
      if (!sinkWrite(sink, next - out, out)) { why = sinkReason(sink); break; }
    }

    if (st == TINFL_STATUS_DONE) break;
    if (st < TINFL_STATUS_DONE) { why = "damaged pack"; break; }
    if (st == TINFL_STATUS_HAS_MORE_OUTPUT) {
      // The ring is full and the stream is not done, so it goes back to the
      // start — WITHOUT clearing it. The whole ring is the LZ dictionary and
      // miniz looks back through all of it; windowing it down (or zeroing it)
      // corrupts the output quietly instead of failing, which is the kind of bug
      // that shows up as one wrong animation three states later.
      if (next != ring + TINFL_LZ_DICT_SIZE) { why = "damaged pack"; break; }
      next = ring;
      // Expanding the built-in pack is seconds of work on this single core, and
      // the idle task still has to run or the task watchdog fires.
      vTaskDelay(1);
    } else {
      // It wants more input. That is the normal case while the entry's own bytes
      // are still arriving; with none left, the stream was cut short.
      if (avail != 0) { why = "damaged pack"; break; }
      if (left == 0) { why = endReason(r); break; }
    }
  }

  // Whatever is left of the entry has to be stepped over: the decompressor
  // stops at the final block and does not necessarily consume the padding bits
  // after it, so the position after an entry comes from the header, never from
  // what the decompressor reports having eaten.
  if (!why && left > 0 && !skipBytes(r, left)) why = endReason(r);

  // A pack that does not expand is worth a line with numbers in it: the reasons
  // above are all "the bytes were not what the header said", and which one it
  // was is the whole question.
  if (why)
    ESP_LOGW(TAG, "deflate: %s (out %u/%u, left %u, in %u, stack %u)",
             why, (unsigned)outTotal, (unsigned)compSize, (unsigned)left,
             (unsigned)avail,
             (unsigned)uxTaskGetStackHighWaterMark(nullptr));

  free(ring);
  free(d);
  return why;
}

// ── The ZIP ───────────────────────────────────────────────────

#define SIG_LOCAL   0x04034b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_EOCD    0x06054b50u

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// An entry name becomes a path component under the theme directory, and the
// manifest that references it is plain text a human may edit — so the rules are
// narrow: ASCII, no separators, no dotfiles, and short enough for littlefs.
static bool nameOk(const char* n, size_t len) {
  if (len == 0 || len >= THEME_NAME_MAX) return false;
  if (n[0] == '.') return false;
  if (strstr(n, "..")) return false;
  for (size_t i = 0; i < len; i++) {
    const unsigned char c = (unsigned char)n[i];
    if (c <= 0x20 || c >= 0x7f || c == '/' || c == '\\') return false;
  }
  return true;
}

ThemePackResult themePackInstall(const ThemePackSource* src, long declaredBytes) {
  ThemePackResult res = {false, nullptr, 0, 0};

  clearTheme();

  // Check the room now that the old theme is gone — before it, the figure says
  // nothing about what will fit. The caller's number is a courtesy (it lets the
  // web UI refuse before two megabytes go over the air); the running total
  // below is the authority, because the pack's own index sits at the end of a
  // body that arrives as a stream and cannot be sought back to.
  const long freeNow = freeBytes();
  if (freeNow < 0) {
    res.err = "storage is not mounted";
    return res;
  }
  const long budget = freeNow - RESERVE_BYTES;
  if (budget <= 0) {
    res.err = "no room on the partition";
    return res;
  }
  if (declaredBytes > 0 && declaredBytes > budget) {
    ESP_LOGW(TAG, "pack wants %ld bytes, %ld available", declaredBytes, budget);
    res.err = "pack does not fit";
    return res;
  }

  // The manifest is held in RAM only until the last entry has landed, so there is
  // never a moment where it points at files that are not there yet. One install
  // runs at a time — the boot one finishes before the web server starts, and the
  // routes share the server's task — so a static buffer is enough.
  static uint8_t manifest[MANIFEST_MAX];
  Sink manifestSink = {nullptr, manifest, sizeof(manifest), 0, false};
  bool haveManifest = false;

  ZipReader r = {src, {0}, 0, 0, false};
  uint8_t sig[4];
  const char* why = nullptr;

  while (!why) {
    if (!readExact(&r, sig, sizeof(sig)))
      { why = endReason(&r); break; }
    const uint32_t signature = rd32(sig);
    if (signature == SIG_CENTRAL || signature == SIG_EOCD) break;   // end of the pack
    if (signature != SIG_LOCAL) { why = "damaged pack"; break; }

    uint8_t hdr[26];
    if (!readExact(&r, hdr, sizeof(hdr))) { why = endReason(&r); break; }
    const uint16_t flags = rd16(hdr + 2);
    const uint16_t method = rd16(hdr + 4);
    const uint32_t wantCrc = rd32(hdr + 10);
    const uint32_t compSize = rd32(hdr + 14);
    const uint32_t uncompSize = rd32(hdr + 18);
    const uint16_t nameLen = rd16(hdr + 22);
    const uint16_t extraLen = rd16(hdr + 24);

    // Each of these is a pack the generator did not write, so they are refused
    // with a reason rather than misread. The data-descriptor one is worth its
    // own message: it is what you get from a zip written to a pipe, and it looks
    // like corruption unless you know.
    if (flags & 0x0001) { why = "encrypted pack"; break; }
    if (flags & 0x0008) { why = "pack has no sizes in its headers"; break; }
    if (flags & 0x0800) { why = "pack has non-ASCII names"; break; }
    if (method != 0 && method != 8) { why = "unsupported compression"; break; }
    if (compSize == 0xFFFFFFFFu || uncompSize == 0xFFFFFFFFu)
      { why = "zip64 pack"; break; }
    if (nameLen == 0 || nameLen >= THEME_NAME_MAX) { why = "bad name in pack"; break; }

    char name[THEME_NAME_MAX];
    if (!readExact(&r, name, nameLen)) { why = endReason(&r); break; }
    name[nameLen] = 0;
    if (!skipBytes(&r, extraLen)) { why = endReason(&r); break; }

    // A directory entry carries no data. Zips written by some tools have them;
    // nothing else here cares.
    if (name[nameLen - 1] == '/') {
      if (!skipBytes(&r, compSize)) why = endReason(&r);
      continue;
    }
    if (!nameOk(name, nameLen)) { why = "bad name in pack"; break; }

    const bool isManifest = (strcmp(name, "manifest.txt") == 0);
    Sink fileSink = {nullptr, nullptr, 0, 0, false};
    char path[THEME_PATH_MAX];
    if (isManifest) {
      if (haveManifest) { why = "pack has two manifests"; break; }
      manifestSink.len = 0;
      manifestSink.spilled = false;
    } else {
      snprintf(path, sizeof(path), "%s/%s", ANIM_THEME_DIR, name);
      fileSink.fd = fopen(path, "wb");
      if (!fileSink.fd) { why = "cannot write to the partition"; break; }
    }

    uint32_t crc = 0;
    if (compSize == 0) {
      // A zero-byte entry: nothing to read and nothing to expand.
    } else if (method == 0) {
      why = expandStored(&r, isManifest ? &manifestSink : &fileSink, compSize, &crc);
    } else {
      why = expandDeflated(&r, isManifest ? &manifestSink : &fileSink, compSize, &crc);
    }

    Sink* sink = isManifest ? &manifestSink : &fileSink;
    if (!why && crc != wantCrc) why = "damaged pack";
    // The header's size is a claim about data the CRC cannot vouch for on its
    // own, so it is checked too: a short entry has to fail here rather than be
    // counted as room that was used.
    if (!why && sink->len != uncompSize) why = "damaged pack";

    if (fileSink.fd) {
      // The last word on whether the bytes reached the partition is the close:
      // stdio buffers, so a write that ran the partition dry reports success
      // here and fails there, and a short .caf would only show up as a blank
      // state later.
      if (fclose(fileSink.fd) != 0 && !why) why = sinkReason(&fileSink);
      // Never leave a half-written animation behind for the manifest to point at.
      if (why) unlink(path);
    }
    if (why) {
      ESP_LOGW(TAG, "entry '%s' method %u comp %u uncomp %u: %s", name,
               (unsigned)method, (unsigned)compSize, (unsigned)uncompSize, why);
      break;
    }

    if (isManifest) haveManifest = true;
    res.files++;
    res.bytes += (unsigned)sink->len;
    if ((long)res.bytes > budget) { why = "pack does not fit"; break; }
  }

  if (!why && !haveManifest) why = "pack has no manifest.txt";
  if (!why && res.files < 2) why = "pack has no animations";

  if (!why) {
    // Last, so that until this moment the partition carries no manifest at all
    // and a reboot would put the built-in theme back.
    FILE* mf = fopen(ANIM_MANIFEST, "wb");
    if (!mf || fwrite(manifest, 1, manifestSink.len, mf) != manifestSink.len)
      why = "cannot write the manifest";
    if (mf && fclose(mf) != 0) why = "cannot write the manifest";
  }

  res.ok = (why == nullptr);
  res.err = why;
  if (!res.ok) ESP_LOGE(TAG, "pack rejected: %s", why);
  return res;
}

// ── The built-in pack ─────────────────────────────────────────

typedef struct {
  const uint8_t* p;
  size_t left;
} MemSource;

static int memRead(void* ctx, void* buf, size_t len) {
  MemSource* m = (MemSource*)ctx;
  if (m->left == 0) return 0;
  const size_t n = len < m->left ? len : m->left;
  memcpy(buf, m->p, n);
  m->p += n;
  m->left -= n;
  return (int)n;
}

bool themePackInstallDefault(void) {
  const size_t blob = (size_t)(default_pack_zip_end - default_pack_zip_start);
  MemSource m = {default_pack_zip_start, blob};
  const ThemePackSource src = {memRead, &m};
  ESP_LOGI(TAG, "installing the built-in pack (%u KB, blob at %p)",
           (unsigned)(blob / 1024), (const void*)default_pack_zip_start);
  const ThemePackResult res = themePackInstall(&src, -1);
  // The flag rides with the install: it is what tells the web UI that what is on
  // the device is the firmware's own set and not one somebody uploaded.
  settingsSetThemeFactory(res.ok);
  if (res.ok)
    ESP_LOGI(TAG, "built-in theme installed: %u file(s), %u KB", res.files,
             res.bytes / 1024);
  else
    ESP_LOGE(TAG, "built-in theme failed: %s", res.err);
  return res.ok;
}

// The boot path's version, on a task of its own. Two reasons, both about the
// caller: app_main's stack is 3.5 KB and this expands most of a megabyte, and
// running it inline would also hold up the boot for the seconds it takes.
static SemaphoreHandle_t s_done;
static bool s_bootOk;

static void installOnTask(void*) {
  s_bootOk = themePackInstallDefault();
  xSemaphoreGive(s_done);
  vTaskDelete(nullptr);
}

bool themePackInstallDefaultOnTask(void) {
  s_done = xSemaphoreCreateBinary();
  if (!s_done) return false;
  if (xTaskCreate(installOnTask, "theme", 8192, nullptr, 5, nullptr) != pdPASS) {
    vSemaphoreDelete(s_done);
    s_done = nullptr;
    return false;
  }
  xSemaphoreTake(s_done, portMAX_DELAY);
  vSemaphoreDelete(s_done);
  s_done = nullptr;
  return s_bootOk;
}

bool themePackPresent(void) {
  FILE* fd = fopen(ANIM_MANIFEST, "r");
  if (!fd) return false;
  fclose(fd);
  return true;
}
