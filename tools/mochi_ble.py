#!/usr/bin/env python3
"""Send commands to Clawd Mochi over BLE (no cable, no WiFi).

Same command vocabulary as tools/mochi.py — but the transport is a BLE GATT
write instead of USB serial, so the device does not have to be tethered.

Usage:
  mochi_ble.py eyes                 # normal eyes
  mochi_ble.py squish               # squish eyes
  mochi_ble.py state idle           # play a themed animation ("state list" to list)
  mochi_ble.py status Thinking...   # status text below the eyes
  mochi_ble.py raw "state idle"     # any raw command from the serial protocol
  mochi_ble.py scan                 # list nearby BLE devices

Command reference: see CLAUDE-CODE-BRIDGE.md (方案 D / 方案 E share it).

Requires: bleak   (pip install bleak)
Note: the "img" image transfer is serial-only — it streams a raw pixel dump
over the UART, which a GATT write cannot carry.
"""

import asyncio
import sys

from bleak import BleakClient, BleakScanner

# The device is found by the service it advertises, not by its name: the name is
# configurable from the web UI (and the device restarts with the new one), so
# matching on it would lose the device the moment someone renamed it. DEVICE_NAME
# is only a fallback, for firmware predating the service-UUID advertisement.
DEVICE_NAME = "clawd-mochi"

# 16-bit UUIDs expanded to their canonical 128-bit form.
SVC_UUID = "0000abf0-0000-1000-8000-00805f9b34fb"
CHR_UUID = "0000abf1-0000-1000-8000-00805f9b34fb"

SCAN_TIMEOUT = 8.0


def is_ours(device, adv):
    """True for the Clawd Mochi peripheral, whatever it is currently called."""
    if SVC_UUID in (adv.service_uuids or []):
        return True
    return (device.name or "").lower() == DEVICE_NAME


async def find_device():
    return await BleakScanner.find_device_by_filter(is_ours, timeout=SCAN_TIMEOUT)


async def send(commands):
    dev = await find_device()
    if dev is None:
        print("Error: no Clawd Mochi in range (it advertises the command "
              "service; check that it is powered and nearby).", file=sys.stderr)
        return 1

    async with BleakClient(dev) as client:
        for cmd in commands:
            await client.write_gatt_char(CHR_UUID, cmd.encode(), response=False)
            await asyncio.sleep(0.4)      # let the device finish redrawing
    return 0


async def scan():
    print("Scanning for BLE devices…")
    for device, adv in (await BleakScanner.discover(timeout=SCAN_TIMEOUT,
                                                    return_adv=True)).values():
        mark = "  <-- clawd mochi" if is_ours(device, adv) else ""
        print(f"  {device.address}  {device.name}{mark}")
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
    if sub == "state":
        # state <name> — play a themed clawd animation; "state off" goes back to
        # the built-in eyes. "state list" prints the states the theme provides.
        arg = rest[0] if rest else "off"
        return ["states" if arg == "list" else ("state " + arg).strip()]
    if sub == "text":
        return ["t" + " ".join(rest)]
    if sub == "bg":
        return ["bg" + (rest[0] if rest else "#000000")]
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
