---
name: alfredo-build
description: "Alfredo ESP32 固件编译、资源生成、刷机、串口监控。触发词: build, flash, monitor, assets, 编译, 刷机, 烧录, 资源"
---

# Alfredo ESP32 Build Skill

Build, flash, and monitor firmware for the Alfredo robot (M5Stack CoreS3 / ESP32-S3).

## Project Layout

```
alfredo-esp32/
├── scripts/
│   ├── export_idf_env.sh          # Sources ESP-IDF environment
│   ├── generate_emoji.sh          # Full emoji asset pipeline
│   └── alfred_svg/                # Source-of-truth SVG face designs
├── components/alfredo-fonts/
│   ├── generate_alfred_emoji.py   # SVG→PNG→C array converter
│   ├── svg/alfred/                # Working SVG files (copied from scripts/alfred_svg/)
│   └── png/                       # Generated PNG files
├── build/
│   ├── alfredo.bin                # Compiled firmware
│   └── generated_assets.bin       # Assets partition (emoji PNGs)
└── partitions/v2/16m.csv          # Partition table (assets at 0x800000)
```

## Environment Setup

Every build command needs the ESP-IDF environment. Always source it first:

```bash
source /Users/dev/robot/m5stack/alfredo-esp32/scripts/export_idf_env.sh
```

This script:
- Uses ESP-IDF v5.5.2 from `~/.espressif/esp-idf-v5.5.2/`
- Auto-detects the correct Python venv
- Supports override via `ALFREDO_IDF_PATH` env var

**Important:** Always `cd` to the project root before running `idf.py`:
```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
```

## Commands

### Generate Emoji Assets

Regenerates all emoji face images from SVGs. Run this whenever SVG files change.

```bash
bash /Users/dev/robot/m5stack/alfredo-esp32/scripts/generate_emoji.sh
```

What it does:
1. Finds Python with Pillow (checks ESP-IDF venv → Homebrew → system)
2. Validates `rsvg-convert` is installed
3. Runs `generate_alfred_emoji.py`: SVG → rsvg-convert → PNG → C arrays (32px, 64px) + twemoji_128 PNGs
4. Cleans up stray `components/alfredo-fonts/build/` directory (avoids CMake conflicts)
5. Deletes `build/generated_assets.bin` to force rebuild

### Build Firmware

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh
idf.py build
```

Show the last ~10 lines of output to confirm success. Look for "Project build complete".

### Flash (App + Assets)

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh
idf.py -p /dev/cu.usbmodem1101 flash
```

USB port: `/dev/cu.usbmodem1101`. If flash fails with "port not found", ask user to check USB connection. You can check with `ls /dev/cu.usbmodem*`.

### Flash Assets Only

When only emoji/face images changed (no code changes), flash just the assets partition — much faster:

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem1101 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x800000 build/generated_assets.bin
```

### Monitor Serial Output

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh
idf.py -p /dev/cu.usbmodem1101 monitor
```

Exit with `Ctrl+]`. Use timeout to avoid hanging indefinitely.

### Full Pipeline (Assets → Build → Flash)

When user says "全部重来", "从头编译", or wants the complete pipeline:

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
bash scripts/generate_emoji.sh
source scripts/export_idf_env.sh
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
```

### Clean Rebuild

When build is corrupted or target needs resetting:

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
source scripts/export_idf_env.sh
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

## Emoji Pipeline Details

The face rendering pipeline has two paths:

1. **Compiled-in** (32px, 64px): SVG → PNG → C arrays → linked into firmware binary
2. **Assets partition** (128px): SVG → PNG → `generated_assets.bin` → flashed to 0x800000

At runtime, the 128px assets partition **overrides** the compiled-in collections. If a face is missing from the assets partition, it becomes invisible — even if compiled in.

Key files controlling which faces are included:
- `FACE_TO_EMOTIONS` dict in `generate_alfred_emoji.py` — maps SVG face names to emotion lists. Empty `[]` means the face is NOT included in assets.
- `EMOTION_CODEPOINTS` dict — maps emotion names to Unicode codepoints for the emoji system.

## Common Issues

| Problem | Solution |
|---------|----------|
| Flash fails "port not found" | Check USB: `ls /dev/cu.usbmodem*` |
| Faces all look the same | Assets not rebuilt. Run `generate_emoji.sh` then build+flash |
| CMake error in alfredo-fonts | Delete `components/alfredo-fonts/build/` or `tmp/` dirs |
| Old assets cached | Delete `build/generated_assets.bin` then rebuild |
| Blank/missing face | Check `FACE_TO_EMOTIONS` has entry with non-empty emotion list |
