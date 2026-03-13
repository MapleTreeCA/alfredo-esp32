# Alfredo ESP32 Agent Guide

## Scope

This repo is the firmware for `alfredo-esp32`, currently centered on `M5Stack CoreS3`.
Most tasks only need app flow, runtime config, protocol, board config, and a small part of audio/display code.

Do not scan generated assets, build outputs, vendored components, or non-CoreS3 material unless the task clearly requires it.

## Start Here

Read these first for most firmware tasks:

1. `README.md`
2. `main/main.cc`
3. `main/application.cc`
4. `main/application.h`
5. `main/runtime_config.cc`
6. `main/runtime_config.h`
7. `main/device_state_machine.cc`
8. `main/device_state_machine.h`
9. `main/protocols/websocket_protocol.cc`
10. `main/protocols/websocket_protocol.h`
11. `main/boards/m5stack-core-s3/README.md`
12. `main/boards/m5stack-core-s3/config.h`
13. `main/boards/m5stack-core-s3/config.json`
14. `main/boards/m5stack-core-s3/m5stack_core_s3.cc`

## Read By Task

Pick only the relevant area:

- Gateway/WebSocket interop:
  `docs/websocket.md`, `main/protocols/websocket_protocol.*`, `main/runtime_config.*`, `main/application.*`
- state/timing/session bugs:
  `main/device_state_machine.*`, `main/application.*`, `doc/state-machine-analysis.md`
- audio pipeline:
  `main/audio/**`, then `main/device_state_machine.*`
- display/emotion/emoji task:
  `main/display/**`, `scripts/generate_alfred_emoji.py`, `scripts/alfred_svg/**`, `components/alfredo-fonts/**`
- CoreS3 hardware/board task:
  `main/boards/m5stack-core-s3/**`, then `main/boards/common/**`
- runtime config/settings task:
  `main/runtime_config.*`, `main/settings.*`, `main/boards/m5stack-core-s3/config.json`
- MCP/tool integration task:
  `main/mcp_server.*`, `main/boards/common/press_to_talk_mcp_tool.*`
- build/flash/environment task:
  `docs/build-assets-and-flash-manual.md`, `scripts/release.py`, `scripts/export_idf_env.sh`, `sdkconfig.defaults*`, `CMakeLists.txt`, `main/CMakeLists.txt`
- partition/layout task:
  `partitions/**`, `sdkconfig.defaults*`, build docs

## Usually Skip

Ignore these unless the task explicitly depends on them:

- `build/`
- `managed_components/`
- `logs/`
- `releases/`
- `.venv-codex/`
- `.venv-emoji/`
- `.git/`
- `README_ja.md`
- `README_zh.md`
- `main/assets/locales/**`
- generated or packaged binary assets such as `*.ogg` and release zip files
- `docs/custom-board.md` when not adding a new board
- `scripts/Image_Converter/**`
- `scripts/p3_tools/**`
- `scripts/ogg_converter/**`

## Working Rules

- Assume the default target board is `m5stack-core-s3` unless the task says otherwise.
- For protocol bugs, do not inspect all display/audio files first. Start with `websocket_protocol`, `runtime_config`, `application`, and `device_state_machine`.
- For asset work, trace source -> generator -> output. Do not read every generated asset file.
- For build issues, prefer build docs and scripts before opening large source trees.
- Use `rg` on state names, message types, config keys, and board symbols before broad file reads.
