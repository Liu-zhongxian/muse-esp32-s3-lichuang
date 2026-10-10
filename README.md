<!--
Copyright (c) Meta Platforms, Inc. and affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Muse · 立创实战派 ESP32-S3 适配

针对**立创实战派 ESP32-S3 N16R8** 的 Muse 适配版本，重点解决 **2 英寸、320×240 横屏**上的 UI 布局、中英文显示，以及屏幕和配网问题。

适用配置：**16 MB Flash / 8 MB PSRAM，ST7789 屏幕、FT6336 触摸**。

## 这个仓库改了什么

- **横屏 UI**：调整设置列表、标题、输入框和四行键盘，修复输入框越界、布局错位。
- **中英文切换**：默认中文，在设置中即时切换中文 / English，重启保留选择；补齐中文字体显示。
- **屏幕与触摸**：修正背光控制、LCD 初始化与片选时序，以及触摸坐标映射。
- **配网内存**：缩小显示刷新缓冲区，解决本次遇到的配网任务因连续内存不足而无法启动的问题。
- **音频适配**：接入板载 ES8311 扬声器和 ES7210 双麦克风。

## 界面效果

| 中文输入页 | 英文输入页 |
|---|---|
| ![中文横屏 UI](docs/lichuang/images/input-zh.png) | ![英文横屏 UI](docs/lichuang/images/input-en.png) |

## 使用

下载主分支 **`main`**，按[配置与烧录教程](docs/lichuang/getting-started.md)选择实战派预设，填写自己的 SDK token 后编译烧录。适配代码已包含在仓库中，无需逐个复制 UI 文件。

[UI 使用与修改](docs/lichuang/ui.md) · [常见问题排查](docs/lichuang/troubleshooting.md) · [板卡引脚](esp32/devices/lichuang-szp.md) · [验证记录](docs/lichuang/validation.md)

已有实机记录确认显示、触摸、音频、配对与 Wi-Fi；合并主分支后 224 项主机测试通过。**云端在线对话尚未验证**。中文界面暂不包含拼音输入法。实机验证仅覆盖上述板型。

基于 [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk)；保留 [LICENSE](LICENSE) 与[第三方许可说明](THIRD_PARTY_NOTICES.md)。
