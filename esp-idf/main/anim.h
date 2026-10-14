// Plays clawd animations from the theme on LittleFS.
// See CLAUDE-CODE-BRIDGE.md / the .caf format description in tools/gif2caf.py.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts (or switches to) the animation for `state`, looking it up in the
// theme manifest. Pass an empty string to stop and go idle.
void animPlayState(const char* state);

// Longest state name the player accepts — a buffer for animCurrentState() has
// to be at least this big.
#define ANIM_STATE_MAX 24

// Copies the state the player is showing into `out` (empty when it is stopped,
// e.g. while an image or the canvas owns the panel). Lets a caller ask whether
// the pet is mid-work without keeping its own copy of the player's bookkeeping.
void animCurrentState(char* out, unsigned outLen);

// Colour the screen is cleared to when the state changes (RGB565). This is also
// what shows through the artwork: palette index 0 is treated as transparent, so
// the background is a runtime setting rather than something baked into the .caf.
// Defaults to white.
void animSetBackground(unsigned short colour);

// Lists the states the mounted theme provides, space-separated, into `out`.
void animListStates(char* out, unsigned outLen);

// Spawns the player task. Call once, after LittleFS is mounted.
void animInit(void);

// Where the active theme lives: manifest.txt plus one .caf per state. Shared so
// the theme upload routes and the player cannot disagree about the path.
#define ANIM_THEME_DIR   "/littlefs/theme"
#define ANIM_MANIFEST    ANIM_THEME_DIR "/manifest.txt"

// Re-reads the theme after it has been replaced on disk: drops any cached
// state and restarts on `idle`. Call once an upload has finished.
void animReloadTheme(void);

// Registers a function the player calls once per *band*, with the RGB565 band
// buffer about to be pushed: `out` is `rows` rows of `stride` pixels whose first
// panel row is `bandY` and whose first panel column is `x0`.
//
// It is the only place a caller can own pixels inside the animation's area. The
// player pushes whole bands — a band is the panel's full width — so anything
// drawn *after* a frame is erased by the next band push. That is what the corner
// transport icon (main.cpp) uses it for: a theme whose art fills the panel
// covers that corner in every frame, and repainting on top of the frame only
// moved the flicker from the housekeeping tick's 5 Hz to the frame rate.
//
// Keep it short: it runs once per band, inside the frame, and must not block.
typedef void (*animBandFn)(uint16_t* out, int stride, int x0, int bandY, int rows);
void animSetBandCallback(animBandFn fn);

#ifdef __cplusplus
}
#endif
