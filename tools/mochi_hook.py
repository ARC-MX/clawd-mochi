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
terminal) is dropped. A run with nothing on stdin is not an agent but somebody
testing by hand (the commands below): it files under `default` for a minute and
then ages out like anything else.

A long tool call fires no hooks for minutes at a stretch, and the device's own
nap timer (`sleepafter`) used to expire in the middle of one — the pet dozed off
while an agent was still working. That is the firmware's business now: it does
not nap out of `working`/`thinking` at all (petIsWorking() in main.cpp), so this
script never touches `sleepafter`. It is a setting the user owns — and now a
stored one, so writing it here would mean a flash write per turn.

Only *changes* reach the device — PreToolUse fires on every tool call — but a
work state is re-asserted every RESYNC_S. A device that rebooted (or that the
web UI was poked on) has no way to say so, and would otherwise sit on the wrong
animation until the next change, which can be minutes away. When the last
session goes away the panel is brought to rest instead, for the same reason in
reverse: a `working` left standing never naps out.

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
# A device reboot is invisible from here: it drops whatever it was showing, and
# the ledger below has no way to know it happened. While an agent is *working*,
# re-assert the state this often so the panel finds its way back on its own.
#
# Work states only. Re-asserting `idle`/`done` would restart their animation and
# — worse — keep restarting the device's nap timer, so the pet would never sleep.
RESYNC_S = 180
# A run with no stdin has no session_id: it is somebody testing the hook by hand
# (the commands in CLAUDE-CODE-BRIDGE.md), not an agent. It files under this key
# and gets a short life, so a one-line test does not sit on the panel for half an
# hour — and it has to be a state the panel can act on either way.
MANUAL_ID = "default"
MANUAL_TTL_S = 60
# A session this quiet is assumed gone. `SessionEnd` covers a clean exit; this
# covers a killed terminal, a crashed host, a session that never said goodbye.
# Long on purpose — one Bash call can run for minutes with no hook in between,
# and dropping a *working* session is the bug this file exists to fix.
SESSION_TTL_S = 1800

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
    """Which Claude Code session this event belongs to, from the stdin payload.

    Returns None for a run with nothing on stdin — a manual test, not an agent.
    """
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
    return None


def session_entry(state, now, sid):
    """A fresh registry entry for `sid`, grown old the way that sid deserves."""
    entry = {"state": state, "ts": now}
    if sid is None:
        entry["ttl"] = MANUAL_TTL_S
    return entry


def entry_is_live(entry, now):
    return now - entry.get("ts", 0) <= entry.get("ttl", SESSION_TTL_S)


def aggregate(sessions):
    """The state the whole fleet is in, or None if no session is registered."""
    if not sessions:
        return None
    return max(sessions.values(),
               key=lambda s: PRIORITY.get(s.get("state"), 0)).get("state", "idle")


def stale(rec, want, now, again=False, resync=False):
    """True if `want` is not what the device was last told, or was not heard.

    `again` lets an identical value be sent a second time once it is clear of
    the debounce window — used for `done`, which is an event rather than a
    state: a later turn finishing is a new reason to celebrate. `resync` re-sends
    an identical *work* state on a timer instead, because the device can forget
    what it was showing and nothing here would ever notice.
    """
    if rec.get("value") != want:
        return True
    if rec.get("ok"):
        if again and now - rec.get("ts", 0) >= DEBOUNCE_MS / 1000.0:
            return True
        return resync and now - rec.get("ts", 0) >= RESYNC_S
    return now - rec.get("ts", 0) >= RETRY_MS / 1000.0


def send_state(state):
    """Push one state to the device: serial first, then BLE. True if it landed."""
    for cli in (SERIAL, BLE):
        try:
            r = subprocess.run(cli + ["state", state], timeout=12,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
            if r.returncode == 0:
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


def note_failure(state):
    """Record that a state could not be delivered. Never raises."""
    try:
        os.makedirs(os.path.dirname(FAIL_LOG), exist_ok=True)
        with open(FAIL_LOG, "a") as fh:
            fh.write("%s  could not deliver %s (serial and BLE both failed)\n"
                     % (time.strftime("%Y-%m-%d %H:%M:%S"), state))
    except OSError:
        pass


def push(ev=None, sid=None):
    """Recompute the fleet's state and tell the device, if it changed.

    `ev` is the event that triggered this, or None for the detached retry: that
    one must not move a session's state — by the time it runs the session may
    have moved on — so it only re-reads the fleet and rechecks the panel.
    """
    now = time.time()
    key = sid or MANUAL_ID
    if ev is not None:
        with FileLock(LOCK):
            blob = load_blob()
            sessions = blob.get("sessions") or {}
            if ev == "SessionEnd":
                sessions.pop(key, None)
            elif ev == "SessionStart":
                # `startup` is a new session, but `resume` and `compact` land
                # here mid-turn as well: an entry that exists keeps its state,
                # it is only marked as heard from.
                entry = sessions.get(key)
                if entry is None:
                    sessions[key] = session_entry(EVENT_STATE[ev], now, sid)
                else:
                    entry["ts"] = now
            else:
                sessions[key] = session_entry(EVENT_STATE[ev], now, sid)
            blob["sessions"] = sessions
            save_blob(blob)

    with FileLock(LOCK):
        blob = load_blob()
        sessions = blob.get("sessions") or {}
        sent = blob.get("sent") or {}

        # Prune the sessions that went quiet — a killed terminal never sends
        # SessionEnd and would otherwise pin the panel on its last state.
        sessions = {k: v for k, v in sessions.items() if entry_is_live(v, now)}
        blob["sessions"] = sessions

        fleet = aggregate(sessions)
        celebrate = fleet == "idle" and ev == "Stop"
        if fleet is None:
            # Nobody left to speak for. A `done` that just started playing should
            # be left to finish, but a *work* state must not be left standing: the
            # firmware does not nap out of one (it cannot tell a live tool call
            # from a dead session), so an orphaned `working` would sit there until
            # someone poked the device by hand.
            panel = None if sent.get("value") in (None, "idle", "done") else "idle"
        else:
            panel = "done" if celebrate else fleet

        # While someone is working the panel is also re-asserted on a timer: a
        # rebooted or poked device is otherwise stuck showing the wrong thing
        # until the next state *change*, which can be minutes away.
        resync = panel in ("working", "thinking")
        if panel is None or not stale(sent, panel, now, again=celebrate,
                                      resync=resync):
            # Already showing it: PreToolUse fires on every single tool call, and
            # the device is told once per *change*, not once per event.
            save_blob(blob)
            return
        blob["sent"] = {"value": panel, "ts": now, "ok": False}

        # The intent is on disk before the lock is released, so a second hook
        # firing at the same moment sees it and does not repeat the send.
        save_blob(blob)

    # Off the lock: a device that is unplugged costs 24 s of timeouts here, and
    # holding the lock for that would stall every other session's hooks.
    ok = send_state(panel)
    with FileLock(LOCK):
        blob = load_blob()
        rec = blob.get("sent")
        if isinstance(rec, dict) and not rec.get("ok"):
            rec["ok"] = ok
            rec["ts"] = time.time()
        save_blob(blob)

    if not ok:
        if os.environ.get(RETRY_ENV):
            note_failure(panel)         # the retry itself failed: that is news
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
