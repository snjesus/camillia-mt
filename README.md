# Camillia for Meshtastic

Meshtastic-compatible mesh radio firmware for ESP32-S3 handheld LoRa devices.

**Website:** <https://camillia.sumat.org/>

## Table of Contents

- [Hardware](#hardware)
- [Supported Devices](#supported-devices)
- [Features](#features)
- [First-Time Setup](#first-time-setup)
- [Configuration](#configuration)
- [Releases](#releases)
- [Use of AI](#use-of-ai)
- [License](#license)
- [Usage and Controls Guide (docs/USE.md)](docs/USE.md)
- [Build and Flash Guide (docs/BUILD.md)](docs/BUILD.md)
- [Maps (docs/MAPS.md)](docs/MAPS.md)
- [Hardware Targets (docs/HARDWARE.md)](docs/HARDWARE.md)
- [Bluetooth Keyboards (docs/BLUETOOTH_KEYBOARDS.md)](docs/BLUETOOTH_KEYBOARDS.md)

## Hardware

- [LilyGo T-Deck](https://lilygo.cc/products/t-deck) — ESP32-S3, SX1262 LoRa, 320x240 display, physical keyboard, trackball, L76K GPS
- [LilyGo T-Lora Pager TFT](https://lilygo.cc/products/t-lora-pager) — ESP32-S3, SX1262 LoRa, 480x222 TFT, physical keyboard, roller wheel + click, GPS
- [M5Stack Cardputer](https://shop.m5stack.com/products/m5stack-cardputer-kit-w-m5stamps3) + Cap LoRa/GPS module
- [Heltec](https://heltec.org/) WiFi LoRa 32 V4 + TFT expansion kit (Heltec V4 expansion profile)
- [Attaky Mesh Deck](https://shop.attaky.com/) — ESP32-S3, SX1262 LoRa, 320x240 touch display, 48-key QWERTY, D-pad, GPS
- [Elecrow ThinkNode M9](https://www.elecrow.com/thinknode-m9-meshcore-communication-terminal-with-full-keyboard-2-4inch-lcd-esp32-s3-lr1110-gps-2300mah.html) — ESP32-S3, **LR1110** LoRa, 2.4" 320x240 display, 37-key QWERTY + d-pad, GPS, 2300 mAh
- Square (unreleased codename) — ESP32-S3, SX1262 LoRa, 320x240 touch UI, GNSS, 16 MB flash and 8 MB PSRAM

No additional hardware required.

## Supported Devices

- LilyGo T-Deck (`tdeck`): keyboard + trackball + touch input, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- LilyGo T-Lora Pager TFT (`tlora-pager-tft`): keyboard + roller wheel input, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- M5Stack Cardputer + Cap LoRa/GPS (`cardputer-cap`): keyboard-driven input/navigation, microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config).
- Heltec WiFi LoRa 32 V4 + TFT expansion kit (`heltec-v4`): touch-first UI, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config); microSD is not enabled in this profile.
- Heltec WiFi LoRa 32 V4 + TFT expansion kit, vertical UI (`heltec-v4-vertical`): same functionality as `heltec-v4` with a vertical-oriented UI layout.
- Attaky Mesh Deck (`mesh-deck`): keyboard + D-pad + touch input, GPS, and full mesh UI support; no microSD — config, DM history and the node archive live in internal flash.
- Elecrow ThinkNode M9 (`m9`): keyboard + d-pad input (no touch), microSD config import/export, GPS, and full mesh UI support (channels, ANN, DMs, MAP, CFG, web config). The only LR1110 board in the lineup.
- Square (`square`): bring-up target with a touch-first 320x240 UI, optional external BLE keyboard, ES8311 sound notifications, GNSS, browser VNC Host/Remote control, and 1-bit SD_MMC storage, including firmware config import/export at `/camillia/config.yaml`. LP5814 brightness, ADS1115 battery, audio, SD, BLE, and Remote support still need hardware verification. It is not published in releases yet.

Notes:
- All keyboard-specific shortcuts apply to keyboard builds (`tdeck`, `tlora-pager-tft`, `cardputer-cap`, `mesh-deck`, and `m9`).
- Environmental telemetry via BME280/BMP280/AHT20 is available on Heltec V4 expansion builds when a compatible sensor is present.

## Features

- **8 configurable LoRa channels** — each independently named, keyed, and color-coded
- **ANN tab** — read-only announcement feed (join/leave events, channel activity)
- **Web configuration** — browser-based settings UI, on by default, served over the device's own Wi-Fi AP or your network
- **YAML config** — import/export all settings and channel keys via microSD at `/camillia/config.yaml`

## First-Time Setup

On first boot, connect to the `camillia-mt` Wi-Fi access point, then open `http://192.168.4.1` in a browser. Set your node name, region, and channel keys. All settings are saved to the device and persist across reboots.

## Configuration

### Web config

Web config is enabled by default on a new device, so a freshly flashed board comes up as the `camillia-mt` access point. Connect to it and navigate to `http://192.168.4.1`. All settings (node identity, LoRa parameters, channel keys, etc.) can be configured here without reflashing. Changes persist across reboots.

Once the device joins your own Wi-Fi network it serves the full page — the same settings plus Utilities, a live feed, chat, and the node map — at the address shown on the Config screen. In access-point mode it serves **Web Config Lite**: the complete settings form without those extra tabs, which do not fit in the memory left once Wi-Fi is running. The Cardputer always serves Lite, and pauses chat while web config is on; see [USE.md](docs/USE.md#web-config).

The Config screen's **Choose WiFi** list includes an **AP** entry that forces the device's own access point even when a network is configured, so web config stays reachable out of range. That choice persists across reboots.

### SD card

Export or import a full YAML configuration file via the **CFG** tab. The file is read from and written to `/camillia/config.yaml` on the microSD card.

### 中文字库（CJK fallback）

中文渲染链：**Montserrat → 思源黑体（CJK）**。节点名、频道名、聊天消息、私信预览与正文中的中文都与英文一样内联显示，无需逐处处理。

字库由 `tools/gen_cjk_font.py` 从思源黑体 SC（Source Han Sans SC，SIL OFL 1.1）子集化生成，字集按[现代汉语字频表（Jun Da）](https://lingua.mtsu.edu/chinese-computing/statistics/char/list.php)排序，外加 CJK 标点与全角符号。分两档：

| 档位 | 内容 | 体积 | 适用机型 |
|------|------|------|----------|
| 全量 `src/fonts/cjk_font.h` | 9,903 字 + 标点（约 99.9% 覆盖） | ~2.4 MB | 16MB 机型（含 T-Lora Pager），OTA 槽已扩至 ~5 MB |
| 小档 `src/fonts/cjk_font_small.h` | 7,000 字 + 标点（约 99.6% 覆盖） | ~1.6 MB | Cardputer（8MB 闪存），OTA 槽扩至 3.75 MB |

emoji 表情已完全移除（字体、面板、快捷键一并删除），腾出的空间全部留给中文字库。表情回应（tapback）的收发保持兼容：设备上以 `[+]` `[-]` `[!!]` `[?]` `[lol]` `:(` 文本样式显示，网页端与其他客户端之间仍收发原图标。

**升级须知：从 v4.7.x 或更早版本升级，必须用 USB 全量刷写出厂镜像**（例如 `dist/camillia-mt-cardputer-cap-v4.8.0.bin`）。本版本扩大了 OTA 应用槽（3.125MB → 16MB 机型约 5MB / Cardputer 3.75MB），分区表变更无法通过 OTA 迁移；全量刷写一次后，设备间 OTA 恢复正常。T-Lora Pager 的内部 LittleFS 备用存储随分区表缩小，首次挂载时会自动重建。

重新生成字库：

```bash
python3 tools/gen_cjk_font.py path/to/SourceHanSansSC-Regular.otf [N] [out.h]
```

默认输出全量 `src/fonts/cjk_font.h`；`N` 截取前 N 个高频字，第三个参数指定输出文件（如 `src/fonts/cjk_font_small.h`）。工具 docstring 里有各档位与分区表的详细说明。

### Pinyin IME (compose screen)

On keyboard builds the compose screen has a pinyin input method for typing Chinese into messages. The bar between the message box and the key legend holds a **中/EN** toggle button plus the live composition and candidate cells.

- **中/EN toggle** — click the button, or press **Fn+Space** on the Cardputer / **Sym+Space** on the T-Lora Pager. The mode persists across compose sessions; arrow left/right (or `,`/`.` while composing) flip candidate pages.
- **Type** — letters build the pinyin composition (max 6); the bar shows `pinyin: 1.你 2.呢 ...` five candidates at a time (three per page on the Cardputer's narrow panel).
- **Commit** — digits **1–5** pick the matching candidate, **Space** commits the first one, **Enter** commits the raw letters as typed.
- **Undo** — **Backspace** removes one composition letter (once empty it deletes message text again), **Esc** clears the composition.

The engine ([src/pinyin_ime.cpp](src/pinyin_ime.cpp)) binary-searches a flash-resident candidate table generated by `tools/gen_pinyin_ime.py` from the [rime/rime-pinyin-simp](https://github.com/rime/rime-pinyin-simp) dictionary (Apache-2.0) — each pinyin keeps its top-12 weighted hanzi, and candidates are filtered to the codepoints the CJK fallback face actually renders so none can rasterize as a missing glyph (~15 KB of table, 364 syllables, 1,418 characters). The engine itself is ported from [Meshtastic-CardputerADV-CN](https://github.com/hansgao0422/Meshtastic-CardputerADV-CN) (MIT, © hansgao0422), reworked onto fixed buffers. Regenerate the table with:

```bash
python3 tools/gen_pinyin_ime.py path/to/pinyin_simp.dict.yaml
```

## Releases

Releases are cut with [`release.sh`](release.sh), which builds every device
profile, merges factory images, signs the OTA images, tags the commit, and
publishes a GitHub release with the `.bin`/`.sig` assets. Debug symbols
(`.elf`/`.map`) stay in `dist/` on the release machine rather than bloating the
release by ~35MB per profile.

Pushing the tag also triggers [the build workflow](.github/workflows/build.yml),
which compiles every environment from a clean checkout. It is a breakage check,
not a release gate — `release.sh` publishes before CI finishes — and it never
uploads release assets, since only the release machine holds the OTA signing
key.

```bash
./release.sh        # prompts for a version, e.g. 3.2.0
```

Publishes a release. On-device OTA and the website flasher both track GitHub's
*latest* release.


## Use of AI

Hello!  I've been a developer professionally since about 2001 working on a large list of technologies.  I've created this project in my spare time so I could contribute to my favorite new hobby (mesh networking) and try out coding with an AI partner (Claude).  Lots of this code has been touched by AI but as I go through the process I'm reviewing the code.  AI is tool, and like any other tool can be used well or used poorly.

This project is a bit more than a proof of concept but not something that has any commercial value.  I'm doing this for fun and to learn.  Feel free to contribute, use or ignore.

## License

Camillia for Meshtastic is released under the **GNU General Public License v3.0 or later**
(`SPDX-License-Identifier: GPL-3.0-or-later`).

Copyright © 2025–2026 Michael A. Cojocari and contributors.

This firmware is Meshtastic-compatible and links against, or interoperates
with, portions of the Meshtastic project (also GPLv3), so it is distributed
under the same license. There is **no warranty**; see the full license text
for details. The full license text accompanies this source distribution in
[docs/LICENSE.md](docs/LICENSE.md); you can also obtain a copy at
<https://www.gnu.org/licenses/gpl-3.0.html>.

Third-party libraries used by this project (e.g. LVGL, RadioLib, Arduino-ESP32,
TFT_eSPI / LovyanGFX, nanopb) remain under their respective licenses; see each
library's source for terms.
