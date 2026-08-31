# Build and Flash

## Requirements

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) (CLI or IDE extension)
- USB cable connected to your target device

Dependencies are fetched automatically by PlatformIO on first build.


## Flashing a release binary

Download your device image and `flash.sh` from the [Releases](https://github.com/oumike/camillia-mt/releases) page.

Supported release file names:

- `camillia-mt-tdeck-vX.Y.Z.bin`
- `camillia-mt-tlora-pager-tft-vX.Y.Z.bin`
- `camillia-mt-cardputer-cap-vX.Y.Z.bin`
- `camillia-mt-heltec-vX.Y.Z.bin`
- `camillia-mt-heltec-vertical-vX.Y.Z.bin`
- `camillia-mt-mesh-deck-vX.Y.Z.bin`
- `camillia-mt-m9-vX.Y.Z.bin`
- `camillia-mt-square-vX.Y.Z.bin`

Then run:

```bash
./flash.sh camillia-mt-vX.Y.Z.bin [port]
```

Port defaults to `/dev/ttyUSB0`. On macOS use `/dev/cu.usbmodem*`.

`flash.sh` requires `esptool.py`:

```bash
pip install esptool
```

## Development
### Build and flash with PlatformIO

Build only (no flash):

```bash
pio run -e tdeck
pio run -e tlora-pager-tft
pio run -e cardputer-cap
pio run -e heltec-v4
pio run -e heltec-v4-vertical
pio run -e mesh-deck
pio run -e m9
pio run -e square
```

Open serial monitor without rebuilding:

```bash
pio device monitor
```

### Build and flash with helper script

```bash
Usage: ./build-upload-monitor.sh [--tdeck|-t] [--debug|-d] [--cardputer|-C] [--pager|-P] [--heltec|-H] [--heltec-vertical|--vertical|-V] [--mesh-deck|--attaky|-M] [--m9|-9] [--square] [--erase|-E]
  --tdeck, -t  Use T-Deck environment (tdeck)
  --debug, -d   Use debug PlatformIO environment (tdeck-debug)
  --cardputer, -C  Use Cardputer + Cap LoRa/GPS environment (cardputer-cap)
  --pager, -P   Use T-Lora Pager TFT environment (tlora-pager-tft)
  --heltec, -H  Use Heltec V4 expansion environment (heltec-v4)
  --heltec-vertical, --vertical, -V  Use vertical Heltec env (heltec-v4-vertical)
  --mesh-deck, --attaky, -M  Use Attaky Mesh Deck environment (mesh-deck)
  --m9, -9      Use Elecrow ThinkNode M9 environment (m9)
  --square      Use Square environment (square)
                If neither is provided, you'll be prompted to choose a device.
  --erase, -E   Erase flash before clean build/upload
                M9 uses the combined upload_erase target.
```

Example usage:

```bash
./build-upload-monitor.sh --tdeck
./build-upload-monitor.sh --cardputer
./build-upload-monitor.sh --pager
./build-upload-monitor.sh --heltec
./build-upload-monitor.sh --vertical
./build-upload-monitor.sh --m9
./build-upload-monitor.sh --square
```

You can also run the script with no flags and pick a device from the prompt.

## Environment

| Setting | Value |
|---|---|
| Platform | espressif32 7.0.1 |
| Framework | Arduino |
| Flash | 16 MB, dual-slot OTA partitions — ~5 MB app slots since v4.8.0 (3.75 MB on the 8 MB Cardputer). Mesh Deck, Heltec V4 Vertical and Square use `partitions_16mb_fs.csv`, which adds a trailing LittleFS partition after the app slots; Square also stores files on SD_MMC |
| PSRAM | enabled (OPI; none on Cardputer) |
| Upload speed | 115200 |

## Notes

- The board must be in download mode to flash. On the T-Deck, hold the trackball button while pressing reset, or let PlatformIO trigger it automatically via USB CDC.
- Square uses native USB-CDC and may require its DFU/download-mode gesture before upload.
- `-DARDUINO_USB_CDC_ON_BOOT=1` routes `Serial` over USB, no UART adapter needed.
- After flashing, the device boots directly into the firmware.

### Patched libraries

Two `pre:` scripts rewrite third-party sources in `.pio/libdeps` before a build.
PlatformIO gives every environment its own copy, so a patch only reaches the env
that lists the script.

- `tools/patch_lgfx_dmadesc.py` — every display env. LovyanGFX and M5GFX (which
  vendors the same file) free the SPI DMA descriptor array before allocating its
  replacement, record the new size whether or not the allocation succeeded, and
  then dereference the result without a null check. Under memory pressure that
  turns a 12-24 byte allocation failure into `StoreProhibited` at `EXCVADDR
  0x00000004` inside `Bus_SPI::_setup_dma_desc_links`, permanently — the stale
  size stops it ever retrying. The patch allocates before freeing and bails out
  when there is nothing usable, so a failure drops a frame instead. See #49 for
  the memory budget that causes the failure in the first place.
- `tools/patch_radiolib_lr11x0.py` — `m9` only; see the ThinkNode M9 notes below.

Both are idempotent. The LovyanGFX patch fails the build if an existing
`Bus_SPI.cpp` no longer matches or is only partially patched; the RadioLib
patch still emits a warning on version drift. If you see `NOT patched - run the
build once more` on a fresh checkout, the library had not been fetched yet;
build again.

### Heltec (heltec-v4, heltec-v4-vertical)

- These envs moved from `partitions.csv` to `partitions_16mb_fs.csv` to give the
  board a filesystem, since it has no SD slot. The app slots and NVS are at the
  same offsets in both tables, so an OTA between them is safe — but **OTA does
  not rewrite the partition table**, which lives outside both app slots. A device
  updated over the air keeps its old table, finds no `littlefs` partition, logs
  `[fs] internal flash mount FAILED (partition 'littlefs' missing?)` and carries
  on with chat history in RAM only.
- To actually get the partition, flash over USB once:
  `pio run -e heltec-v4 -t upload`. Nothing needs erasing first — the new table
  keeps NVS where it was, so settings, channels and the node identity survive.
- The env sets `board_upload.flash_size` and `board_upload.maximum_size`
  alongside `board_build.flash_size`, and all three are load-bearing. The
  `esp32-s3-devkitc-1` board definition declares an 8 MB part; that is the size
  the bootloader is built against, and it rejects a partition table reaching
  past it *before any app code runs*. The symptom is a boot loop showing nothing
  but the ROM banner and `entry 0x403c98d0`, over and over — no bootloader log,
  no panic, no app output. The same note is on the mesh-deck env, which hit it
  first.

### v4.8.0 分区扩大与升级说明

- 为容纳全量中文字库（思源黑体 9,903 字，约 2.4MB），v4.8.0 扩大了 OTA 应用槽：
  16MB 机型约 5MB（`partitions.csv` / `partitions_16mb_fs.csv` /
  `partitions_pager_fs.csv`，槽位 0x4E0000），Cardputer（8MB 闪存）扩到 3.75MB
  （`partitions_cardputer.csv`，槽位 0x3C0000，配 7,000 字字库
  `cjk_font_small.h`）。
- **从 v4.7.x 或更早版本升级必须用 USB 全量刷写出厂镜像**（`dist/` 下不带
  `-ota` 后缀的 `.bin`）。OTA 只写应用槽，不会重写 0x8000 处的分区表，旧表上
  无法容纳新固件。
- **NVS 偏移随分区表移动了**（旧表 0x650000 / pager 旧表 0x710000 → 新表
  0x9D0000 / Cardputer 0x790000），全量刷写后节点名、频道密钥、WiFi 配置等
  会全部重置。刷机前先在 CFG 页把配置导出到 SD 卡 `/camillia/config.yaml`，
  刷完后重新连上 AP 再导入即可；没有 SD 卡槽的机型只能手工重配。
- T-Lora Pager 的内部 LittleFS 备用存储随分区表缩小（8.8MB → 6.1MB），首次
  挂载会自动重建，原有离线数据不保留。
- 全量刷写一次后，设备间 OTA 恢复正常，后续版本不再需要 USB。

### ThinkNode M9

- **The console is an external UART bridge, not native USB-CDC.** The `m9` env
  builds with `ARDUINO_USB_CDC_ON_BOOT=0` for that reason; a build with CDC on
  boot sends its logs to a `ttyACM` port this board does not expose and looks
  completely silent.
- Erase and flash in one esptool session with `pio run -e m9 -t upload_erase`
  (or `./build-upload-monitor.sh --m9 --erase`). The separate `erase` target
  needs a second port grab this board does not always give up cleanly.
- First build on a fresh checkout may print
  `[patch_radiolib_lr11x0] NOT patched - run the build once more`. That is the
  RadioLib old-firmware patch running before PlatformIO has fetched RadioLib —
  run the build again and it lands. Skipping it shows up as `[radio] init
  failed: -706` on preproduction units.
