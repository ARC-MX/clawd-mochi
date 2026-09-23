#!/usr/bin/env python3
"""CLI to send commands to Clawd Mochi via serial (USB).

Ported from upstream PR #3 ("Serial USB control for claude code hooks"),
fixed to the original 240x240 geometry.

Usage:
  mochi.py text Hello world     # type "Hello world" on the terminal screen
  mochi.py eyes                 # show normal eyes (w)
  mochi.py squish               # show squish eyes (s)
  mochi.py terminal             # switch to terminal view (d)
  mochi.py logo                 # show logo animation
  mochi.py quit                 # exit terminal mode (q)
  mochi.py bg '#ff0000'         # set background colour
  mochi.py status Thinking...   # show text below eyes (size 2)
  mochi.py status -s3 Klar!     # show with font size 3
  mochi.py status               # clear status text
  mochi.py canvas               # enter draw mode
  mochi.py line 10 10 100 100 '#fff'   # draw a line on the canvas
  mochi.py img photo.jpg        # display an image on screen
  mochi.py raw "status3 test"   # send a raw serial command
  mochi.py ports                # list available serial ports

Requires: pyserial  (and Pillow, for the 'img' subcommand)
"""

import sys
import time

import serial
import serial.tools.list_ports

DEFAULT_BAUD = 115200

# The device is a 240x240 ST7789. Change these if you run a different panel.
DISP_W = 240
DISP_H = 240


def find_port():
    """Find the first likely ESP32 serial port."""
    for p in serial.tools.list_ports.comports():
        desc = (p.description or "").lower()
        if any(k in desc for k in
               ("cp210", "ch340", "ch910", "usb serial", "usb-serial", "esp32", "jtag")):
            return p.device
    # Fallback: first port that isn't COM1.
    for p in serial.tools.list_ports.comports():
        if p.device.upper() != "COM1":
            return p.device
    return None


def _open(port, baud):
    """Open the port without resetting the ESP32.

    This board's auto-reset circuit needs DTR asserted and RTS deasserted while
    the port is open, plus HUPCL cleared so that *closing* the port does not
    drop those lines and pulse the chip's reset. (The upstream PR's
    ``dtr=False, rts=False`` combination resets this board on every open.)
    """
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 2
    ser.dtr = True
    ser.rts = False
    ser.open()
    try:
        import termios
        attrs = termios.tcgetattr(ser.fileno())
        attrs[2] &= ~termios.HUPCL          # cflag
        termios.tcsetattr(ser.fileno(), termios.TCSANOW, attrs)
    except Exception:
        pass                                # not POSIX, or not supported here
    time.sleep(0.05)
    return ser


def send_cmd(port, cmd, baud=DEFAULT_BAUD, wait=8.0):
    """Send one command and wait for the device's reply.

    The firmware redraws the screen before replying (a view animation can take
    a few seconds), so poll rather than assuming a fixed delay.
    """
    ser = _open(port, baud)
    ser.write((cmd + "\n").encode())
    ser.flush()
    resp = ""
    end = time.time() + wait
    while time.time() < end:
        if ser.in_waiting:
            resp += ser.read(ser.in_waiting).decode(errors="replace")
            if "\n" in resp:
                break
        else:
            time.sleep(0.05)
    ser.close()
    return resp.strip()


def send_image(port, image_path, tint=None, baud=DEFAULT_BAUD):
    """Read an image, resize to the panel, convert to RGB565 and stream it."""
    from PIL import Image

    img = Image.open(image_path).convert("RGB")
    img = img.resize((DISP_W, DISP_H), Image.LANCZOS)

    tr = tg = tb = 0
    if tint:
        tint = tint.lstrip("#")
        tr, tg, tb = int(tint[0:2], 16), int(tint[2:4], 16), int(tint[4:6], 16)

    # RGB565, little-endian — matches the panel's configured data endianness.
    pixels = img.load()
    data = bytearray(DISP_W * DISP_H * 2)
    idx = 0
    for y in range(DISP_H):
        for x in range(DISP_W):
            r, g, b = pixels[x, y]
            if tint:
                # Blend: dark pixels take the tint, bright pixels go white.
                brightness = (r + g + b) / (3 * 255)
                r = int(tr + (255 - tr) * brightness)
                g = int(tg + (255 - tg) * brightness)
                b = int(tb + (255 - tb) * brightness)
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data[idx] = rgb565 & 0xFF
            data[idx + 1] = (rgb565 >> 8) & 0xFF
            idx += 2

    ser = _open(port, baud)

    ser.write(b"img\n")
    ser.flush()
    time.sleep(0.5)
    resp = ""
    t0 = time.time()
    while time.time() - t0 < 10:
        if ser.in_waiting:
            resp += ser.read(ser.in_waiting).decode(errors="replace")
            if "ready" in resp:
                break
        time.sleep(0.05)
    if "ready" not in resp:
        print(f"Unexpected response: '{resp}'", file=sys.stderr)
        ser.close()
        sys.exit(1)

    # 128-byte chunks with a little pacing keeps the UART RX buffer happy.
    chunk, sent, total = 128, 0, len(data)
    while sent < total:
        n = min(chunk, total - sent)
        ser.write(data[sent:sent + n])
        sent += n
        time.sleep(0.002)
    ser.flush()

    resp = ""
    t0 = time.time()
    while time.time() - t0 < 20:
        if ser.in_waiting:
            resp += ser.read(ser.in_waiting).decode(errors="replace")
            if "done" in resp:
                break
        time.sleep(0.1)
    print(resp.strip() if resp else "transfer complete")
    ser.close()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(0)

    subcmd = sys.argv[1].lower()

    if subcmd == "ports":
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device:10s}  {p.description}")
        sys.exit(0)

    port = find_port()
    if not port:
        print("Error: no serial port found. Run 'mochi.py ports' to list.",
              file=sys.stderr)
        sys.exit(1)

    if subcmd == "img":
        if len(sys.argv) < 3:
            print("Error: 'img' needs an image path.", file=sys.stderr)
            sys.exit(1)
        tint = None
        if len(sys.argv) >= 5 and sys.argv[3] == "-t":
            tint = sys.argv[4]
        send_image(port, sys.argv[2], tint)
        return

    if subcmd == "text":
        cmd = "t" + " ".join(sys.argv[2:])
    elif subcmd == "eyes":
        cmd = "w"
    elif subcmd == "squish":
        cmd = "s"
    elif subcmd == "terminal":
        cmd = "d"
    elif subcmd == "logo":
        cmd = "logo"
    elif subcmd == "quit":
        cmd = "q"
    elif subcmd == "canvas":
        cmd = "canvas"
    elif subcmd == "state":
        # state <name> — play a themed clawd animation; "state off" goes back to
        # the built-in eyes. "states" lists what the mounted theme provides.
        arg = sys.argv[2] if len(sys.argv) > 2 else "off"
        cmd = "states" if arg == "list" else ("state " + arg).strip()
    elif subcmd == "bg":
        cmd = "bg" + (sys.argv[2] if len(sys.argv) > 2 else "#000000")
    elif subcmd == "line":
        if len(sys.argv) < 7:
            print("Error: 'line' needs x1 y1 x2 y2 #RRGGBB", file=sys.stderr)
            sys.exit(1)
        cmd = "line %s,%s,%s,%s,%s" % tuple(sys.argv[2:7])
    elif subcmd == "status":
        size, color, args = "2", "", sys.argv[2:]
        if len(args) >= 2 and args[0].startswith("-s"):
            size, args = args[0][2:], args[1:]
        if args and args[0].startswith("-c"):
            color, args = " -c" + args[0][2:], args[1:]
        cmd = "status" + size + color + " " + " ".join(args)
    elif subcmd == "raw":
        cmd = " ".join(sys.argv[2:])
    else:
        print(f"Unknown subcommand: {subcmd}\n", file=sys.stderr)
        print(__doc__)
        sys.exit(1)

    print(send_cmd(port, cmd))


if __name__ == "__main__":
    main()
