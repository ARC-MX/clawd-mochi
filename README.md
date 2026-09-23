<!-- LOGO -->
<p align="center">
  <img src="pics/clawd_mochi_banner.png" alt="Clawd Mochi Logo" width="700"/>
</p>

# Clawd Mochi 🦀🤖

**English: [README.en.md](README.en.md)** — 本文件是默认版本；两份如有出入，以这份为准。

一个实体桌面摆件，灵感来自 **Clawd** —— Anthropic 的 Claude Code 那只像素螃蟹吉祥物。一块 ESP32-C3 驱动 1.54 寸彩屏，并自带一个手机网页控制器 —— 不需要装 App、不需要联网、不依赖云服务。

**成本：约 ¥50 · 制作时间：约 1 小时 · 难度：入门**

Instagram 支持项目：[![Instagram](https://img.shields.io/badge/Instagram-E4405F?logo=instagram&logoColor=fff&style=for-the-badge)](https://instagram.com/clawd.mochi)

📦 可打印外壳在 MakerWorld：[https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)

---

> ⚠️ 这是一个独立的粉丝项目，与 Anthropic 无隶属、赞助或背书关系。"Claude" 与 "Clawd" 是 Anthropic 的商标。

---

<p align="center">
  <img src="pics/clawd_mochi_3_4.jpeg" alt="Assembled Clawd Mochi on a desk" width="500"/>
  &nbsp;
  <img src="pics/clawd_mochi_claude_code.jpeg" alt="Claude Code view" width="500"/>
</p>

## 它能做什么

Clawd Mochi 放在桌面上，用一块小彩屏播放动画表情。连上它自带的热点，就能用手机或电脑的浏览器控制：

- **主题动画** —— 宠物的表情是从文件系统里读出来播放的动画贴纸，换一套主题不必重新烧录固件
  （`idle`、`thinking`、`working`、`done`、`error`、`sleep` 等等）
- **Claude Code** —— 显示 "Claude Code" 界面，带一个可交互的小终端
- **画布** —— 用手机在屏幕上实时画画

---

## 元器件清单

| 元件 | 规格 | 参考价 |
| ------------------- | -------------------------------- | ------ |
| ESP32-C3 Super Mini | 带 WiFi 的微控制器 | ¥18 |
| ST7789 1.54" TFT | 240×240 SPI 彩屏 | ¥22 |
| 8 根短线 | 8–10 cm 杜邦线 | ¥4 |
| 2× M2×6mm 螺丝 | 固定屏幕压边 | ¥1 |
| 双面胶 | 把元件固定在壳内 | ¥1 |
| USB-C 线 | 供电 | — |
| 3D 打印外壳 | PLA 或 PETG，约 30g | ¥4 |

**合计：约 ¥50**

---

## 接线

> ⚠️ VCC 只能接 **3.3V**，绝不要接 5V。SPI 用 GPIO 8 和 10（硬件 SPI，速度快）。不要用 GPIO 6/7 做 SPI。

| 屏幕引脚 | ESP32-C3 GPIO | 建议线色 |
| ----------- | -------------- | ---------------------- |
| VCC | 3V3 | 红 |
| GND | GND | 黑 |
| SDA | GPIO 10 (MOSI) | 橙 |
| SCL | GPIO 8 (SCK) | 绿 |
| RES | GPIO 2 | 紫 |
| DC | GPIO 1 | 蓝 |
| CS | GPIO 4 | 白 |
| BL | GPIO 3 | 黄 |

---

## 软件

这台设备上跑的是 **ESP-IDF** 固件（`esp-idf/` 目录），不是 Arduino sketch。仓库里的 `clawd_mochi.ino` 是最初的 Arduino 单文件版本，保留作为历史。

- **拿到成品设备的人**：不需要编译任何东西 —— 插上 USB、连它的热点、用浏览器控制，见下一节「怎么用它」；Windows 上另有一份分步说明（含一段可以让 AI 代做的提示词）：[中文（默认）](WINDOWS-SETUP.md) · [English](WINDOWS-SETUP.en.md)
- **要自己做一台、或改固件的人**：见 [esp-idf/README.md](esp-idf/README.md)（与 Arduino 版的差异、启动流程、完整的串口/BLE 命令表）

---

## 怎么用它

### 连上它，打开控制器

1. 用 USB-C 给它供电（任何 USB 充电器或充电宝都行）
2. 等约 3 秒，开机动画结束
3. 在手机或电脑的 **WiFi 设置** 里
4. 连接网络 **`ClaWD-Mochi`** · 密码 **`clawd1234`**（出厂默认 —— 网页里的设备卡片能改这两项，同一个名字也用于蓝牙广播）
5. 打开浏览器访问 **`http://192.168.4.1`**

你会看到网页控制器：

<img src="pics/clawd_mochi_webpage.jpeg" alt="Webpage view" width="500"/>

### 控制器功能

| 按钮 / 控件 | 作用 |
| ------------------ | ----------------------------------------------- |
| Idle | 播放主题的"空闲"动画 |
| Done | 播放主题的"完成"动画 |
| Claude Code | 显示代码界面，打开终端 |
| Canvas | 进入绘画模式 —— 用手机在屏幕上画 |
| Pet BG | 宠物透出来的底色（存 NVS，重启保留） |
| Canvas BG | 画布的底色（与上面独立） |
| Pen color | 画布画笔颜色 |
| Display on/off | 背光开关 |
| Brightness | 背光亮度 0–100%（存 NVS，重启保留） |
| Expression | 播放主题提供的任意状态 |
| Upload theme | 替换设备上的主题包 —— 见"自定义"一节 |
| ✓ done（画布内） | 退出画布模式 |

宠物还会打盹：连续两分钟没有任何东西驱动它，就切到主题的 `sleep` 状态，下一次状态请求时醒来。任何命令都会重置这个计时，但只有**状态切换**能把它唤醒 —— 显示一行状态文字不该吵醒它。可以用串口或 BLE 发 `sleepafter <秒>` 改这个间隔（0 表示关闭）；它**不持久化**，重启回到默认的 120 秒。

---

## 3D 外壳

电子件外壳（主体 + 后盖）在 `clawd_mochi` 模型目录里：

| 文件 | 说明 |
| ------------------------------------------------------------------------------------ | ----------------------------------------- |
| [`./models/clawd_mochi/clawd_mochi_v1.stl`](./models/clawd_mochi/clawd_mochi_v1.stl) | 主体外壳（含前壳与后盖） |

### 打印参数

| 参数 | 值 |
| ------------ | ----------------------------------- |
| 材料 | PLA 或 PETG |
| 层高 | 0.15–0.20 mm |
| 填充 | 15% gyroid |
| 支撑 | 需要 —— 屏幕窗口有悬空 |
| 摆放 | 面朝下，背面平贴打印板 |

建议配色：主体橙色 PLA，后盖哑光黑。

也可以从 MakerWorld 下载模型：[https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)

### 纯 3D 版 Clawd（不含电子件）

如果你只想要一个摆件，用单独的 3D Clawd 模型（没有屏幕和电子件的开孔）。

<img src="pics/clawd_3D_squished_eyes_4_3.png" alt="3D printed Clawd model with squished eyes" width="500"/>

模型文件：

| 文件 | 说明 |
| ---- | ----------- |
| [`./models/clawd_3d/clawd_3D_no_AMS.stl`](./models/clawd_3d/clawd_3D_no_AMS.stl) | 原始 Clawd 3D 模型 |
| [`./models/clawd_3d_squished_eyes/clawd_3D_squished_eyes_no_AMS.stl`](./models/clawd_3d_squished_eyes/clawd_3D_squished_eyes_no_AMS.stl) | 眯眼版本 |

也可以从 MakerWorld 下载：[https://makerworld.com/en/models/2576503-clawd-claude-code-mascot#profileId-2841183](https://makerworld.com/en/models/2576503-clawd-claude-code-mascot#profileId-2841183)

---

## 组装提示

1. 先打印外壳（主体 + 后盖），**先试装屏幕再上胶**
2. 焊接前先把 8 根线穿过后盖的过线槽
3. 用双面胶把 ESP32 固定在后盖内侧
4. 用 2× M2×6mm 螺丝穿过压边孔固定屏幕
5. USB-C 线走线槽引出，扣上后盖

---

## 自定义

### 主题（动画）

宠物的表情来自 `esp-idf/data/theme/`：一个纯文本 manifest 把状态名映射到 `.caf` 动画文件，所以你可以**直接在设备上编辑文件**来改变某个状态的样子（或者新增一个状态）—— 不需要动固件。

```
esp-idf/data/theme/manifest.txt   # state=file，一行一个，另有 scale=N
esp-idf/data/theme/*.caf          # 由 tools/gif2caf.py 从 GIF 转换而来
```

一个主题目录加上它的 manifest 就是一个**主题包**，设备上**只放得下一个** —— 2.44 MB 的 LittleFS 分区刚好够装一组约 2 MB 的包，没有余地。在网页的 `// theme` 区切换：它会逐个文件上传新包、最后提交。为什么是"三步替换"而不是直接换，见 [CLAUDE-CODE-BRIDGE.md](CLAUDE-CODE-BRIDGE.md)。

把一套贴图打包成主题包：

```bash
python3 tools/mktheme.py ../stickers/128/calico -o /tmp/calico-pack \
        --scale 2 --no-upscale
```

它会打印打包后的体积与分区预算的对比，所以一个过大的包在烧录前就能看出来，而不是上传到一半才发现。`tools/mochi_theme.py` 是整条流水线的入口 —— `convert`、`pack`、`verify`、`preview` —— 其中 `preview` 最值得知道：

```bash
python3 tools/mochi_theme.py preview /tmp/calico-pack -o /tmp/calico.png
```

它把**屏幕实际会显示的样子**渲染出来 —— manifest 里的 `scale`、以你的背景色透出的透明区域、画面在存储位置上的取值 —— 所以不用烧录就能判断一组包好不好。建议上传前先跑一次，因为换主题是**破坏性**的：旧包会先被删掉，没有地方留备份。

**决定一组贴图能不能装下的有两个因素**，而且它们互相影响：

- **素材分辨率。** 这些贴图集有多个尺寸，占空间的是画本身而不是画布。`stickers/calico/` 把螃蟹画成 205x155，六个状态加起来 3.6 MB；`stickers/128/calico/` 画成 96x74，同样六个状态只有 991 KB。
- **是存大图还是让设备放大。** 行程编码只在像素锐利时才紧凑，所以在转换阶段放大既费字节又伤锐度。`--no-upscale --scale 2` 把小图存下来、由固件在推送时放大：cloudling 的 idle 动画因此从 481 KB 降到 203 KB，画面完全一样。

另外还有 `--stride N`：每 N 帧保留一帧，把被丢掉那几帧的时长折算到留下的帧上，动画总时长不变 —— 这是拿流畅度换体积，而不是偷偷把主题放快。

播放速度来自主题包本身、不来自固件：每个 `.caf` 里存着从源 GIF 取的每帧时长，所以 8 fps 做的主题和 17 fps 做的主题各按自己当初的节奏播放。背景色用串口或 BLE 的 `bg#RRGGBB` 设置，并存在 NVS 里（重启后保留）；每一帧的调色板索引 0 都是透明的，所以 `.caf` 里烘死的那个颜色永远不会显示出来。

### Logo 动画时长

```cpp
// 在 animLogoReveal() 里 —— logo 完全展开后停留多久
delayMs(1500);     // 毫秒 —— 改这个数

// 展开每一步之间
delayMs(24);       // 越小越快
```

这段展开有自己的固定节奏，不跟随网页上的任何设置：它是一次性的开机小演出，不是宠物的动画。

---

## 参与贡献

非常欢迎贡献！一些方向：

- **新动画** —— 增加新的表情、过渡或空闲动作
- **新界面** —— 天气、时钟、通知角标、像素画场景
- **声音** —— 加一个小蜂鸣器做音效
- **传感器** —— 接触摸传感器或按钮做实体交互
- **OTA 升级** —— 支持无线升级固件
- **MQTT / Home Assistant** —— 接入智能家居平台

参与方式：fork 本仓库、改好、提 PR。改动请针对 `esp-idf/` —— 那是设备实际在跑的固件；`clawd_mochi.ino` 作为最初版本的存档保留。

## 许可证

本项目使用 MIT 许可证 —— 详见 [LICENSE](LICENSE)。

**注意：** 3D 模型与媒体素材使用 **CC BY-NC-SA 4.0** 许可证。

---

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=yousifamanuel/clawd-mochi)](https://www.star-history.com/#yousifamanuel/clawd-mochi)
