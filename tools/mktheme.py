#!/usr/bin/env python3
"""Build a Clawd Mochi theme pack from a sticker set.

A "theme" is one directory holding a manifest.txt plus one .caf per state, and
the device holds exactly one at a time. This assembles that directory from a
sticker set, so switching themes is a matter of building a different pack and
putting it on the device.

Two things don't line up automatically and are worth knowing about:

  * States are named after what Claude Code does (idle, thinking, working,
    done, error, sleep); the sticker sets name their art after what the
    character is doing. The mapping below bridges that, and sets differ — the
    cloudling set has no "-happy", so `done` needs pointing somewhere else.

  * Capacity. The LittleFS partition is 2.44 MB and these sets differ wildly in
    how well they compress. clawd's crab occupies a third of its canvas, so its
    twelve animations fit in 2.1 MB; calico's fills the canvas and six states
    alone came to 3.6 MB. --no-upscale and --stride are the levers, and the
    script reports the total so a pack that cannot fit is obvious here rather
    than at flash time.

Usage:
  mktheme.py ../../stickers/128/calico -o /tmp/calico-pack --scale 2
  mktheme.py ../../stickers/cloudling -o /tmp/cloudling-pack --scale 2 \\
            --map done=attention
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CONVERT = os.path.join(HERE, "gif2caf.py")

# device state -> suffix of the sticker file that plays it
DEFAULT_MAP = {
    "idle":     "idle",
    "thinking": "thinking",
    "working":  "typing",
    "done":     "happy",
    "error":    "error",
    "sleep":    "sleeping",
}

# Shipped alongside the states above so the pack has art to point at later.
SPARE_SUFFIXES = ["juggling", "notification", "sweeping", "carrying"]

MANIFEST_HEAD = """\
# Clawd Mochi theme — maps a device state to an animation file.
#
# Plain text on purpose: a theme can be edited on the device (or swapped
# wholesale) without rebuilding the firmware, and the parser stays trivial.
# Format: <state>=<file.caf>   ·   '#' starts a comment
#
# These are the states the Claude Code hooks drive:
#   idle          nothing happening
#   thinking      a prompt was submitted
#   working       a tool is running
#   done          the turn finished
#   error         something failed
#   sleep         idle for a long time
#
# The rest are spare art shipped with the theme; point any state at them by
# editing this file.

# Magnification the device applies while pushing. 1 means the .caf is already
# panel-sized; higher means the art is stored small and blown up on push, which
# keeps the file small because run-length encoding needs crisp pixels.
scale={scale}

"""


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="directory of sticker GIFs")
    ap.add_argument("-o", "--out", required=True, help="theme directory to write")
    ap.add_argument("--prefix", default=None,
                    help="sticker filename prefix (default: the source dir name)")
    ap.add_argument("--scale", type=int, default=1,
                    help="magnification for the device to apply (default 1)")
    ap.add_argument("--map", action="append", default=[],
                    metavar="STATE=SUFFIX",
                    help="override the art a state plays, e.g. done=attention")
    ap.add_argument("--stride", type=int, default=1, help="passed to gif2caf")
    ap.add_argument("--no-upscale", action="store_true", help="passed to gif2caf")
    ap.add_argument("--budget", type=int, default=2400,
                    help="partition budget in KB to check against (default 2400)")
    ap.add_argument("--spares", default=",".join(SPARE_SUFFIXES),
                    help="comma-separated spare suffixes to include, or '' for none")
    args = ap.parse_args()

    src = args.source.rstrip("/")
    prefix = args.prefix or os.path.basename(src)
    mapping = dict(DEFAULT_MAP)
    for m in args.map:
        if "=" not in m:
            sys.exit(f"error: --map wants STATE=SUFFIX, got {m!r}")
        k, v = m.split("=", 1)
        mapping[k.strip()] = v.strip()

    def sticker(suffix):
        """The .gif for a suffix, tolerant of the '_' some sets use for '-'."""
        for cand in (f"{prefix}-{suffix}.gif", f"{prefix}_{suffix}.gif"):
            p = os.path.join(src, cand)
            if os.path.exists(p):
                return p
        return None

    os.makedirs(args.out, exist_ok=True)

    # Wipe stale animations first. A leftover .caf from an earlier build would
    # silently inflate the pack — worse, it would be uploaded along with the
    # rest and eat partition the new theme needs.
    stale = 0
    for f in os.listdir(args.out):
        if f.endswith(".caf") or f == "manifest.txt":
            os.remove(os.path.join(args.out, f))
            stale += 1
    if stale:
        print(f"  cleared {stale} file(s) left by a previous build")

    def build(suffix, missing_ok):
        gif = sticker(suffix)
        if gif is None:
            if missing_ok:
                return None
            sys.exit(f"error: {src} has no '{prefix}-{suffix}.gif' (use --map)")
        cmd = [sys.executable, CONVERT, gif, "-o", args.out]
        if args.stride > 1:
            cmd += ["--stride", str(args.stride)]
        if args.no_upscale:
            cmd += ["--no-upscale"]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        return os.path.basename(gif).rsplit(".", 1)[0] + ".caf"

    print(f"building {prefix} -> {args.out}  (scale {args.scale})")
    entries = []
    for state in ("idle", "thinking", "working", "done", "error", "sleep"):
        caf = build(mapping[state], missing_ok=False)
        entries.append((state, caf, None if caf else None))
        print(f"  {state:9s} -> {caf}")

    spares = []
    for suffix in [s for s in args.spares.split(",") if s.strip()]:
        caf = build(suffix.strip(), missing_ok=True)
        if caf:
            spares.append((f"spare-{suffix.strip()}", caf))

    with open(os.path.join(args.out, "manifest.txt"), "w") as fh:
        fh.write(MANIFEST_HEAD.format(scale=args.scale))
        for state, caf, _ in entries:
            fh.write(f"{state}={caf}\n")
        if spares:
            fh.write("\n")
            for name, caf in spares:
                fh.write(f"{name}={caf}\n")

    total = 0
    for f in os.listdir(args.out):
        total += os.path.getsize(os.path.join(args.out, f))
    kb = total / 1024
    print(f"\n  {len(entries) + len(spares)} animations, {kb:.0f} KB "
          f"of a {args.budget} KB budget")
    if kb > args.budget:
        print("  *** DOES NOT FIT — try --stride 2, --no-upscale, or fewer states")
        return 1
    print("  fits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
