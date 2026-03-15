# Alfredo CoreS3 对接 Gateway 开发进度与问题说明

更新时间：2026-03-12

## 1. 范围与目标

本阶段目标是让 `alfredo-esp32`（CoreS3）脱离原先云端链路，直接对接自建 `gateway`，由 gateway 统一承接：

- STT
- LLM
- TTS

固件侧只保留端上语音采集、唤醒词、状态机、音频编解码与播放控制。

## 2. 当前架构（已跑通）

- 固件通过 WebSocket 连接 gateway：`ws://10.0.0.175:18910/ws`
- 音频上行：设备持续发送 Opus 帧
- 网关处理链路：`STT -> LLM -> TTS -> Opus 下行`
- 设备下行播放：接收 Opus 并解码播放

已在串口日志中确认完成握手和会话建立（出现 `Session ID`，并可完成问答一轮）。

## 3. 已完成项（本阶段）

- CoreS3 默认 WebSocket 启动配置已对齐自建 gateway（`18910/ws`）。
- 增加 SD 卡运行时配置（`/sdcard/alfredo.cfg`，兼容旧路径 `/sdcard/alfredo-config.json`），可免重编译调整：
  - websocket
  - wake_word（`commands/display/phonemes/threshold/min_confidence`）
  - conversation
  - audio/display/wifi/power_save/head_gimbal
- 语音状态机已切到“回复后继续监听”的对话路径，并加入 TTS 后保护窗口参数。
- 设备端 AEC/VAD 路径可用（日志可见 AFE/AEC pipeline）。
- Gateway 已可用脚本一键启动（见下方“运行与排查”）。

## 4. 当前已知问题（重点）

### 4.1 端到端“流式 STT”已接通（2026-03-11）

当前系统已支持 `interim + final` 双阶段 STT 事件：

- 设备上行音频是流式（持续 Opus 帧）
- gateway 在监听中按时间窗口发送 `{"type":"stt","state":"interim","text":"..."}`（中间结果）
- gateway 在 turn 结束后发送 `{"type":"stt","state":"final","text":"..."}`（最终结果）

已落地事件语义：

- interim：用于实时 UI 文本刷新、早期策略触发
- final：用于后续 LLM 输入和最终文本确认

说明：当前 interim 采用 gateway 侧增量调度（周期转写当前累计音频），并非 STT 提供商原生双向流式接口；后续可再迭代到 provider-native streaming 以进一步降低时延和成本。

### 4.2 英文唤醒词 `alfredo` 仍有约束

日志显示命令词图构建时 `alfredo` 被判定为无效，当前稳定生效的是拼音形式（如 `a fu lei duo`）。

影响：直接说英文 `alfredo` 可能无法稳定唤醒。

### 4.3 仍有轻微误触发/杂音触发风险

在“回复后继续监听”模式下，环境噪声仍可能触发新一轮监听与对话，需要继续做阈值和门限调优（含后 TTS 保护窗口、VAD/能量门限联调）。

### 4.4 播放偶发轻微断续

已比早期明显改善，但在部分回复中仍有轻微断续，后续需继续联调下行帧节奏、解码队列水位和播放缓冲参数。

## 5. 运行与排查（当前可用）

### 5.1 启动 gateway（已对齐 alfredo）

```bash
cd /Users/dev/robot/m5stack/gateway
./scripts/start-alfredo-gateway.sh
```

后台模式：

```bash
cd /Users/dev/robot/m5stack/gateway
./scripts/start-alfredo-gateway.sh --daemon
tail -f /tmp/codex-gateway.log
```

### 5.2 固件串口监控

```bash
cd /Users/dev/robot/m5stack/alfredo-esp32
. /Users/dev/.espressif/esp-idf-v5.5.2/export.sh
idf.py -p /dev/cu.usbmodem1101 monitor
```

重点看：

- `Connecting to websocket server: ws://10.0.0.175:18910/ws`
- `Session ID: ...`
- `State: idle -> listening -> speaking -> listening`

### 5.3 通过 Gateway 页面下发 SD 配置（新增）

前提：设备已连接到 gateway（日志中可见 `session=... connected`）。

操作步骤：

1. 打开 `http://127.0.0.1:18910/`
2. 在 `Device SD Config` 面板点击 `Refresh Devices`
3. 选择目标设备（按 `device_id (remote_addr)` 展示）
4. 编辑 `Runtime Config JSON`
5. 点击 `Apply To Device SD`（可选 `Reboot After Apply`）

网关会向设备发送 `system` 命令：

- `command=write_sdcard_runtime_config`
- `config={...}`
- `reboot=true/false`

设备侧会写入 `/sdcard/alfredo.cfg`，成功后立即 `ApplyRuntimeConfig()`，若 `reboot=true` 则自动重启。

## 6. 下一线程建议接力点（按优先级）

1. 处理英文 `alfredo` 唤醒词可用性（命令词模型/词表/策略调整）。
2. 继续压低噪声误触发率（VAD + 后 TTS 保护窗口 + 能量阈值联合调参）。
3. 继续优化轻微断续（下行节流、解码队列背压、水位观测）。
4. 评估 provider-native streaming STT（减少 interim 重复转写开销）。

## 7. 会话上下文的正确精简流程（可复用）

目标：每次改动都能快速定位“是采集问题、状态机问题、网关时序问题，还是资源/刷写问题”。

### Step 1：先固定基线（不混改）

- 固件、gateway、assets 三者只改一处，其他保持不变。
- 每次改动记录：
  - 改了哪一层（firmware/gateway/assets）
  - 改了哪些参数
  - 改完后是否重启生效

### Step 2：双端同时监控

- 看 gateway 日志：`turn=start/finalize/stt/llm/tts/downlink/done`
- 看串口日志：`idle -> listening -> speaking -> listening`
- 先确认有没有“音频帧上行”，再看识别文本对不对。

### Step 3：先测短句，再测长句

- 短句验证链路是否通（2-5 秒）
- 长句验证稳定性（20-90 秒）
- 长句后重点看下一轮是否能继续采集（是否卡在 listening 但无音频）

### Step 4：出现异常时按分类排查

- `no_audio/speech_frames=0`：先查采集/VAD/状态机切换。
- 文本误识别（如 `you/thank you`）：先查回声残留、后 TTS 保护窗口、噪声门限。
- 长回复后卡住：先查 TTS 下行时长、播放队列、回到 listening 的时机。
- 重启/崩溃：先抓串口启动栈和最后一条 gateway turn 日志。

### Step 5：资源改动必须走“生成 -> 打包 -> 刷写 -> 校验”

- 生成 emoji/png
- 重新打 `generated_assets.bin`
- 刷 `0x800000` 分区
- 校验 hash 后看屏幕实际效果（不要只看源码）

### Step 6：每轮结论只保留三句话

- 本轮改了什么
- 结果是否生效
- 下一步只做哪一个变量

## 8. 这轮实际做了什么（精简版）

### 8.1 Gateway/对话链路

- 跑通了 `STT -> LLM -> TTS` 全链路，并支持 `interim + final` STT 事件。
- 增加了通过 Web 页面写 SD 运行时配置（设备在线时可热更新并可选重启）。
- 多轮定位了“长回复后下一轮误采集/卡 listening”的核心现象，并按时序持续收敛。

### 8.2 固件侧交互

- 增加触控快捷控制：
  - 右上/右下短按调音量
  - 长按触发睡眠表情
  - 短按可退出手动睡眠
- 多次编译、刷写、串口监控闭环验证。

### 8.3 资源与视觉

- 重新生成并刷写 assets，恢复并调整主题资源。
- 背景调整为深色系（`#1a1414`）。
- 表情持续微调（眼睛间距、瞳孔尺寸、口腔颜色），并通过 assets 刷写后实机验证。

### 8.4 当前共识

- 主链路可用，但“噪声误触发 + 长回复后下一轮稳定性”仍是核心优化项。
- 后续应坚持“单变量改动 + 双端日志对齐 + 分层回归验证”，避免并行改动导致定位失真。
