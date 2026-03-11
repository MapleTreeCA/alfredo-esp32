# Alfredo ESP32

Language: **English** | [简体中文](README_zh.md)

Alfredo ESP32 is a standalone firmware repository for an `ESP32` voice robot, currently focused on `M5Stack CoreS3`.

This repository already contains the full source tree, board configuration, build scripts, and Alfred face assets. It is no longer a patch-only overlay repository.

## Highlights

- Alfred-style emotion mapping
- Speaking mouth animation
- Sleep-state `zZz` overlay
- `LVGL` mmap image scaling fix
- CoreS3 touch volume adjustment tuning
- CoreS3 default emoji collection switched to `twemoji_128`

## Build Requirements

- `ESP-IDF 5.4+`
- `Python 3`
- `Pillow`
- `rsvg-convert`

## Quick Start

### 1. Initial setup

```bash
idf.py set-target esp32s3
idf.py reconfigure
python3 scripts/generate_alfred_emoji.py
```

`idf.py reconfigure` prepares dependency components first, then Alfred emoji assets can be generated.

### 2. Build CoreS3 firmware

```bash
python3 scripts/release.py m5stack-core-s3
```

If you prefer a manual build flow, see [main/boards/m5stack-core-s3/README.md](main/boards/m5stack-core-s3/README.md).

### 3. Flash

```bash
idf.py flash monitor
```

## Common Commands

Regenerate Alfred emoji assets:

```bash
python3 scripts/generate_alfred_emoji.py
```

Read the CoreS3 board notes:

```bash
sed -n '1,120p' main/boards/m5stack-core-s3/README.md
```

## Repository Layout

- `main/`: firmware source code
- `main/boards/m5stack-core-s3/`: CoreS3 board config and notes
- `scripts/generate_alfred_emoji.py`: Alfred emoji generation script
- `scripts/alfred_svg/`: Alfred SVG source assets
- `components/alfredo-fonts/`: local font and emoji component
- `docs/`: additional development and build docs
- `sdkconfig.defaults*`: default build configuration

## Notes

- The component dependency is local: `alfredo-fonts -> ../components/alfredo-fonts`
- The current CoreS3 face scale is `90`
- The repository keeps a layout compatible with the existing build chain
- Regenerated content such as `build/`, `managed_components/`, and local `sdkconfig` files is intentionally excluded from version control
