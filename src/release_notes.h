// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(v4.8.3
- emoji 回归：重新内置 400 个常用表情字体（约 207KB，为 v4.8.0 全量字库的约 1/4 大小）。输入框 😀 按钮、快速表情 'E' 键恢复；聊天、私信、节点名、频道名中的 emoji 与中文均可内联混排显示。
- 表情回应（tapback）在设备上重新显示为真实 emoji 图标；字库未覆盖的表情仍回退为 [+]/[-]/[!!] 等文本样式，收发与其他客户端互通不受影响。
- 中文字库按需精简：Cardputer 从 7,000 字精简到 5,000 字（覆盖约 99% 日常用字），16MB 机型维持全量 9,903 字不变。精简后 Cardputer 固件占用从 98.9% 降至 84.7%，OTA 余量恢复健康水平。
- 未覆盖的生僻字显示为缺字方框；字库可随时用 tools/gen_cjk_font.py 重新生成调整。

v4.8.1
- 输入法更换为五笔86：编写界面直接用五笔编码输入汉字，内置码表覆盖 7,000 个高频汉字（10,700+ 编码，约 111KB）。一级/二级简码内置：一级简码首字排第一，两键即可取二级简码字；`,`/`.` 翻页、数字 1-3/1-5 选字、空格上屏首选，交互与原拼音输入法一致。Z 不是编码键，按下会直接输入字母 z。码表来自 rime/rime-wubi（wubi86.dict.yaml，LGPL-3.0）。

v4.8.0
- 中文字库大幅扩容：16MB 机型（含 T-Lora Pager）内置全量 9,903 字简体中文字库（思源黑体，覆盖约 99.9% 常用汉字）；Cardputer 因 8MB 闪存上限内置 7,000 字（覆盖约 99.6%）。节点名、频道名、聊天消息、私信均可内联显示中文。
- 应用分区（OTA 槽位）随之扩大：16MB 机型扩到约 5MB，Cardputer 扩到 3.75MB。
- 移除 emoji 表情字体与表情面板（输入框 😀 按钮、快速表情 'E' 键一并移除），腾出的空间全部用于中文字库。
- 表情回应（tapback）收发保持兼容：设备上以 [+]/[-]/[!!]/[?]/[lol]/:( 等文本样式显示，网页端仍显示原图标，与其他客户端互通不受影响。
- 首次从 v4.7.x 或更早版本升级必须通过 USB 全量刷写出厂镜像（分区表变更，无法 OTA 迁移）；此后设备间 OTA 恢复正常。
- 修复 square 配置从未参与完整编译而漏掉的一处代码错误（分页器音频探测引用了仅分页器声明的寄存器变量）。)CAMNOTES";
