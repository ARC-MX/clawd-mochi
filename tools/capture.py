#!/usr/bin/env python3
"""Send a command and dump the device's serial log for a few seconds.

Used to read anim.cpp's frame-rate log ("<state>: 30.0 fps").

Usage: capture.py "state idle" [seconds]
"""

import sys
import time

import serial

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from mochi import _open, find_port


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "state idle"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0

    port = find_port()
    ser = _open(port, 115200)
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    ser.flush()

    end = time.time() + secs
    while time.time() < end:
        data = ser.read(4096)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
    ser.close()


if __name__ == "__main__":
    main()
