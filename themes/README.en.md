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
| [`calico.zip`](calico.zip) | 10 | 1506 KB | 725 KB | the calico cat (upstream `calico`) |
| [`cloudling.zip`](cloudling.zip) | 10 | 572 KB | 235 KB | the cloud (upstream `cloudling`) |
| [`face.zip`](face.zip) | 11 | 311 KB | 7 KB | just the face, bezel to the panel's edge (another shell around `origin`'s) |

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

## `calico` — the calico cat (10 states)

One of the three characters of the upstream desktop clawd-on-desk (its own
`themes/calico`); the sticker set is `../stickers/128/calico` outside this
repository. Sitting and staring, a paw to its chin while thinking, flat on the
keyboard while working, arms up with sparkles when done, flopped over with X eyes
when something failed, and asleep on its back; the four spares are playing with a
ball, an alert, sweeping, and carrying a fish.

<table>
  <tr>
    <td align="center"><img src="../pics/calico/idle.gif" width="200"><br><code>idle</code></td>
    <td align="center"><img src="../pics/calico/thinking.gif" width="200"><br><code>thinking</code></td>
    <td align="center"><img src="../pics/calico/working.gif" width="200"><br><code>working</code></td>
  </tr>
  <tr>
    <td align="center"><img src="../pics/calico/done.gif" width="200"><br><code>done</code></td>
    <td align="center"><img src="../pics/calico/error.gif" width="200"><br><code>error</code></td>
    <td align="center"><img src="../pics/calico/sleep.gif" width="200"><br><code>sleep</code></td>
  </tr>
</table>

<table>
  <tr>
    <td align="center"><img src="../pics/calico/spare-juggling.gif" width="130"><br><code>spare-juggling</code></td>
    <td align="center"><img src="../pics/calico/spare-notification.gif" width="130"><br><code>spare-notification</code></td>
    <td align="center"><img src="../pics/calico/spare-sweeping.gif" width="130"><br><code>spare-sweeping</code></td>
    <td align="center"><img src="../pics/calico/spare-carrying.gif" width="130"><br><code>spare-carrying</code></td>
  </tr>
</table>

```bash
python3 tools/mktheme.py ../stickers/128/calico -o themes/calico \
        --scale 2 --no-upscale --spares juggling,notification,sweeping,carrying
python3 tools/recenter_pack.py themes/calico
python3 tools/mochi_theme.py bundle themes/calico -o themes/calico.zip
python3 tools/theme_gifs.py themes/calico pics/calico
```

## `cloudling` — the cloud (10 states)

The cloud from the same set. It has **no `-happy`**, so `done` points at
`attention` the way upstream does (the cloud lights up). A small puff when idle,
a magnifying glass while thinking, tools while working, a whole storm cloud with
X eyes when something fails, a nightcap when asleep; the spares are juggling, an
alert, sweeping and carrying.

<table>
  <tr>
    <td align="center"><img src="../pics/cloudling/idle.gif" width="200"><br><code>idle</code></td>
    <td align="center"><img src="../pics/cloudling/thinking.gif" width="200"><br><code>thinking</code></td>
    <td align="center"><img src="../pics/cloudling/working.gif" width="200"><br><code>working</code></td>
  </tr>
  <tr>
    <td align="center"><img src="../pics/cloudling/done.gif" width="200"><br><code>done</code></td>
    <td align="center"><img src="../pics/cloudling/error.gif" width="200"><br><code>error</code></td>
    <td align="center"><img src="../pics/cloudling/sleep.gif" width="200"><br><code>sleep</code></td>
  </tr>
</table>

<table>
  <tr>
    <td align="center"><img src="../pics/cloudling/spare-juggling.gif" width="130"><br><code>spare-juggling</code></td>
    <td align="center"><img src="../pics/cloudling/spare-notification.gif" width="130"><br><code>spare-notification</code></td>
    <td align="center"><img src="../pics/cloudling/spare-sweeping.gif" width="130"><br><code>spare-sweeping</code></td>
    <td align="center"><img src="../pics/cloudling/spare-carrying.gif" width="130"><br><code>spare-carrying</code></td>
  </tr>
</table>

It is a white cloud, which barely reads against the factory pale-mint background —
the clips above are rendered on a light blue (`#cfe7f7`), and that is worth doing
on the panel too (Pet BG in the web UI, or `bg#cfe7f7` over serial, which stores
it in NVS).

```bash
python3 tools/mktheme.py ../stickers/128/cloudling -o themes/cloudling \
        --scale 2 --no-upscale --map done=attention \
        --spares juggling,notification,sweeping,carrying
python3 tools/recenter_pack.py themes/cloudling
python3 tools/mochi_theme.py bundle themes/cloudling -o themes/cloudling.zip
python3 tools/theme_gifs.py themes/cloudling pics/cloudling '#cfe7f7'
```

## Placement: why these two packs need `recenter_pack.py`

The converter centres the artwork in the **source GIF's canvas** and stores that
corner as the frame's panel position; the device then magnifies the art *from
that corner* by the manifest's `scale`. When the art was drawn on a canvas that
is not panel-sized the two disagree: these sticker sets are 128×128 and play at
scale 2, and the position the converter computes puts the pet up and to the left
(the cloud worst — a 47×48 frame magnified lands at (40,40), 33 px out; the cat
13–19 px).

`tools/recenter_pack.py` rewrites only those two coordinates per `.caf` —
`(panel - w*scale) // 2` — leaving the artwork, palette, frame count and timing
exactly as the converter produced them. `clawd` and `origin` do not need it:
their art is drawn on a 240 canvas at `scale=1`, so the converter's position *is*
the panel position.

Using the 240-canvas sets (`../stickers/240/...`) at `scale=1` would centre by
itself, but costs: calico is **2926 KB** for six states (over the 2400 KB budget —
it does not fit at all), cloudling is 1997 KB for six (fits, with no room for a
single spare). That is why these two are 128-canvas at scale 2, recentred.

## `face` — just the face, bezel to the edge (11 states)

The same faces from the same generator (`tools/origin_art.py`) in another shell:
**no pet, only the screen** — and the screen fills the panel. The black bezel is
16 px against all four edges, the lit panel 208×208, and the face is drawn on a
16 px unit grid (twice `origin`'s 8 px, which is what lands it exactly in the 240
panel). Six hook states and five spares, with faces identical to `origin`'s.

<table>
  <tr>
    <td align="center"><img src="../pics/face/idle.gif" width="200"><br><code>idle</code></td>
    <td align="center"><img src="../pics/face/thinking.gif" width="200"><br><code>thinking</code></td>
    <td align="center"><img src="../pics/face/working.gif" width="200"><br><code>working</code></td>
  </tr>
  <tr>
    <td align="center"><img src="../pics/face/done.gif" width="200"><br><code>done</code></td>
    <td align="center"><img src="../pics/face/error.gif" width="200"><br><code>error</code></td>
    <td align="center"><img src="../pics/face/sleep.gif" width="200"><br><code>sleep</code></td>
  </tr>
</table>

<table>
  <tr>
    <td align="center"><img src="../pics/face/spare-happy.gif" width="130"><br><code>spare-happy</code></td>
    <td align="center"><img src="../pics/face/spare-slack.gif" width="130"><br><code>spare-slack</code></td>
    <td align="center"><img src="../pics/face/spare-grin.gif" width="130"><br><code>spare-grin</code></td>
    <td align="center"><img src="../pics/face/spare-shine.gif" width="130"><br><code>spare-shine</code></td>
    <td align="center"><img src="../pics/face/spare-blank.gif" width="130"><br><code>spare-blank</code></td>
  </tr>
</table>

`origin_art.py --shell` decides how much gets drawn: `full` the whole pet (what
`origin` uses), `frame` the screen with its bezel, `bare` the face alone, and
`panel` the bezel out to the panel's edges (this one). The faces are identical in
every shell; only what surrounds them goes away.

```bash
python3 tools/origin_art.py --shell panel --prefix face --pack themes/face
python3 tools/mochi_theme.py bundle themes/face -o themes/face.zip
python3 tools/theme_gifs.py themes/face pics/face
```

**This pack ignores Pet BG.** Every pixel of every frame is opaque — the `.caf`'s
field colour does not exist in it — so whatever the background is set to never
shows. Every other theme wants a background picked for it (`origin` cream,
`cloudling` light blue); this is the one that does not.

## Where the previews come from

Every clip above is **not** the source art: each is decoded frame by frame from
the `.caf` the device plays and composited by the firmware's own rules (480×480,
twice the panel), so it is the effect you actually get. Regenerate after changing
a pack:

```bash
python3 tools/theme_gifs.py themes/clawd    pics/states
python3 tools/theme_gifs.py themes/origin   pics/origin '#fdf0dd'
python3 tools/theme_gifs.py themes/calico   pics/calico
python3 tools/theme_gifs.py themes/cloudling pics/cloudling '#cfe7f7'
python3 tools/theme_gifs.py themes/face    pics/face
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
