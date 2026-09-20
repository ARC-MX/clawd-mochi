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

// Playback rate: 1 slow (~10 fps), 2 the authored rate (~15 fps), 3 fast
// (~22 fps). Anything else falls back to 2.
void animSetSpeed(unsigned level);

// Lists the states the mounted theme provides, space-separated, into `out`.
void animListStates(char* out, unsigned outLen);

// Spawns the player task. Call once, after LittleFS is mounted.
void animInit(void);

#ifdef __cplusplus
}
#endif
