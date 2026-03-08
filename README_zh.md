# Alfredo ESP32

语言：**简体中文** | [English](README.md)

Alfredo ESP32 是一个面向 `ESP32` 语音机器人的独立固件仓库，当前以 `M5Stack CoreS3` 为主。

这个仓库已经包含完整源码树、板级配置、构建脚本和 Alfred 表情资源，不再是只分发补丁的中转仓。

## 主要特性

- Alfred 风格表情映射
- 说话时嘴型动画
- 睡眠状态 `zZz` 浮层
- `LVGL` mmap 图像缩放修复
- CoreS3 触摸调音量优化
- CoreS3 默认表情集切到 `twemoji_128`

## 编译环境

- `ESP-IDF 5.4+`
- `Python 3`
- `Pillow`
- `rsvg-convert`

## 快速开始

### 1. 首次准备

```bash
idf.py set-target esp32s3
idf.py reconfigure
python3 scripts/generate_alfred_emoji.py
```

`idf.py reconfigure` 会先准备依赖组件，然后再生成 Alfred 表情资源。

### 2. 编译 CoreS3 固件

```bash
python3 scripts/release.py m5stack-core-s3
```

如果你更习惯手动编译，可以参考 [main/boards/m5stack-core-s3/README.md](main/boards/m5stack-core-s3/README.md)。

### 3. 烧录

```bash
idf.py flash monitor
```

## 常用命令

重新生成 Alfred 表情资源：

```bash
python3 scripts/generate_alfred_emoji.py
```

查看 CoreS3 板级说明：

```bash
sed -n '1,120p' main/boards/m5stack-core-s3/README.md
```

## 仓库结构

- `main/`: 固件主源码
- `main/boards/m5stack-core-s3/`: CoreS3 板级配置和说明
- `scripts/generate_alfred_emoji.py`: Alfred 表情生成脚本
- `scripts/alfred_svg/`: Alfred SVG 表情源文件
- `docs/`: 其它开发和构建文档
- `sdkconfig.defaults*`: 默认构建配置

## 说明

- 当前 CoreS3 表情缩放系数是 `90`
- 仓库保留了与现有构建链兼容的目录结构
- `build/`、`managed_components/`、本地 `sdkconfig` 等再生内容默认不入库
