#include "runtime_config.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <cJSON.h>
#include <esp_log.h>

namespace {

constexpr const char* TAG = "RuntimeConfig";
constexpr bool kEnableSdCardRuntimeConfig = false;
// Use 8.3 filename as primary path for FATFS compatibility on boards
// where long filename (LFN) support is not enabled.
constexpr const char* kRuntimeConfigPath = "/sdcard/alfredo.cfg";
constexpr const char* kLegacyRuntimeConfigPath = "/sdcard/alfredo-config.json";

struct RuntimeConfigCache {
    bool initialized = false;
    bool has_content = false;
    std::string loaded_path;
    std::string content;
};

std::mutex g_runtime_config_mutex;
RuntimeConfigCache g_runtime_config_cache;

bool ReadBool(cJSON* object, const char* key, bool& out) {
    auto item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    out = cJSON_IsTrue(item);
    return true;
}

bool ReadInt(cJSON* object, const char* key, int& out) {
    auto item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    out = item->valueint;
    return true;
}

bool ReadString(cJSON* object, const char* key, std::string& out) {
    auto item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsString(item)) {
        return false;
    }
    out = item->valuestring;
    return true;
}

bool ReadConfigTextFromPath(const char* path, std::string& out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        int saved_errno = errno;
        ESP_LOGD(
            TAG,
            "Runtime config not readable at %s: errno=%d (%s)",
            path,
            saved_errno,
            strerror(saved_errno)
        );
        return false;
    }

    struct stat st = {};
    if (fstat(fd, &st) != 0) {
        int saved_errno = errno;
        close(fd);
        ESP_LOGW(
            TAG,
            "Failed to stat %s errno=%d (%s)",
            path,
            saved_errno,
            strerror(saved_errno)
        );
        return false;
    }

    long length = static_cast<long>(st.st_size);
    if (length <= 0) {
        close(fd);
        ESP_LOGW(TAG, "Runtime config %s is empty", path);
        return false;
    }

    std::vector<char> buffer(static_cast<size_t>(length) + 1, '\0');
    ssize_t read_size = read(fd, buffer.data(), static_cast<size_t>(length));
    close(fd);
    if (read_size < 0 || read_size != length) {
        int saved_errno = errno;
        ESP_LOGW(
            TAG,
            "Failed to read %s errno=%d (%s) read=%d expected=%ld",
            path,
            saved_errno,
            strerror(saved_errno),
            static_cast<int>(read_size),
            length
        );
        return false;
    }

    out.assign(buffer.data(), static_cast<size_t>(length));
    return true;
}

void RefreshRuntimeConfigCacheLocked() {
    g_runtime_config_cache = RuntimeConfigCache{};
    g_runtime_config_cache.initialized = true;
    if (!kEnableSdCardRuntimeConfig) {
        ESP_LOGI(TAG, "SD card runtime config is disabled; using compiled defaults only");
        return;
    }

    std::string content;
    if (ReadConfigTextFromPath(kRuntimeConfigPath, content)) {
        g_runtime_config_cache.has_content = true;
        g_runtime_config_cache.loaded_path = kRuntimeConfigPath;
        g_runtime_config_cache.content = std::move(content);
        return;
    }
    if (ReadConfigTextFromPath(kLegacyRuntimeConfigPath, content)) {
        g_runtime_config_cache.has_content = true;
        g_runtime_config_cache.loaded_path = kLegacyRuntimeConfigPath;
        g_runtime_config_cache.content = std::move(content);
        return;
    }
}

std::unique_ptr<cJSON, decltype(&cJSON_Delete)> LoadRoot() {
    std::string cached_content;
    std::string cached_path;
    {
        std::lock_guard<std::mutex> lock(g_runtime_config_mutex);
        if (!g_runtime_config_cache.initialized) {
            RefreshRuntimeConfigCacheLocked();
        }
        if (!g_runtime_config_cache.has_content) {
            return {nullptr, cJSON_Delete};
        }
        cached_content = g_runtime_config_cache.content;
        cached_path = g_runtime_config_cache.loaded_path;
    }

    cJSON* root = cJSON_Parse(cached_content.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Failed to parse %s", cached_path.c_str());
        return {nullptr, cJSON_Delete};
    }
    if (!cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Runtime config %s must contain a JSON object", cached_path.c_str());
        cJSON_Delete(root);
        return {nullptr, cJSON_Delete};
    }
    return {root, cJSON_Delete};
}

}  // namespace

bool RuntimeConfig::IsSdCardRuntimeConfigEnabled() {
    return kEnableSdCardRuntimeConfig;
}

const char* RuntimeConfig::GetConfigPath() {
    return kRuntimeConfigPath;
}

void RuntimeConfig::InvalidateCache() {
    std::lock_guard<std::mutex> lock(g_runtime_config_mutex);
    g_runtime_config_cache = RuntimeConfigCache{};
}

bool RuntimeConfig::LoadWebsocketConfig(RuntimeWebsocketConfig& out) {
    auto root = LoadRoot();
    if (root == nullptr) {
        return false;
    }

    auto websocket = cJSON_GetObjectItem(root.get(), "websocket");
    if (!cJSON_IsObject(websocket)) {
        return false;
    }

    auto url = cJSON_GetObjectItem(websocket, "url");
    if (cJSON_IsString(url)) {
        out.has_url = true;
        out.url = url->valuestring;
    }

    auto token = cJSON_GetObjectItem(websocket, "token");
    if (cJSON_IsString(token)) {
        out.has_token = true;
        out.token = token->valuestring;
    }

    auto version = cJSON_GetObjectItem(websocket, "version");
    if (cJSON_IsNumber(version)) {
        out.has_version = true;
        out.version = version->valueint;
    }

    return out.has_url || out.has_token || out.has_version;
}

bool RuntimeConfig::LoadWakeWordConfig(RuntimeWakeWordConfig& out) {
    auto root = LoadRoot();
    if (root == nullptr) {
        return false;
    }

    auto wake_word = cJSON_GetObjectItem(root.get(), "wake_word");
    if (!cJSON_IsObject(wake_word)) {
        return false;
    }

    auto commands = cJSON_GetObjectItem(wake_word, "commands");
    if (cJSON_IsString(commands)) {
        out.has_commands = true;
        out.commands = commands->valuestring;
    }

    auto display = cJSON_GetObjectItem(wake_word, "display");
    if (cJSON_IsString(display)) {
        out.has_display = true;
        out.display = display->valuestring;
    }

    auto phonemes = cJSON_GetObjectItem(wake_word, "phonemes");
    if (cJSON_IsString(phonemes)) {
        out.has_phonemes = true;
        out.phonemes = phonemes->valuestring;
    }

    auto threshold = cJSON_GetObjectItem(wake_word, "threshold");
    if (cJSON_IsNumber(threshold)) {
        out.has_threshold = true;
        out.threshold = threshold->valueint;
    }

    return out.has_commands || out.has_display || out.has_phonemes || out.has_threshold;
}

bool RuntimeConfig::LoadConversationConfig(RuntimeConversationConfig& out) {
    auto root = LoadRoot();
    if (root == nullptr) {
        return false;
    }

    auto conversation = cJSON_GetObjectItem(root.get(), "conversation");
    if (!cJSON_IsObject(conversation)) {
        return false;
    }

    out.has_continue_listening_after_tts_stop =
        ReadBool(conversation, "continue_listening_after_tts_stop", out.continue_listening_after_tts_stop);
    out.has_post_tts_listen_guard_ms =
        ReadInt(conversation, "post_tts_listen_guard_ms", out.post_tts_listen_guard_ms);
    out.has_tts_downlink_drain_quiet_ms =
        ReadInt(conversation, "tts_downlink_drain_quiet_ms", out.tts_downlink_drain_quiet_ms);
    out.has_wake_word_detection_in_listening =
        ReadBool(conversation, "wake_word_detection_in_listening", out.wake_word_detection_in_listening);
    out.has_xiaozhi_compat_mode =
        ReadBool(conversation, "xiaozhi_compat_mode", out.xiaozhi_compat_mode);
    out.has_mic_send_gate_enabled =
        ReadBool(conversation, "mic_send_gate_enabled", out.mic_send_gate_enabled);
    out.has_aec_mode = ReadString(conversation, "aec_mode", out.aec_mode);

    return out.has_continue_listening_after_tts_stop ||
        out.has_post_tts_listen_guard_ms ||
        out.has_tts_downlink_drain_quiet_ms ||
        out.has_wake_word_detection_in_listening ||
        out.has_xiaozhi_compat_mode ||
        out.has_mic_send_gate_enabled ||
        out.has_aec_mode;
}

bool RuntimeConfig::LoadAudioConfig(RuntimeAudioConfig& out) {
    auto root = LoadRoot();
    if (root == nullptr) {
        return false;
    }

    auto audio = cJSON_GetObjectItem(root.get(), "audio");
    if (!cJSON_IsObject(audio)) {
        return false;
    }

    out.has_output_volume = ReadInt(audio, "output_volume", out.output_volume);
    out.has_input_gain = ReadInt(audio, "input_gain", out.input_gain);
    out.has_input_reference = ReadBool(audio, "input_reference", out.input_reference);
    return out.has_output_volume || out.has_input_gain || out.has_input_reference;
}

bool RuntimeConfig::LoadDisplayConfig(RuntimeDisplayConfig& out) {
    auto root = LoadRoot();
    if (root == nullptr) {
        return false;
    }

    auto display = cJSON_GetObjectItem(root.get(), "display");
    if (!cJSON_IsObject(display)) {
        return false;
    }

    out.has_brightness = ReadInt(display, "brightness", out.brightness);
    out.has_theme = ReadString(display, "theme", out.theme);

    return out.has_brightness || out.has_theme;
}

bool RuntimeConfig::LoadWifiConfig(RuntimeWifiConfig& out) {
    auto root = LoadRoot();
    if (root == nullptr) {
        return false;
    }

    auto wifi = cJSON_GetObjectItem(root.get(), "wifi");
    if (!cJSON_IsObject(wifi)) {
        return false;
    }

    out.has_sleep_mode = ReadBool(wifi, "sleep_mode", out.sleep_mode);
    return out.has_sleep_mode;
}

bool RuntimeConfig::LoadPowerSaveConfig(RuntimePowerSaveConfig& out) {
    auto root = LoadRoot();
    if (root == nullptr) {
        return false;
    }

    auto power_save = cJSON_GetObjectItem(root.get(), "power_save");
    if (!cJSON_IsObject(power_save)) {
        return false;
    }

    out.has_enabled = ReadBool(power_save, "enabled", out.enabled);
    out.has_sleep_seconds = ReadInt(power_save, "sleep_seconds", out.sleep_seconds);
    out.has_shutdown_seconds = ReadInt(power_save, "shutdown_seconds", out.shutdown_seconds);

    return out.has_enabled || out.has_sleep_seconds || out.has_shutdown_seconds;
}

bool RuntimeConfig::LoadHeadGimbalConfig(RuntimeHeadGimbalConfig& out) {
    auto root = LoadRoot();
    if (root == nullptr) {
        return false;
    }

    auto head_gimbal = cJSON_GetObjectItem(root.get(), "head_gimbal");
    if (!cJSON_IsObject(head_gimbal)) {
        return false;
    }

    out.has_pan_trim = ReadInt(head_gimbal, "pan_trim", out.pan_trim);
    out.has_tilt_trim = ReadInt(head_gimbal, "tilt_trim", out.tilt_trim);
    out.has_pan_inverted = ReadBool(head_gimbal, "pan_inverted", out.pan_inverted);
    out.has_tilt_inverted = ReadBool(head_gimbal, "tilt_inverted", out.tilt_inverted);
    out.has_pan_min = ReadInt(head_gimbal, "pan_min", out.pan_min);
    out.has_pan_max = ReadInt(head_gimbal, "pan_max", out.pan_max);
    out.has_tilt_min = ReadInt(head_gimbal, "tilt_min", out.tilt_min);
    out.has_tilt_max = ReadInt(head_gimbal, "tilt_max", out.tilt_max);
    out.has_center_pan = ReadInt(head_gimbal, "center_pan", out.center_pan);
    out.has_center_tilt = ReadInt(head_gimbal, "center_tilt", out.center_tilt);
    out.has_center_duration_ms = ReadInt(head_gimbal, "center_duration_ms", out.center_duration_ms);

    return out.has_pan_trim ||
        out.has_tilt_trim ||
        out.has_pan_inverted ||
        out.has_tilt_inverted ||
        out.has_pan_min ||
        out.has_pan_max ||
        out.has_tilt_min ||
        out.has_tilt_max ||
        out.has_center_pan ||
        out.has_center_tilt ||
        out.has_center_duration_ms;
}
