// Plays clawd animations from the theme on LittleFS.
// See CLAUDE-CODE-BRIDGE.md / the .caf format description in tools/gif2caf.py.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts (or switches to) the animation for `state`, looking it up in the
// theme manifest. Pass an empty string to stop and go idle.
void animPlayState(const char* state);

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

#ifdef __cplusplus
}
#endif
