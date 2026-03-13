#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "runtime_config.h"
#include "settings.h"

#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <utility>
#include <algorithm>
#include <cctype>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_vfs_fat.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>
#include <unistd.h>

#define TAG "Application"

namespace {
constexpr int kDefaultTtsFrameDurationMs = 60;
constexpr int kMaxTtsDrainWaitMs = 6000;
constexpr int kMinTtsDrainQuietMs = 80;
constexpr int kTtsDrainFrameQuietMultiplier = 2;

#if CONFIG_CONTINUE_LISTENING_AFTER_TTS_STOP
constexpr bool kDefaultContinueListeningAfterTtsStop = true;
#else
constexpr bool kDefaultContinueListeningAfterTtsStop = false;
#endif

#if CONFIG_POST_TTS_LISTEN_GUARD_MS > 0
constexpr int kDefaultPostTtsListenGuardMs = CONFIG_POST_TTS_LISTEN_GUARD_MS;
#else
constexpr int kDefaultPostTtsListenGuardMs = 0;
#endif

constexpr int kDefaultTtsDownlinkDrainQuietMs = CONFIG_TTS_DOWNLINK_DRAIN_QUIET_MS;

#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
constexpr bool kDefaultWakeWordDetectionInListening = true;
#else
constexpr bool kDefaultWakeWordDetectionInListening = false;
#endif
}  // namespace


Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif
    continue_listening_after_tts_stop_ = kDefaultContinueListeningAfterTtsStop;
    post_tts_listen_guard_ms_ = kDefaultPostTtsListenGuardMs;
    tts_downlink_drain_quiet_ms_ = kDefaultTtsDownlinkDrainQuietMs;
    wake_word_detection_in_listening_ = kDefaultWakeWordDetectionInListening;
    xiaozhi_compat_mode_ = false;

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    esp_timer_create_args_t listening_resume_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_RESUME_LISTENING);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "listen_resume_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&listening_resume_timer_args, &listening_resume_timer_handle_);
}

Application::~Application() {
    CancelPendingListeningResume();
    if (listening_resume_timer_handle_ != nullptr) {
        esp_timer_stop(listening_resume_timer_handle_);
        esp_timer_delete(listening_resume_timer_handle_);
    }
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    return state_machine_.TransitionTo(state);
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);
    ApplyRuntimeConfig();

    // Setup the display
    auto display = board.GetDisplay();
    display->SetupUI();
    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.SetMicSendGateEnabled(!xiaozhi_compat_mode_);
    audio_service_.EnableDeviceAec(aec_mode_ == kAecOnDeviceSide);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        
        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "triangle_exclamation", Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network asynchronously
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::ApplyRuntimeConfig() {
    RuntimeAudioConfig runtime_audio_config;
    if (RuntimeConfig::LoadAudioConfig(runtime_audio_config)) {
        Settings audio_settings("audio", true);
        int volume = runtime_audio_config.output_volume;
        int input_gain = runtime_audio_config.input_gain;
        bool input_reference = runtime_audio_config.input_reference;
        bool has_output = runtime_audio_config.has_output_volume;
        bool has_input_gain = runtime_audio_config.has_input_gain;
        bool has_input_reference = runtime_audio_config.has_input_reference;

        if (has_output) {
            if (volume < 1) {
                volume = 1;
            }
            if (volume > 100) {
                volume = 100;
            }
            if (audio_settings.GetInt("output_volume", -1) != volume) {
                audio_settings.SetInt("output_volume", volume);
            }
        }

        if (has_input_gain) {
            if (input_gain < 0) {
                input_gain = 0;
            }
            if (input_gain > 42) {
                input_gain = 42;
            }
            if (audio_settings.GetInt("input_gain", -1) != input_gain) {
                audio_settings.SetInt("input_gain", input_gain);
            }
        }

        if (has_input_reference) {
            if (audio_settings.GetBool("input_reference", input_reference) != input_reference) {
                audio_settings.SetBool("input_reference", input_reference);
            }
        }

        ESP_LOGI(
            TAG,
            "Runtime audio config applied from %s: has_output=%d output_volume=%d has_input_gain=%d input_gain=%d has_input_reference=%d input_reference=%d",
            RuntimeConfig::GetConfigPath(),
            has_output,
            volume,
            has_input_gain,
            input_gain,
            has_input_reference,
            input_reference
        );
    }

    RuntimeConversationConfig runtime_conversation_config;
    if (!RuntimeConfig::LoadConversationConfig(runtime_conversation_config)) {
        return;
    }

    if (runtime_conversation_config.has_continue_listening_after_tts_stop) {
        continue_listening_after_tts_stop_ = runtime_conversation_config.continue_listening_after_tts_stop;
    }

    if (runtime_conversation_config.has_post_tts_listen_guard_ms) {
        post_tts_listen_guard_ms_ = runtime_conversation_config.post_tts_listen_guard_ms;
        if (post_tts_listen_guard_ms_ < 0) {
            post_tts_listen_guard_ms_ = 0;
        }
    }

    if (runtime_conversation_config.has_tts_downlink_drain_quiet_ms) {
        tts_downlink_drain_quiet_ms_ = runtime_conversation_config.tts_downlink_drain_quiet_ms;
        if (tts_downlink_drain_quiet_ms_ < 0) {
            tts_downlink_drain_quiet_ms_ = 0;
        }
    }

    if (runtime_conversation_config.has_wake_word_detection_in_listening) {
        wake_word_detection_in_listening_ = runtime_conversation_config.wake_word_detection_in_listening;
    }
    if (runtime_conversation_config.has_xiaozhi_compat_mode) {
        xiaozhi_compat_mode_ = runtime_conversation_config.xiaozhi_compat_mode;
    }

    bool mic_send_gate_enabled = !xiaozhi_compat_mode_;
    if (runtime_conversation_config.has_mic_send_gate_enabled) {
        mic_send_gate_enabled = runtime_conversation_config.mic_send_gate_enabled;
    }
    audio_service_.SetMicSendGateEnabled(mic_send_gate_enabled);

    if (runtime_conversation_config.has_aec_mode) {
        aec_mode_ = ParseAecMode(runtime_conversation_config.aec_mode);
    }

    ESP_LOGI(TAG,
        "Runtime conversation config applied from %s: continue_after_tts=%d guard_ms=%d drain_quiet_ms=%d wake_word_in_listening=%d xiaozhi_compat=%d mic_send_gate=%d aec_mode=%d",
        RuntimeConfig::GetConfigPath(),
        continue_listening_after_tts_stop_,
        post_tts_listen_guard_ms_,
        tts_downlink_drain_quiet_ms_,
        wake_word_detection_in_listening_,
        xiaozhi_compat_mode_,
        mic_send_gate_enabled,
        static_cast<int>(aec_mode_));
}

AecMode Application::ParseAecMode(const std::string& mode) const {
    std::string normalized = mode;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "off") {
        return kAecOff;
    }
    if (normalized == "device" || normalized == "device_side" || normalized == "on_device") {
        return kAecOnDeviceSide;
    }
    if (normalized == "server" || normalized == "server_side") {
        return kAecOnServerSide;
    }

    ESP_LOGW(TAG, "Unknown runtime aec_mode: %s, keep default", mode.c_str());
    return aec_mode_;
}

void Application::Run() {
    // Set the priority of the main task to 10
    vTaskPrioritySet(nullptr, 10);

    const EventBits_t ALL_EVENTS = 
        MAIN_EVENT_SCHEDULE |
        MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE |
        MAIN_EVENT_CLOCK_TICK |
        MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED |
        MAIN_EVENT_NETWORK_DISCONNECTED |
        MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING |
        MAIN_EVENT_STOP_LISTENING |
        MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED |
        MAIN_EVENT_RESUME_LISTENING;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_RESUME_LISTENING) {
            HandleResumeListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();

            if (protocol_ != nullptr) {
                auto state = GetDeviceState();
                bool active_conversation_state =
                    (state == kDeviceStateListening) || (state == kDeviceStateSpeaking);
                bool stale_connecting =
                    (state == kDeviceStateConnecting) && (clock_ticks_ > 15);
                if ((active_conversation_state && !protocol_->IsAudioChannelOpened()) || stale_connecting) {
                    ESP_LOGW(TAG, "State %d unhealthy (ticks=%d), reset to idle", (int)state, clock_ticks_);
                    protocol_->CloseAudioChannel();
                    SetDeviceState(kDeviceStateIdle);
                }
            }
        
            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate([](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->ActivationTask();
            app->activation_task_handle_ = nullptr;
            vTaskDelete(NULL);
        }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = false;

    auto display = Board::GetInstance().GetDisplay();
    std::string message = std::string(Lang::Strings::VERSION) + firmware_version_;
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");

    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    Schedule([this]() {
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    });
}

void Application::ActivationTask() {
    // Check for new assets version
    CheckAssetsVersion();

    // Mark current image valid and collect local firmware version.
    CheckNewVersion();

    // Initialize the protocol
    InitializeProtocol();

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    // Disable remote assets upgrade path; only apply local flashed assets.
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

    const auto* app_desc = esp_app_get_description();
    if (app_desc != nullptr) {
        firmware_version_ = app_desc->version;
    } else {
        firmware_version_ = "unknown";
    }

    const auto* partition = esp_ota_get_running_partition();
    if (partition != nullptr && std::strcmp(partition->label, "factory") != 0) {
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(partition, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Marking running partition as valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    ESP_LOGI(TAG, "Cloud OTA and activation are disabled; firmware version: %s", firmware_version_.c_str());
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    Settings websocket_settings("websocket", false);
    RuntimeWebsocketConfig runtime_websocket_config;
    bool has_runtime_websocket = RuntimeConfig::LoadWebsocketConfig(runtime_websocket_config) &&
        runtime_websocket_config.has_url && !runtime_websocket_config.url.empty();
    bool has_persisted_websocket = !websocket_settings.GetString("url").empty();
    bool has_boot_default_websocket = std::strlen(CONFIG_BOOT_DEFAULT_WEBSOCKET_URL) > 0;
    ESP_LOGI(TAG,
        "Protocol select: websocket (runtime=%d persisted=%d boot_default=%d)",
        has_runtime_websocket, has_persisted_websocket, has_boot_default_websocket);
    protocol_ = std::make_unique<WebsocketProtocol>();

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (packet == nullptr) {
            return;
        }
        if (!ShouldAcceptIncomingTtsAudio()) {
            return;
        }
        NoteIncomingTtsAudio(*packet);
        if (!audio_service_.PushPacketToDecodeQueue(std::move(packet), true)) {
            ESP_LOGW(TAG, "Drop incoming audio packet: decode queue unavailable");
        }
    });
    
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    
    protocol_->OnIncomingJson([this](const cJSON* root) {
        HandleIncomingJsonMessage(root);
    });
    
    protocol_->Start();
}

void Application::HandleIncomingJsonMessage(const cJSON* root) {
    if (!cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Invalid incoming json payload");
        return;
    }

    auto type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        ESP_LOGW(TAG, "Missing message type");
        return;
    }

    if (strcmp(type->valuestring, "tts") == 0) {
        HandleIncomingTtsMessage(root);
        return;
    }
    if (strcmp(type->valuestring, "stt") == 0) {
        HandleIncomingSttMessage(root);
        return;
    }
    if (strcmp(type->valuestring, "llm") == 0) {
        HandleIncomingLlmMessage(root);
        return;
    }
    if (strcmp(type->valuestring, "mcp") == 0) {
        HandleIncomingMcpMessage(root);
        return;
    }
    if (strcmp(type->valuestring, "system") == 0) {
        HandleIncomingSystemMessage(root);
        return;
    }
    if (strcmp(type->valuestring, "alert") == 0) {
        HandleIncomingAlertMessage(root);
        return;
    }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
    if (strcmp(type->valuestring, "custom") == 0) {
        HandleIncomingCustomMessage(root);
        return;
    }
#endif

    ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
}

void Application::HandleIncomingTtsMessage(const cJSON* root) {
    auto state = cJSON_GetObjectItem(root, "state");
    if (!cJSON_IsString(state)) {
        ESP_LOGW(TAG, "TTS message missing state");
        return;
    }

    if (strcmp(state->valuestring, "start") == 0) {
        OpenTtsDownlinkWindow();
        Schedule([this]() {
            aborted_ = false;
            ESP_LOGI(TAG, "TTS start: speaking window opened");
            SetDeviceState(kDeviceStateSpeaking);
        });
        return;
    }

    if (strcmp(state->valuestring, "stop") == 0) {
        Schedule([this]() {
            if (GetDeviceState() == kDeviceStateSpeaking || tts_downlink_window_open_.load(std::memory_order_acquire)) {
                if (xiaozhi_compat_mode_) {
                    CancelPendingListeningResume();
                    CloseTtsDownlinkWindow();
                    if (GetDeviceState() != kDeviceStateSpeaking) {
                        return;
                    }
                    if (listening_mode_ == kListeningModeManualStop) {
                        SetDeviceState(kDeviceStateIdle);
                    } else {
                        SetDeviceState(kDeviceStateListening);
                    }
                    return;
                }

                CancelPendingListeningResume();
                ESP_LOGI(TAG, "TTS stop: draining playback before state transition");
                DrainTtsPlaybackUntilQuiet();
                CloseTtsDownlinkWindow();
                if (GetDeviceState() != kDeviceStateSpeaking) {
                    return;
                }

                if (listening_mode_ == kListeningModeManualStop) {
                    SetDeviceState(kDeviceStateIdle);
                } else {
                    // auto/realtime modes return to listening after TTS drains.
                    if (continue_listening_after_tts_stop_) {
                        ScheduleResumeListeningAfterGuard();
                    } else {
                        SetDeviceState(kDeviceStateIdle);
                    }
                }
            }
        });
        return;
    }

    if (strcmp(state->valuestring, "sentence_start") == 0) {
        auto text = cJSON_GetObjectItem(root, "text");
        if (!cJSON_IsString(text)) {
            return;
        }

        auto display = Board::GetInstance().GetDisplay();
        ESP_LOGI(TAG, "<< %s", text->valuestring);
        Schedule([display, message = std::string(text->valuestring)]() {
            display->SetChatMessage("assistant", message.c_str());
        });
        return;
    }
}

void Application::ScheduleResumeListeningAfterGuard() {
    if (post_tts_listen_guard_ms_ <= 0) {
        pending_resume_listening_ = false;
        SetDeviceState(kDeviceStateListening);
        return;
    }

    pending_resume_listening_ = true;
    if (listening_resume_timer_handle_ == nullptr) {
        ESP_LOGW(TAG, "Listening resume timer unavailable, skipping guard");
        pending_resume_listening_ = false;
        SetDeviceState(kDeviceStateListening);
        return;
    }

    esp_timer_stop(listening_resume_timer_handle_);
    uint64_t delay_us = (uint64_t)post_tts_listen_guard_ms_ * 1000ULL;
    auto err = esp_timer_start_once(listening_resume_timer_handle_, delay_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to schedule post-TTS listening guard (%d), resuming immediately", err);
        pending_resume_listening_ = false;
        SetDeviceState(kDeviceStateListening);
        return;
    }
    ESP_LOGI(TAG, "Post-TTS listening guard enabled: %d ms", post_tts_listen_guard_ms_);
}

void Application::CancelPendingListeningResume() {
    if (listening_resume_timer_handle_ != nullptr) {
        esp_timer_stop(listening_resume_timer_handle_);
    }
    pending_resume_listening_ = false;
}

bool Application::ShouldAcceptIncomingTtsAudio() const {
    if (xiaozhi_compat_mode_) {
        return true;
    }
    return tts_downlink_window_open_.load(std::memory_order_acquire);
}

void Application::OpenTtsDownlinkWindow() {
    last_tts_frame_duration_ms_.store(kDefaultTtsFrameDurationMs, std::memory_order_relaxed);
    last_tts_audio_at_us_.store(esp_timer_get_time(), std::memory_order_release);
    tts_downlink_window_open_.store(true, std::memory_order_release);
}

void Application::CloseTtsDownlinkWindow() {
    tts_downlink_window_open_.store(false, std::memory_order_release);
}

void Application::NoteIncomingTtsAudio(const AudioStreamPacket& packet) {
    if (packet.frame_duration > 0) {
        last_tts_frame_duration_ms_.store(packet.frame_duration, std::memory_order_relaxed);
    }
    last_tts_audio_at_us_.store(esp_timer_get_time(), std::memory_order_release);
}

void Application::DrainTtsPlaybackUntilQuiet() {
    int quiet_ms = tts_downlink_drain_quiet_ms_;
    int frame_ms = last_tts_frame_duration_ms_.load(std::memory_order_relaxed);
    if (frame_ms > 0) {
        int frame_based_quiet_ms = frame_ms * kTtsDrainFrameQuietMultiplier;
        if (frame_based_quiet_ms < kMinTtsDrainQuietMs) {
            frame_based_quiet_ms = kMinTtsDrainQuietMs;
        }
        if (frame_based_quiet_ms > quiet_ms) {
            quiet_ms = frame_based_quiet_ms;
        }
    }
    if (quiet_ms < 0) {
        quiet_ms = 0;
    }
    if (quiet_ms > kMaxTtsDrainWaitMs) {
        quiet_ms = kMaxTtsDrainWaitMs;
    }

    const int64_t quiet_us = (int64_t)quiet_ms * 1000LL;
    const int64_t max_wait_us = (int64_t)kMaxTtsDrainWaitMs * 1000LL;
    const int64_t started_us = esp_timer_get_time();
    ESP_LOGI(TAG, "TTS drain quiet window: %d ms", quiet_ms);

    while (tts_downlink_window_open_.load(std::memory_order_acquire)) {
        audio_service_.WaitForPlaybackQueueEmpty();
        if (quiet_us <= 0) {
            break;
        }

        int64_t now_us = esp_timer_get_time();
        int64_t last_audio_us = last_tts_audio_at_us_.load(std::memory_order_acquire);
        if (now_us - last_audio_us >= quiet_us) {
            break;
        }
        if (now_us - started_us >= max_wait_us) {
            ESP_LOGW(TAG, "TTS drain quiet window timeout after %d ms", kMaxTtsDrainWaitMs);
            break;
        }

        int64_t remaining_us = quiet_us - (now_us - last_audio_us);
        int delay_ms = (int)((remaining_us + 999) / 1000);
        if (delay_ms <= 0) {
            delay_ms = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    int64_t drained_ms = (esp_timer_get_time() - started_us) / 1000LL;
    ESP_LOGI(TAG, "TTS drain completed in %ld ms", static_cast<long>(drained_ms));
}

void Application::HandleResumeListeningEvent() {
    if (!pending_resume_listening_) {
        return;
    }
    pending_resume_listening_ = false;

    if (GetDeviceState() != kDeviceStateSpeaking) {
        return;
    }
    if (listening_mode_ == kListeningModeManualStop) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }
    SetDeviceState(kDeviceStateListening);
}

void Application::HandleIncomingSttMessage(const cJSON* root) {
    auto text = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(text)) {
        return;
    }

    auto display = Board::GetInstance().GetDisplay();
    ESP_LOGI(TAG, ">> %s", text->valuestring);
    Schedule([display, message = std::string(text->valuestring)]() {
        display->SetChatMessage("user", message.c_str());
    });
}

void Application::HandleIncomingLlmMessage(const cJSON* root) {
    auto emotion = cJSON_GetObjectItem(root, "emotion");
    if (!cJSON_IsString(emotion)) {
        return;
    }

    auto display = Board::GetInstance().GetDisplay();
    Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
        display->SetEmotion(emotion_str.c_str());
    });
}

void Application::HandleIncomingMcpMessage(const cJSON* root) {
    auto payload = cJSON_GetObjectItem(root, "payload");
    if (cJSON_IsObject(payload)) {
        McpServer::GetInstance().ParseMessage(payload);
    }
}

void Application::HandleIncomingSystemMessage(const cJSON* root) {
    auto command = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(command)) {
        return;
    }

    ESP_LOGI(TAG, "System command: %s", command->valuestring);
    if (strcmp(command->valuestring, "reboot") == 0) {
        Schedule([this]() {
            Reboot();
        });
        return;
    }

    if (strcmp(command->valuestring, "standby") == 0) {
        Schedule([this]() {
            ESP_LOGI(TAG, "Entering standby by system command");

            CancelPendingListeningResume();
            CloseTtsDownlinkWindow();
            audio_service_.ResetDecoder();

            auto state = GetDeviceState();
            if (state == kDeviceStateSpeaking) {
                AbortSpeaking(kAbortReasonNone);
            }

            if (protocol_ && protocol_->IsAudioChannelOpened()) {
                protocol_->CloseAudioChannel();
            }

            if (GetDeviceState() != kDeviceStateIdle) {
                if (!SetDeviceState(kDeviceStateIdle)) {
                    ESP_LOGW(TAG, "Failed to switch to idle for standby");
                }
            } else {
                DismissAlert();
            }
        });
        return;
    }

    if (strcmp(command->valuestring, "write_sdcard_runtime_config") == 0) {
        auto config = cJSON_GetObjectItem(root, "config");
        if (!cJSON_IsObject(config)) {
            ESP_LOGW(TAG, "write_sdcard_runtime_config requires object field: config");
            return;
        }
        bool reboot_after_write = cJSON_IsTrue(cJSON_GetObjectItem(root, "reboot"));

        char* rendered = cJSON_Print(config);
        if (rendered == nullptr) {
            ESP_LOGW(TAG, "Failed to serialize config JSON for SD write");
            return;
        }
        std::string config_json(rendered);
        cJSON_free(rendered);

        Schedule([this, config_json = std::move(config_json), reboot_after_write]() {
            HandleWriteSdCardRuntimeConfig(config_json, reboot_after_write);
        });
        return;
    }

    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
}

void Application::HandleWriteSdCardRuntimeConfig(const std::string& config_json, bool reboot_after_write) {
    if (!RuntimeConfig::IsSdCardRuntimeConfigEnabled()) {
        ESP_LOGW(TAG, "SD card runtime config is disabled in firmware; ignoring write request");
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification("SD config disabled");
        return;
    }

    const char* path = RuntimeConfig::GetConfigPath();
    const char* mount_point = "/sdcard";

    uint64_t fs_total_bytes = 0;
    uint64_t fs_free_bytes = 0;
    esp_err_t fs_info_err = esp_vfs_fat_info(mount_point, &fs_total_bytes, &fs_free_bytes);
    if (fs_info_err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount check failed for %s: %s", mount_point, esp_err_to_name(fs_info_err));
    } else {
        ESP_LOGI(
            TAG,
            "SD mount check ok: total=%u free=%u bytes",
            static_cast<unsigned int>(fs_total_bytes),
            static_cast<unsigned int>(fs_free_bytes)
        );
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        int saved_errno = errno;
        ESP_LOGW(
            TAG,
            "Failed to open runtime config for writing: %s errno=%d (%s)",
            path,
            saved_errno,
            strerror(saved_errno)
        );
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification("SD config write failed");
        return;
    }

    size_t expected = config_json.size();
    ssize_t written = write(fd, config_json.data(), expected);
    int flush_result = fsync(fd);
    close(fd);

    if (written < 0 || static_cast<size_t>(written) != expected || flush_result != 0) {
        int saved_errno = errno;
        ESP_LOGW(
            TAG,
            "Failed to write runtime config to %s (written=%d expected=%u flush=%d errno=%d %s)",
            path,
            static_cast<int>(written),
            static_cast<unsigned int>(expected),
            flush_result,
            saved_errno,
            strerror(saved_errno)
        );
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification("SD config write failed");
        return;
    }

    ESP_LOGI(TAG, "Runtime config saved to %s (%u bytes)", path, static_cast<unsigned int>(written));
    RuntimeConfig::InvalidateCache();
    ApplyRuntimeConfig();
    auto display = Board::GetInstance().GetDisplay();
    display->ShowNotification("SD config updated");

    if (reboot_after_write) {
        Reboot();
    }
}

void Application::HandleIncomingAlertMessage(const cJSON* root) {
    auto status = cJSON_GetObjectItem(root, "status");
    auto message = cJSON_GetObjectItem(root, "message");
    auto emotion = cJSON_GetObjectItem(root, "emotion");
    if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
        Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
        return;
    }

    ESP_LOGW(TAG, "Alert command requires status, message and emotion");
}

#if CONFIG_RECEIVE_CUSTOM_MESSAGE
void Application::HandleIncomingCustomMessage(const cJSON* root) {
    char* root_json = cJSON_PrintUnformatted(root);
    if (root_json != nullptr) {
        ESP_LOGI(TAG, "Received custom message: %s", root_json);
        cJSON_free(root_json);
    }

    auto payload = cJSON_GetObjectItem(root, "payload");
    if (!cJSON_IsObject(payload)) {
        ESP_LOGW(TAG, "Invalid custom message format: missing payload");
        return;
    }

    char* payload_json = cJSON_PrintUnformatted(payload);
    if (payload_json == nullptr) {
        ESP_LOGW(TAG, "Failed to serialize custom payload");
        return;
    }
    std::string payload_text(payload_json);
    cJSON_free(payload_json);

    auto display = Board::GetInstance().GetDisplay();
    Schedule([display, payload_str = std::move(payload_text)]() {
        display->SetChatMessage("system", payload_str.c_str());
    });
}
#endif

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, mode]() {
                ContinueOpenAudioChannel(mode);
            });
            return;
        }
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            return;
        }
    }

    SetListeningMode(mode);
}

void Application::HandleStartListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this]() {
                ContinueOpenAudioChannel(kListeningModeManualStop);
            });
            return;
        }
        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!protocol_) {
        return;
    }

    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    auto state = GetDeviceState();
    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s (state: %d)", wake_word.c_str(), (int)state);

    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();
        auto wake_word = audio_service_.GetLastWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update),
            // then continue with OpenAudioChannel which may block for ~1 second
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking || state == kDeviceStateListening) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
        // Drop stale uplink frames before opening a fresh wake-word turn.
        audio_service_.ResetUplinkStateForNewTurn();

        if (state == kDeviceStateListening) {
            protocol_->SendStartListening(GetDefaultListeningMode());
            audio_service_.ResetDecoder();
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            // Re-enable wake word detection as it was stopped by the detection itself
            audio_service_.EnableWakeWordDetection(true);
        } else {
            // Play popup sound and start listening again
            play_popup_on_listening_ = true;
            SetListeningMode(GetDefaultListeningMode());
        }
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            audio_service_.EnableWakeWordDetection(true);
            return;
        }
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);

    // Set flag to play popup sound after state changes to listening
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#else
    // Set flag to play popup sound after state changes to listening
    // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#endif
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    if (new_state != kDeviceStateSpeaking) {
        CancelPendingListeningResume();
        CloseTtsDownlinkWindow();
    }

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->ClearChatMessages();  // Clear messages first
            display->SetEmotion("neutral"); // Then set emotion (wechat mode checks child count)
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening: {
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // In realtime mode, each new listening window must always notify
            // the gateway to open a fresh turn; otherwise UI may stay in
            // listening while backend has no active turn.
            bool need_start_turn = play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning();
            if (listening_mode_ == kListeningModeRealtime) {
                need_start_turn = true;
            }

            if (need_start_turn) {
                // For auto mode, wait for playback queue to be empty before enabling voice processing
                // This prevents audio truncation when STOP arrives late due to network jitter
                if (listening_mode_ == kListeningModeAutoStop) {
                    audio_service_.WaitForPlaybackQueueEmpty();
                }

                // Drop stale uplink audio so the next turn starts from fresh mic data.
                audio_service_.ResetUplinkStateForNewTurn();

                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                bool restart_voice_processing =
                    play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning();
                if (listening_mode_ == kListeningModeRealtime) {
                    // Realtime mode opens many consecutive turns; force a clean capture window.
                    restart_voice_processing = true;
                }
                if (restart_voice_processing) {
                    audio_service_.EnableVoiceProcessing(true);
                }
            }

            // Wake word policy in listening mode is runtime configurable.
            audio_service_.EnableWakeWordDetection(
                wake_word_detection_in_listening_ && audio_service_.IsAfeWakeWord());
            
            // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        }
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

ListeningMode Application::GetDefaultListeningMode() const {
    return aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (state == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload = std::move(payload)]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::ResetProtocol() {
    Schedule([this]() {
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}
