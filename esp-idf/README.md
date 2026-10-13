# Clawd Mochi — ESP-IDF port (C++)

This is the firmware the board actually runs. It started as a port of the
Arduino sketch [`clawd_mochi.ino`](../clawd_mochi.ino) and has since moved well
past it: the themed animation packs, the BLE command channel, the boot card, the
transport icon and the stored device identity exist only here. Treat the sketch
as a legacy snapshot and build this one.

## What changed vs the Arduino version


| Arduino                             | ESP-IDF                                                                             |
| ------------------------------------- | ------------------------------------------------------------------------------------- |
| `Adafruit_GFX` + `Adafruit_ST7789`  | self-contained`display.{h,cpp}`, on `esp_lcd_panel_io_spi` + `esp_lcd_panel_st7789` |
| `SPI.begin(...)`                    | the`esp_lcd` panel IO                                                               |
| `WiFi.softAP(...)`                  | `esp_wifi` SoftAP                                                                   |
| `WebServer` (sync routing)          | `esp_http_server`                                                                   |
| `delay()`                           | `vTaskDelay()`                                                                      |
| `String`                            | C strings (`char[]` / `strtok_r`)                                                   |
| `PROGMEM` arrays                    | plain`const` arrays (`logo_data.h`, `splash_*.h`)                                   |
| web UI in a generated`index_html.h` | `data/index.html`, served from the LittleFS partition                               |
| —                                  | themed `.caf` animation packs, uploaded as a `.zip` (`anim.cpp`, `theme_pack.c`)    |
| —                                  | BLE command channel (`ble_cli.c`)                                                   |
| —                                  | device name and WiFi password in NVS (`settings.c`)                                 |
| —                                  | one command queue shared by the USB and BLE readers (`cmd_queue.h`)                 |

`display.cpp` reimplements only the primitives the app uses (`fillScreen`,
`fillRect`, `fillTriangle`, `drawLine`, `fillCircle`, `drawFastHLine/VLine`, the
5×7 text font) on top of the `esp_lcd` panel handle. It does **not** pull in the
full Adafruit_GFX/SPITFT stack.

## Files

```
esp-idf/
├── CMakeLists.txt            # project; bundles data/ into the storage partition
├── partitions.csv            # app 0x180000 @ 0x10000, storage 0x270000 (LittleFS)
├── sdkconfig.defaults        # esp32c3, 4 MB flash, custom partition table,
│                             #  NimBLE-only BT, 1000 Hz tick (animation pacing)
├── data/                     # the storage partition: the web page, and only it
│   └── index.html
└── main/
    ├── main.cpp              # app_main, the fixed views, HTTP routes, dispatch
    ├── display.h/.cpp        # ST7789 over esp_lcd + minimal GFX
    ├── anim.h/.cpp           # the themed animation player
    ├── theme_pack.h/.c       # unpacks a theme pack (a .zip) onto the partition
    ├── default_pack.zip      # the theme the firmware carries (generated, in flash)
    ├── transport_icon.h/.cpp # corner glyph: which link is driving the pet
    ├── settings.h/.c         # device name + AP password, in NVS
    ├── ble_cli.h/.c          # BLE (NimBLE) command channel
    ├── cmd_queue.h           # the queue every transport posts commands to
    ├── font5x7.h             # 5x7 font (generated)
    ├── logo_data.h           # boot logo bitmap (generated)
    ├── splash_text.h         # boot splash title bitmap (generated)
    └── splash_credit.h       # boot splash credit bitmap (generated)
```

Themes do not ship in that image: a theme is a pack — a `.zip` of one
`manifest.txt` and the `.caf` files it names — uploaded from the web page into
`/littlefs/theme` at runtime. `main/default_pack.zip` is the exception and the
safety net: the six states the Claude Code hooks drive, deflated to 168 KB and
embedded in the app, which `theme_pack.c` expands onto the partition whenever it
finds no manifest there (a fresh flash, or a failed upload). The theme sources
and the packs built from them live at the repository root under `themes/`; see
the top-level README for the `tools/mochi_theme.py bundle` step that makes one.

The generated headers come from `tools/`: `logo2header.py` for the logo,
`text2header.py` for the two splash bitmaps. Those bitmaps bake their background
in, so the bitmap and the firmware's `SPLASH_BG` have to be regenerated together.

## Build & flash

ESP-IDF v6.1, target `esp32c3`. `sdkconfig.defaults` carries the target, the
flash size, the custom partition table and the BT configuration, so a fresh
checkout needs nothing set by hand.

```bash
source $IDF_PATH/export.sh          # or . ./export.sh from the IDF install

cd esp-idf
idf.py build
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor
```

The port is `/dev/ttyACM0`, not the `/dev/ttyUSB0` this README used to say: the
ESP32-C3 exposes its own USB-Serial-JTAG, so there is no CH34x/CP210x bridge in
the path.

`idf.py flash` writes four images: bootloader, partition table, app, and the
LittleFS `storage` partition built from `data/`. The web UI lives in that last
one, so changing it is a reflash. A theme is not: it is a `.zip` uploaded from
the page, which is why this image has room to spare. A full flash does wipe the
partition, so a pushed theme is replaced by the built-in pack on the next boot.

## Wiring

These are the pins the firmware drives (`main/main.cpp`). The table here used to
be the original sketch's classic-ESP32 pinout, which never matched this board.


| Display pin | ESP32-C3 GPIO |
| ------------- | --------------- |
| GND         | GND           |
| VCC         | 3V3           |
| SCL (SCK)   | GPIO 8        |
| SDA (MOSI)  | GPIO 10       |
| RES         | GPIO 2        |
| DC          | GPIO 1        |
| CS          | GPIO 4        |
| BL          | GPIO 3        |

**Two of those are strapping pins** — GPIO2 and GPIO8, two of the ESP32-C3's
three (the third, GPIO9, is unused here). They are sampled at reset, so their
level at boot is not just the panel's business:

- **GPIO2 (RES)** does not select a boot mode, but Espressif recommends holding
  it high regardless, for glitch immunity. The panel's reset line is not a
  substitute for a pull-up.
- **GPIO8 (SCK)** is sampled for the download-mode combination and also controls
  ROM message printing. SPI boot tolerates any level on it, but it floats at
  reset — the SPI clock is not driven yet — so it wants a pull-up too.
- **GPIO9** is pulled low (and the chip reset) to enter joint download boot. It
  is not wired to the display.

## Boot sequence

1. **Splash card** — the yellow commemorative card, held 1.6 s.
2. **Boot card** — the logo on its white rounded card, revealed by a lit edge
   that wipes down from the top edge, then runs back up over the finished card.
   The caption is one unclippable glyph run, which is why the reveal waits until
   the band has cleared it rather than drawing it row by row. `logo` replays the
   whole thing without a reboot.
3. **WiFi screen** — name, password and `192.168.4.1`, held 10 s, or until
   something asks for a state.
4. **The pet** — the theme's `idle`; after the sleep-after interval with no
   commands it drops to the theme's `sleep`.

## Talking to it

`ClaWD-Mochi` / `clawd1234` on `http://192.168.4.1` are factory defaults. The
device card in the web UI changes the name (the hotspot and the Bluetooth
advertisement share it) and the password, and the device restarts to apply them.

USB serial and BLE take the same commands — one dispatch
(`serialHandleLine()` in `main.cpp`), one queue:


| Command                           | Does                                          |
| ----------------------------------- | ----------------------------------------------- |
| `state <name>` / `state off`      | play a state from the theme's manifest / stop |
| `states`                          | list the states the mounted theme provides    |
| `w` / `s`                         | idle / done — the web UI's two buttons       |
| `d` / `q`                         | enter / leave the code view's terminal        |
| `t<text>`                         | type into that terminal                       |
| `logo` / `a`                      | replay the boot card                          |
| `canvas`                          | hand the panel to the drawing view            |
| `line x1,y1,x2,y2,#RRGGBB`        | draw a stroke on it                           |
| `bg#RRGGBB`                       | the pet's background                          |
| `status[N] [-c#RRGGBB] <text>`    | status line, size 1–4                        |
| `img`                             | stream a 240×240 RGB565 frame (USB only)     |
| `device` / `device <name> [pass]` | read / set the identity; setting restarts     |
| `sleepafter <seconds>`            | idle time before the pet naps;`0` disables    |

The nap only starts from a resting state — while the panel shows `thinking` or
`working` the timer is ignored, so a long tool call cannot put the pet to sleep
in the middle of the work (`petIsWorking()`, `main.cpp`).
