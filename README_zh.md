# Alfredo ESP32

语言：**简体中文** | [English](README.md)

Alfredo ESP32 是一个面向 `ESP32` 语音机器人的独立固件仓库，当前主目标板是 `M5Stack CoreS3`。

这个仓库已经包含完整源码树、板级配置、构建脚本和 Alfred 表情资源，不再是只分发补丁的中转仓。

## 主要特性

- CoreS3 固件：采集、唤醒词、状态机、音频编解码、显示控制
- Gateway 语音链路：`STT -> LLM -> TTS`（WebSocket）
- SD 卡运行时配置（`/sdcard/alfredo.cfg`），支持免重编译调参
- Alfred 表情体系：说话嘴型、睡眠态、主题和 assets 自定义

## 编译环境

- `ESP-IDF 5.5.2`（推荐）
- `Python 3`
- `Pillow`
- `rsvg-convert`

## 快速开始

### 1. 初始化环境

```bash
cd <alfredo-esp32-repo>
source scripts/export_idf_env.sh
idf.py set-target esp32s3
idf.py reconfigure
python3 scripts/generate_alfred_emoji.py
```

`idf.py reconfigure` 会先解析依赖组件，再进入构建。

### 2. 编译 CoreS3 固件

```bash
python3 scripts/release.py m5stack-core-s3
```

如果你更习惯手动编译，可以参考 [main/boards/m5stack-core-s3/README.md](main/boards/m5stack-core-s3/README.md)。

### 3. 烧录

```bash
source scripts/export_idf_env.sh
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

## Gateway 对接（当前主路径）

- WebSocket 默认使用：`ws://<gateway_ip>:18910/ws`
- 设备端运行时配置放在：`/sdcard/alfredo.cfg`
- 可通过 Gateway 页面下发配置（Device SD Config）

详细进度和排障见：

- [docs/alfredo-core-s3-gateway-progress.md](docs/alfredo-core-s3-gateway-progress.md)
- [main/boards/m5stack-core-s3/README.md](main/boards/m5stack-core-s3/README.md)
- [docs/build-assets-and-flash-manual.md](docs/build-assets-and-flash-manual.md)（assets 编译、固件编译烧录、当前配置说明）

## 表情与 Assets 流程

重新生成 Alfred 表情资源：

```bash
python3 scripts/generate_alfred_emoji.py
```

重新打包默认 assets（含字体、emoji、srmodels）：

```bash
python3 scripts/build_default_assets.py \
  --sdkconfig sdkconfig \
  --builtin_text_font font_puhui_basic_20_4 \
  --emoji_collection twemoji_128 \
  --output build/generated_assets.bin \
  --light_text_color '#ffffff' \
  --light_background_color '#1a1414' \
  --dark_text_color '#ffffff' \
  --dark_background_color '#1a1414'
```

仅刷 assets 分区（不重刷 app）：

```bash
python -m esptool \
  --chip esp32s3 -p /dev/cu.usbmodem1101 -b 460800 \
  write_flash 0x800000 build/generated_assets.bin
```

## 仓库结构

- `main/`: 固件主源码
- `main/boards/m5stack-core-s3/`: CoreS3 板级配置和说明
- `scripts/generate_alfred_emoji.py`: Alfred 表情生成脚本
- `scripts/alfred_svg/`: Alfred SVG 表情源文件
- `components/alfredo-fonts/`: 本地字体与 emoji 组件（已去历史命名）
- `docs/`: 其它开发和构建文档
- `sdkconfig.defaults*`: 默认构建配置

## 说明

- 组件依赖已切到本地：`alfredo-fonts -> ../components/alfredo-fonts`
- 当前 CoreS3 表情缩放系数是 `90`
- `build/`、`managed_components/`、本地 `sdkconfig` 等再生内容默认不入库
