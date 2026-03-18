# Alfredo ESP32 资源编译与烧录手册（M5Stack CoreS3）

本手册记录当前项目可复现的流程：
- 如何编译 `assets.bin`
- 如何编译/烧录固件
- 当前实际使用的配置（WS、PSRAM、唤醒词、语言）
- 当前表情资源来源与样式
- 表情动画、灯带提示与状态机

适用目录：`/Users/dev/robot/m5stack/alfredo-esp32`

---

## 1. 当前使用配置（已验证）

### 1.1 目标板与网络
- 目标板：`m5stack-core-s3`（`esp32s3`）
- 默认 WebSocket：
- `CONFIG_BOOT_DEFAULT_WEBSOCKET_URL="ws://10.0.0.175:1455/ws"`
  - `CONFIG_BOOT_DEFAULT_WEBSOCKET_VERSION=1`

配置来源：
- `sdkconfig.defaults.esp32s3`
- `main/boards/m5stack-core-s3/config.json`

### 1.2 PSRAM（稳定配置）
- `CONFIG_SPIRAM_MODE_QUAD=y`
- `CONFIG_SPIRAM_SPEED_40M=y`

说明：此配置用于避免 OCTAL/高速初始化不稳定导致的重启。

### 1.3 唤醒词（英文）
- `CONFIG_USE_AFE_WAKE_WORD=y` — 使用预训练 Wakenet 模型（AFE 流水线）
- `CONFIG_SR_WN_WN9_HIMFIVE=y` — 模型：`wn9_himfive`，唤醒词为 **"Hi M Five"**
- `CONFIG_SEND_WAKE_WORD_DATA` 关闭（仅本地唤醒，不向 gateway 发送预卷音频）
- `CONFIG_LANGUAGE_EN_US=y`

> **说明：** 与旧版 Multinet（自定义命令词）不同，Wakenet 是专用唤醒词神经网络，
> 无需手工调整音素字符串和阈值，男女声识别率均更稳定。
> 运行时由 `audio_service.cc::SetModelsList()` 自动选用 `AfeWakeWord` 实现类。

---

## 2. 表情资源体系

### 2.1 目录结构

```
scripts/
├── alfred_svg/            ← SVG 源文件（设计稿，所有表情的源头）
│   ├── calm.svg
│   ├── smile.svg
│   ├── wink.svg
│   ├── sleeping.svg       (睡眠基础帧，无 ZZZ)
│   ├── wakeup1.svg, wakeup2.svg
│   └── ... (共 18 个 SVG)
└── generate_emoji.sh      ← 一键生成脚本（自动检测 Python 环境）

components/alfredo-fonts/
├── svg/alfred/            ← 编译用 SVG（从 scripts/alfred_svg/ 复制）
├── generate_alfred_emoji.py  ← 核心生成器（SVG → PNG → C 数组 + assets）
├── src/
│   ├── emoji/             ← 生成的 C 源文件（RGB565A8 图像数组）
│   ├── font_emoji_32.c    ← 32px 字体映射表
│   └── font_emoji_64.c    ← 64px 字体映射表
├── png/
│   ├── twemoji_32/        ← 32px PNG（编译为静态 C 数组）
│   ├── twemoji_64/        ← 64px PNG（编译为静态 C 数组）
│   ├── twemoji_128/       ← 128px PNG → 打包进 generated_assets.bin
│   └── noto-emoji_*/      ← 同步生成的备用集合
└── tmp/alfred_faces/      ← 中间文件（rsvg 输出，不提交）
```

### 2.2 生成管线

```
SVG 源文件 (scripts/alfred_svg/)
    │
    ▼  rsvg-convert
PNG 文件 (32px, 64px, 128px)
    │
    ├── 32px/64px → C 数组 (components/alfredo-fonts/src/emoji/*.c)
    │               编译进固件，通过 Twemoji32/Twemoji64 类注册
    │
    └── 128px → twemoji_128/ 目录
                │
                ▼  idf.py build (assets partition)
            generated_assets.bin (0x800000)
                │
                ▼  运行时 assets.cc 加载
            替换 Twemoji32/64 的 emoji_collection
```

**关键点：** 运行时 `assets.cc` 从 `generated_assets.bin` 加载 `emoji_collection` 并 **替换** 编译的 Twemoji32/64 集合。因此 128px PNG 目录 `twemoji_128/` 必须包含所有需要显示的表情，否则表情会变空白。

### 2.3 当前表情列表

| SVG 文件 | Emotion 名称 | 用途 |
|----------|-------------|------|
| calm | neutral, cool, confident | 待机主表情（idle 动画） |
| smile | happy | 开心 |
| laugh | laughing, funny | 大笑 |
| worried | sad, crying, confused | 担忧 |
| angry | angry | 生气 |
| shy | loving, embarrassed, kissy | 害羞 |
| surprised | surprised, shocked | 惊讶 |
| wink | winking, silly | 眨眼（idle 动画用） |
| sleepy | relaxed, sleepy | 困倦 |
| talk | delicious | 说话嘴形 |
| thinking | thinking | 思考/连接中 |
| sleeping | sleeping | 睡眠（powersave） |
| listening | listening | 监听中 |
| noconnection | noconnection | 无连接 |
| grieved | grieved | 严重错误 |
| wakeup1 | wakeup1 | 唤醒动画帧 1（半睁眼） |
| wakeup2 | wakeup2 | 唤醒动画帧 2（完全睁开） |

### 2.4 SVG 设计规范

- 画布：`viewBox="0 0 128 128"`
- 颜色：`#39ff14`（霓虹绿）
- 眼睛：`<rect>` 几何风格
- 背景：透明（不含背景矩形）

---

## 3. 状态机、表情与灯带映射

### 3.1 设备状态 → 表情

代码位置：`main/application.cc` → `ResolveFaceForState()`

| 设备状态 | Emotion | 说明 |
|---------|---------|------|
| `kDeviceStateUnknown` | `neutral` | 未知状态 |
| `kDeviceStateIdle` | `neutral` | 空闲待机，触发 idle 动画 |
| `kDeviceStateStarting` | `thinking` | 启动中 |
| `kDeviceStateWifiConfiguring` | `thinking` | WiFi 配置中 |
| `kDeviceStateConnecting` | `thinking` | 连接中（含唤醒后连接） |
| `kDeviceStateUpgrading` | `thinking` | OTA 升级中 |
| `kDeviceStateActivating` | `thinking` | 激活中 |
| `kDeviceStateListening` | `listening` | 监听用户语音 |
| `kDeviceStateAudioTesting` | `listening` | 音频测试 |
| `kDeviceStateSpeaking`（唤醒回复） | `wakeup2` | 唤醒确认语音播放中，触发 wakeup 动画 |
| `kDeviceStateSpeaking`（正常回复） | `nullptr`（由 TTS 控制） | 正常对话回复 |
| `kDeviceStateFatalError` | `grieved` | 严重错误 |

### 3.2 表情动画

#### 待机动画（Idle Animation）
- **触发条件：** `SetEmotion("neutral")` 且非 PowerSave 模式
- **帧序列：** `neutral`（5 秒）→ `winking`（0.8 秒）→ 循环
- **代码位置：** `main/display/lcd_display.cc` → `StartIdleAnimation()` / `StopIdleAnimation()`
- **实现：** `esp_timer` one-shot，每帧结束后调度下一帧

#### 睡眠动画（Sleep Animation / PowerSave）
- **触发条件：** `SetPowerSaveMode(true)`（空闲 120 秒后自动进入）
- **表情：** `sleeping`（单帧，无动画）
- **代码位置：** `main/display/lcd_display.cc` → `StartSleepAnimation()` / `StopSleepAnimation()`
- **附带行为：** 屏幕亮度降至 10，云台头部低头

#### 唤醒动画（Wakeup Animation）
- **触发条件：** `SetEmotion("wakeup2")`（唤醒词检测后进入 Speaking 状态且 `wake_word_ack_in_progress_` 为 true）
- **帧序列：** `wakeup1`（600ms）→ `wakeup2`（400ms）→ 循环
- **代码位置：** `main/display/lcd_display.cc` → `StartWakeupAnimation()` / `StopWakeupAnimation()`
- **结束时机：** Speaking 结束后切换到其他状态

### 3.3 状态机相关灯带事件（M5GO S3）

硬件与边界：

- 灯带硬件是 M5GO S3 底座的 `WS2812C x10`
- 控制引脚是 `GPIO5`
- `Port B/Port C` 舵机口保持原占用，不参与灯带控制
- 主板 AXP2101 的 `CHGLED` 已关闭，复位键附近那颗小灯不再作为状态机提示

当前正式规则：

| 触发点 | 条件 | 灯带行为 |
|-------|------|---------|
| 外部电源接入 | `VBUS` 从未接入变为接入 | 10 灯橙色闪烁 10 次后停止 |
| 唤醒词检测到 | 进入 `HandleWakeWordDetectedEvent()` | 10 灯橙色闪烁 3 次后停止 |
| 外部电源断开 | `VBUS` 从接入变为断开 | 立即熄灭 |

实现说明：

- 外部电源检测用的是 `AXP2101::IsVbusGood()`，不是 `IsCharging()`
- 原因是设备可能带电池；线插着时，`VBUS=1` 与 “电池此刻正在充电” 不是同一个状态
- CoreS3 板级里 `ChargingStrip::OnStateChanged()` 被故意留空，因此 `Idle/Connecting/Listening/Speaking` 的切换不会重启灯效

代码位置：

- `main/boards/m5stack-core-s3/m5stack_core_s3.cc`
- `main/boards/common/axp2101.cc`
- `main/application.cc`

### 3.4 PowerSave 定时器

代码位置：`main/boards/m5stack-core-s3/m5stack_core_s3.cc` → `InitializePowerSaveTimer()`

```cpp
power_save_timer_ = new PowerSaveTimer(-1, 120, 600);
//                                     ^    ^    ^
//                              cpu_freq  sleep  shutdown
```

| 参数 | 当前值 | 说明 |
|------|--------|------|
| `cpu_max_freq` | `-1`（不降频） | 睡眠时不调整 CPU 频率 |
| `seconds_to_sleep` | `120`（2 分钟） | 空闲多久进入睡眠模式 |
| `seconds_to_shutdown` | `600`（10 分钟） | 空闲多久触发关机 |

外部电源规则：

- 只要 `VBUS=1`（插着电源线/外部供电），自动休眠与自动关机计时器都会被关闭
- `VBUS` 拔掉后，计时器重新从 0 开始
- 这里判断的是“线是否插着”，不是“电池当前是否在充电”

可通过 SD 卡配置文件 `/sdcard/alfredo.cfg` 运行时覆盖：
```json
{
  "power_save": {
    "enabled": true,
    "sleep_seconds": 120,
    "shutdown_seconds": 600
  }
}
```

### 3.5 状态流转示意

```
                    ┌──────────────────────────────────────┐
                    │                                      │
                    ▼                                      │
┌────────────┐  ┌──────────┐  ┌──────────────┐  ┌────────────────┐
│  Starting  │→ │   Idle   │→ │  Connecting  │→ │   Speaking     │
│ (thinking) │  │(neutral) │  │  (thinking)  │  │(wakeup1↔wakeup2)│
└────────────┘  └──────────┘  └──────────────┘  └────────────────┘
                    │                                      │
                    │ 120s                                  │
                    ▼                                      ▼
               ┌──────────┐                         ┌──────────┐
               │PowerSave │                         │Listening │
               │(sleeping)│                         │(listening)│
               └──────────┘                         └──────────┘
                    │                                      │
                    │ 600s                                  ▼
                    ▼                                ┌──────────┐
               ┌──────────┐                         │ Speaking │
               │ Shutdown │                         │ (TTS控制) │
               └──────────┘                         └──────────┘
```

补充：

- 唤醒词命中时，除了表情和本地确认音外，还会同步触发一次 `10 灯闪 3 次`
- `VBUS` 接入事件独立于上面的设备状态图，不会切换 `DeviceState`，但会触发一次 `10 灯闪 10 次`

---

## 4. 一次性环境安装

> 以下命令在 macOS + zsh 下验证。

### 4.1 ESP-IDF 环境
- 安装并初始化 ESP-IDF（v5.5.2）
- 默认路径：`~/.espressif/esp-idf-v5.5.2/`

### 4.2 资源生成工具
- `rsvg-convert`（SVG 转 PNG）：`brew install librsvg`
- Python + Pillow：`generate_emoji.sh` 会自动检测以下环境：
  1. ESP-IDF Python venv（优先）
  2. Homebrew Python
  3. 系统 Python

---

## 5. 编译表情资源

### 5.1 一键生成（推荐）

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
bash scripts/generate_emoji.sh
```

该脚本会：
1. 自动检测 Python 环境（需要 Pillow）
2. 检查 `rsvg-convert` 是否安装
3. 运行 `components/alfredo-fonts/generate_alfred_emoji.py`
4. 清理临时文件（`components/alfredo-fonts/build/` 目录，避免 CMake 冲突）
5. 删除旧的 `build/generated_assets.bin`，强制下次构建重新打包

### 5.2 手动生成（了解细节）

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
python3 components/alfredo-fonts/generate_alfred_emoji.py
```

生成物：
- `components/alfredo-fonts/src/emoji/*.c` — 每个表情的 RGB565A8 C 数组（32px 和 64px）
- `components/alfredo-fonts/src/font_emoji_32.c` — 32px 字体映射表
- `components/alfredo-fonts/src/font_emoji_64.c` — 64px 字体映射表
- `components/alfredo-fonts/png/twemoji_128/*.png` — 128px PNG（含 `_speaking` 变体）

### 5.3 修改表情设计

1. 编辑 `scripts/alfred_svg/<face>.svg`（使用 `#39ff14` 绿色，rect 几何风格）
2. 复制到编译目录：`cp scripts/alfred_svg/<face>.svg components/alfredo-fonts/svg/alfred/`
3. 运行 `bash scripts/generate_emoji.sh`

### 5.4 添加新表情

1. 创建 SVG：`scripts/alfred_svg/newface.svg`
2. 复制到：`components/alfredo-fonts/svg/alfred/newface.svg`
3. 编辑 `components/alfredo-fonts/generate_alfred_emoji.py`：
   - `FACE_TO_EMOTIONS` 添加 `"newface": ["newemotion"]`
   - `EMOTION_CODEPOINTS` 添加 `"newemotion": 0xXXXXX`（选一个未用的 Unicode codepoint）
4. 编辑 `main/display/lvgl_display/emoji_collection.cc`：
   - 添加 32px/64px 的 `extern` 声明和 `AddEmoji` 调用
5. 运行 `bash scripts/generate_emoji.sh`

---

## 6. 编译与烧录固件

### 6.1 加载 IDF 环境

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh
```

### 6.2 编译（含 assets）

```bash
idf.py build
```

### 6.3 烧录全部（App + Assets）

```bash
idf.py -p /dev/cu.usbmodem1101 flash
```

### 6.4 仅烧录 Assets 分区

```bash
python -m esptool \
  --chip esp32s3 \
  -p /dev/cu.usbmodem1101 \
  -b 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash 0x800000 build/generated_assets.bin
```

### 6.5 全量清理重编

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

### 6.6 串口监控

```bash
idf.py -p /dev/cu.usbmodem1101 monitor
```

---

## 7. 常见问题与排查

### 7.1 表情显示空白
- **原因：** `twemoji_128/` 目录缺少对应 emotion 的 PNG
- **排查：** 检查 `generate_alfred_emoji.py` 的 `FACE_TO_EMOTIONS`，确保 emotion 有映射（不是 `[]`）
- **修复：** 添加映射后重新运行 `bash scripts/generate_emoji.sh` 并删除 `build/generated_assets.bin`

### 7.2 CMake 编译冲突
- **原因：** `generate_alfred_emoji.py` 的中间文件在 `components/alfredo-fonts/build/` 下，ESP-IDF 误认为子项目 build 目录
- **修复：** 已改用 `tmp/alfred_faces/`；如残留 `build/` 目录，运行 `rm -rf components/alfredo-fonts/build/`

### 7.3 资源未更新
- **原因：** `idf.py build` 会缓存 `generated_assets.bin`，不会自动重新打包
- **修复：** 删除 `build/generated_assets.bin` 后再 `idf.py build`（`generate_emoji.sh` 已自动处理）

### 7.4 `No serial data received` / `Device not configured`
- 先重新插 USB 线
- 确认端口存在：`ls /dev/cu.usbmodem* /dev/tty.usbmodem*`
- 必要时手动进下载模式（按住 BOOT，再点 RST）

### 7.5 启动重启循环（PSRAM）
- 检查是否误用 OCTAL/高速配置
- CoreS3 建议保持：`CONFIG_SPIRAM_MODE_QUAD=y` + `CONFIG_SPIRAM_SPEED_40M=y`

### 7.6 唤醒后报 `Websocket URL is empty`
- 检查 `sdkconfig.defaults.esp32s3` 和 `main/boards/m5stack-core-s3/config.json`
- 确认 `CONFIG_BOOT_DEFAULT_WEBSOCKET_URL` 非空

---

## 8. 发布前核对清单

- [ ] `scripts/alfred_svg/*.svg` 为目标表情版本
- [ ] SVG 已复制到 `components/alfredo-fonts/svg/alfred/`
- [ ] 已运行 `bash scripts/generate_emoji.sh`
- [ ] `components/alfredo-fonts/png/twemoji_128/` 包含所有需要的 emotion PNG
- [ ] `emoji_collection.cc` 已注册所有表情的 32px/64px extern + AddEmoji
- [ ] `generate_alfred_emoji.py` 的 `FACE_TO_EMOTIONS` 和 `EMOTION_CODEPOINTS` 一致
- [ ] 已 `idf.py build && idf.py flash` 并验证设备表情显示正常
- [ ] 待机动画、睡眠表情、唤醒动画均正常
