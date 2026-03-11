# Alfredo ESP32 资源编译与烧录手册（M5Stack CoreS3）

本手册记录当前项目可复现的流程：
- 如何编译 `assets.bin`
- 如何编译/烧录固件
- 当前实际使用的配置（WS、PSRAM、唤醒词、语言）
- 当前表情资源来源与样式

适用目录：`/Users/dev/robot/m5stack/alfredo-esp32`

---

## 1. 当前使用配置（已验证）

### 1.1 目标板与网络
- 目标板：`m5stack-core-s3`（`esp32s3`）
- 默认 WebSocket：
  - `CONFIG_BOOT_DEFAULT_WEBSOCKET_URL="ws://10.0.0.175:18910/ws"`
  - `CONFIG_BOOT_DEFAULT_WEBSOCKET_VERSION=1`

配置来源：
- `sdkconfig.defaults.esp32s3`
- `main/boards/m5stack-core-s3/config.json`

### 1.2 PSRAM（稳定配置）
- `CONFIG_SPIRAM_MODE_QUAD=y`
- `CONFIG_SPIRAM_SPEED_40M=y`

说明：此配置用于避免 OCTAL/高速初始化不稳定导致的重启。

### 1.3 唤醒词（英文）
- `CONFIG_USE_CUSTOM_WAKE_WORD=y`
- `CONFIG_CUSTOM_WAKE_WORD="alfredo;hey alfredo;hello alfredo"`
- `CONFIG_CUSTOM_WAKE_WORD_DISPLAY="Alfredo"`
- `CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=20`
- `CONFIG_SR_MN_EN_MULTINET6_QUANT=y`
- `CONFIG_LANGUAGE_EN_US=y`

### 1.4 当前表情资源
- 表情 SVG 源：`scripts/alfred_svg/*.svg`
- 生成器：`scripts/generate_alfred_emoji.py`
- 资源组件目录：`components/alfredo-fonts`
- 资源打包使用集合：`twemoji_128`
- 当前眼睛风格：
  - 常规表情：`宽7, 高4`（7x4）
  - `sleeping`：更宽更扁的闭眼
  - `sleepy`：半闭眼

---

## 2. 一次性环境安装

> 以下命令在 macOS + zsh 下验证。

### 2.1 ESP-IDF 环境
- 安装并初始化 ESP-IDF（建议 v5.5.x）
- 项目统一入口脚本（已处理旧环境变量污染）：
  - `scripts/export_idf_env.sh`
- 默认会加载：
  - `/Users/dev/.espressif/esp-idf-v5.5.2/export.sh`
- 如本机 IDF 不在默认路径，可先设置：
  - `export ALFREDO_IDF_PATH=/your/esp-idf`

### 2.2 Python 与工具
- `rsvg-convert`（SVG 转 PNG）
- Python 虚拟环境（项目内用于表情生成）：
  - `./.venv-emoji`

---

## 3. 只编译并烧录 Assets（不改 App 固件）

适合只改表情、背景、字体资源时使用。

### 3.1 生成 Alfredo 表情资源
```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
./.venv-emoji/bin/python scripts/generate_alfred_emoji.py
```

### 3.2 打包默认 assets.bin
```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh

python3 scripts/build_default_assets.py \
  --sdkconfig sdkconfig \
  --builtin_text_font font_puhui_basic_20_4 \
  --emoji_collection twemoji_128 \
  --output build/generated_assets.bin \
  --light_text_color '#39ff14' \
  --light_background_color '#1a1414' \
  --dark_text_color '#39ff14' \
  --dark_background_color '#1a1414'
```

### 3.3 仅刷 assets 分区
```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh

python -m esptool \
  --chip esp32s3 \
  -p /dev/cu.usbmodem1101 \
  -b 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash 0x800000 build/generated_assets.bin
```

---

## 4. 编译并烧录完整固件（App + Assets）

适合修改代码/配置后发布安装版。

### 4.1 全量清理并重编（推荐）
```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh

idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

### 4.2 一键烧录（含 assets 分区）
```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh

idf.py -p /dev/cu.usbmodem1101 flash
```

### 4.3 串口监控
```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh

idf.py -p /dev/cu.usbmodem1101 monitor
```

### 4.4 无交互终端抓日志（可选）
在 CI 或无 TTY 的终端里，`idf.py monitor` 可能报错。可用 pyserial 抓启动日志：

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh

python - <<'PY'
import serial, time
port = "/dev/cu.usbmodem1101"
ser = serial.Serial(port, 115200, timeout=0.2)
start = time.time()
buf = []
while time.time() - start < 8:
    d = ser.read(4096)
    if d:
        buf.append(d)
ser.close()
print(b"".join(buf).decode("utf-8", errors="ignore"))
PY
```

---

## 5. 常见问题与排查

### 5.1 `No serial data received` / `Device not configured`
- 先重新插 USB 线
- 确认端口存在：
  - `ls /dev/cu.usbmodem* /dev/tty.usbmodem*`
- 必要时手动进下载模式（按住 BOOT，再点 RST）

### 5.2 启动重启循环（PSRAM）
- 重点检查是否误用 OCTAL/高速配置
- CoreS3 建议保持：
  - `CONFIG_SPIRAM_MODE_QUAD=y`
  - `CONFIG_SPIRAM_SPEED_40M=y`

### 5.3 唤醒后报 `Websocket URL is empty`
- 检查：
  - `sdkconfig.defaults.esp32s3`
  - `sdkconfig`
  - `main/boards/m5stack-core-s3/config.json`
- 确认 `CONFIG_BOOT_DEFAULT_WEBSOCKET_URL` 非空

### 5.4 环境报错 `idf5.5_py3.9_env ... not found`
- 原因：旧的 `IDF_PYTHON_ENV_PATH` 残留
- 处理：
  - 使用 `source scripts/export_idf_env.sh`
  - 或手动 `unset IDF_PYTHON_ENV_PATH` 后再 `source <idf>/export.sh`

---

## 6. 发布前核对清单

- 已确认 `sdkconfig.defaults.esp32s3` 中 WS/PSRAM/唤醒词配置正确
- 已确认 `scripts/alfred_svg/*.svg` 为目标表情版本
- 已重新执行：
  - `scripts/generate_alfred_emoji.py`
  - `scripts/build_default_assets.py`
- 已烧录并验证：
  - 设备可稳定启动
  - 可连网关
  - 唤醒词可触发
  - 表情显示符合预期
