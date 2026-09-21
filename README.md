<!-- LOGO -->
<p align="center">
  <img src="pics/clawd_mochi_banner.png" alt="Clawd Mochi Logo" width="700"/>
</p>

# Clawd Mochi 🦀🤖

A physical desk companion inspired by **Clawd** — the pixel-crab mascot of Claude Code by Anthropic. An ESP32-C3 drives a 1.54" color TFT display and hosts a mobile web controller — no app, no internet, no cloud required.

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
  (`idle`, `thinking`, `working`, `done`, `error`, `sleep`, and more)
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

## Software setup

### Step 1 — Install Arduino IDE

Download [Arduino IDE 2.x](https://www.arduino.cc/en/software) and install it.

### Step 2 — Add ESP32 board support

1. Open Arduino IDE → **File → Preferences**
2. In "Additional boards manager URLs" paste:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**, search `esp32`, install **"esp32 by Espressif Systems"**

### Step 3 — Install libraries

Go to **Tools → Library Manager** and install both:

- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`

### Step 4 — Configure board settings

Go to **Tools** and set:

| Setting         | Value                   |
| --------------- | ----------------------- |
| Board           | ESP32C3 Dev Module      |
| USB CDC On Boot | **Enabled** ← important |
| CPU Frequency   | 160 MHz                 |
| Upload Speed    | 921600                  |

### Step 5 — Upload the sketch

1. Clone or download this repo
2. Open `clawd_mochi/clawd_mochi.ino` in Arduino IDE
3. Connect the ESP32 via USB-C
4. Select the correct port under **Tools → Port**
5. Click **Upload** (→ arrow button)
6. Wait for "Hard resetting via RTS pin..." — this means success

---

## How to use it

### Connect and open the controller

1. Power the ESP32 via USB-C (any USB charger or power bank)
2. Wait ~3 seconds for the boot animation to finish
3. On your phone or computer, go to **WiFi settings**
4. Connect to the network: **`ClaWD-Mochi`** · password: **`clawd1234`**
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
| Speed slider       | Scales the animation rate (slow / normal / fast) |
| Pet BG             | Background the pet shows through                |
| Canvas BG          | Background of the drawing surface (separate)    |
| Pen color          | Sets drawing color for canvas                   |
| Display on/off     | Toggles the backlight                           |
| Expression         | Plays any state the mounted theme provides      |
| Upload theme       | Replaces the pack on the device — see Customisation |
| ✓ done (in canvas) | Exits canvas mode                               |

The pet also naps: with nothing driving it for two minutes it switches to the
theme's `sleep` state, and wakes on the next state request. Any command restarts
the timer, but only a state change rouses it — a status line should not. Change
the interval with `sleepafter <seconds>` over serial or BLE (0 disables); it is
not persisted, so a reboot returns to the 120 s default.

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

The pet's expressions come from `esp-idf/data/theme/`: a plain-text manifest maps
state names to `.caf` animation files, so you can change what a state looks like
(or add one) by editing files on the device — no firmware change.

```
esp-idf/data/theme/manifest.txt   # state=file, one per line, plus scale=N
esp-idf/data/theme/*.caf          # converted from GIF by tools/gif2caf.py
```

A theme directory plus its manifest is a **pack**, and the device holds exactly
one — the 2.44 MB LittleFS partition has room for one pack of roughly 2 MB and
nothing else. Switch packs from the web UI's `// theme` section, which posts the
new pack file by file and then commits it; see CLAUDE-CODE-BRIDGE.md for why
that is a three-step replacement rather than a swap.

Build a pack from a sticker set with:

```bash
python3 tools/mktheme.py ../stickers/128/calico -o /tmp/calico-pack \
        --scale 2 --no-upscale
```

It prints the packed size against the partition budget, so an oversized pack is
obvious before flashing rather than during upload.

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
authored at 17 fps each play at the rate they were drawn for. The web UI's speed
slider scales that (1 = 1.5x slower, 2 = as authored, 3 = 0.67x). The background
colour follows `bg#RRGGBB`, and is a runtime setting — palette index 0 in every
frame is transparent, so the colour baked into the `.caf` is never displayed.

### Logo animation duration

```cpp
// In animLogoReveal() — how long the logo holds once revealed
delayMs(1500);     // milliseconds — change this number

// Time between reveal steps
delayMs(24);       // lower = faster
```

The reveal is deliberately *not* wired to the speed setting: it is a one-shot
boot flourish, and tying it to `animSpeed` made the web UI's speed slider look
like it controlled the pet's animations when it only ever changed this.

---

## Contributing

Contributions are very welcome! Here are some ideas:

- **New animations** — add new expressions, transitions, or idle behaviors
- **New views** — weather display, clock, notification badges, pixel art scenes
- **Sound** — add a small buzzer for sound effects
- **Sensors** — connect a touch sensor or button for physical interaction
- **OTA updates** — add over-the-air firmware updates
- **MQTT / Home Assistant** — connect to smart home platforms

To contribute: fork the repo, make your changes, and open a pull request. Please keep the single-file structure (`clawd_mochi.ino`) so it stays easy for beginners to flash.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

**Note:** 3D models and media assets are licensed under **CC BY-NC-SA 4.0**.

---

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=yousifamanuel/clawd-mochi)](https://www.star-history.com/#yousifamanuel/clawd-mochi)
