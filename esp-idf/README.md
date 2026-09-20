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

## Wiring (unchanged)

Same as the original — see the main [README](../README.md):

| Display pin | ESP32-C3 GPIO |
| ----------- | ------------- |
| SDA (MOSI)  | GPIO 10       |
| SCL (SCK)   | GPIO 8        |
| RES         | GPIO 2        |
| DC          | GPIO 1        |
| CS          | GPIO 4        |
| BL          | GPIO 3        |
| VCC         | 3V3           |
| GND         | GND           |

After flashing, connect to WiFi `ClaWD-Mochi` (pw `clawd1234`) and open
`http://192.168.4.1`.
