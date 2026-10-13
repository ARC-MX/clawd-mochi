# Theme packs

**中文（默认版本）: [README.md](README.md)** — 本文件是英文镜像；两份如有出入，以中文那份为准。

The device holds exactly **one** theme at a time — the 2.44 MB storage partition
has no room for a second — so changing themes means replacing. A theme is **one
pack** here:

```
themes/<name>/          # the source: manifest.txt + one .caf per state
themes/<name>.zip       # the one file the device takes (mochi_theme.py bundle)
```

A `.caf` is a binary animation (`tools/gif2caf.py` converts it from a GIF); the
`.zip` is what the device actually receives. Pick a pack in the web UI, push it,
and the device unzips it itself. A pack directory is what gets zipped and
expanded onto the device's partition, so **it holds the pack and nothing else** —
previews and notes stay outside it (see the end of each section).

| Pack | States | Unpacked | Zip | What it is |
| --- | --- | --- | --- | --- |
| [`clawd.zip`](clawd.zip) | 12 | 2054 KB | 384 KB | the crab from the upstream sticker set — the factory look |
| [`origin.zip`](origin.zip) | 11 | 408 KB | 8 KB | the orange cube pet, drawn in code |

---

## `clawd` — the crab (12 states)

**Six of them are driven by Claude Code**: `idle` nothing happening, `thinking` a
prompt was submitted, `working` a tool is running, `done` the turn finished,
`error` something failed (nothing plays it automatically today; it is manual
only), `sleep` idle for a long time.

<table>
  <tr>
    <td align="center"><img src="../pics/states/idle.gif" width="200"><br><code>idle</code></td>
    <td align="center"><img src="../pics/states/thinking.gif" width="200"><br><code>thinking</code></td>
    <td align="center"><img src="../pics/states/working.gif" width="200"><br><code>working</code></td>
  </tr>
  <tr>
    <td align="center"><img src="../pics/states/done.gif" width="200"><br><code>done</code></td>
    <td align="center"><img src="../pics/states/error.gif" width="200"><br><code>error</code></td>
    <td align="center"><img src="../pics/states/sleep.gif" width="200"><br><code>sleep</code></td>
  </tr>
</table>

**The other six are spare art** — point a state at one by editing
`clawd/manifest.txt` (say `idle=clawd-idle.caf`):

<table>
  <tr>
    <td align="center"><img src="../pics/states/spare-idle.gif" width="130"><br><code>spare-idle</code></td>
    <td align="center"><img src="../pics/states/spare-reading.gif" width="130"><br><code>spare-reading</code></td>
    <td align="center"><img src="../pics/states/juggling.gif" width="130"><br><code>juggling</code></td>
  </tr>
  <tr>
    <td align="center"><img src="../pics/states/notification.gif" width="130"><br><code>notification</code></td>
    <td align="center"><img src="../pics/states/sweeping.gif" width="130"><br><code>sweeping</code></td>
    <td align="center"><img src="../pics/states/carrying.gif" width="130"><br><code>carrying</code></td>
  </tr>
</table>

The art comes from the upstream sticker sets (the `../stickers/` directory beside
this repository). Rebuild it with:

```bash
python3 tools/mktheme.py ../stickers/240/clawd -o themes/clawd
python3 tools/mochi_theme.py bundle themes/clawd -o themes/clawd.zip
```

## `origin` — the orange cube pet (11 states)

The character from the product art: a square head, small ears on both sides, a
black-framed screen on the front, three legs underneath, and the expressions
drawn inside that screen. Its artwork has **no upstream source** — it is pixel
art that `tools/origin_art.py` draws frame by frame (change how it looks by
changing the constants in that script, then `--pack`).

<table>
  <tr>
    <td align="center"><img src="../pics/origin/idle.gif" width="200"><br><code>idle</code> standby</td>
    <td align="center"><img src="../pics/origin/thinking.gif" width="200"><br><code>thinking</code></td>
    <td align="center"><img src="../pics/origin/working.gif" width="200"><br><code>working</code></td>
  </tr>
  <tr>
    <td align="center"><img src="../pics/origin/done.gif" width="200"><br><code>done</code></td>
    <td align="center"><img src="../pics/origin/error.gif" width="200"><br><code>error</code></td>
    <td align="center"><img src="../pics/origin/sleep.gif" width="200"><br><code>sleep</code></td>
  </tr>
</table>

<table>
  <tr>
    <td align="center"><img src="../pics/origin/spare-happy.gif" width="130"><br><code>spare-happy</code></td>
    <td align="center"><img src="../pics/origin/spare-slack.gif" width="130"><br><code>spare-slack</code></td>
    <td align="center"><img src="../pics/origin/spare-grin.gif" width="130"><br><code>spare-grin</code></td>
  </tr>
  <tr>
    <td align="center"><img src="../pics/origin/spare-shine.gif" width="130"><br><code>spare-shine</code></td>
    <td align="center"><img src="../pics/origin/spare-blank.gif" width="130"><br><code>spare-blank</code></td>
  </tr>
</table>

This set is drawn against a **cream** background; set the pet background (Pet BG
in the web UI, or `bg#FDF0DD` over serial) to `#FDF0DD` for the reference look.
Regenerate the art, the pack and a preview:

```bash
python3 tools/origin_art.py --pack themes/origin      # draw and pack in one go
python3 tools/mochi_theme.py bundle themes/origin -o themes/origin.zip
python3 tools/mochi_theme.py preview themes/origin -o /tmp/origin.png --bg '#FDF0DD'
```

## Where the previews come from

Every clip above is **not** the source art: each is decoded frame by frame from
the `.caf` the device plays and composited by the firmware's own rules (480×480,
twice the panel), so it is the effect you actually get. Regenerate after changing
a pack:

```bash
python3 tools/theme_gifs.py themes/clawd  pics/states
python3 tools/theme_gifs.py themes/origin pics/origin '#fdf0dd'
```

They live in `pics/` rather than beside their packs: a pack directory is zipped
whole and expanded onto the device's partition, where another file is just
storage spent on nothing.

## What the firmware carries

`esp-idf/main/default_pack.zip` (168 KB, 892 KB expanded) holds the **six hook
states** of `clawd` only — a device that has never been pushed to still has a
face. The six spare animations are not in it: all twelve come to 384 KB
compressed and the app partition has 240 KB of room left (`idf.py build` prints
the margin). When boot finds no manifest on the storage partition it expands this
one there; `restore built-in` in the web UI does the same by hand.

## Getting one onto the device

1. Join the device's hotspot `Clawd-Mochi` (default password `clawd1234`) and
   open `http://192.168.4.1`
2. In `// theme`, add a `.zip` to the library and push it
3. The transfer finishing is not the end of it: the device writes what it
   unpacked at littlefs' 128-byte granularity, about **78 KB/s** — roughly 4
   seconds for `origin` (408 KB) and 25 for `clawd` (2054 KB). The page counts
   the seconds for you.

A refusal or a failure does not leave an empty theme — the device puts its
built-in pack back. The design and its trade-offs are in
[../CLAUDE-CODE-BRIDGE.md](../CLAUDE-CODE-BRIDGE.md).
