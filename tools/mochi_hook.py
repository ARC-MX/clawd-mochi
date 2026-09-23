#!/usr/bin/env python3
"""Claude Code hook -> Clawd Mochi (ESP32) state driver.

Maps a Claude Code event to a themed animation state and pushes it to the
device over USB serial, falling back to BLE when the device is untethered.

Serial first, and not by a small margin: a command over USB is a write and a
reply, measured at 0.086 s, while BLE has to scan, connect, write and
disconnect — about 4 s. The device's USB socket is the ESP32-C3's native
USB-Serial-JTAG and the firmware reads it (it did not always: see the serial
fixes in the firmware history, before which a serial attempt blocked until its
own timeout and every state change landed 12 s late). BLE remains the path when
the cable is out.

Debounces: the same state sent again within DEBOUNCE_MS is dropped, so
PreToolUse (which fires on every tool call) does not hammer the serial port.

Usage (registered in ~/.claude/settings.json as an async hook):
  mochi_hook.py UserPromptSubmit   -> state thinking
  mochi_hook.py PreToolUse         -> state working
  mochi_hook.py Stop               -> state done

Always exits 0: the device being offline must never block Claude Code.
"""

import json
import os
import subprocess
import sys
import time

BASE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable or "python3"

# Serial first — see the module docstring for the 0.086 s vs ~4 s measurement.
SERIAL = [PY, os.path.join(BASE, "mochi.py")]
BLE = [PY, os.path.join(BASE, "mochi_ble.py")]

DEBOUNCE_MS = 300
CACHE = os.path.join(os.environ.get("XDG_RUNTIME_DIR") or "/tmp",
                     "mochi-hook-cache.json")

# Claude Code event -> themed animation state (see data/theme/manifest.txt).
MAP = {
    "UserPromptSubmit": "thinking",
    "PreToolUse": "working",
    "Stop": "done",
}


def debounced(state):
    """True if this exact state was already sent within the debounce window."""
    now = time.time()
    try:
        with open(CACHE) as fh:
            prev = json.load(fh)
    except (OSError, ValueError):
        prev = {}
    if prev.get("state") == state and now - prev.get("ts", 0) < DEBOUNCE_MS / 1000.0:
        return True
    try:
        with open(CACHE, "w") as fh:
            json.dump({"state": state, "ts": now}, fh)
    except OSError:
        pass
    return False


def main():
    ev = sys.argv[1] if len(sys.argv) > 1 else ""
    state = MAP.get(ev)
    if not state:
        return 0
    if debounced(state):
        return 0

    for cmd in (SERIAL, BLE):
        try:
            r = subprocess.run(cmd + ["state", state], timeout=12,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if r.returncode == 0:
                return 0
        except Exception:
            continue
    return 0


if __name__ == "__main__":
    sys.exit(main())
