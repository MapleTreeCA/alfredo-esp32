# 使用说明 


**编译**

```bash
python ./scripts/release.py m5stack-core-s3
```

如需手动编译，请参考 `m5stack-core-s3/config.json` 修改 menuconfig 对应选项。

**SD 卡运行时配置（免重编译）**

将配置文件放到 SD 卡：`/sdcard/alfredo.cfg`（兼容读取旧路径 `/sdcard/alfredo-config.json`）。  
可直接参考：`main/boards/m5stack-core-s3/alfredo-config.example.json`。
设备在启动时读取该文件，修改后重启设备即可生效。

可通过 SD 卡配置（无需重新 build/烧录）：

- `websocket.url/token/version`
- `wake_word.commands/display/phonemes/threshold/min_confidence`（仅对 Multinet 自定义唤醒词有效；当前使用 Wakenet `wn9_himfive`，此项无实际作用）
- `conversation.aec_mode`（`off` / `device` / `server`）
- `conversation.continue_listening_after_tts_stop`
- `conversation.post_tts_listen_guard_ms`
- `conversation.tts_downlink_drain_quiet_ms`
- `conversation.wake_word_detection_in_listening`
- `audio.output_volume`
- `audio.input_gain`（建议范围 `24~42`，默认 `36`）
- `audio.input_reference`（CoreS3 建议 `false`，仅在确认参考通道接线正确时再设为 `true`）
- `display.brightness/theme`
- `wifi.sleep_mode`
- `power_save.enabled/sleep_seconds/shutdown_seconds`
- `head_gimbal` 的 trim/限位/反向/默认居中参数

仍然需要编译（不能仅靠 SD 卡修改）：

- 板型与芯片目标（CoreS3/ESP32S3）
- 引脚映射（音频、屏幕、摄像头、舵机 GPIO）
- 音频采样率与底层驱动编译选项
- 资源打包（字体/语言包/assets 分区内容）
- 传感器型号等硬件相关 `sdkconfig` 选项

**当前唤醒词：`wn9_himfive`（Wakenet，说 "Hi M Five" 唤醒）**

> Wakenet 预训练模型无需手工填写音素。
> 如将来切换回 Multinet 自定义模式，可用以下工具生成音素：
>
> ```bash
> /Users/dev/.espressif/python_env/idf5.5_py3.14_env/bin/python \
>   managed_components/espressif__esp-sr/tool/multinet_g2p.py \
>   -t "your wake word here"
> ```

**烧录**

```bash
idf.py flash
```

> [!NOTE]
> 进入下载模式：长按复位按键(约3秒)，直至内部指示灯亮绿色，松开按键。

**开发进度与问题说明**

请见：`../../../docs/alfredo-core-s3-gateway-progress.md`


 
