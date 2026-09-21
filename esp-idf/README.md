# Clawd Mochi — ESP-IDF port (C++)

This directory contains an ESP-IDF port of the original Arduino sketch
[`clawd_mochi.ino`](../clawd_mochi.ino). The logic is functionally identical
(same animations, same web controller, same routes) but it builds with the
Espressif IoT Development Framework using C++ instead of Arduino.

## What changed vs the Arduino version

| Arduino | ESP-IDF |
| ------- | ------- |
| `Adafruit_GFX` + `Adafruit_ST7789` | self-contained `display.{h,cpp}` driver |
| `SPI.begin(...)` | `driver/spi_master.h` |
| `WiFi.softAP(...)` | `esp_wifi` SoftAP |
| `WebServer` (sync routing) | `esp_http_server` |
| `delay()` | `vTaskDelay()` |
| `String` | C strings (`char[]` / `strtok_r`) |
| `PROGMEM` arrays | plain `const` arrays in `logo_data.h` |

`display.cpp` reimplements only the primitives the sketch uses
(`fillScreen`, `fillRect`, `fillTriangle`, `drawLine`, `fillCircle`,
`drawFastHLine/VLine`, the 5×7 text font), driven through ESP-IDF's SPI master
driver. It does **not** pull in the full Adafruit_GFX/SPITFT stack.

## Files

```
esp-idf/
├── CMakeLists.txt          # top-level project
├── sdkconfig.defaults      # target esp32c3, 4MB flash
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp            # port of clawd_mochi.ino
│   ├── display.h/.cpp      # ST7789 driver + minimal GFX
│   ├── font5x7.h           # classic 5x7 font (generated)
│   ├── logo_data.h         # LOGO_BITMAP, RGB565 boot logo (generated)
│   └── index_html.h        # web controller HTML (generated)
```

## Build & flash

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/)
(any recent release, e.g. v5.x) with the `esp32c3` target.

```bash
source $IDF_PATH/export.sh          # or . ./export.sh from the IDF install

cd esp-idf
idf.py set-target esp32c3
idf.py menuconfig                    # optional — flash size etc.
idf.py build
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor
```

## Wiring

These are the pins the firmware actually drives (`main/main.cpp`). The table
here used to list the original Arduino sketch's ESP32-C3 pinout, which the port
dropped when it moved to a classic ESP32.

| Display pin | ESP32 GPIO |
| ----------- | ---------- |
| SDA (MOSI)  | GPIO 21    |
| SCL (SCK)   | GPIO 18    |
| RES         | GPIO 23    |
| DC          | GPIO 5     |
| CS          | GPIO 32    |
| BL          | GPIO 14    |
| VCC         | 3V3        |
| GND         | GND        |

After flashing, connect to WiFi `ClaWD-Mochi` (pw `clawd1234`) and open
`http://192.168.4.1`.

## Simulating it (Wokwi)

`wokwi.toml` and `diagram.json` in this directory configure the project for the
[Wokwi](https://wokwi.com) simulator, wired to the pins above. Build first, then
start the simulator:

```bash
idf.py build
# VS Code: F1 -> "Wokwi: Start Simulator"   (needs the Wokwi extension)
```

Wokwi reads `build/flasher_args.json` rather than the app `.bin`, which is what
gets the bootloader, the custom partition table **and the 2.44 MB LittleFS
theme** into the simulation. Point it at the `.bin` instead and the device
boots into an empty filesystem — `mountStorage()` formats it on the fly and the
pet has nothing to play.

**What the simulation is good for, and what it is not:**

| | |
| --- | --- |
| ✅ Boot splash, and the animations themselves | the theme is in the flash image, so this is the real firmware drawing the real pack |
| ✅ Checking code changes boot and run | panics, asserts and log output are all visible |
| ❌ **WiFi and BLE** | Wokwi does not simulate a network. No AP to join, so the web controller, `/state` and theme upload are unreachable |
| ❌ **Colours, exactly** | the display is Wokwi's `wokwi-ili9341` standing in for the ST7789 — there is no ST7789 part. Same SPI TFT family, different controller: this panel needs invert-on and a 240x240 window inside a taller GRAM, and the sim part may not honour either |
| ❌ **Backlight** | GPIO 14 is deliberately left unwired; the PWM brightness is not simulated |
| ❌ **Timing** | simulation speed is not wall-clock, so frame-rate numbers from it mean nothing |

So: trust it for *"does the art appear, is it the right art, does it boot"*, and
verify colour and timing on the real panel. `tools/mochi_theme.py preview`
answers the same art question from the pack alone, without a simulator or a
device — it is faster, and it is the one that knows about `scale`.

