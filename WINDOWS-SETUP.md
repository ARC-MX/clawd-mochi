# 在 Windows 上使用 Clawd Mochi

**English: [WINDOWS-SETUP.en.md](WINDOWS-SETUP.en.md)** — 本文件是默认版本；两份如有出入，以这份为准。

写给：拿到这台桌宠、要在 Windows 上把它跑起来的人。

分三层，按需要往下读即可：

- **第一部分：只用它** —— 插上 USB、连热点、浏览器控制。不需要装任何开发工具
- **第二部分（可选）：从 Windows 编译并烧录固件** —— 改代码或换主题时才需要
- **第三部分（可选）：让 Claude Code 驱动它** —— 宠物跟着 Claude Code 的状态动

---

## 第一部分：只用它

### 1. 插上设备（免驱动）

用一根**能传数据的** USB-C 线接到电脑（有些线只供电，不认设备 —— 这是最常见的问题）。

打开 **设备管理器 → 端口 (COM 和 LPT)**，应出现：

```
USB JTAG/serial debug unit (COM5)      ← 端口号可能是别的数字，记下来
```

Windows 10/11 免驱。如果没出现：换一根线、换一个 USB 口，再看设备管理器里有没有带黄色感叹号的设备。

> 这块开发板（ESP32-C3 Super Mini）的 USB 是芯片**原生 USB**，不是 CH340/CP2102 那种外置转换芯片 —— 所以不需要装任何厂商驱动。

### 2. 连上它的热点，打开网页

设备屏幕上（开机时或按 `logo` 重放）会显示 **WiFi 名**和**密码**：

```
WiFi
ClaWD-Mochi          ← 出厂默认，可以在网页里改
password: clawd1234
Open browser:
192.168.4.1
```

以**屏幕上显示的为准**（名字可能已经被改过）。

在 Windows 的 WiFi 列表里连接那个名字，密码按屏幕填。连上后提示 **"无 Internet 连接"是正常的** —— 设备自己就是个热点，不需要外网。

然后用浏览器打开：**<http://192.168.4.1>**

### 3. 网页上能做什么

| 位置 | 作用 |
| --- | --- |
| `// controls` | 屏幕开/关 |
| `// brightness` | 背光亮度 0–100%（存在设备里，重启保留） |
| `// views` | 切到 空闲 / 完成 动画、Claude Code 视图、画布 |
| `// theme` | 换主题包（选一个文件夹上传，会替换设备上现有的那套）、单独播放某个表情 |
| `// device` | 改设备名（**热点名和蓝牙广播名是同一个**）和 WiFi 密码；保存后设备**重启**，需要用新名字重新连 |
| `// terminal` | 设备上的小终端（打字） |
| Canvas | 用手机/触屏在屏幕上画画 |

页面最上方还有一行署名。

### 4. 不用网页时：串口命令行

装了 Python 3 之后（`pip install pyserial`；要推图片再加 `pip install pillow`），可以直接指挥它 —— 插着 USB 就行，不需要连热点：

```bat
python tools\mochi.py state thinking      :: 播放"思考"动画
python tools\mochi.py bright 60           :: 背光 60%
python tools\mochi.py bg "#204060"        :: 宠物底色（存 NVS，重启保留）
python tools\mochi.py canvas              :: 进入画布
python tools\mochi.py line 20 20 220 220 "#00ff88"
python tools\mochi.py img photo.jpg       :: 整屏推一张图
python tools\mochi.py ports               :: 列出可用串口
```

蓝牙（不插线）用 `tools\mochi_ble.py`，命令词完全一样，但需要 `pip install bleak`。

---

## 第二部分（可选）：从 Windows 编译并烧录固件

只有要改固件或用新主题时才需要。

### 1. 装 ESP-IDF v6.1

用官方 Windows 安装器（Universal Online Installer / Offline Installer），**版本选 v6.1** —— 这个项目的 `sdkconfig` 是 v6.1 生成的，装别的版本可能出现配置项不存在的错误。

<https://dl.espressif.com/dl/esp-idf/>

安装器会自带 Python 和工具链，并生成一个 **"ESP-IDF 6.1 PowerShell"**（或 CMD）快捷方式 —— 后面所有命令都在那个终端里跑，不要在普通的 PowerShell 里跑。

### 2. 拿到代码

把本仓库拷到 Windows 上（`git clone` 你手上的那个地址，或者直接拷贝整个文件夹）。

### 3. 编译并烧录

在 **ESP-IDF 6.1 终端**里：

```bat
cd <仓库路径>\esp-idf
idf.py build
idf.py -p COM5 flash
```

`COM5` 换成**设备管理器里那个端口号**。

- 目标芯片 `esp32c3`、分区表、网页和主题素材都已经在仓库里，不需要额外设置
- **一次会写 4 个镜像**：bootloader、分区表、固件、以及网页+主题所在的存储分区 —— 所以改了网页或主题，也是重新烧一次，不需要单独上传

### 4. 看设备日志

```bat
idf.py -p COM5 monitor
```

退出监视器按 **Ctrl + ]**。**烧录前必须先退出监视器**，否则串口被占用，烧录会失败。

---

## 第三部分（可选）：让 Claude Code 驱动它

宠物跟着 Claude Code 的状态走：提交提示 → "思考"，调用工具 → "干活"，一轮结束 → "完成"。

### 1. 装 Python 依赖

```bat
pip install pyserial bleak
```

（`bleak` 是无线那半条路用的，只插线的话 `pyserial` 就够。）

### 2. 注册 hooks

编辑 `%USERPROFILE%\.claude\settings.json`，在 `hooks` 里加这三项（路径按你自己的改；**用正斜杠 `/`**，JSON 里不用转义反斜杠）：

```json
{
  "hooks": {
    "UserPromptSubmit": [
      { "hooks": [ { "type": "command", "async": true, "timeout": 30,
        "command": "\"C:/Python311/python.exe\" \"C:/path/to/clawd-mochi/tools/mochi_hook.py\" UserPromptSubmit" } ] }
    ],
    "PreToolUse": [
      { "hooks": [ { "type": "command", "async": true, "timeout": 30,
        "command": "\"C:/Python311/python.exe\" \"C:/path/to/clawd-mochi/tools/mochi_hook.py\" PreToolUse" } ] }
    ],
    "Stop": [
      { "hooks": [ { "type": "command", "async": true, "timeout": 30,
        "command": "\"C:/Python311/python.exe\" \"C:/path/to/clawd-mochi/tools/mochi_hook.py\" Stop" } ] }
    ]
  }
}
```

`mochi_hook.py` 会**先走串口（快，约 0.1 秒），串口不可用再走蓝牙**（约 4 秒）；两条都失败也**不会**影响 Claude Code（脚本恒返回 0，静默失败）。

设备插着 USB 时走串口最快；想让它无线跟着你，拔掉线即可（蓝牙）。

### 3. 验证

```bat
python tools\mochi_hook.py PreToolUse
```

设备应该切到"干活"动画。没反应的话，手动跑一次里面的命令看看是哪一段失败：

```bat
python tools\mochi.py state working
python tools\mochi_ble.py state working
```

---

## 常见问题

| 现象 | 处理 |
| --- | --- |
| WiFi 列表里找不到设备热点 | 设备是不是还停在开机动画（约 3 秒）或 WiFi 信息页（10 秒）？看屏幕是否亮着。名字可能被改过 —— 以屏幕显示为准 |
| 浏览器打不开 `192.168.4.1` | 确认连的是**设备的热点**（不是家里 WiFi）；地址是 `192.168.4.1` 而不是 `192.168.4.1:80` 之类 |
| `idf.py flash` 说找不到端口 | 显式指定：`idf.py -p COM5 flash`；端口号看设备管理器 |
| 烧录失败、串口被占用 | 先关掉 `idf.py monitor`、串口助手、Arduino IDE 的串口监视器等任何占用串口的程序 |
| **烧录"成功"但设备行为没变** | ESP-IDF 的增量烧录偶尔会误判（本机就踩过）：删掉 `esp-idf\build\*_flashed.bin` 再烧一次；仍不行就用 `esptool.py`（安装器里自带）直接写 `build\clawd_mochi.bin` |
| `mochi.py` 找不到串口 | 跑 `python tools\mochi.py ports` 看列表。它按描述关键字自动认端口；若认错（比如选中了蓝牙虚拟串口），目前没有 `--port` 参数，可以临时改 `tools\mochi.py` 里 `find_port()` 的第一行 |
| 网页画布画了没反应 | 会弹 `stroke not drawn: …` 的提示说明原因；另外注意：**Claude Code 一发命令，宠物就会从你手里接管屏幕**（这是刻意的设计，让宠物优先反映 Claude Code 的状态） |
| 蓝牙搜不到设备 | 设备是不是已经被另一台电脑/手机的蓝牙连走了？它同时只接受有限个连接；也可以先用串口确认设备活着（`python tools\mochi.py bright`） |

---

## 硬件与更多文档

- 接线表、元器件清单、3D 外壳：仓库根目录的 [README.md](README.md)
- ESP-IDF 移植版说明（与 Arduino 版的差异、启动流程、串口/BLE 命令表）：[esp-idf/README.md](esp-idf/README.md)
- 与 Claude Code 的联动方案（BLE / 串口 / WiFi 三条路线）：[CLAUDE-CODE-BRIDGE.md](CLAUDE-CODE-BRIDGE.md)
