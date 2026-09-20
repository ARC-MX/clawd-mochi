# Clawd Mochi ↔ Claude Code 联动方案

> 状态：**设计文档（未实施）**
> 目标：让 ESP32 上的实体设备实时反映 Claude Code 的状态（空闲 / 思考中 / 正在用工具 / 需要授权）

---

## 1. 现状

**两块是分开的，mochi 目前完全不接 Claude Code。**

| 项目 | 接 Claude Code？ | 说明 |
|---|---|---|
| `clawd/`（桌面宠物） | ✅ 已接 | Electron 应用，通过 hooks 实时响应 |
| `clawd-mochi/`（本 ESP32 项目） | ❌ 未接 | 纯手动：连其热点、浏览器点按钮 |

`clawd-mochi/README.md` 明确写着 "no app, no internet, no cloud required"——固件里那个 **"Claude Code" 视图只是显示模式**（画个界面 + 一个可打字的终端），**不与真实的 Claude Code 通信**。

### 1.1 已跑通的参考链路（桌面端）

本机 `~/.claude/settings.json` 里已装好 clawd hooks，覆盖 15 个事件：

```
Claude Code ──hook──> node ~/.claude/hooks/clawd-hook.js <EventName>
                      （携带 CLAWD_REMOTE=1 / CLAWD_SSH_REMOTE=1）
                           └──> 经 SSH 转发到桌面端 Electron 渲染宠物
另外：PermissionRequest → http://127.0.0.1:23334/permission/<id>（本机已在监听）
```

hook 的输入是 **stdin 上的 JSON**，`clawd-hook.js` 实际读取的字段：
`tool_name`、`tool_input`、`session_id`、`transcript_path`、`message`、`cwd`。

这套机制可以直接复用为 mochi 的参考实现。

### 1.2 设备现有 HTTP 接口

| 请求 | 效果 |
|---|---|
| `GET /cmd?k=w` | 正常眼睛（空闲） |
| `GET /cmd?k=s` | 眯眼（开心/提示） |
| `GET /cmd?k=d` | 切到 Claude Code 视图，进入终端模式 |
| `GET /cmd?k=a` | 播放 logo 动画 |
| `GET /cmd?k=q` | 退出终端模式，回到代码视图 |
| `GET /char?c=<字符>` | 往终端打字（单字符） |
| `GET /state` | `{view,busy,term,bl,speed}` |
| `GET /backlight?on=0\|1` | 背光开关 |
| `GET /speed?v=1\|2\|3` | 动画速度 |

> 注：服务端 `max_uri_handlers = 16`，目前已注册 10 个，新增路由要注意余量，且**必须注册在通配路由之前**（当前没有通配路由，安全）。

---

## 2. 拓扑方案（关键决策）

**障碍**：固件是 **SoftAP**（`192.168.4.1`）。宿主机要给它发指令，就必须能路由到该地址。

本机现状：只有**一块无线网卡**（`wlP9s9`，现连 `10.9.13.98`）；另有**两块有线网卡**在线（`enp1s0f1np1` = 192.168.200.11、`enP2p1s0f1np1` = 192.168.200.13）。

### 方案 A — 固件加 STA 模式（推荐）

让 ESP32 加入你家 WiFi（`WIFI_MODE_APSTA`，热点保留），hook 直接 POST 到它的 IP / `clawd-mochi.local`。

- ✅ 宿主机网络**完全不用动**
- ✅ 设备可被局域网内任何机器访问
- ✅ 顺带获得 OTA / 远程调试的可能
- ⚠️ 需改固件；需知道 WiFi SSID/密码（建议放 `sdkconfig` 或 NVS，不要硬编码进 git）
- ⚠️ mDNS 在 IDF v6.1 是托管组件 `espressif/mdns`（**本地缓存已有 v1.12.0，可离线用**）

### 方案 B — 宿主机加入设备热点

固件不动，宿主机连 `ClaWD-Mochi` 热点。

- ✅ **零固件改动**
- ❌ 宿主机唯一无线网卡会切走（有线还在，但默认路由/上网会变）
- ❌ 设备离线时宿主机也失去外网，体验割裂
- ❌ 只有连在该热点上的机器能驱动设备

### 方案 C — 中间机 / 第二网卡

用一台常年在线的小机器（或给本机加一块 USB 无线网卡）专职连设备热点，跑转发。

- ✅ 宿主机网络干净
- ❌ 多一个组件要维护

### 方案 D — 蓝牙（BLE）

**点对点直连，绕开上面所有网络拓扑问题**：宿主机不必切无线网卡、设备也不必连路由器。

已核实的条件：

| 端 | 状态 |
|---|---|
| 宿主机 | `hci0`（MediaTek USB，**HCI 5.4**，支持 BLE），BlueZ **5.72** |
| 宿主工具 | `gatttool` / `btmgmt` / `hcitool` / `rfcomm` 已装；`bleak` 可 `pip install`（PyPI 可达） |
| ESP32 | 芯片支持 BT Classic + BLE（`CONFIG_SOC_BT_SUPPORTED=y`）；**固件当前未启用**（`CONFIG_BT_ENABLED` 未开） |
| 官方参考 | `examples/bluetooth/nimble/bleprph_wifi_coex`（BLE 外设 + WiFi 共存，与本场景一致） |

两条技术路线：

| 维度 | D1. BLE GATT（NimBLE） | D2. BT Classic SPP |
|---|---|---|
| ESP32 侧开销 | **轻** | 重（需 Bluedroid 经典栈） |
| 宿主侧 | `bleak` / `gatttool` | **最简**：写 `/dev/rfcomm0` |
| 配对 | 可免配对 | 需配对 |
| 功耗 | 低 | 较高 |
| 适合 | 状态推送（本场景） | 串口流 |

**推荐 D1（BLE GATT + NimBLE）**。

- ✅ 宿主机网络与设备网络**都不受影响**
- ✅ 功耗远低于 WiFi（对 brownout 更友好）
- ✅ 无需路由器，桌面级距离足够（~10m）
- ⚠️ 需改固件：启用 BT 栈、加 GATT 服务、**扩大 app 分区**（见 §5.4）
- ⚠️ WiFi 与蓝牙共享射频，需 `esp_coex` 调度（本项目 Web 负载轻，影响可接受）

### 方案 E — USB 串口（⭐ 已实现，首选）

**设备本来就插着 USB**，所以直接走串口最省事：不需要路由器、不需要配对、不需要改网络。

- ✅ **已实现并验证**（移植自上游 PR #3）
- ✅ 宿主机网络、设备网络、蓝牙**全都不涉及**
- ✅ 零额外依赖（宿主机只需 `pyserial`；发图另需 `Pillow`）
- ✅ 顺带能传整幅图像、显示状态文字
- ⚠️ 需要有线的 USB 连接（设备不能离得太远）
- ⚠️ 同一时刻只有一台主机能用（点对点）

**设备端**：UART0（CH340）上的行命令解析器，115200 波特，`\n` 结尾。
**宿主端**：`tools/mochi.py`。

```bash
python3 tools/mochi.py status "Claude is thinking…"
python3 tools/mochi.py squish
python3 tools/mochi.py img photo.jpg
```

| 命令 | 效果 |
|---|---|
| `w` / `s` / `d` / `a` / `q` | 眼睛 / 眯眼 / Claude Code 视图 / logo / 退出终端 |
| `t<文本>` | 往终端打字 |
| `status[N] [-c#RRGGBB] <文本>` | 眼睛下方状态文字（N=1..4 字号） |
| `bg#RRGGBB` · `speed1\|2\|3` · `canvas` · `logo` | 背景 / 速度 / 画布 / logo |
| `line x1,y1,x2,y2,#RRGGBB` | 画线 |
| `img` | 接收整幅原始 RGB565 图像 |

> ⚠️ **本机硬件注意**：这块板的自动复位电路需要**打开串口时 `DTR=True、RTS=False`，并关闭 HUPCL**（否则关闭端口时会掉线触发复位）。上游 PR 用的 `DTR=False/RTS=False` 在本板会导致每次打开都复位。`tools/mochi.py` 的 `_open()` 已经处理好了。

**Claude Code hooks 示例**（写进 `~/.claude/settings.json`）：

```json
{
  "hooks": {
    "UserPromptSubmit": [{
      "hooks": [{
        "type": "command",
        "command": "cd /path/to/clawd-mochi && python3 tools/mochi.py squish && python3 tools/mochi.py status \"$(basename \"$PWD\")\"",
        "async": true,
        "timeout": 20
      }]
    }],
    "Stop": [{
      "hooks": [{
        "type": "command",
        "command": "cd /path/to/clawd-mochi && python3 tools/mochi.py eyes",
        "async": true,
        "timeout": 20
      }]
    }]
  }
}
```

> `async: true` 是**必须的**；`timeout` 也要给足——设备端每次视角切换会跑一段动画（实测约 4 秒），同步等待会把 CC 拖慢。

### 对比

| 维度 | A（STA） | B（宿主连AP） | C（中间机） | D（蓝牙） | **E（USB 串口）** |
|---|---|---|---|---|---|
| 固件改动 | 有 | 无 | 无 | 有（较重） | **已完成** |
| 宿主机网络影响 | 无 | 大 | 无 | 无 | **无** |
| 多机可用 | 是 | 否 | 是 | 否 | 否（点对点） |
| 依赖路由器 | 是 | 否 | 是 | 否 | **否** |
| 需要额外硬件 | 否 | 否 | 是 | 否 | **否**（用现有 USB 线） |
| 实施成本 | 中 | 低 | 中高 | 中高 | **已付出（零剩余）** |
| 推荐度 | ⭐⭐⭐ | ⭐ | ⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

**首选方案 E（USB 串口）**：设备本来就在 USB 上，零新增硬件、零网络改动，且**已经实现并验证通过**。
A（WiFi STA）可作为补充——若希望**局域网内多台机器**都能驱动设备（例如手机也能控制），它仍然有价值。D（蓝牙）适合设备需要脱离 USB 独立摆放的场景。

---

## 3. 建议架构（以方案 A 为例）

> 本节以 WiFi 路线（方案 A）为例说明整体数据流；蓝牙路线（方案 D）只在**传输层**不同——把下图的 `HTTP GET` 换成一次 BLE 特征值写入即可，事件映射与 hook 约束完全一致。蓝牙侧的具体骨架见 §5.4。

```
Claude Code
   │  各事件 hook（stdin JSON）
   ▼
~/.claude/hooks/mochi-hook.js  ──HTTP GET──>  http://clawd-mochi.local/cmd?k=...
   │                                                   │
   │（非阻塞、失败即忽略）                              ▼
   └── 永远不阻塞 CC 主流程                        ESP32 屏幕状态变化
```

要点：

- hook **必须 async + 短超时**，且设备不在线时静默失败——绝不能拖慢或阻塞 Claude Code。
- 用 mDNS 名 `clawd-mochi.local` 免去写死 IP（本机需能解析 mDNS，Linux 上通常由 avahi 提供）。
- 若 mDNS 解析不稳，退化为**静态 IP**（在路由器上给设备 MAC 绑定固定地址）最稳。

---

## 4. 事件 → 动作映射

| Claude Code 事件 | 设备动作 | 指令 | 理由 |
|---|---|---|---|
| `SessionStart` | 正常眼睛 | `/cmd?k=w` | 会话开始，回到待机 |
| `UserPromptSubmit` | 眯眼 | `/cmd?k=s` | "听到了，开始想" |
| `PreToolUse` | Claude Code 视图 + 打字 | `/cmd?k=d` 后 `/char?c=...` | 展示正在用的工具名 |
| `Notification` | 眯眼 | `/cmd?k=s` | 需要关注 |
| `PermissionRequest` | 眯眼（可加闪烁） | `/cmd?k=s` | 等待授权 |
| `Stop` | 正常眼睛 | `/cmd?k=w` | 回到空闲 |
| `SessionEnd` | 正常眼睛 | `/cmd?k=w` | 收尾 |

> `PreToolUse` 想显示工具名有个约束：`/char` 一次只收一个字符，逐字发送会打成串请求。建议**仅在工具名变化时**发送，且只发前几个字符；或者后续在固件里加一个 `/text?s=...` 批量接口（更干净）。

---

## 5. 实现骨架

### 5.1 固件侧（STA + mDNS）

在现有 `wifiInitSoftAP()` 基础上扩展（保留 AP，叠加 STA）：

```cpp
// main.cpp
#include "esp_netif.h"
#include "mdns.h"

#define WIFI_STA_SSID CONFIG_MOCHI_STA_SSID
#define WIFI_STA_PASS CONFIG_MOCHI_STA_PASS

static void wifiInitSta() {
  esp_netif_create_default_wifi_sta();          // 与 AP 的 netif 并存

  wifi_config_t sta = {};
  strncpy((char*)sta.sta.ssid,     WIFI_STA_SSID, sizeof(sta.sta.ssid));
  strncpy((char*)sta.sta.password, WIFI_STA_PASS, sizeof(sta.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));   // 原为 WIFI_MODE_AP
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
  // esp_wifi_start() 已在 wifiInitSoftAP() 里调用；若拆开注意顺序

  // 注册事件：WIFI_EVENT_STA_CONNECTED / IP_EVENT_STA_GOT_IP 时打日志拿 IP
}

static void mdnsStart() {
  ESP_ERROR_CHECK(mdns_init());
  ESP_ERROR_CHECK(mdns_hostname_set("clawd-mochi"));
  ESP_ERROR_CHECK(mdns_instance_name_set("Clawd Mochi"));
}
```

`main/idf_component.yml` 追加：

```yaml
dependencies:
  joltwallet/littlefs: "~=1.21.0"
  espressif/mdns: "~=1.12.0"     # 本地缓存已有 1.12.0，可离线解析
```

`main/CMakeLists.txt` 的 `REQUIRES` 追加 `mdns`（组件名）。

凭证建议走 `menuconfig`（Kconfig.projbuild + sdkconfig），避免把 WiFi 密码提交进仓库。

### 5.2 宿主侧 hook 脚本

`~/.claude/hooks/mochi-hook.js`：

```js
#!/usr/bin/env node
// 用法: node mochi-hook.js <EventName>   (事件 JSON 从 stdin 传入)
const http = require('http');

const HOST = process.env.MOCHI_HOST || 'clawd-mochi.local';
const PORT = Number(process.env.MOCHI_PORT || 80);

const MAP = {
  SessionStart:     'w',
  UserPromptSubmit: 's',
  PreToolUse:       'd',
  Notification:     's',
  Stop:             'w',
  SessionEnd:       'w',
};

function fire(path) {
  const req = http.get({ host: HOST, port: PORT, path, timeout: 800 }, res => res.resume());
  req.on('error', () => {});          // 设备不在线 -> 静默忽略
  req.on('timeout', () => req.destroy());
}

let raw = '';
process.stdin.on('data', d => (raw += d));
process.stdin.on('end', () => {
  const c = MAP[process.argv[2]];
  if (c) fire(`/cmd?k=${c}`);
});
```

### 5.3 注册到 Claude Code

写进 `~/.claude/settings.json` 的 `hooks`（与现有 clawd hooks 并存）：

```json
{
  "hooks": {
    "PreToolUse": [{
      "matcher": "",
      "hooks": [{
        "type": "command",
        "command": "node /home/mxwang/.claude/hooks/mochi-hook.js PreToolUse",
        "async": true,
        "timeout": 5
      }]
    }]
  }
}
```

> `async: true` 与 `timeout` 是**必须的**——hook 绝不能阻塞 Claude Code。

### 5.4 蓝牙路线骨架（方案 D）

#### 固件侧 — 启用 NimBLE 与 WiFi 共存

在 `main/sdkconfig.defaults`（或 `sdkconfig`）追加。以下**照抄官方 `bleprph_wifi_coex` 示例，勿自创**：

```ini
CONFIG_BT_ENABLED=y
CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y      # 仅 BLE，不开经典蓝牙，省 RAM
CONFIG_BT_BLUEDROID_ENABLED=n
CONFIG_BT_NIMBLE_ENABLED=y            # NimBLE 比 Bluedroid 轻得多
CONFIG_ESP_WIFI_IRAM_OPT=n            # 为蓝牙腾出 IRAM
```

`main/CMakeLists.txt` 的 `REQUIRES` 增加 `bt`。

> ⚠️ **分区表必须同步调整**：官方示例强制 `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`，因为蓝牙栈会显著增大 app；其 `sdkconfig.defaults.esp32` 注释也专门说明「确保分区对 ESP32 构建有足够空间」。
> 本项目 `partitions.csv` 的 `factory` 当前只有 `1M`，而现有二进制已 ~0.88MB——加 NimBLE 后极可能超出。
> 建议改成 `factory` = `1.5M`、`storage` = `0x240000`（storage 现在 3MB 却只用了 28KB，缩小无损失）。

#### 固件侧 — 自定义 GATT 服务

参考示例 `examples/bluetooth/nimble/bleprph_wifi_coex/main/gatt_svr.c`，加一个**可写**特征值：

```cpp
// 自定义 128-bit UUID（自行生成，两端必须一致）
static const ble_uuid128_t mochi_svc_uuid   = BLE_UUID128_INIT(/* 16 字节 */);
static const ble_uuid128_t mochi_state_uuid = BLE_UUID128_INIT(/* 16 字节 */);

static int mochi_chr_access(uint16_t conn, uint16_t attr,
                            struct ble_gatt_access_ctxt* ctxt, void* arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint8_t c = 0;
    os_mbuf_copydata(ctxt->om, 0, 1, &c);
    applyCommandFromChar((char)c);      // 与 /cmd?k=<c> 走同一套动作
  }
  return 0;
}

static const struct ble_gatt_svc_def mochi_svcs[] = {
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &mochi_svc_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]){
      {
        .uuid     = &mochi_state_uuid.u,
        .access_cb = mochi_chr_access,
        .flags    = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
      },
      { 0 }
    },
  },
  { 0 }
};
```

建议把现有 `routeCmd()` 里的 `switch (c)` 抽成 `applyCommandFromChar(char)`，让 **HTTP 与 BLE 两条路径共用**同一套动作，避免逻辑重复。

#### 宿主侧 — BLE central

优先 `bleak`（`pip install bleak`，PyPI 已验证可达）：

```python
import asyncio
from bleak import BleakClient

ADDR = "AA:BB:CC:DD:EE:FF"      # 设备 MAC（配对后固定）
CHR  = "0000xxxx-0000-1000-8000-00805f9b34fb"   # 与固件 UUID 一致

async def push(ch: str):
    async with BleakClient(ADDR) as c:
        await c.write_gatt_char(CHR, ch.encode())

asyncio.run(push("s"))
```

备选：`gatttool`（**已安装，免依赖**）：

```bash
gatttool -b AA:BB:CC:DD:EE:FF --char-write-req -a 0x0010 -n 73   # 0x73 == 's'
```

hook 脚本把 §5.2 里的 HTTP 请求换成一次 BLE 写入即可；**同样必须 async + 短超时 + 失败静默**，并保留去抖。

---

## 6. 分阶段实施

### 路线 A（WiFi）

| 阶段 | 内容 | 验证 |
|---|---|---|
| A1 | 固件加 STA + mDNS，串口打印获得的 IP | 路由器后台能看到设备；`ping clawd-mochi.local` 通 |
| A2 | 宿主机手动 `curl http://clawd-mochi.local/cmd?k=s` | 屏幕切眯眼 |
| A3 | 写 `mochi-hook.js`，手动喂 JSON 测试 | `echo '{}' \| node mochi-hook.js SessionStart` |
| A4 | 注册到 settings.json，实际跑一次会话 | 屏幕随 CC 状态变化 |
| A5 | （可选）固件加 `/text?s=...` 批量文本接口，让终端显示更顺 | 工具名整串显示 |

### 路线 D（蓝牙 / BLE）

| 阶段 | 内容 | 验证 |
|---|---|---|
| D1 | 先调 `partitions.csv`（`factory`→`1.5M`、`storage`→`0x240000`），再启用 NimBLE 编译 | 编译通过且不超分区 |
| D2 | 加 GATT 服务，上电开始广播 | `bluetoothctl scan on` 能看到 `clawd-mochi` |
| D3 | 宿主 `pip install bleak`，脚本连上并写特征值 | 屏幕状态随之改变 |
| D4 | 把 hook 的 HTTP 请求换成 BLE 写入（async + 短超时 + 去抖） | 屏幕随 CC 状态变化，且 CC 不卡顿 |
| D5 | 共存验证 | WiFi 热点与 Web UI 仍正常工作 |

> D1 必须先做——分区不够时蓝牙根本编不进去。

---

## 7. 风险与注意事项

- **绝不能阻塞 Claude Code**：hook 必须 `async`、超时 ≤1s、错误静默。设备掉线时 CC 应完全无感。
- **凭证管理**：WiFi 密码进 Kconfig/sdkconfig，不要提交进 git（当前 `sdkconfig` 是被跟踪的）。
- **mDNS 可靠性**：跨网段/某些路由器会挡 mDNS 多播；稳一点就用**静态 IP**。
- **设备并发**：httpd 是单线程处理，`/char` 逐字发会排队。批量文本接口更合适。
- **钩子事件量**：`PreToolUse` 在密集工具调用时会非常频繁（一次会话可能上百次）。建议在脚本里**做去抖**（例如同一状态 300ms 内只发一次），否则设备会疲于刷新。
- **AP 与 STA 同信道**：`WIFI_MODE_APSTA` 下 AP 会跟随 STA 的信道，若与家里 AP 冲突可能影响设备侧连接质量。
- **功耗/供电**：WiFi STA 常连会增加平均电流，注意此前出现过的 **brownout** 问题（见 MIGRATION-PLAN.md），供电不足时优先排查。

### 蓝牙路线（方案 D）专有

- **分区溢出（最先要解决）**：蓝牙栈会显著增大 app，本项目 `factory` 目前仅 1M 而二进制已 ~0.88MB——**不先调分区表就编译会失败**（见 §5.4）。
- **蓝牙与 WiFi 共存**：两者共享射频，需 `esp_coex` 调度。本项目 Web 负载很轻、影响可接受，但仍建议实测确认不丢包、不频繁断连。
- **点对点限制**：蓝牙同一时刻只有一台主机能驱动设备（不像 WiFi 方案可多机访问）。若需要"手机也能控制"，得保留 WiFi 路线。
- **配对与地址**：BLE 地址在配对后固定；换机、重置固件或更换主机蓝牙适配器后需重新配对。免配对方案需在固件侧固定地址并放宽安全策略，会降低安全性。
- **去抖更关键**：BLE 写入比 HTTP 更廉价，但连接间隔（connection interval）通常在 30–50ms 量级，高频写入仍会排队——§7 的 300ms 去抖同样适用。

---

## 8. 附：与现有 clawd 桌面端的关系

两套可以共存、互不干扰：桌面端继续走 SSH 远程渲染，mochi 走局域网 HTTP。若希望**统一入口**，可以让桌面端在收到 hook 后额外转发一份给设备，这样只需安装一套 hook——但会增加桌面端的耦合，非必要不建议。
