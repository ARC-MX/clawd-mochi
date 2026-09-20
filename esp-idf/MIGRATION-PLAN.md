# ESP-IDF 移植：换官方 esp_lcd 驱动 + 恢复原版配置 + 网页改文件系统

## Context

针对 `clawd-mochi/esp-idf/`（经典 ESP32 目标）的三项相关改动：

1. **把手写的 ST7789 驱动换成 ESP-IDF 官方 `esp_lcd` 栈**（`esp_lcd_panel_io_spi` + `esp_lcd_panel_st7789`）。
   当前 `display.cpp` 自己实现 SPI 初始化、ST7789 命令序列、MADCTL 与偏移处理——这一路手写正是前面反复出显示 bug（偏移、镜像、反色、字节序）的根源。

2. **把显示配置恢复成原版 Arduino 工程（`clawd_mochi.ino`，用 `Adafruit_ST7789`）的行为**。
   读 Adafruit 源码发现：我们移植版的旋转移位量与它**正好反了**。

3. **把网页 UI 从固件里搬出来**：现在是 `main/index_html.h` 以 C 字符串内嵌；改为放进文件系统分区，用官方文件服务模式投递。

WiFi / IP / HTTP 部分**已经在用官方库**（`esp_wifi` + `esp_netif` + `esp_http_server`），无需改动。

预期结果：显示层建立在官方驱动上、配置与原版一致、网页从 flash 文件系统提供。

---

## Part 1 — 驱动换成 esp_lcd

**保持 `Display` 类公共 API 不变**，让 `main.cpp` 里约 1000 行绘图代码原样可用。该类是薄薄一层 GFX，只换后端。

关键架构事实：**当前驱动没有帧缓冲**——所有图元（矩形/线/三角/圆/文字）最终都分解为纯色填充。于是整个后端可以归约为一个操作：

```
setAddrWindow(x,y,w,h) + fillWindow(color,count)   →   esp_lcd_panel_draw_bitmap(panel, x, y, x+w, y+h, scratch)
```

**`main/display.h`**
- `#include "driver/spi_master.h"` → `esp_lcd_panel_io.h` / `esp_lcd_panel_ops.h` / `esp_lcd_panel_vendor.h`
- `spi_device_handle_t _spi` → `esp_lcd_panel_handle_t _panel` + `esp_lcd_panel_io_handle_t _io`
- 构造函数签名保持 5 个 GPIO 不变（main.cpp 依赖它）

**`main/display.cpp`**
- `init(w,h)`：`spi_bus_initialize(SPI2_HOST,…)` → `esp_lcd_new_panel_io_spi()` → `esp_lcd_new_panel_st7789()` → `esp_lcd_panel_reset()` → `esp_lcd_panel_init()` → `esp_lcd_panel_invert_color(true)` → `setRotation(1)`（施加 mirror/swap/gap）→ `esp_lcd_panel_disp_on_off(true)`
- `setRotation(m)`：用 `esp_lcd_panel_mirror` + `esp_lcd_panel_swap_xy` + `esp_lcd_panel_set_gap`，按 Part 2 的表
- `setAddrWindow` / `fillWindow` 合并为 `fillArea(x,y,w,h,color)`：填 256 像素临时缓冲 → `draw_bitmap` → 等信号量
- 删除 `sendCommand` / `sendData` / `spiPreCb`（后者是死代码：`t.user` 从未赋值，实际在误操作 GPIO0）
- 其余 GFX/文字图元保持原样

**两个必须处理的坑**（读 IDF 驱动源码确认）：
- `esp_lcd_panel_draw_bitmap()` **是异步的**——像素只入 DMA 队列即返回。复用同一块 scratch 缓冲而不等待会写坏数据。做法：`io_config.on_color_trans_done` 回调里 `xSemaphoreGiveFromISR`，回填缓冲前先 `xSemaphoreTake`。这同时让绘图保持原来"同步返回"的语义。
- `esp_lcd` 的 `init()` **不发 `DISPON`**，必须自己调 `esp_lcd_panel_disp_on_off(panel, true)`。

**颜色字节序**：设 `panel_config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE`，并**删掉** fillWindow 里的软件字节交换。esp_lcd 不会替你交换字节；该选项让 ST7789 按原生小端接收，正好匹配 `color565()` 的输出。（与现在「`RAMCTRL 0xF0` + 手工交换」等价，但省掉 CPU 开销。）

**`main/CMakeLists.txt`**：`REQUIRES` 增加 `esp_lcd`（当前其头文件不在 include 路径上；库本身已在链接，因为顶层 CMakeLists 未限制组件）。

---

## Part 2 — 按原版恢复显示配置

依据 `/tmp/Adafruit_ST7789.cpp`（原版所用库），`init(240,240)` 时：
`_rowstart = 320-240 = 80`、`_rowstart2 = 0`、`_colstart = _colstart2 = 0`。

`main.cpp` 用的是 rotation 1：
```cpp
case 1:  madctl = MADCTL_MY | MADCTL_MV;
         _xstart = _rowstart  = 80;   // ← 偏移在 X
         _ystart = _colstart2 = 0;
```

正确对照表（以及当前代码的偏差）：

| rot | mirror_x | mirror_y | swap_xy | x_gap | y_gap | 当前代码 | 需改 |
|-----|----------|----------|---------|-------|-------|----------|------|
| 0 | ✓ | ✓ | – | 0 | 80 | 0, 0 | ⚠️ |
| 1 | – | ✓ | ✓ | **80** | **0** | 0, 80 | ⚠️ 反了 |
| 2 | – | – | – | 0 | 0 | 0, 0 | ✅ |
| 3 | ✓ | – | ✓ | 0 | 0 | 80, 0 | ⚠️ |

**反色**：Adafruit 的 init 表里明确发 `INVON`，且原版草图全程没调用 `invertDisplay()` → 用 `esp_lcd_panel_invert_color(panel, true)`。

**SPI 时钟保持 26.67 MHz**，不恢复原版的 40 MHz：我们的 MOSI=21 / SCLK=18 不是 IOMUX 原生脚，走 GPIO 矩阵时上限为 80MHz APB ÷ 3 = 26.67 MHz，40 MHz 会在 `spi_bus_add_device` 直接 abort（前面实测过）。

**其余各项已与原版一致，不要动**：布局常量（`EYE_*` / `TERM_*` / `LOGO_CX/CY` / `PREFIX_PX`）、颜色值（`C_ORANGE=(218,17,0)` 等）、以及全部固定文本字符串（"Anthropic"、"Claude"/"Code"、"Clawd"/"Mochi"、WiFi 页文案）——均已逐项核对无误。

---

## Part 3 — 网页改用文件系统（LittleFS）

LittleFS 经确认**离线可用**：本地组件缓存 `/opt/esp-idf/tools/components/joltwallet/littlefs/`（1.20.4/1.21.1/1.22.3），且激活脚本配置了本地镜像 `IDF_COMPONENT_LOCAL_STORAGE_URL:file:///opt/esp-idf/tools`。
（若组件解析出意外问题，退回 IDF 核心组件 **SPIFFS** 只需改挂载调用与 CMake 函数名，代价很小。）

新增/修改：

| 文件 | 改动 |
|---|---|
| `main/idf_component.yml` | **新增**：`dependencies: joltwallet/littlefs: "~=1.21.0"` |
| `partitions.csv` | **新增**（项目根）：见下 |
| `sdkconfig` | `CONFIG_PARTITION_TABLE_SINGLE_APP=y` → `CONFIG_PARTITION_TABLE_CUSTOM=y`（`CUSTOM_FILENAME` 默认已是 `partitions.csv`） |
| 顶层 `CMakeLists.txt` | `project(clawd_mochi)` 之后加 `littlefs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/web FLASH_IN_PROJECT)` |
| `web/index.html` | **新增**：从 `index_html.h` 抽出的 HTML |
| `main/main.cpp` | 启动时挂载 LittleFS；重写 `routeRoot` 从文件流式发送 |
| `main/index_html.h` | **删除**（含 `main.cpp:40` 的 include） |

`partitions.csv`（4 MiB flash，factory 维持 1M 不变；`storage` 大小按需可缩）：
```
# Name,     Type, SubType,  Offset,   Size,     Flags
nvs,        data, nvs,      0x9000,   0x6000,
phy_init,   data, phy,      0xf000,   0x1000,
factory,    app,  factory,  0x10000,  1M,
storage,    data, littlefs, ,         0x2F0000,
```

`routeRoot` 按官方 `file_serving` 例程的 `download_get_handler` 模式改写：
`fopen("/littlefs/index.html","r")` → 循环 `fread` + `httpd_resp_send_chunk` → 空 chunk 收尾；先 `httpd_resp_set_type(req, "text/html")`。

**注意**：**不要**引入 `/*` 通配路由。`httpd_find_uri_handler` 按注册顺序取第一个匹配，且 `httpd_register_uri_handler` 会拒绝被更早的通配符覆盖的 URI——而现有 `/cmd`、`/char` 等注册处**都丢弃了返回值**，一旦通配符在前，这些路由会静默失效。只服务一个文件时保持 `r.uri = "/"` 不变，仅改其处理函数体即可。

---

## 验证

1. **构建**：`idf.py build` —— 编译通过；构建日志中分区表应列出 `storage`，并生成 `storage.bin`。
2. **烧录**：`sudo chmod a+rw /dev/ttyUSB0` 后 `idf.py -p /dev/ttyUSB0 flash`（`FLASH_IN_PROJECT` 使一条命令同时写入文件系统镜像）。
3. **串口日志**（pyserial 抓取）：LittleFS 挂载成功、`esp_lcd` 无 abort、无 brownout、`SoftAP started`。
4. **屏幕**：开机橘红底 + 白色 logo（不镜像、不偏移）；WiFi 信息页颜色正常（黑底白字 + 橘红 IP）。
5. **网页**：连 `ClaWD-Mochi` / `clawd1234`，打开 `http://192.168.4.1`，页面从文件系统加载成功；点击各按钮（`/cmd`、`/canvas` 等）功能仍正常——这是验证路由未被破坏的关键一步。
6. **回归检查**：`grep -r INDEX_HTML main/` 应为空，确认已完全切换到文件系统。
