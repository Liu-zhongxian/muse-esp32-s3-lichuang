# 使用与修改 UI

[返回首页](../../README.md)

## 使用已经适配好的界面

按[快速开始](getting-started.md)编译实战派预设即可启用全部现有适配。

- **切换语言**：头像页左滑进入设置，点第一行「中文 / English」。切换即时生效，并保存在设备设置中。
- **首次语言**：未保存过语言选择时默认中文；重刷后已有选择会保留。
- **设置页**：上下滚动列表，点击进入，左上角返回。
- **Wi-Fi 输入页**：顶部保留标题和输入框，底部四行键盘。密码右侧「显示 / 隐藏」切换可见性。
- **长文本**：部分列表采用省略显示，进入编辑页查看完整值。

切换语言只翻译界面文案，不翻译网络名、密码和聊天内容。键盘输入为字母、数字和符号，目前没有拼音输入法。

## 继续改界面时改哪些文件

以下文件已经随本适配修改，无需仅为安装而再次编辑：

| 想改的内容 | 入口 |
|---|---|
| 中英文文案 | [`muse_i18n.c`](../../esp32/components/muse/muse_i18n.c) |
| 默认语言选项 | [`Kconfig`](../../esp32/components/muse/Kconfig) 中的 `MUSE_DEFAULT_CHINESE` |
| 保存语言选择 | [`muse_settings.c`](../../esp32/components/muse/muse_settings.c) |
| 设置、Wi-Fi、密码输入与键盘布局 | [`muse_settings_ui.c`](../../esp32/components/muse/muse_settings_ui.c) |
| 主界面、状态文字和字体回退 | [`muse_ui.c`](../../esp32/components/muse/muse_ui.c) |
| 菜单文案 | [`muse_menu.c`](../../esp32/components/muse/muse_menu.c) |
| 实战派屏幕、触摸与刷新缓冲区 | [`board_lichuang_szp.c`](../../esp32/components/muse/boards/board_lichuang_szp.c) |

翻译中的 `%s`、`%d` 等格式占位符需要与英文保持一致；新文案要通过 `muse_tr()` 查表。修改后从 `esp32` 目录运行：

```sh
python3 -m unittest discover -s tests -p 'test_muse_i18n.py'
```

该测试编译实际 C 翻译模块，检查双向切换、未翻译内容透传与占位符一致性。完整测试命令和环境见[验证说明](validation.md)。

布局代码与上游其他板型共用，修改时保留对屏幕尺寸的判断，不要把所有设备强制改成 320×240。新增板型时遵循[板级适配说明](../../esp32/devices/AGENTS.md)，先确定真实硬件参数。

UI 验收需要检查两种语言、长文本、输入框、密码显示/隐藏和重启后语言保持；同时在实体屏幕上点按验证。内部截图正确不能单独证明屏幕通信或触摸映射正确。
