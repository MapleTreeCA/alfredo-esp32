#ifndef _RUNTIME_CONFIG_H_
#define _RUNTIME_CONFIG_H_

#include <string>

struct RuntimeWebsocketConfig {
    bool has_url = false;
    std::string url;
    bool has_token = false;
    std::string token;
    bool has_version = false;
    int version = 0;
};

struct RuntimeWakeWordConfig {
    bool has_commands = false;
    std::string commands;
    bool has_display = false;
    std::string display;
    bool has_phonemes = false;
    std::string phonemes;
    bool has_threshold = false;
    int threshold = 0;
};

struct RuntimeConversationConfig {
    bool has_continue_listening_after_tts_stop = false;
    bool continue_listening_after_tts_stop = true;
    bool has_post_tts_listen_guard_ms = false;
    int post_tts_listen_guard_ms = 0;
    bool has_tts_downlink_drain_quiet_ms = false;
    int tts_downlink_drain_quiet_ms = 0;
    bool has_wake_word_detection_in_listening = false;
    bool wake_word_detection_in_listening = false;
    bool has_xiaozhi_compat_mode = false;
    bool xiaozhi_compat_mode = false;
    bool has_mic_send_gate_enabled = false;
    bool mic_send_gate_enabled = true;
    bool has_aec_mode = false;
    std::string aec_mode;
};

struct RuntimeAudioConfig {
    bool has_output_volume = false;
    int output_volume = 0;
    bool has_input_gain = false;
    int input_gain = 0;
    bool has_input_reference = false;
    bool input_reference = false;
};

struct RuntimeDisplayConfig {
    bool has_brightness = false;
    int brightness = 0;
    bool has_theme = false;
    std::string theme;
};

struct RuntimeWifiConfig {
    bool has_sleep_mode = false;
    bool sleep_mode = true;
};

struct RuntimePowerSaveConfig {
    bool has_enabled = false;
    bool enabled = true;
    bool has_sleep_seconds = false;
    int sleep_seconds = 0;
    bool has_shutdown_seconds = false;
    int shutdown_seconds = 0;
};

struct RuntimeHeadGimbalConfig {
    bool has_pan_trim = false;
    int pan_trim = 0;
    bool has_tilt_trim = false;
    int tilt_trim = 0;
    bool has_pan_inverted = false;
    bool pan_inverted = false;
    bool has_tilt_inverted = false;
    bool tilt_inverted = false;
    bool has_pan_min = false;
    int pan_min = 0;
    bool has_pan_max = false;
    int pan_max = 0;
    bool has_tilt_min = false;
    int tilt_min = 0;
    bool has_tilt_max = false;
    int tilt_max = 0;
    bool has_center_pan = false;
    int center_pan = 90;
    bool has_center_tilt = false;
    int center_tilt = 90;
    bool has_center_duration_ms = false;
    int center_duration_ms = 0;
};

class RuntimeConfig {
public:
    static const char* GetConfigPath();
    static void InvalidateCache();
    static bool LoadWebsocketConfig(RuntimeWebsocketConfig& out);
    static bool LoadWakeWordConfig(RuntimeWakeWordConfig& out);
    static bool LoadConversationConfig(RuntimeConversationConfig& out);
    static bool LoadAudioConfig(RuntimeAudioConfig& out);
    static bool LoadDisplayConfig(RuntimeDisplayConfig& out);
    static bool LoadWifiConfig(RuntimeWifiConfig& out);
    static bool LoadPowerSaveConfig(RuntimePowerSaveConfig& out);
    static bool LoadHeadGimbalConfig(RuntimeHeadGimbalConfig& out);
};

#endif // _RUNTIME_CONFIG_H_
