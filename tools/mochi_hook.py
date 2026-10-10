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

The panel shows one animation, but Claude Code is not one process: several
sessions run at once and every one of them fires these hooks. Each of them used
to write its state straight to the device, so the last writer won — a session
that finished put the pet to sleep, or parked it on `done`, while the others
were still working. The state is kept per session now, and the panel gets the
*aggregate*: the busiest state anyone is in (working > thinking > idle). `done`
plays when the *last* running session stops, not when any one of them does.

Telling the sessions apart needs the hook's stdin payload — `session_id` — and
this script used to ignore stdin entirely, which is why every session looked
like the same one. `SessionStart`/`SessionEnd` register and retire a session; a
session that goes quiet for SESSION_TTL_S without a `SessionEnd` (a killed
terminal) is dropped.

A long tool call fires no hooks for minutes at a stretch, and the device's own
nap timer (`sleepafter`, 120 s by default) used to run out in the middle of one
— the pet dozed off while an agent was still working. While any session is busy
the hook pushes that timer out to SLEEP_BUSY_S and puts it back once the fleet
is idle; the timer belongs to the hook after that. Set SLEEP_BUSY_S = 0 to
leave the device's own value alone.

The device's serial reader is up about five seconds after it is powered, so the
first event after a reboot can arrive while it is still starting. That send is
retried once, from a detached process — sleeping here would eat the hook's own
timeout — and only a *second* failure is logged, to ~/.clawd/mochi-hook.log.

Usage (registered in ~/.claude/settings.json as an async hook):
  mochi_hook.py SessionStart      -> session registered (idle, if it is alone)
  mochi_hook.py UserPromptSubmit  -> state thinking
  mochi_hook.py PreToolUse        -> state working
  mochi_hook.py Stop              -> state done, once the last session stops
  mochi_hook.py SessionEnd        -> session retired

Always exits 0: the device being offline must never block Claude Code.
"""

import json
import os
import subprocess
import sys
import tempfile
import time

BASE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable or "python3"

# Serial first — see the module docstring for the 0.086 s vs ~4 s measurement.
SERIAL = [PY, os.path.join(BASE, "mochi.py")]
BLE = [PY, os.path.join(BASE, "mochi_ble.py")]

# The same state again within this window is dropped, so two sessions stopping
# at once play `done` once instead of twice.
DEBOUNCE_MS = 300
# The device is powered up ~5 s after it gets power; a retry after this long
# catches the first event of a freshly booted machine.
BOOT_RETRY_S = 6
# A send nobody answered (device unplugged) is retried, but not on every single
# tool call.
RETRY_MS = 2000
# A session this quiet is assumed gone. `SessionEnd` covers a clean exit; this
# covers a killed terminal, a crashed host, a session that never said goodbye.
# Long on purpose — one Bash call can run for minutes with no hook in between,
# and dropping a *working* session is the bug this file exists to fix.
SESSION_TTL_S = 1800
# Device nap timer, in seconds (see the module docstring). SLEEP_IDLE_S is the
# firmware's own default; SLEEP_BUSY_S = 0 leaves the timer unmanaged.
SLEEP_IDLE_S = 120
SLEEP_BUSY_S = 1800

# Set on the detached retry, so it logs a failure instead of spawning another.
RETRY_ENV = "MOCHI_HOOK_RETRYING"
# Appended to when nothing answered. Inside ~/.clawd/ because that is where this
# machine keeps the other clawd logs, and because /tmp does not survive the reboot
# whose investigation this log exists for.
FAIL_LOG = os.path.join(os.path.expanduser("~"), ".clawd", "mochi-hook.log")
# gettempdir(), not "/tmp": on Windows the latter resolves to a drive-root path
# that does not exist, and every write failed silently, so the debounce never
# worked there.
CACHE = os.path.join(os.environ.get("XDG_RUNTIME_DIR") or tempfile.gettempdir(),
                     "mochi-hook-state.json")
LOCK = CACHE + ".lock"
STALE_LOCK_S = 5.0        # a lock file older than this lost its owner

# Claude Code event -> the state that session is in afterwards. Stop means the
# turn ended, not the session: it goes back to idle and may start another one.
EVENT_STATE = {
    "SessionStart": "idle",
    "UserPromptSubmit": "thinking",
    "PreToolUse": "working",
    "Stop": "idle",
}
# What the panel shows when the sessions disagree. Anything unlisted sits at 0.
PRIORITY = {"working": 3, "thinking": 2, "idle": 1}


class FileLock:
    """Portable exclusive lock — fcntl is POSIX-only, and Windows is supported.

    Best effort by design: failing to get it is not fatal, because the state
    file is replaced atomically and the worst case is a lost update that the
    next event repairs.
    """

    def __init__(self, path, timeout=2.0):
        self.path = path
        self.timeout = timeout

    def __enter__(self):
        deadline = time.time() + self.timeout
        while True:
            try:
                fd = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                os.write(fd, str(os.getpid()).encode())
                os.close(fd)
                return self
            except FileExistsError:
                if self._steal():
                    continue
            except OSError:
                return self                     # unwritable runtime dir
            if time.time() >= deadline:
                return self
            time.sleep(0.02)

    def _steal(self):
        """Drop a lock file whose owner died. True if it is gone now."""
        try:
            if time.time() - os.path.getmtime(self.path) > STALE_LOCK_S:
                os.unlink(self.path)
                return True
        except OSError:
            pass
        return False

    def __exit__(self, *exc):
        try:
            os.unlink(self.path)
        except OSError:
            pass
        return False


def load_blob():
    try:
        with open(CACHE) as fh:
            blob = json.load(fh)
        if isinstance(blob, dict):
            return blob
    except (OSError, ValueError):
        pass
    return {}


def save_blob(blob):
    """Write via a temp file + rename, so a reader never sees half a file."""
    tmp = CACHE + ".tmp"
    try:
        with open(tmp, "w") as fh:
            json.dump(blob, fh)
        os.replace(tmp, CACHE)
    except OSError:
        pass


def session_id():
    """Which Claude Code session this event belongs to, from the stdin payload."""
    raw = ""
    if not sys.stdin.isatty():      # a manual run has no payload and no pipe
        try:
            raw = sys.stdin.read()
        except (OSError, ValueError):
            raw = ""
    if raw.strip():
        try:
            payload = json.loads(raw)
        except ValueError:
            payload = None
        if isinstance(payload, dict):
            sid = payload.get("session_id")
            if isinstance(sid, str) and sid:
                return sid
    # Nothing to tell the sessions apart: every event is one session, which is
    # exactly the behaviour this script had before it looked at stdin.
    return "default"


def aggregate(sessions):
    """The state the whole fleet is in, or None if no session is registered."""
    if not sessions:
        return None
    return max(sessions.values(),
               key=lambda s: PRIORITY.get(s.get("state"), 0)).get("state", "idle")


def stale(rec, want, now, again=False):
    """True if `want` is not what the device was last told, or was not heard.

    `again` lets an identical value be sent a second time once it is clear of
    the debounce window — used for `done`, which is an event rather than a
    state: a later turn finishing is a new reason to celebrate.
    """
    if rec.get("value") != want:
        return True
    if rec.get("ok"):
        return again and now - rec.get("ts", 0) >= DEBOUNCE_MS / 1000.0
    return now - rec.get("ts", 0) >= RETRY_MS / 1000.0


def send_all(cmds):
    """Run every command on the first transport that takes them all.

    Serial first, then BLE — a half-delivered pair (the nap timer over USB, the
    state over BLE) is worse than a retry, so one transport has to take both.
    """
    for cli in (SERIAL, BLE):
        try:
            for cmd in cmds:
                r = subprocess.run(cli + list(cmd), timeout=12,
                                   stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
                if r.returncode != 0:
                    break
            else:
                return True
        except Exception:
            continue
    return False


def retry_later():
    """Try once more in a detached process, after the boot-race window.

    Detached because sleeping here would eat the hook's own timeout and take
    the log line down with it.
    """
    try:
        subprocess.Popen([PY, os.path.abspath(__file__), "--retry"],
                         start_new_session=True,
                         env=dict(os.environ, **{RETRY_ENV: "1"}),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        pass


def note_failure(cmds):
    """Record that something could not be delivered. Never raises."""
    try:
        os.makedirs(os.path.dirname(FAIL_LOG), exist_ok=True)
        with open(FAIL_LOG, "a") as fh:
            fh.write("%s  could not deliver %s (serial and BLE both failed)\n"
                     % (time.strftime("%Y-%m-%d %H:%M:%S"),
                        " + ".join(" ".join(c) for c in cmds)))
    except OSError:
        pass


def push(ev=None, sid=None):
    """Recompute the fleet's state and tell the device, if it changed.

    `ev` is the event that triggered this, or None for the detached retry: that
    one must not move a session's state — by the time it runs the session may
    have moved on — so it only re-reads the fleet and rechecks the panel.
    """
    now = time.time()
    if ev is not None:
        with FileLock(LOCK):
            blob = load_blob()
            sessions = blob.get("sessions") or {}
            if ev == "SessionEnd":
                sessions.pop(sid, None)
            elif ev == "SessionStart":
                # `startup` is a new session, but `resume` and `compact` land
                # here mid-turn as well: an entry that exists keeps its state,
                # it is only marked as heard from.
                entry = sessions.get(sid)
                if entry is None:
                    sessions[sid] = {"state": EVENT_STATE[ev], "ts": now}
                else:
                    entry["ts"] = now
            else:
                sessions[sid] = {"state": EVENT_STATE[ev], "ts": now}
            blob["sessions"] = sessions
            save_blob(blob)

    with FileLock(LOCK):
        blob = load_blob()
        sessions = blob.get("sessions") or {}
        sent = blob.get("sent") or {}
        nap = blob.get("nap") or {}

        # Prune the sessions that went quiet — a killed terminal never sends
        # SessionEnd and would otherwise pin the panel on its last state.
        sessions = {k: v for k, v in sessions.items()
                    if now - v.get("ts", 0) <= SESSION_TTL_S}
        blob["sessions"] = sessions

        busy = aggregate(sessions)
        # With no session at all there is nothing to say: the last turn's `done`
        # should be allowed to finish playing, and the device naps on its own.
        celebrate = busy == "idle" and ev == "Stop"
        panel = None if busy is None else ("done" if celebrate else busy)

        cmds = []
        if SLEEP_BUSY_S > 0:
            want_nap = SLEEP_IDLE_S if busy in (None, "idle") else SLEEP_BUSY_S
            if stale(nap, want_nap, now):
                blob["nap"] = {"value": want_nap, "ts": now, "ok": False}
                cmds.append(("raw", "sleepafter", str(want_nap)))
        if panel is not None and stale(sent, panel, now, again=celebrate):
            blob["sent"] = {"value": panel, "ts": now, "ok": False}
            cmds.append(("state", panel))

        # The intent is on disk before the lock is released, so a second hook
        # firing at the same moment sees it and does not repeat the send.
        save_blob(blob)
        if not cmds:
            return

    # Off the lock: a device that is unplugged costs 24 s of timeouts here, and
    # holding the lock for that would stall every other session's hooks.
    ok = send_all(cmds)
    with FileLock(LOCK):
        blob = load_blob()
        for key in ("sent", "nap"):
            rec = blob.get(key)
            if isinstance(rec, dict) and not rec.get("ok"):
                rec["ok"] = ok
                rec["ts"] = time.time()
        save_blob(blob)

    if not ok:
        if os.environ.get(RETRY_ENV):
            note_failure(cmds)          # the retry itself failed: that is news
        else:
            retry_later()


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--retry":
        time.sleep(BOOT_RETRY_S)
        push()
        return 0

    ev = sys.argv[1] if len(sys.argv) > 1 else ""
    if ev not in EVENT_STATE and ev != "SessionEnd":
        return 0                        # an event we do not drive the pet from
    push(ev, session_id())
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        sys.exit(0)
