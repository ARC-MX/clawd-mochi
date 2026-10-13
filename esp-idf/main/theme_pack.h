// Theme packs — the .zip tools/mochi_theme.py bundle writes, installed over
// whatever theme the device is holding.
//
// The storage partition holds exactly one theme and a pack is most of it, so
// there is nowhere to stage a second copy while the first is still there: the
// old theme is deleted before the new pack is read, and the new manifest is
// written last. A failure therefore leaves the partition with no manifest —
// the one state the rest of the firmware knows how to recover from, which is
// what themePackInstallDefault() is for, and what the boot path checks.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where a pack's bytes come from. `read` moves up to `len` bytes into `buf` and
// returns how many (> 0), or 0 at the end of the input, or -1 on error. A pack
// that stops early is caught by the reader rather than by a caller: it needs the
// ZIP's end-of-archive marker, so a truncated upload cannot look like a pack
// that simply ended.
typedef struct {
  int (*read)(void* ctx, void* buf, size_t len);
  void* ctx;
} ThemePackSource;

typedef struct {
  bool ok;
  const char* err;   // short reason for the web UI, or NULL on success
  unsigned files;    // entries written
  unsigned bytes;    // bytes expanded onto the partition
} ThemePackResult;

// Replaces the device's theme with the pack. DESTRUCTIVE, always: the old theme
// is gone from the first moment, whether or not the new pack turns out to be
// usable. `declaredBytes` is the pack's unpacked size when the caller knows it —
// the web UI reads it out of the pack's index, so a pack that cannot possibly
// fit is refused before two megabytes go over the air — or -1 when it does not,
// in which case the running total is the only guard.
ThemePackResult themePackInstall(const ThemePackSource* src, long declaredBytes);

// Puts the pack built into the firmware back on the partition, and marks the
// theme as the built-in one. Used at boot when nothing is mounted and after a
// failed install, so the pet is never left without art to play.
bool themePackInstallDefault(void);

// The same, on a task of its own, waited for. The install expands most of a
// megabyte onto LittleFS through tinfl, which does not fit app_main's 3.5 KB
// stack — see the note on the boot call site.
bool themePackInstallDefaultOnTask(void);

// True when a theme is mounted, i.e. its manifest is there to be read.
bool themePackPresent(void);

#ifdef __cplusplus
}
#endif
