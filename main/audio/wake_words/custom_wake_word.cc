#include "custom_wake_word.h"
#include "audio_service.h"
#include "system_info.h"
#include "assets.h"
#include "runtime_config.h"

#include <esp_log.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <esp_vad.h>
#include <cJSON.h>

#include <algorithm>
#include <cctype>

#define TAG "CustomWakeWord"

namespace {

std::string Trim(const std::string& value) {
    auto start = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    if (start >= end) {
        return "";
    }
    return std::string(start, end);
}

std::vector<std::string> SplitAndTrim(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= value.size()) {
        auto end = value.find(delimiter, start);
        auto token = Trim(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!token.empty()) {
            parts.push_back(token);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

std::string JoinCommandsForLog(const std::deque<CustomWakeWord::Command>& commands) {
    std::string joined;
    for (size_t i = 0; i < commands.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += commands[i].command;
    }
    return joined;
}

void AppendCommandVariants(std::deque<CustomWakeWord::Command>& commands,
                           const std::string& command_list,
                           const std::string& text,
                           const std::string& action,
                           const std::string& phoneme_list = "") {
    auto command_variants = SplitAndTrim(command_list, ';');
    auto phoneme_variants = SplitAndTrim(phoneme_list, ';');
    for (size_t i = 0; i < command_variants.size(); ++i) {
        CustomWakeWord::Command command = {
            .command = command_variants[i],
            .phonemes = i < phoneme_variants.size() ? phoneme_variants[i] : "",
            .text = text,
            .action = action,
        };
        commands.push_back(std::move(command));
    }
}

}  // namespace

CustomWakeWord::CustomWakeWord()
    : wake_word_pcm_(), wake_word_opus_() {
}

CustomWakeWord::~CustomWakeWord() {
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->destroy(afe_data_);
        afe_data_ = nullptr;
    }

    if (multinet_model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(multinet_model_data_);
        multinet_model_data_ = nullptr;
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    if (wake_word_encode_task_buffer_ != nullptr) {
        heap_caps_free(wake_word_encode_task_buffer_);
    }

    if (models_ != nullptr) {
        esp_srmodel_deinit(models_);
    }
}

void CustomWakeWord::ParseWakenetModelConfig() {
    // Read index.json
    auto& assets = Assets::GetInstance();
    void* ptr = nullptr;
    size_t size = 0;
    if (!assets.GetAssetData("index.json", ptr, size)) {
        ESP_LOGE(TAG, "Failed to read index.json");
        return;
    }
    cJSON* root = cJSON_ParseWithLength(static_cast<char*>(ptr), size);
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse index.json");
        return;
    }
    cJSON* multinet_model = cJSON_GetObjectItem(root, "multinet_model");
    if (cJSON_IsObject(multinet_model)) {
        cJSON* language = cJSON_GetObjectItem(multinet_model, "language");
        cJSON* duration = cJSON_GetObjectItem(multinet_model, "duration");
        cJSON* threshold = cJSON_GetObjectItem(multinet_model, "threshold");
        cJSON* commands = cJSON_GetObjectItem(multinet_model, "commands");
        if (cJSON_IsString(language)) {
            language_ = language->valuestring;
        }
        if (cJSON_IsNumber(duration)) {
            duration_ = duration->valueint;
        }
        if (cJSON_IsNumber(threshold)) {
            threshold_ = threshold->valuedouble;
        }
        if (cJSON_IsArray(commands)) {
            for (int i = 0; i < cJSON_GetArraySize(commands); i++) {
                cJSON* command = cJSON_GetArrayItem(commands, i);
                if (cJSON_IsObject(command)) {
                    cJSON* command_name = cJSON_GetObjectItem(command, "command");
                    cJSON* phonemes = cJSON_GetObjectItem(command, "phonemes");
                    cJSON* text = cJSON_GetObjectItem(command, "text");
                    cJSON* action = cJSON_GetObjectItem(command, "action");
                    if (cJSON_IsString(command_name) && cJSON_IsString(text) && cJSON_IsString(action)) {
                        Command parsed_command = {
                            .command = command_name->valuestring,
                            .phonemes = cJSON_IsString(phonemes) ? phonemes->valuestring : "",
                            .text = text->valuestring,
                            .action = action->valuestring,
                        };
                        commands_.push_back(std::move(parsed_command));
                        ESP_LOGI(TAG, "Command: %s, Text: %s, Action: %s", command_name->valuestring, text->valuestring, action->valuestring);
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}


bool CustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;
    commands_.clear();
    threshold_ = 0.2f;

    if (models_list == nullptr) {
        language_ = "cn";
        models_ = esp_srmodel_init("model");
#ifdef CONFIG_CUSTOM_WAKE_WORD
        threshold_ = CONFIG_CUSTOM_WAKE_WORD_THRESHOLD / 100.0f;
        AppendCommandVariants(commands_, CONFIG_CUSTOM_WAKE_WORD, CONFIG_CUSTOM_WAKE_WORD_DISPLAY, "wake",
#ifdef CONFIG_CUSTOM_WAKE_WORD_PHONEMES
            CONFIG_CUSTOM_WAKE_WORD_PHONEMES
#else
            ""
#endif
        );
#endif
    } else {
        models_ = models_list;
        ParseWakenetModelConfig();
    }

    RuntimeWakeWordConfig runtime_wake_word_config;
    if (RuntimeConfig::LoadWakeWordConfig(runtime_wake_word_config)) {
        if (runtime_wake_word_config.has_threshold) {
            int threshold_percent = runtime_wake_word_config.threshold;
            if (threshold_percent < 1) {
                threshold_percent = 1;
            }
            if (threshold_percent > 99) {
                threshold_percent = 99;
            }
            threshold_ = threshold_percent / 100.0f;
        }

        if (runtime_wake_word_config.has_commands && !runtime_wake_word_config.commands.empty()) {
            std::string display = runtime_wake_word_config.has_display && !runtime_wake_word_config.display.empty()
                ? runtime_wake_word_config.display
                : runtime_wake_word_config.commands;
            std::string phonemes = runtime_wake_word_config.has_phonemes
                ? runtime_wake_word_config.phonemes
                : "";

            commands_.clear();
            AppendCommandVariants(commands_, runtime_wake_word_config.commands, display, "wake", phonemes);
            ESP_LOGI(TAG, "Using runtime custom wake words from %s", RuntimeConfig::GetConfigPath());
        }
    }

    if (commands_.empty()) {
        ESP_LOGE(TAG, "No custom wake words configured");
        return false;
    }
    ESP_LOGI(TAG, "Custom wake word variants: %s", JoinCommandsForLog(commands_).c_str());

    if (models_ == nullptr || models_->num == -1) {
        ESP_LOGE(TAG, "Failed to initialize wakenet model");
        return false;
    }

    // 初始化 multinet (命令词识别)
    mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (mn_name_ == nullptr) {
        ESP_LOGW(TAG, "Language '%s' multinet not found, falling back to any multinet model", language_.c_str());
        mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, NULL);
    }
    if (mn_name_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize multinet, mn_name is nullptr");
        ESP_LOGI(TAG, "Please refer to https://pcn7cs20v8cr.feishu.cn/wiki/CpQjwQsCJiQSWSkYEvrcxcbVnwh to add custom wake word");
        return false;
    }

    multinet_ = esp_mn_handle_from_name(mn_name_);
    multinet_model_data_ = multinet_->create(mn_name_, duration_);
    multinet_->set_det_threshold(multinet_model_data_, threshold_);

    esp_err_t alloc_ret = esp_mn_commands_alloc(multinet_, multinet_model_data_);
    if (alloc_ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Speech command list already initialized, recreating it");
        esp_mn_commands_free();
        alloc_ret = esp_mn_commands_alloc(multinet_, multinet_model_data_);
    }
    if (alloc_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate speech commands, error=%s", esp_err_to_name(alloc_ret));
        return false;
    }

    auto add_command = [](int id, const Command& cmd) -> esp_err_t {
        return cmd.phonemes.empty()
            ? esp_mn_commands_add(id, cmd.command.c_str())
            : esp_mn_commands_phoneme_add(id, cmd.command.c_str(), cmd.phonemes.c_str());
    };

    // Validate every command first. Invalid aliases should not make wake-word
    // initialization fail entirely.
    std::deque<Command> valid_commands;
    for (const auto& command : commands_) {
        esp_mn_commands_clear();
        esp_err_t ret = add_command(1, command);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Skip invalid wake command '%s': %s",
                command.command.c_str(), esp_err_to_name(ret));
            continue;
        }
        if (auto* err = esp_mn_commands_update(); err != nullptr) {
            ESP_LOGW(TAG, "Skip bad wake phrase '%s', bad_phrase_count=%d",
                command.command.c_str(), err->num);
            for (int i = 0; i < err->num; ++i) {
                auto* phrase = err->phrases[i];
                ESP_LOGW(TAG, "Bad phrase[%d]: command=%s phonemes=%s", i,
                    phrase != nullptr && phrase->string != nullptr ? phrase->string : "(null)",
                    phrase != nullptr && phrase->phonemes != nullptr ? phrase->phonemes : "(null)");
            }
            continue;
        }
        valid_commands.push_back(command);
    }

    if (valid_commands.empty()) {
        ESP_LOGE(TAG, "No valid wake commands after validation");
        return false;
    }
    if (valid_commands.size() != commands_.size()) {
        ESP_LOGW(TAG, "Filtered wake commands: %d -> %d",
            (int)commands_.size(), (int)valid_commands.size());
    }
    commands_.swap(valid_commands);

    // Rebuild command graph with validated commands.
    esp_mn_commands_clear();
    for (int i = 0; i < commands_.size(); i++) {
        esp_err_t ret = add_command(i + 1, commands_[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add command: %s (%s)", commands_[i].command.c_str(), esp_err_to_name(ret));
            return false;
        }
    }
    if (auto* err = esp_mn_commands_update(); err != nullptr) {
        ESP_LOGE(TAG, "Failed to update speech command graph, bad_phrase_count=%d", err->num);
        for (int i = 0; i < err->num; ++i) {
            auto* phrase = err->phrases[i];
            ESP_LOGE(TAG, "Bad phrase[%d]: command=%s phonemes=%s", i,
                phrase != nullptr && phrase->string != nullptr ? phrase->string : "(null)",
                phrase != nullptr && phrase->phonemes != nullptr ? phrase->phonemes : "(null)");
        }
        return false;
    }
    ESP_LOGI(TAG, "Speech command graph updated, variant_count=%d threshold=%.2f", (int)commands_.size(), threshold_);

    int ref_num = codec_->input_reference() ? 1 : 0;
    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; ++i) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; ++i) {
        input_format.push_back('R');
    }
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), models_, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_config->aec_init = codec_->input_reference();
    afe_config->aec_mode = AEC_MODE_SR_HIGH_PERF;
    afe_config->vad_init = true;
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    char* ns_model_name = esp_srmodel_filter(models_, ESP_NSNET_PREFIX, NULL);
    if (ns_model_name != nullptr) {
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
    } else {
        afe_config->ns_init = false;
    }

    afe_iface_ = esp_afe_handle_from_config(afe_config);
    if (afe_iface_ != nullptr) {
        afe_data_ = afe_iface_->create_from_config(afe_config);
    }
    if (afe_data_ != nullptr) {
        xTaskCreate([](void* arg) {
            auto self = static_cast<CustomWakeWord*>(arg);
            self->AudioDetectionTask();
            vTaskDelete(NULL);
        }, "custom_ww_detect", 4096, this, 3, nullptr);
        ESP_LOGI(TAG, "AFE front-end enabled for custom wake word");
    } else {
        ESP_LOGW(TAG, "AFE front-end unavailable, falling back to raw audio for custom wake word");
    }
    
    multinet_->print_active_speech_commands(multinet_model_data_);
    return true;
}

void CustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void CustomWakeWord::Start() {
    running_ = true;
    ResetAfeSpeechDebugState();
}

void CustomWakeWord::Stop() {
    running_ = false;
    ResetAfeSpeechDebugState();

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
    if (multinet_model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->clean(multinet_model_data_);
    }
    input_buffer_.clear();
    afe_input_buffer_.clear();
    detect_buffer_.clear();
}

void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (multinet_model_data_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    // Check running state inside lock to avoid TOCTOU race with Stop()
    if (!running_) {
        return;
    }

    if (afe_data_ != nullptr) {
        afe_input_buffer_.insert(afe_input_buffer_.end(), data.begin(), data.end());
        size_t feed_chunk = afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
        while (afe_input_buffer_.size() >= feed_chunk) {
            afe_iface_->feed(afe_data_, afe_input_buffer_.data());
            afe_input_buffer_.erase(afe_input_buffer_.begin(), afe_input_buffer_.begin() + feed_chunk);
        }
        return;
    }

    // If input channels is 2, we need to fetch the left channel data
    if (codec_->input_channels() == 2) {
        for (size_t i = 0; i < data.size(); i += 2) {
            input_buffer_.push_back(data[i]);
        }
    } else {
        input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
    }
    
    DetectFromBuffer(input_buffer_);
}

size_t CustomWakeWord::GetFeedSize() {
    if (afe_data_ != nullptr) {
        return afe_iface_->get_feed_chunksize(afe_data_);
    }
    if (multinet_model_data_ == nullptr) {
        return 0;
    }
    return multinet_->get_samp_chunksize(multinet_model_data_);
}

void CustomWakeWord::DetectFromBuffer(std::vector<int16_t>& buffer, size_t max_chunks) {
    if (multinet_model_data_ == nullptr) {
        return;
    }

    int chunksize = multinet_->get_samp_chunksize(multinet_model_data_);
    if (chunksize <= 0 || buffer.empty()) {
        return;
    }
    size_t chunk_size = static_cast<size_t>(chunksize);
    size_t processed_chunks = 0;
    size_t consumed = 0;
    while (buffer.size() - consumed >= chunk_size) {
        auto begin = buffer.begin() + static_cast<std::ptrdiff_t>(consumed);
        std::vector<int16_t> chunk(begin, begin + chunksize);
        StoreWakeWordData(chunk);

        esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, chunk.data());

        if (mn_state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *mn_result = multinet_->get_results(multinet_model_data_);
            for (int i = 0; i < mn_result->num && running_; i++) {
                ESP_LOGI(TAG, "Custom wake word detected: command_id=%d, string=%s, prob=%f",
                    mn_result->command_id[i], mn_result->string, mn_result->prob[i]);
                auto& command = commands_[mn_result->command_id[i] - 1];
                if (command.action == "wake") {
                    last_detected_wake_word_ = command.text;
                    running_ = false;
                    input_buffer_.clear();
                    afe_input_buffer_.clear();
                    detect_buffer_.clear();

                    if (wake_word_detected_callback_) {
                        wake_word_detected_callback_(last_detected_wake_word_);
                    }
                }
            }
            multinet_->clean(multinet_model_data_);
        } else if (mn_state == ESP_MN_STATE_TIMEOUT) {
            ESP_LOGD(TAG, "Command word detection timeout, cleaning state");
            multinet_->clean(multinet_model_data_);
        }

        if (!running_) {
            break;
        }
        consumed += chunk_size;
        ++processed_chunks;
        if (max_chunks > 0 && processed_chunks >= max_chunks) {
            break;
        }
    }
    if (consumed > 0) {
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
    }
}

void CustomWakeWord::AudioDetectionTask() {
    int fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    ESP_LOGI(TAG, "Custom wake word AFE task started, feed size: %d fetch size: %d",
        afe_iface_->get_feed_chunksize(afe_data_), fetch_size);

    while (true) {
        if (!running_) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        auto res = afe_iface_->fetch_with_delay(afe_data_, pdMS_TO_TICKS(100));
        if (!running_ || res == nullptr || res->ret_value == ESP_FAIL) {
            continue;
        }

        if (res->vad_state == VAD_SPEECH) {
            if (!afe_speech_active_) {
                afe_speech_active_ = true;
                afe_speech_frames_ = 0;
                afe_speech_peak_volume_db_ = res->data_volume;
                ESP_LOGI(TAG, "AFE speech started: vad=%d volume=%.1f", res->vad_state, res->data_volume);
            }
            ++afe_speech_frames_;
            afe_speech_peak_volume_db_ = std::max(afe_speech_peak_volume_db_, res->data_volume);
        } else if (afe_speech_active_) {
            ESP_LOGI(TAG, "AFE speech ended: frames=%d peak_volume=%.1f", afe_speech_frames_, afe_speech_peak_volume_db_);
            ResetAfeSpeechDebugState();
        }

        {
            std::lock_guard<std::mutex> lock(input_buffer_mutex_);
            auto samples = res->data_size / sizeof(int16_t);
            detect_buffer_.insert(detect_buffer_.end(), res->data, res->data + samples);
            // Bound each loop's work to avoid starving IDLE0 and triggering task watchdog.
            DetectFromBuffer(detect_buffer_, 3);
        }
        // Always yield a tiny timeslice; this task can become compute-heavy when background
        // noise causes continuous fetches and would otherwise starve IDLE0.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void CustomWakeWord::ResetAfeSpeechDebugState() {
    afe_speech_active_ = false;
    afe_speech_frames_ = 0;
    afe_speech_peak_volume_db_ = -120.0f;
}

void CustomWakeWord::StoreWakeWordData(const std::vector<int16_t>& data) {
    // store audio data to wake_word_pcm_
    wake_word_pcm_.push_back(data);
    // keep about 2 seconds of data, detect duration is 30ms (sample_rate == 16000, chunksize == 512)
    while (wake_word_pcm_.size() > 2000 / 30) {
        wake_word_pcm_.pop_front();
    }
}

void CustomWakeWord::EncodeWakeWordData() {
    const size_t stack_size = 4096 * 7;
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
        assert(wake_word_encode_task_stack_ != nullptr);
    }
    if (wake_word_encode_task_buffer_ == nullptr) {
        wake_word_encode_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
        assert(wake_word_encode_task_buffer_ != nullptr);
    }

    wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
        auto this_ = (CustomWakeWord*)arg;
        {
            auto start_time = esp_timer_get_time();
            // Create encoder
            esp_opus_enc_config_t opus_enc_cfg = AS_OPUS_ENC_CONFIG();
            void* encoder_handle = nullptr;
            auto ret = esp_opus_enc_open(&opus_enc_cfg, sizeof(esp_opus_enc_config_t), &encoder_handle);
            if (encoder_handle == nullptr) {
                ESP_LOGE(TAG, "Failed to create audio encoder, error code: %d", ret);
                std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                this_->wake_word_opus_.push_back(std::vector<uint8_t>());
                this_->wake_word_cv_.notify_all();
                return;
            }
            // Get frame size
            int frame_size = 0;
            int outbuf_size = 0;
            esp_opus_enc_get_frame_size(encoder_handle, &frame_size, &outbuf_size);
            frame_size = frame_size / sizeof(int16_t);
            // Encode all PCM data
            int packets = 0;
            std::vector<int16_t> in_buffer;
            esp_audio_enc_in_frame_t in = {};
            esp_audio_enc_out_frame_t out = {};
            for (auto& pcm: this_->wake_word_pcm_) {
                if (in_buffer.empty()) {
                    in_buffer = std::move(pcm);
                } else {
                    in_buffer.reserve(in_buffer.size() + pcm.size());
                    in_buffer.insert(in_buffer.end(), pcm.begin(), pcm.end());
                }
                while (in_buffer.size() >= frame_size) {
                    std::vector<uint8_t> opus_buf(outbuf_size);
                    in.buffer = (uint8_t *)(in_buffer.data());
                    in.len = (uint32_t)(frame_size * sizeof(int16_t));
                    out.buffer = opus_buf.data();
                    out.len = outbuf_size;
                    out.encoded_bytes = 0;
                    ret = esp_opus_enc_process(encoder_handle, &in, &out);
                    if (ret == ESP_AUDIO_ERR_OK) {
                        std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                        this_->wake_word_opus_.emplace_back(opus_buf.data(), opus_buf.data() + out.encoded_bytes);
                        this_->wake_word_cv_.notify_all();
                        packets++;
                    } else {
                        ESP_LOGE(TAG, "Failed to encode audio, error code: %d", ret);
                    }
                    in_buffer.erase(in_buffer.begin(), in_buffer.begin() + frame_size);
                }
            }
            this_->wake_word_pcm_.clear();
            // Close encoder
            esp_opus_enc_close(encoder_handle);
            auto end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Encode wake word opus %d packets in %ld ms", packets, (long)((end_time - start_time) / 1000));

            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
        }
        vTaskDelete(NULL);
    }, "encode_wake_word", stack_size, this, 2, wake_word_encode_task_stack_, wake_word_encode_task_buffer_);
}

bool CustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
