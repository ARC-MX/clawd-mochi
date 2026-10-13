<!-- LOGO -->
<p align="center">
  <img src="pics/clawd_mochi_banner.png" alt="Clawd Mochi Logo" width="700"/>
</p>

# Clawd Mochi 🦀🤖

A physical desk companion inspired by **Clawd** — the pixel-crab mascot of Claude Code by Anthropic. An ESP32-C3 drives a 1.54" color TFT display and hosts a mobile web controller — no app, no internet, no cloud required.

**中文（默认）: [README.md](README.md)** — this file is the English version. If the two
disagree, the Chinese one is what this fork treats as authoritative.

**Cost: ~$6–8 · Build time: ~1 hour · Skill level: Beginner**

Support the project on Instagram: [![Instagram](https://img.shields.io/badge/Instagram-E4405F?logo=instagram&logoColor=fff&style=for-the-badge)](https://instagram.com/clawd.mochi)

📦 3D printable case on MakerWorld: [https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)

---

> ⚠️ This is an independent fan project. It is not affiliated with, sponsored by, or endorsed by Anthropic. "Claude" and "Clawd" are trademarks of Anthropic.

---

<p align="center">
  <img src="pics/clawd_mochi_3_4.jpeg" alt="Assembled Clawd Mochi on a desk" width="500"/>
  &nbsp;
  <img src="pics/clawd_mochi_claude_code.jpeg" alt="Claude Code view" width="500"/>
</p>

## What it does

Clawd Mochi sits on your desk and shows animated expressions on a small color display. You control it from any phone or browser by connecting to its built-in WiFi hotspot:

- **Themed animations** — the pet's expressions are animated stickers played from
  the filesystem, so a theme can be swapped without reflashing the firmware
  (twelve states: `idle`, `thinking`, `working`, `done`, `error`, `sleep` and more —
  the full list is under Customisation)
- **Claude Code** — displays "Claude Code" with an interactive terminal
- **Canvas** — draw anything on the display from your phone in real time

---

## Parts list

| Part                | Spec                             | ~Price |
| ------------------- | -------------------------------- | ------ |
| ESP32-C3 Super Mini | microcontroller with WiFi        | ~$2.50 |
| ST7789 1.54" TFT    | 240×240 SPI color display        | ~$3.00 |
| 8 short wires       | 8–10 cm Dupont / jumper wires    | ~$0.50 |
| 2× M2×6mm screws    | to mount display bezel           | ~$0.10 |
| Double-sided tape   | to secure components inside case | ~$0.10 |
| USB-C cable         | for power                        | —      |
| 3D printed case     | PLA or PETG, ~30g                | ~$0.50 |

**Total: ~$7–8**

---

## Wiring

> ⚠️ Connect VCC to **3.3V only** — never 5V. Use GPIO 8 and 10 for SPI (hardware SPI, fast). Do not use GPIO 6/7 for SPI.

| Display pin | ESP32-C3 GPIO  | Wire color (suggested) |
| ----------- | -------------- | ---------------------- |
| VCC         | 3V3            | Red                    |
| GND         | GND            | Black                  |
| SDA         | GPIO 10 (MOSI) | Orange                 |
| SCL         | GPIO 8 (SCK)   | Green                  |
| RES         | GPIO 2         | Purple                 |
| DC          | GPIO 1         | Blue                   |
| CS          | GPIO 4         | White                  |
| BL          | GPIO 3         | Yellow                 |

---

## Software

The device runs **ESP-IDF** firmware (the `esp-idf/` directory), not an Arduino sketch.
`clawd_mochi.ino` in this repository is the original single-file Arduino version, kept as a
historical record.

- **If you have a device**: there is nothing to compile — plug in USB, join its hotspot, drive it
  from a browser. See *How to use it* below, or the Windows guide, which also carries a prompt you
  can hand to an AI: [English](WINDOWS-SETUP.en.md) · [中文（默认）](WINDOWS-SETUP.md)
- **If you are building one, or changing the firmware**: see
  [esp-idf/README.md](esp-idf/README.md) — the differences from the Arduino sketch, the boot
  sequence, and the full serial/BLE command table

---

## How to use it

### Connect and open the controller

1. Power the ESP32 via USB-C (any USB charger or power bank)
2. Wait ~3 seconds for the boot animation to finish
3. On your phone or computer, go to **WiFi settings**
4. Connect to the network: **`ClaWD-Mochi`** · password: **`clawd1234`** (factory
   defaults — the device card in the web UI changes both, and the same name is
   used for the Bluetooth advertisement)
5. Open a browser and go to **`http://192.168.4.1`**

You should see the web controller:

<img src="pics/clawd_mochi_webpage.jpeg" alt="Webpage view" width="500"/>

### Controller features

| Button / control   | What it does                                    |
| ------------------ | ----------------------------------------------- |
| Idle               | Plays the theme's resting animation             |
| Done               | Plays the theme's finished animation            |
| Claude Code        | Shows code display, opens terminal              |
| Canvas             | Enter drawing mode — draw on display from phone |
| Pet BG             | Background the pet shows through                |
| Canvas BG          | Background of the drawing surface (separate)    |
| Pen color          | Sets drawing color for canvas                   |
| Display on/off     | Toggles the backlight                           |
| Brightness         | Backlight level, 0-100% (stored on the device)   |
| Expression         | Plays any state the mounted theme provides      |
| Upload theme       | Replaces the pack on the device — see Customisation |
| ✓ done (in canvas) | Exits canvas mode                               |

The pet also naps: with nothing driving it for two minutes it switches to the
theme's `sleep` state, and wakes on the next state request. Any command restarts
the timer, but only a state change rouses it — a status line should not. Change
the interval with `sleepafter <seconds>` over serial or BLE (0 disables); it is
stored in NVS, so it survives a reboot (the default is 120 s).

**It never naps in the middle of the work, though**: while the panel is showing
`working` or `thinking` the timer does not fire. A long tool call produces no
events at all, and "quiet for a while" cannot tell a build still running from a
session that died; this way the pet errs on the side of staying awake. The cost
is that a session killed outright — no `Stop`, no `SessionEnd` — leaves it awake
until the next state request.

---

## 3D case

The electronics case (body + back) is in the `clawd_mochi` model folder:

| File                                                                                 | Description                               |
| ------------------------------------------------------------------------------------ | ----------------------------------------- |
| [`./models/clawd_mochi/clawd_mochi_v1.stl`](./models/clawd_mochi/clawd_mochi_v1.stl) | Main case layout with body and back parts |

### Print settings

| Setting      | Value                               |
| ------------ | ----------------------------------- |
| Material     | PLA or PETG                         |
| Layer height | 0.15–0.20 mm                        |
| Infill       | 15% gyroid                          |
| Supports     | Yes — for display window overhang   |
| Orientation  | Face-down, flat back on build plate |

Suggested colors: orange PLA for body, matte black for back plate.

You can also download the models from MakerWorld: [https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)

### 3D Clawd (no electronics)

If you just want a display piece, use the separate 3D Clawd model (no screen or electronics cutouts).

<img src="pics/clawd_3D_squished_eyes_4_3.png" alt="3D printed Clawd model with squished eyes" width="500"/>

Model files:

| File | Description |
| ---- | ----------- |
| [`./models/clawd_3d/clawd_3D_no_AMS.stl`](./models/clawd_3d/clawd_3D_no_AMS.stl) | Original Clawd 3D model |
| [`./models/clawd_3d_squished_eyes/clawd_3D_squished_eyes_no_AMS.stl`](./models/clawd_3d_squished_eyes/clawd_3D_squished_eyes_no_AMS.stl) | Squished eyes variant |

You can also download the models from MakerWorld: [https://makerworld.com/en/models/2576503-clawd-claude-code-mascot#profileId-2841183](https://makerworld.com/en/models/2576503-clawd-claude-code-mascot#profileId-2841183)

---

## Assembly tips

1. Print the case file (body + back) and test-fit the display before gluing anything
2. Thread the 8 wires through the back plate slot before soldering
3. Use double-sided tape to fix the ESP32 against the inside of the back plate
4. Secure the display with 2× M2×6mm screws through the bezel holes
5. Route the USB-C cable through the back plate slot and snap the back on

---

## Customisation

### Theme (animations)

The pet's expressions come from a **pack**: a directory holding a plain-text
manifest that maps state names to `.caf` animation files, so what a state looks
like is a file edit — no firmware change.

```
themes/clawd/manifest.txt   # state=file, one per line, plus scale=N
themes/clawd/*.caf          # converted from GIF by tools/gif2caf.py
themes/clawd.zip            # the one file the device takes (mochi_theme.py bundle)
```

The device holds **one** pack: the 2.44 MB LittleFS partition has room for one
of roughly 2 MB and nothing else, so changing themes means replacing. Add the
`.zip` to the theme library in the web UI's `// theme` section and push it —
the whole file goes in one request, the device checks its unpacked size against
the central directory before writing it, and anything that does not fit is
refused. A refusal, like a failure, puts the **firmware's built-in pack** back
rather than leaving an empty theme; the `restore built-in` button does the same
by hand.

What is built in is `themes/clawd` (the crab) — but only the **six** states Claude
Code drives, compressed into the app partition as
`esp-idf/main/default_pack.zip` (168 KB), so a device that has never been pushed
to still has a face. The six spare animations are not in there: all twelve come
to 384 KB compressed and the app partition has 240 KB of room. How that works is
in [CLAUDE-CODE-BRIDGE.md](CLAUDE-CODE-BRIDGE.md).

Budget the waiting time for the partition's own pace: littlefs writes in 128-byte
units, about **78 KB/s**, and the progress bar in the web UI only covers the
transfer. The slow part comes after it — a 2 MB pack is another **25 seconds** of
writing, `themes/origin` (296 KB) about 4. The first boot after a flash is the
same story: the device writes the 892 KB built-in pack first, and the panel sits
on the boot card for about 11 seconds.

### The states this theme provides

The pack holds twelve states and about **2.0 MB** — the partition is 2.44 MB, which is why
exactly one pack fits. Every clip below is decoded frame by frame from the `.caf` the device
plays (480×480, twice the panel), so it is the effect you actually get.

**The first six are the ones Claude Code drives**:

<table>
  <tr>
    <td align="center"><img src="pics/states/idle.gif" width="200"><br><code>idle</code></td>
    <td align="center"><img src="pics/states/thinking.gif" width="200"><br><code>thinking</code></td>
    <td align="center"><img src="pics/states/working.gif" width="200"><br><code>working</code></td>
  </tr>
  <tr>
    <td align="center"><img src="pics/states/done.gif" width="200"><br><code>done</code></td>
    <td align="center"><img src="pics/states/error.gif" width="200"><br><code>error</code></td>
    <td align="center"><img src="pics/states/sleep.gif" width="200"><br><code>sleep</code></td>
  </tr>
</table>

| State | When it plays |
| --- | --- |
| `idle` | nothing is happening — after boot, or the web UI's Idle button |
| `thinking` | a prompt was submitted (hook: `UserPromptSubmit`) |
| `working` | a tool is running (hook: `PreToolUse`) |
| `done` | a turn finished (hook: `Stop`) — only when the *last* working session stops |
| `sleep` | two minutes of quiet (the interval is adjustable, above); never while the panel shows `working`/`thinking` |
| `error` | **nothing drives this yet** — play it by hand with `state error`, or from the web UI's Expression picker |

With several Claude Code sessions running at once the panel shows the busiest
one (`working` > `thinking` > `idle`): one session finishing neither takes the
screen from the others nor puts the pet to sleep early. Sessions are told apart
by the `session_id` in the hook's stdin — see
[`CLAUDE-CODE-BRIDGE.md`](./CLAUDE-CODE-BRIDGE.md) §方案 E.

**The last six ship as spare art** — point any state at one in `manifest.txt`
(`idle=clawd-idle.caf` swaps the default resting animation, for instance):

<table>
  <tr>
    <td align="center"><img src="pics/states/spare-idle.gif" width="130"><br><code>spare-idle</code></td>
    <td align="center"><img src="pics/states/spare-reading.gif" width="130"><br><code>spare-reading</code></td>
    <td align="center"><img src="pics/states/juggling.gif" width="130"><br><code>juggling</code></td>
  </tr>
  <tr>
    <td align="center"><img src="pics/states/notification.gif" width="130"><br><code>notification</code></td>
    <td align="center"><img src="pics/states/sweeping.gif" width="130"><br><code>sweeping</code></td>
    <td align="center"><img src="pics/states/carrying.gif" width="130"><br><code>carrying</code></td>
  </tr>
</table>

| State | Animation | Frames | One loop | Size |
| --- | --- | --- | --- | --- |
| `idle` | `clawd-headphones-groove.caf` | 48 | 3.2 s | 167 KB |
| `thinking` | `clawd-thinking.caf` | 45 | 3.0 s | 139 KB |
| `working` | `clawd-typing.caf` | 45 | 3.0 s | 281 KB |
| `done` | `clawd-happy.caf` | 45 | 3.0 s | 94 KB |
| `error` | `clawd-error.caf` | 45 | 3.0 s | 125 KB |
| `sleep` | `clawd-sleeping.caf` | 45 | 3.0 s | 84 KB |
| `juggling` | `clawd-juggling.caf` | 45 | 3.0 s | 153 KB |
| `notification` | `clawd-notification.caf` | 77 | 5.6 s | 265 KB |
| `sweeping` | `clawd-sweeping.caf` | 45 | 3.0 s | 151 KB |
| `carrying` | `clawd-carrying.caf` | 45 | 3.0 s | 196 KB |
| `spare-idle` | `clawd-idle.caf` | 48 | 3.2 s | 132 KB |
| `spare-reading` | `clawd-idle-reading.caf` | 60 | 4.0 s | 266 KB |

Ask the device which states its mounted theme offers at any time: send `states` over serial or
BLE, or look at the picker in the web UI's `// theme` section. Regenerate the clips above with
`python3 tools/theme_gifs.py themes/clawd pics/states` after changing a theme.

Build a pack from a sticker set, then the one file the device takes:

```bash
python3 tools/mktheme.py ../stickers/128/calico -o themes/calico \
        --scale 2 --no-upscale
python3 tools/mochi_theme.py bundle themes/calico -o themes/calico.zip
```

`mktheme` prints the packed size against the partition budget, so an oversized
pack is obvious before pushing rather than during. `tools/mochi_theme.py` fronts
the whole pipeline — `convert`, `pack`, `bundle`, `verify`, `preview` — and
`preview` is the one worth knowing about:

```bash
python3 tools/mochi_theme.py preview /tmp/calico-pack -o /tmp/calico.png
```

It renders what the panel will show — the manifest's `scale`, the transparent
field with your background behind it, the frame at its stored position — so a
pack can be judged without flashing it. Worth doing before a push.

**Two things decide whether a set fits**, and they interact:

- **Source resolution.** These sets ship at several sizes and the art is what
  costs bytes, not the canvas. `stickers/calico/` draws the crab at 205x155 and
  six states come to 3.6 MB; `stickers/128/calico/` draws it at 96x74 and the
  same six come to 991 KB.
- **Whether you store it big or magnify on the device.** Run-length encoding
  only stays compact while pixels stay crisp, so upscaling at conversion time
  costs bytes *and* sharpness. `--no-upscale --scale 2` stores the art small
  and has the firmware magnify it as it pushes; cloudling's idle animation goes
  from 481 KB to 203 KB that way, the same picture either way.

Also available: `--stride N` keeps every Nth frame, folding the dropped frames'
durations into the survivors so the animation still takes as long — that trades
smoothness for size rather than silently speeding a theme up.

Playback rate comes from the pack, not from the firmware: each `.caf` stores a
per-frame duration taken from the source GIF, so a set authored at 8 fps and one
authored at 17 fps each play at the rate they were drawn for. The background
colour follows `bg#RRGGBB` over serial or BLE, and is stored in NVS — it survives a reboot — palette index 0 in every
frame is transparent, so the colour baked into the `.caf` is never displayed.

### The original theme: the orange cube pet (`themes/origin`)

The repository ships a second pack whose character is not the crab from the
upstream sticker sets but the **orange cube pet** from the product art: a square
head, small ears on both sides, a black-framed screen on the front, three legs
underneath, and pixel-art faces drawn inside that screen.

Its artwork has no source — no sticker set contains this character — so it is
**drawn in code**: `tools/origin_art.py` renders 240×240 pixel art frame by
frame and hands it to the same pipeline that converts everything else. Changing
how it looks means changing a few numbers in that script.

Which state plays which face (`idle` uses the flat-eyed standby face, because
that is where the pet spends nearly all of its time):

| State | Face | File | Frames | Loop | Size |
| --- | --- | --- | --- | --- | --- |
| `idle` | standby (two bars, blinks) | `origin-standby.caf` | 24 | 2.9 s | 45 KB |
| `thinking` | thinking (square eyes, blush, gaze wandering) | `origin-thinking.caf` | 16 | 1.6 s | 31 KB |
| `working` | working (`</>` over a scrolling line of code) | `origin-coding.caf` | 20 | 1.6 s | 50 KB |
| `done` | done (sparkle eyes, blush, a little hop) | `origin-done.caf` | 16 | 1.3 s | 33 KB |
| `error` | error (X eyes, a frown, a whole-body flinch) | `origin-error.caf` | 12 | 1.1 s | 26 KB |
| `sleep` | napping (eyes shut, z rising) | `origin-sleeping.caf` | 24 | 3.6 s | 46 KB |
| `spare-happy` | happy (`^^` eyes, blush, a smile) | `origin-happy.caf` | 16 | 1.1 s | 33 KB |
| `spare-slack` | slacking off (sunglasses, a glint crossing them) | `origin-slack.caf` | 16 | 1.9 s | 30 KB |
| `spare-grin` | laughing (eyes shut `◡◡`, a mouth that opens mid-hop) | `origin-grin.caf` | 16 | 1.1 s | 33 KB |
| `spare-shine` | shining (diamond sparkles for eyes, blush) | `origin-shine.caf` | 16 | 1.4 s | 36 KB |
| `spare-blank` | blank (two dots and a flat mouth) | `origin-blank.caf` | 24 | 3.4 s | 43 KB |

<table>
  <tr>
    <td align="center"><img src="pics/origin/idle.gif" width="200"><br><code>idle</code> standby</td>
    <td align="center"><img src="pics/origin/thinking.gif" width="200"><br><code>thinking</code></td>
    <td align="center"><img src="pics/origin/working.gif" width="200"><br><code>working</code></td>
  </tr>
  <tr>
    <td align="center"><img src="pics/origin/done.gif" width="200"><br><code>done</code></td>
    <td align="center"><img src="pics/origin/error.gif" width="200"><br><code>error</code></td>
    <td align="center"><img src="pics/origin/sleep.gif" width="200"><br><code>sleep</code></td>
  </tr>
</table>

<table>
  <tr>
    <td align="center"><img src="pics/origin/spare-happy.gif" width="130"><br><code>spare-happy</code></td>
    <td align="center"><img src="pics/origin/spare-slack.gif" width="130"><br><code>spare-slack</code></td>
    <td align="center"><img src="pics/origin/spare-grin.gif" width="130"><br><code>spare-grin</code></td>
  </tr>
  <tr>
    <td align="center"><img src="pics/origin/spare-shine.gif" width="130"><br><code>spare-shine</code></td>
    <td align="center"><img src="pics/origin/spare-blank.gif" width="130"><br><code>spare-blank</code></td>
  </tr>
</table>

The reference art has six faces, so the rest are mine: `error` is drawn in the
same palette and pixel style (X eyes and a frown), `sleep` is the standby face
asleep (eyes shut, a z rising), and `grin` / `shine` / `blank` come from the
"more expressions" strip along the bottom of the reference — whose first face,
the smiling one, is the `happy` above. The set is drawn against a **cream**
background — the background is a runtime setting, so set Pet BG to `#FDF0DD` in
the web UI for the reference look, or send `bg#FDF0DD` over serial or BLE,
which stores it in NVS.

Installing it costs 408 KB, which leaves about 2.0 MB of the partition free —
part of the point of a second pack.

Regenerate the art, build the pack, look at it:

```bash
python3 tools/origin_art.py --pack themes/origin        # draw and pack in one go
python3 tools/mochi_theme.py bundle themes/origin -o themes/origin.zip
python3 tools/mochi_theme.py preview themes/origin -o /tmp/origin.png --bg '#FDF0DD'
```

`origin_art.py -o /tmp/origin-gifs` draws the GIFs without packing them, and
`--sheet DIR` lays every frame of every state out as a PNG for checking the
motion — a GIF viewer only ever shows you the first frame.

### Logo animation duration

```cpp
// In animLogoReveal() — how long the logo holds once revealed
delayMs(1500);     // milliseconds — change this number

// Time between reveal steps
delayMs(24);       // lower = faster
```

The reveal has its own fixed pacing rather than following anything on the web
UI: it is a one-shot boot flourish, not the pet's animation.

---

## Contributing

Contributions are very welcome! Here are some ideas:

- **New animations** — add new expressions, transitions, or idle behaviors
- **New views** — weather display, clock, notification badges, pixel art scenes
- **Sound** — add a small buzzer for sound effects
- **Sensors** — connect a touch sensor or button for physical interaction
- **OTA updates** — add over-the-air firmware updates
- **MQTT / Home Assistant** — connect to smart home platforms

To contribute: fork the repo, make your changes, and open a pull request. Please aim changes at `esp-idf/` — that is the firmware the device actually runs; `clawd_mochi.ino` is kept as an archive of the original.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

**Note:** 3D models and media assets are licensed under **CC BY-NC-SA 4.0**.

---

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=yousifamanuel/clawd-mochi)](https://www.star-history.com/#yousifamanuel/clawd-mochi)
