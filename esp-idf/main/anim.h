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

// Colour the screen is cleared to when the state changes (RGB565). Defaults to
// white, matching the theme's own background.
void animSetBackground(unsigned short colour);

// Lists the states the mounted theme provides, space-separated, into `out`.
void animListStates(char* out, unsigned outLen);

// Spawns the player task. Call once, after LittleFS is mounted.
void animInit(void);

#ifdef __cplusplus
}
#endif
