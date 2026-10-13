# 主题包

**English: [README.en.md](README.en.md)** — 本文件是默认版本；两份如有出入，以这份为准。

设备上**一次只装一套**主题（2.44 MB 的存储分区放不下第二套），"换主题"就是替换。一套主题是这里的**一个包**：

```
themes/<名字>/          # 源目录：manifest.txt + 每个状态一个 .caf
themes/<名字>.zip       # 打包给设备的那一个文件（tools/mochi_theme.py bundle）
```

`.caf` 是二进制动画（由 `tools/gif2caf.py` 从 GIF 转换而来），`.zip` 才是设备收的东西 —— 网页里选包、推送，设备自己解压。包目录会被整个打进 zip 并展开到设备分区上，所以**目录里只放包本身**：预览图、说明都留在目录外（见每节末尾）。

| 包 | 状态 | 解压后 | zip | 角色 |
| --- | --- | --- | --- | --- |
| [`clawd.zip`](clawd.zip) | 12 | 2054 KB | 384 KB | 上游贴图集里的螃蟹，出厂外观 |
| [`origin.zip`](origin.zip) | 11 | 408 KB | 8 KB | 橙色方块宠，素材是用代码画出来的 |

---

## `clawd` —— 螃蟹（12 个状态）

**前六个由 Claude Code 驱动**：`idle` 空闲、`thinking` 提交了提示、`working` 工具在跑、`done` 一轮结束、`error` 出错（目前没有自动来源，只能手动播）、`sleep` 静置打盹。

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

**后六个是备用素材** —— 改 `clawd/manifest.txt` 里某个状态指向它们即可（例如 `idle=clawd-idle.caf`）：

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

素材来自上游贴图集（仓库外的 `../stickers/`）。重新打包：

```bash
python3 tools/mktheme.py ../stickers/240/clawd -o themes/clawd
python3 tools/mochi_theme.py bundle themes/clawd -o themes/clawd.zip
```

## `origin` —— 橙色方块宠（11 个状态）

产品图上那只角色：方脑袋、两侧小耳朵、正面一块黑框屏幕、下面三条腿，表情画在屏幕里。它的美术**没有上游来源**，是 `tools/origin_art.py` 逐帧画出来的像素画（想改样子就改那个脚本里的常数，然后 `--pack`）。

<table>
  <tr>
    <td align="center"><img src="../pics/origin/idle.gif" width="200"><br><code>idle</code> 待机中</td>
    <td align="center"><img src="../pics/origin/thinking.gif" width="200"><br><code>thinking</code> 思考中</td>
    <td align="center"><img src="../pics/origin/working.gif" width="200"><br><code>working</code> 工作中</td>
  </tr>
  <tr>
    <td align="center"><img src="../pics/origin/done.gif" width="200"><br><code>done</code> 完成啦</td>
    <td align="center"><img src="../pics/origin/error.gif" width="200"><br><code>error</code> 出错</td>
    <td align="center"><img src="../pics/origin/sleep.gif" width="200"><br><code>sleep</code> 打盹</td>
  </tr>
</table>

<table>
  <tr>
    <td align="center"><img src="../pics/origin/spare-happy.gif" width="130"><br><code>spare-happy</code> 开心</td>
    <td align="center"><img src="../pics/origin/spare-slack.gif" width="130"><br><code>spare-slack</code> 摸鱼中</td>
    <td align="center"><img src="../pics/origin/spare-grin.gif" width="130"><br><code>spare-grin</code> 大笑</td>
  </tr>
  <tr>
    <td align="center"><img src="../pics/origin/spare-shine.gif" width="130"><br><code>spare-shine</code> 闪亮</td>
    <td align="center"><img src="../pics/origin/spare-blank.gif" width="130"><br><code>spare-blank</code> 无语</td>
  </tr>
</table>

这套是照**奶油底色**画的，把宠物背景（网页 Pet BG，或串口 `bg#FDF0DD`）调成 `#FDF0DD` 最接近参考图。重新生成：

```bash
python3 tools/origin_art.py --pack themes/origin      # 画 + 打包，一步
python3 tools/mochi_theme.py bundle themes/origin -o themes/origin.zip
python3 tools/mochi_theme.py preview themes/origin -o /tmp/origin.png --bg '#FDF0DD'
```

## 预览图怎么来的

上面每一张都**不是素材 GIF**，而是从设备真正播放的 `.caf` 逐帧解码、按固件的合成规则渲染出来的（480×480，面板的 2 倍）—— 也就是面板上实际的效果。所以改了包之后要重新跑一次：

```bash
python3 tools/theme_gifs.py themes/clawd  pics/states
python3 tools/theme_gifs.py themes/origin pics/origin '#fdf0dd'
```

它们放在 `pics/` 而不是各自的包目录里：包目录会被整个打进 zip、展开到设备的分区上，多一个文件就是白占存储。

## 固件里内置的那份

`esp-idf/main/default_pack.zip`（168 KB，展开 892 KB）只装 `clawd` 的**六个 hook 状态** —— 出厂就有脸，不用推任何东西。备用素材没进去：12 个全带上压缩后 384 KB，而 app 分区只剩 240 KB 余量（`idf.py build` 会打印余量）。开机时如果存储分区里没有 manifest，固件就把它展开过去；网页上的 `restore built-in` 做的是同一件事。

## 装到设备上

1. 连设备热点 `Clawd-Mochi`（默认密码 `clawd1234`），打开 `http://192.168.4.1`
2. `// theme` 区 → 选一个 `.zip` 加进主题库 → push
3. 传输完成不等于结束：设备要按 littlefs 的 128 字节粒度把它写进分区，约 **78 KB/s** —— `origin`（408 KB）约 4 秒，`clawd`（2054 KB）约 25 秒，网页会数着秒告诉你

失败或被拒（比如包比分区还大）不会留下空主题：设备会把内置那套装回去。整体设计与取舍见 [../CLAUDE-CODE-BRIDGE.md](../CLAUDE-CODE-BRIDGE.md)。
