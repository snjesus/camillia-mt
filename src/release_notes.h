// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(v4.8.7
- 修复 LVGL 内存分配断言导致设备挂死的问题：LVGL 默认的 `LV_USE_ASSERT_MALLOC=1` 配合 `while(1)` 断言处理器，在内存池短暂不足时（如红黑树缓存节点分配失败）会在 NULL 检查之前直接挂死设备。关闭该断言后，同样的瞬时不足会安静返回 NULL，调用链正常降级（缓存跳过本次条目，下次重新光栅化），不再死机。该问题此前被 tiny_ttf 假错误刷屏掩盖，v4.8.6 修复刷屏后暴露。
- 无功能/字库变化，与 v4.8.6 相同的字体和内存配置。

v4.8.6
- 修复 LVGL tiny_ttf 对"字体中没有的字符"误报 `cache not allocated` 错误的问题：该字不属于此字体时本应安静回退，但 LVGL 9.5.0 会把它当成"缓存未分配"打出一条 ERROR。回退链中每个汉字都会先在 emoji 字库落空一次，因此屏幕唤醒重绘时串口被刷屏（一次约 1,650 行），UI 刷新也被同步日志拖慢到约 1 秒；中文显示本身不受影响。
- 通过构建前补丁（tools/patch_lvgl_tiny_ttf.py）修正：缺字时直接查询字体并安静返回 false；字在而缓存分配失败时退化为无缓存直查路径。渲染结果与此前完全一致，仅消除假错误与日志开销。该问题自 v4.8.3 引入 emoji 中间层起即存在。
- 无功能变化，字库与内存配置与 v4.8.5 相同。

v4.8.5
- Pager emoji 字库升级为 Noto Emoji 全量可渲染集合（1,489 个码位，约 776KB），与 Cardputer 同级：表情、手势、符号、天气、交通、食物、动植物、箭头等均可渲染。
- 为容纳全量 emoji，Pager（及其他 16MB 机型）的中文字库从 9,903 字精简到 7,000 字（覆盖约 99.3% 日常用字），精简出的空间远大于 emoji 增量，固件占用约 93%（余量约 345KB）。
- Cardputer 维持 v4.8.4 配置不变：全量 emoji + 5,000 字中文（99.1%）。
- ZWJ 组合表情与国旗符号仍不显示（字库去掉了 GSUB 表）；未覆盖字符显示为缺字方框。

v4.8.4
- Emoji 字库补全到 Noto Emoji 全量可渲染集合（1,489 个码位，约 776KB，覆盖表情/符号/天气/交通/食物/动植物/旗帜字符等），Cardputer 闪存在补齐后占用约 99.1%（余量约 34KB）。之前仅 400 常用表情时未命中的表情现在大多可正常渲染；ZWJ 组合表情与国旗符号仍不显示（字库去掉了 GSUB 表）。
- Cardputer 中文字库维持 5,000 字不变；16MB 机型保持全量 9,903 字 + 全量 emoji。

v4.8.3
- emoji 回归：重新内置 400 个常用表情字体（约 207KB，为 v4.8.0 全量字库的约 1/4 大小）。输入框 😀 按钮、快速表情 'E' 键恢复；聊天、私信、节点名、频道名中的 emoji 与中文均可内联混排显示。
- 表情回应（tapback）在设备上重新显示为真实 emoji 图标；字库未覆盖的表情仍回退为 [+]/[-]/[!!] 等文本样式，收发与其他客户端互通不受影响。
- 中文字库按需精简：Cardputer 从 7,000 字精简到 5,000 字（覆盖约 99% 日常用字），16MB 机型维持全量 9,903 字不变。精简后 Cardputer 固件占用从 98.9% 降至 84.7%，OTA 余量恢复健康水平。
- 未覆盖的生僻字显示为缺字方框；字库可随时用 tools/gen_cjk_font.py 重新生成调整。

v4.8.1
- 输入法更换为五笔86：编写界面直接用五笔编码输入汉字，内置码表覆盖 7,000 个高频汉字（10,700+ 编码，约 111KB）。一级/二级简码内置：一级简码首字排第一，两键即可取二级简码字；`,`/`.` 翻页、数字 1-3/1-5 选字、空格上屏首选，交互与原拼音输入法一致。Z 不是编码键，按下会直接输入字母 z。码表来自 rime/rime-wubi（wubi86.dict.yaml，LGPL-3.0）。

v4.8.0
- 中文字库大幅扩容：16MB 机型（含 T-Lora Pager）内置全量 9,903 字简体中文字库（思源黑体，覆盖约 99.9% 常用汉字）；Cardputer 因 8MB 闪存上限内置 7,000 字（覆盖约 99.6%）。节点名、频道名、聊天消息、私信均可内联显示中

[truncated])CAMNOTES";
