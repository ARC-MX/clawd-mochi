#!/usr/bin/env python3
"""Send commands to Clawd Mochi over BLE (no cable, no WiFi).

Same command vocabulary as tools/mochi.py — but the transport is a BLE GATT
write instead of USB serial, so the device does not have to be tethered.

Usage:
  mochi_ble.py eyes                 # normal eyes
  mochi_ble.py squish               # squish eyes
  mochi_ble.py status Thinking...   # status text below the eyes
  mochi_ble.py raw "speed3"         # any raw command from the serial protocol
  mochi_ble.py scan                 # list nearby BLE devices

Command reference: see CLAUDE-CODE-BRIDGE.md (方案 D / 方案 E share it).

Requires: bleak   (pip install bleak)
Note: the "img" image transfer is serial-only — it streams a raw pixel dump
over the UART, which a GATT write cannot carry.
"""

import asyncio
import sys

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "clawd-mochi"

# 16-bit UUIDs expanded to their canonical 128-bit form.
SVC_UUID = "0000abf0-0000-1000-8000-00805f9b34fb"
CHR_UUID = "0000abf1-0000-1000-8000-00805f9b34fb"

SCAN_TIMEOUT = 8.0


async def find_device():
    return await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT)


async def send(commands):
    dev = await find_device()
    if dev is None:
        print(f"Error: '{DEVICE_NAME}' not found. Is it powered and in range?",
              file=sys.stderr)
        return 1

    async with BleakClient(dev) as client:
        for cmd in commands:
            await client.write_gatt_char(CHR_UUID, cmd.encode(), response=False)
            await asyncio.sleep(0.4)      # let the device finish redrawing
    return 0


async def scan():
    print("Scanning for BLE devices…")
    for d in await BleakScanner.discover(timeout=SCAN_TIMEOUT):
        mark = "  <-- clawd-mochi" if (d.name or "") == DEVICE_NAME else ""
        print(f"  {d.address}  {d.name}{mark}")
    return 0


def build_commands(argv):
    """Map friendly subcommands onto the device's serial/BLE command set."""
    sub, rest = argv[1].lower(), argv[2:]

    if sub == "eyes":
        return ["w"]
    if sub == "squish":
        return ["s"]
    if sub == "terminal":
        return ["d"]
    if sub == "quit":
        return ["q"]
    if sub == "logo":
        return ["logo"]
    if sub == "canvas":
        return ["canvas"]
    if sub == "text":
        return ["t" + " ".join(rest)]
    if sub == "bg":
        return ["bg" + (rest[0] if rest else "#000000")]
    if sub == "speed":
        return ["speed" + (rest[0] if rest else "2")]
    if sub == "line":
        if len(rest) < 5:
            print("Error: 'line' needs x1 y1 x2 y2 #RRGGBB", file=sys.stderr)
            return None
        return ["line %s,%s,%s,%s,%s" % tuple(rest[:5])]
    if sub == "status":
        size, color, args = "2", "", list(rest)
        if len(args) >= 2 and args[0].startswith("-s"):
            size, args = args[0][2:], args[1:]
        if args and args[0].startswith("-c"):
            color, args = " -c" + args[0][2:], args[1:]
        return ["status" + size + color + " " + " ".join(args)]
    if sub == "raw":
        return [" ".join(rest)]
    if sub == "img":
        print("Error: 'img' is serial-only — use tools/mochi.py img.", file=sys.stderr)
        return None

    print(f"Unknown subcommand: {sub}\n", file=sys.stderr)
    print(__doc__)
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 0

    sub = sys.argv[1].lower()
    if sub == "scan":
        return asyncio.run(scan())

    commands = build_commands(sys.argv)
    if commands is None:
        return 1
    return asyncio.run(send(commands))


if __name__ == "__main__":
    sys.exit(main())
