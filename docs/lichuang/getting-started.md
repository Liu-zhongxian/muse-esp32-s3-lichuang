# 从源码配置和烧录

[返回首页](../../README.md)

适用于实战派 ESP32-S3 N16R8 和 macOS / Linux 终端。这里沿用仓库已有 ESP-IDF 构建方式，加载实战派预设即可，不需要手动调整每一处 UI。

## 1. 准备环境和源码

需要 USB 数据线、同款开发板、Muse App，以及你自己的 [SDK token](https://gadgets.muse.ai/settings/sdk-tokens)。服务使用条件见 [Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms)。

克隆本仓库的 `lichuang-ui` 分支，而不是原始上游版本或本 fork 的 `main` 分支。选择英文、无空格路径可以避开本次调试中遇到的中文构建路径问题。如果示例目录已存在，换一个新目录，不要覆盖已有工作。

```sh
mkdir -p ~/projects
git clone --branch lichuang-ui https://github.com/Liu-zhongxian/muse-esp32-s3-lichuang.git ~/projects/muse-lichuang
```

也可以在仓库的 `lichuang-ui` 分支页面选择 Code → Download ZIP，再解压到英文、无空格路径。下文以 `~/projects/muse-lichuang` 为例，请对应实际位置。

ESP-IDF 固定使用 v6.0.1。系统依赖安装方式见[上游环境说明](../../esp32/README.md#1-install-esp-idf-v601)。安装到尚未占用的目录：

```sh
git clone -b v6.0.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.1
~/esp/esp-idf-v6.0.1/install.sh esp32s3
. ~/esp/esp-idf-v6.0.1/export.sh
idf.py --version
```

如果已经装好这个版本，只需激活。每次新开终端都要运行 `export.sh`。

## 2. 加载实战派预设并配置自己的 token

进入本适配仓库的 `esp32` 目录，在当前终端定义下面的短命令。它将目标芯片、构建目录和三层配置固定下来，供配置、编译与烧录共用：

```sh
cd ~/projects/muse-lichuang/esp32

szp_idf() {
  idf.py -B build-muse-lichuang-szp \
    -DIDF_TARGET=esp32s3 \
    -DSDKCONFIG=build-muse-lichuang-szp/sdkconfig \
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-lichuang-szp" \
    "$@"
}

szp_idf menuconfig
```

在 `ESP32 Device SDK > Muse Gadgets SDK token` 填入自己的 token，保存退出。不要把 token 写进仓库里的 `sdkconfig.defaults` 或板型预设。

预设已选择实战派、16 MB Flash、8 MB PSRAM 和中文字体；`MUSE_DEFAULT_CHINESE` 对实战派默认开启。不需要为使用现有 UI 再改源码。生成的个人配置位于已忽略的 `build-muse-lichuang-szp/sdkconfig`。

如果之前在此构建目录生成过其他配置，不要继续复用：将旧构建目录移到仓库外备存，再从这一步重新生成。已有配置会覆盖后来修改的预设。

## 3. 编译

```sh
szp_idf build
```

编译成功后，固件及烧录参数在 `build-muse-lichuang-szp/`。检查输出的应用分区容量报告。当前分区表为 [`partitions_muse.csv`](../../esp32/partitions_muse.csv)，应用槽为 4 MiB。

此构建目录和其中的 `.bin` 含你的配置，留在本地。不要直接上传作公共 Release。

## 4. 识别串口并备份

查看串口列表，将下面变量换成自己的实际串口；macOS 常见 `/dev/cu.usbmodem...`，Linux 常见 `/dev/ttyACM0`。

```sh
python -m serial.tools.list_ports
MUSE_PORT='/dev/cu.usbmodemYOUR_PORT'
python -m esptool --chip esp32s3 --port "$MUSE_PORT" flash-id
```

确认设备是目标开发板、Flash 为 16 MB，再保存整片备份。备份可能含原设备配置，不要公开：

```sh
MUSE_BACKUP_DIR=$(mktemp -d "$HOME/muse-backup-XXXXXX")
python -m esptool --chip esp32s3 --port "$MUSE_PORT" \
  read-flash 0 0x1000000 "$MUSE_BACKUP_DIR/original.bin"
python -c 'import pathlib,sys; p=pathlib.Path(sys.argv[1]); assert p.stat().st_size == 16777216; print("16 MiB backup saved:", p)' "$MUSE_BACKUP_DIR/original.bin"
```

此检查确认备份大小，不代替独立读取比对。原始固件恢复会同时覆盖现在的 Muse 和配对配置。

## 5. 首次安装与烧录

**仅从其他固件首次迁移、原分区布局不同的情况下**，在备份成功后执行以下初始化。它清除新 Muse 配置区对应的 48 KiB，旧固件在这段地址的数据也会丢失。已经在使用本适配固件、只更新 UI 时跳过此命令，以保留配对和 Wi-Fi。

```sh
python -m esptool --chip esp32s3 --port "$MUSE_PORT" \
  erase-region 0x11000 0xC000
```

然后烧录并查看启动输出：

```sh
szp_idf -p "$MUSE_PORT" flash monitor
```

退出监视器按 `Ctrl-]`。若无法进入下载模式，按住 BOOT、点按 RESET、松开 BOOT 后重试。不要使用其他板型的烧录地址或预设。

## 6. 验证 UI，再配对联网

1. 确认实体屏幕出现画面，触摸位置与控件一致。
2. 从头像页向左滑进入设置，点第一行「中文 / English」切换语言。
3. 重启，确认所选语言保留；检查 Wi-Fi 输入页的标题、输入框和四行键盘可见。
4. Muse App 中开启 `Settings > Devices > Developer mode`，通过 `Add Device` 添加屏幕对应的 `MuseGadget-XXXXXX`。
5. 按提示填写 2.4 GHz Wi-Fi；要求设备确认时短按 BOOT。
6. 分别确认配对完成、Wi-Fi 获取 IP、Muse 云端连通。连通后再验证按住 BOOT 录音、松开发送和收到回复。

本地历史记录验证到配对和 Wi-Fi，最后一步的在线对话仍待完成。出现问题时按[排查文档](troubleshooting.md)定位。

## 7. 以后更新 UI

使用同一构建目录重新运行 `szp_idf build`、`szp_idf -p "$MUSE_PORT" flash`；新终端中先激活 ESP-IDF，并重新执行第 2 步的目录切换和函数定义。不必重复首次 NVS 初始化。

如果升级涉及板型预设、分区或上游重大变更，先阅读对应改动说明。当前适配关闭 OTA，不能依赖官方 OTA 自动保留本地 UI 修改。
