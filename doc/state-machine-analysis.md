# Alfredo 状态机 & Gateway 协同分析报告

## 1. 设备端状态机 (alfredo-esp32)

### 1.1 状态定义

| 状态 | 值 | 说明 |
|------|---|------|
| Unknown | 0 | 初始状态 |
| Starting | 1 | 启动中 |
| WifiConfiguring | 2 | WiFi 配网 |
| Idle | 3 | 待机（监听唤醒词）|
| Connecting | 4 | 正在连接 Gateway |
| Listening | 5 | 正在收听用户语音 |
| Speaking | 6 | 正在播放 TTS / 固件本地确认音 |
| Upgrading | 7 | OTA 升级（未使用）|
| Activating | 8 | 激活中 |
| AudioTesting | 9 | 音频测试 |
| FatalError | 10 | 致命错误（未使用）|

### 1.2 状态转换图

```
[Unknown] ──→ [Starting]
                  │
         ┌───────┴───────┐
         ▼               ▼
  [WifiConfiguring] ←→ [Activating]
         ↕               │
  [AudioTesting]         ▼
                      [Idle] ←──────────────────────────┐
                        │                                │
                        ├── 唤醒词/按钮 ──→ [Connecting] ─┤ (失败)
                        │                      │         │
                        │                      ▼ (成功)  │
                        │               [Speaking]       │
                        │            (本地 wake ack / TTS)│
                        │                      ▼         │
                        │                  [Listening] ──┤ (手动停止)
                        │                    ↕           │
                        ├── TTS start ──→ [Speaking] ────┘ (TTS stop + manual)
                        │                    │
                        │                    └── TTS stop + auto ──→ [Listening]
                        │
                        └── (channel unhealthy self-heal)
```

补充说明：

- 唤醒词路径已经包含固件本地确认音，不是直接 `Connecting -> Listening`
- 实际路径有两种：
  - `Idle -> Connecting -> Speaking(本地 wake ack) -> Listening -> listen.start`
  - `Idle -> Speaking(本地 wake ack) -> Listening -> listen.start`
- 第二种发生在 audio channel 已经打开时，不需要重新走 `Connecting`

### 1.3 状态-Face 映射

| 状态 | 默认 face | 说明 |
|------|-----------|------|
| Unknown | `neutral` | 未知态回落到默认脸 |
| Starting | `thinking` | 启动中 |
| WifiConfiguring | `thinking` | 配网 / 等待配置 |
| Idle | `neutral` | 待机 |
| Connecting | `thinking` | 正在连 gateway |
| Listening | `listening` | 正在听用户说话 |
| Speaking | `happy` 或保留当前 emotion | wake ack 用 `happy`；正常 TTS 不覆盖 LLM emotion |
| Upgrading | `thinking` | 预留 |
| Activating | `thinking` | 激活中 |
| AudioTesting | `listening` | 复用 listening 脸 |
| FatalError | `grieved` | 致命错误 |

补充：

- 当前固件实际保留的表情 key 只有：`neutral`、`happy`、`sad`、`thinking`、`listening`、`noconnection`、`grieved`、`sleeping`
- 断网不是独立状态，但断网事件会临时切到 `noconnection`
- 省电脸不是状态机状态，显示层会切到 `sleeping`
- `Speaking` 变体不是单独状态映射，而是显示层自动把当前 face 切到 `*_speaking`
- 例如：`neutral -> neutral_speaking`、`listening -> listening_speaking`、`happy -> happy_speaking`
- wake ack 是特例：本地确认音期间显示层切 `happy <-> neutral`
- 正常 TTS 不覆盖 LLM 下发的 emotion，所以如果当前 face 是 `sad` / `thinking` / `happy` / `neutral`，进入 `Speaking` 后会自动显示对应的 `*_speaking`

### 1.4 各状态音频处理

| 状态 | 唤醒词检测 | 语音处理 | 说明 |
|------|-----------|---------|------|
| Idle | **ON** | OFF | 等待唤醒 |
| Connecting | ON (继承) | OFF | 连接中不改变唤醒词状态 |
| Listening | ON (仅 AFE + 配置) | **ON** | 可在听的同时检测唤醒词 |
| Speaking | ON (仅 AFE) | OFF | 播放 TTS 或本地 wake ack，可检测唤醒词打断 |
| WifiConfiguring | OFF | OFF | 配网模式 |

### 1.5 关键时序参数（设备端）

| 参数 | 值 | 来源 |
|------|---|------|
| `post_tts_listen_guard_ms` | 1200ms | sdkconfig |
| `tts_downlink_drain_quiet_ms` | 600ms | sdkconfig |
| `continue_listening_after_tts_stop` | true | sdkconfig |
| `wake_word_detection_in_listening` | false | sdkconfig |
| Protocol `IsTimeout()` | 120s | protocol.cc:82 |
| Hello wait timeout | 10s | websocket_protocol.cc:255 |
| Clock tick (self-heal) | 1s 周期 | application.cc:78 |

---

## 2. Gateway 端会话管理

### 2.1 Gateway 隐式状态

Gateway 没有显式状态枚举，通过字段组合推断：

| 有效状态 | 条件 |
|---------|------|
| Pre-hello | `helloSeen=false` |
| Idle | `helloSeen=true`, `currentTurn=nil`, `activeTurnID=0` |
| Listening | `currentTurn != nil` |
| Processing | `activeTurnID != 0`, `speaking=false` |
| Speaking | `speaking=true` |
| Closed | `closed=true` |

### 2.2 Gateway 时序参数

| 参数 | 默认值 | 环境变量 | 说明 |
|------|-------|---------|------|
| SessionSilence | 700ms | `GATEWAY_SESSION_SILENCE` | 静音超时，结束 turn |
| SessionMaxTurn | 15s | `GATEWAY_SESSION_MAX_TURN` | 单次 turn 最大时长 |
| STTInterimInterval | 900ms | `GATEWAY_STT_INTERIM_INTERVAL` | 流式 STT 间隔 |
| STTInterimMinAudio | 1200ms | `GATEWAY_STT_INTERIM_MIN_AUDIO` | 流式 STT 最小音频 |
| STTTimeout | 45s | `OPENAI_STT_TIMEOUT` | STT API 超时 |
| TTSTimeout | 45s | `OPENAI_TTS_TIMEOUT` | TTS API 超时 |
| CodexTimeout | 90s | `CODEX_TIMEOUT` | LLM API 超时 |
| TTSMaxDuration | 3min | `GATEWAY_TTS_MAX_DURATION` | TTS 最大播放时长 |
| initialNoAudioGrace | 7s | 硬编码 | 无音频帧时的宽限期 |
| restartTurnDelayAfterDrop | 700ms | 硬编码 | 中断后重启 turn 延迟 |

---

## 3. 设备-Gateway 协同流程

### 3.1 正常对话流程

```
设备                          Gateway
  │                              │
  │── hello ──────────────────→  │  helloSeen=true
  │←── hello response ──────── │
  │                              │
  │  [本地检测唤醒词]              │
  │  [播放 hi there 本地确认音]    │
  │  [进入 Listening]             │
  │── listen(start, mode=auto)→  │  startTurn(), arm timers
  │── audio frames ──────────→  │  append to turnBuffer
  │   ...                        │  silence timer fires (700ms)
  │                              │  finalizeTurn → STT → LLM → TTS
  │←── stt(final, text) ──────  │
  │←── llm(emotion, text) ────  │
  │←── tts(start) ────────────  │  speaking=true
  │←── tts(sentence_start) ───  │
  │←── binary opus frames ────  │
  │←── tts(stop) ─────────────  │  speaking=false
  │                              │
  │  [drain playback 600ms]      │
  │  [guard 1200ms]              │
  │── listen(start, mode=auto)→  │  new turn
  │   ...                        │
```

说明：

- wakeup 后播放的是固件本地资源 `main/assets/common/generated/hi-there-daniel.ogg`
- 代码路径是 `HandleWakeWordDetectedEvent()` -> `BeginWakeWordListeningSequence()` -> `PlayWakeWordAckAndEnterListening()`
- `CONFIG_SEND_WAKE_WORD_DATA` 关闭后，不再向 gateway 发送 `listen.detect` 或 wake-word 预卷音频
- gateway 只从 `listen.start` 开始新 turn

### 3.2 唤醒词打断流程

```
设备 (Speaking 状态)            Gateway (Speaking)
  │                              │
  │  [唤醒词检测到]               │
  │── abort(wake_word_detected)→ │  cancelActive(), tts stop
  │←── tts(stop) ─────────────  │
  │                              │
  │  [播放 hi there 本地确认音]    │
  │  [进入 Listening]             │
  │── listen(start, mode=auto)→  │  new turn
  │── audio frames ──────────→  │
```

---

## 4. 发现的缺陷和问题

### 4.1 严重：Connecting 状态无超时保护

**位置**: `application.cc:446-457`, `device_state_machine.cc:80-82`

**问题**: Clock-tick self-heal 只检查 `Listening` 和 `Speaking`：

```cpp
bool active_conversation_state =
    (state == kDeviceStateListening) || (state == kDeviceStateSpeaking);
```

如果 `OpenAudioChannel()` 因网络问题阻塞（虽然内部有 10s 超时），但如果 `SetError` 未被调用（某些失败路径），设备会永远卡在 `Connecting` 状态。

**影响**: 唤醒词检测虽然还在运行（继承自 Idle），但新的唤醒词触发会尝试再次 `ContinueWakeWordInvoke`，可能导致重复连接尝试。

**建议修复**:

```cpp
bool active_conversation_state =
    (state == kDeviceStateListening) || (state == kDeviceStateSpeaking);
bool stale_connecting =
    (state == kDeviceStateConnecting) && (clock_ticks_ > 15);
if ((active_conversation_state && !protocol_->IsAudioChannelOpened()) || stale_connecting) {
    ESP_LOGW(TAG, "State %d unhealthy (ticks=%d), reset to idle", (int)state, clock_ticks_);
    protocol_->CloseAudioChannel();
    SetDeviceState(kDeviceStateIdle);
}
```

### 4.2 中等：Connecting 状态未禁用唤醒词

**位置**: `application.cc:1318-1322`

**问题**: 进入 `Connecting` 时，`HandleStateChangedEvent` 只更新 UI，不改变唤醒词状态。唤醒词保持 ON（从 Idle 继承）。如果用户在连接过程中再次说唤醒词：

1. `HandleWakeWordDetectedEvent` 被触发
2. 状态是 `Connecting`，不在 `Idle/Speaking/Listening/Activating` 的处理分支中
3. 事件被静默忽略（没有任何处理分支匹配 `Connecting`）

这不会崩溃，但用户体验不好——用户说了唤醒词但没有反应。

**建议**: 在 `HandleWakeWordDetectedEvent` 中增加 `Connecting` 状态的处理，或者在进入 `Connecting` 时禁用唤醒词。

### 4.3 中等：Speaking → Listening 的 post-TTS guard 与 Gateway silence timer 冲突

**位置**: 设备 `post_tts_listen_guard_ms_=1200ms`, Gateway `SessionSilence=700ms`

**问题时序**:

```
T=0     Gateway 发送 tts(stop)
T=0     设备收到 tts(stop), 开始 drain playback (600ms)
T=600   设备 drain 完成, 启动 post-TTS guard (1200ms)
T=1800  设备 guard 到期, 进入 Listening, 发送 listen(start)
T=1800  Gateway 收到 listen(start), 启动 silence timer (700ms)
T=2500  如果用户还没开始说话, Gateway silence timeout → finalize turn
```

**影响**: 用户在 TTS 播放结束后有 **1800ms** 的"死区"（设备端 drain + guard），然后只有 **700ms** 的窗口开始说话。总计用户需要在 TTS 结束后 **2500ms 内开始说话**，否则 Gateway 会超时关闭 turn。

对于需要思考后回答的场景（如被问了一个问题），2.5 秒可能不够。

**建议**: 
- 减少 `post_tts_listen_guard_ms` 到 400-600ms
- 或增加 Gateway 的 `SessionSilence` 到 1200-1500ms
- 或在 Gateway 端对 "刚从 TTS 结束的 turn" 使用更长的首次静音超时

### 4.4 中等：Gateway 无 idle session 超时

**位置**: Gateway `session.go`

**问题**: Gateway 没有 idle session 超时。设备端有 120s 的 `IsTimeout()`，但 Gateway 完全依赖设备主动断开。如果设备因 bug 不断开连接但不发消息，Gateway 会积累僵尸 session。

**建议**: Gateway 添加 idle session 超时（如 180s 无任何消息则关闭）。

### 4.5 低：Activating 状态无超时

**位置**: `application.cc:480-485`

**问题**: `ActivationTask` 在独立 FreeRTOS task 中运行，没有超时。如果激活服务器无响应，设备会一直卡在 `Activating`。虽然当前 Cloud OTA 已禁用（`application.cc:584`），但激活流程仍然运行。

**建议**: 为 `ActivationTask` 添加超时（如 30s），超时后回退到 Idle 或 WifiConfiguring。

### 4.6 低：Speaking 状态下唤醒词检测仅限 AFE

**位置**: `application.cc:1375`

```cpp
audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
```

**问题**: 你使用的是 `CustomWakeWord`（Multinet），不是 `AfeWakeWord`。`IsAfeWakeWord()` 返回 false，所以 **Speaking 状态下唤醒词检测被禁用**。

这意味着用户在设备说话时无法通过唤醒词打断，只能按按钮。

**建议**: 如果 CustomWakeWord 也使用了 AFE 前端（你的代码中确实初始化了 AFE），可以考虑也允许 CustomWakeWord 在 Speaking 状态下检测。需要评估 CPU 负载。

### 4.7 低：`continue_listening_after_tts_stop` 与 `listening_mode` 交互复杂

**位置**: `application.cc:739-748`

**问题**: TTS stop 处理逻辑：

```cpp
if (listening_mode_ == kListeningModeManualStop) {
    SetDeviceState(kDeviceStateIdle);
} else {
    if (continue_listening_after_tts_stop_) {
        ScheduleResumeListeningAfterGuard();
    } else {
        SetDeviceState(kDeviceStateIdle);
    }
}
```

当 `continue_listening_after_tts_stop_=true` 且 `listening_mode_=auto` 时，TTS 结束后会自动回到 Listening。但 `listening_mode_` 是在 `SetListeningMode()` 中设置的，如果在 Speaking 期间 mode 被改变（通过运行时配置），行为可能不一致。

### 4.8 信息：未使用的状态

- `kDeviceStateUpgrading`: Cloud OTA 已禁用，此状态永远不会被进入
- `kDeviceStateFatalError`: 没有任何代码设置此状态，且无法退出

---

## 5. 默认时序配置（修复后）

### 5.1 设备端

| 参数 | 修复前 | 修复后 | 说明 |
|------|-------|-------|------|
| `post_tts_listen_guard_ms` | 1200ms | **500ms** ✅ | 减少 TTS 后死区 |
| `tts_downlink_drain_quiet_ms` | 600ms | 600ms | 保持 |
| `continue_listening_after_tts_stop` | true | true | 保持 |
| `wake_word_detection_in_listening` | false | false | 待评估 CPU 负载 |
| Protocol timeout | 120s | 120s | 保持 |
| Hello wait | 10s | 10s | 保持 |
| Connecting self-heal | 无 | **15s** ✅ | 新增超时保护 |

### 5.2 Gateway 端

| 参数 | 修复前 | 修复后 | 说明 |
|------|-------|-------|------|
| `SessionSilence` | 700ms | **1200ms** ✅ | 给用户更多思考时间 |
| `SessionMaxTurn` | 15s | 15s | 保持 |
| `STTInterimInterval` | 900ms | 900ms | 保持 |
| `STTInterimMinAudio` | 1200ms | 1200ms | 保持 |
| `STTTimeout` | 45s | 45s | 保持 |
| `CodexTimeout` | 90s | 90s | 建议后续降到 45s |
| `TTSMaxDuration` | 3min | 3min | 保持 |
| `initialNoAudioGrace` | 7s | 7s | 保持 |
| Session idle timeout | 无 | **180s** ✅ | 新增僵尸连接清理 |

### 5.3 关键时序链路分析（修复后）

**从唤醒到开始听：**
```
唤醒词检测 → Connecting → OpenAudioChannel (≤10s) → Speaking(本地 wake ack) → Listening → listen(start)
最佳情况: ~200ms (channel 已打开)
最差情况: ~10s (需要新建 WebSocket)
若 Connecting 卡住: 15s 后 self-heal 回到 Idle ✅
```

**从说完话到 AI 回复：**
```
用户停止说话 → silence timeout (1200ms) → STT (1-3s) → LLM (1-5s) → TTS 首帧 (~500ms)
总延迟: 2.7s - 9.7s
```

**从 AI 说完到用户可以说话（修复后）：**
```
tts(stop) → drain (600ms) → guard (500ms) → listen(start) → initialNoAudioGrace (7s)
用户有 7s 宽限期开始说话（首帧到达前）
首帧到达后: 1200ms silence timeout
```

对比修复前：
```
修复前: drain(600) + guard(1200) = 1800ms 死区, 然后只有 700ms 窗口
修复后: drain(600) + guard(500) = 1100ms 死区, 然后有 7s 宽限 + 1200ms 窗口 ✅
```

---

## 6. 已完成的修复

| # | 修复项 | 状态 |
|---|-------|------|
| 1 | Connecting 超时保护 (15s self-heal) | ✅ 已实现 |
| 2 | post_tts_listen_guard_ms: 1200→500ms | ✅ 已实现 |
| 3 | Gateway SessionSilence: 700→1200ms | ✅ 已实现 |
| 4 | Gateway idle session timeout: 180s | ✅ 已实现 |
| 5 | 清理未使用状态 Upgrading/FatalError | ✅ 已标记 |
| 6 | 设备-Gateway 时序协调验证 | ✅ 已验证 |

### 待评估项

| # | 项目 | 说明 |
|---|------|------|
| 1 | Speaking 状态唤醒词 | CustomWakeWord 在 Speaking 时被禁用，需评估 CPU |
| 2 | CodexTimeout 90→45s | 需确认 LLM 响应时间分布 |
| 3 | Activating 超时 | 需确认激活流程最大耗时 |
