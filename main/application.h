#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <string_view>
#include <mutex>
#include <deque>
#include <memory>
#include <atomic>

#include "protocol.h"
#include "audio_service.h"
#include "device_state.h"
#include "device_state_machine.h"

struct cJSON;

// Main event bits
#define MAIN_EVENT_SCHEDULE             (1 << 0)
#define MAIN_EVENT_SEND_AUDIO           (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED   (1 << 2)
#define MAIN_EVENT_VAD_CHANGE           (1 << 3)
#define MAIN_EVENT_ERROR                (1 << 4)
#define MAIN_EVENT_ACTIVATION_DONE      (1 << 5)
#define MAIN_EVENT_CLOCK_TICK           (1 << 6)
#define MAIN_EVENT_NETWORK_CONNECTED    (1 << 7)
#define MAIN_EVENT_NETWORK_DISCONNECTED (1 << 8)
#define MAIN_EVENT_TOGGLE_CHAT          (1 << 9)
#define MAIN_EVENT_START_LISTENING      (1 << 10)
#define MAIN_EVENT_STOP_LISTENING       (1 << 11)
#define MAIN_EVENT_STATE_CHANGED        (1 << 12)
#define MAIN_EVENT_RESUME_LISTENING     (1 << 13)


enum AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // Delete copy constructor and assignment operator
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /**
     * Initialize the application
     * This sets up display, audio, network callbacks, etc.
     * Network connection starts asynchronously.
     */
    void Initialize();

    /**
     * Run the main event loop
     * This function runs in the main task and never returns.
     * It handles all events including network, state changes, and user interactions.
     */
    void Run();

    DeviceState GetDeviceState() const { return state_machine_.GetState(); }
    bool IsVoiceDetected() const { return audio_service_.IsVoiceDetected(); }
    
    /**
     * Request state transition
     * Returns true if transition was successful
     */
    bool SetDeviceState(DeviceState state);

    /**
     * Schedule a callback to be executed in the main task
     */
    void Schedule(std::function<void()>&& callback);

    /**
     * Alert with status, message, emotion and optional sound
     */
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();

    void AbortSpeaking(AbortReason reason);

    /**
     * Toggle chat state (event-based, thread-safe)
     * Sends MAIN_EVENT_TOGGLE_CHAT to be handled in Run()
     */
    void ToggleChatState();

    /**
     * Start listening (event-based, thread-safe)
     * Sends MAIN_EVENT_START_LISTENING to be handled in Run()
     */
    void StartListening();

    /**
     * Stop listening (event-based, thread-safe)
     * Sends MAIN_EVENT_STOP_LISTENING to be handled in Run()
     */
    void StopListening();

    void Reboot();
    void WakeWordInvoke(const std::string& wake_word);
    bool CanEnterSleepMode();
    void SendMcpMessage(const std::string& payload);
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const { return aec_mode_; }
    void PlaySound(const std::string_view& sound);
    AudioService& GetAudioService() { return audio_service_; }
    bool IsWakeWordAckInProgress() const { return wake_word_ack_in_progress_.load(std::memory_order_acquire); }
    
    /**
     * Reset protocol resources (thread-safe)
     * Can be called from any task to release resources allocated after network connected
     * This includes closing audio channel and resetting protocol objects
     */
    void ResetProtocol();

private:
    Application();
    ~Application();

    std::mutex mutex_;
    std::deque<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    DeviceStateMachine state_machine_;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    AecMode aec_mode_ = kAecOff;
    std::string last_error_message_;
    AudioService audio_service_;

    bool has_server_time_ = false;
    bool aborted_ = false;
    bool assets_version_checked_ = false;
    std::string firmware_version_;
    bool pending_resume_listening_ = false;
    bool force_start_turn_on_listening_ = false;
    std::atomic<bool> wake_word_ack_in_progress_{false};
    std::atomic<bool> tts_downlink_window_open_{false};
    std::atomic<int64_t> last_tts_audio_at_us_{0};
    std::atomic<int> last_tts_frame_duration_ms_{60};
    int clock_ticks_ = 0;
    TaskHandle_t activation_task_handle_ = nullptr;
    esp_timer_handle_t listening_resume_timer_handle_ = nullptr;


    // Event handlers
    void HandleStateChangedEvent();
    void HandleToggleChatEvent();
    void HandleStartListeningEvent();
    void HandleStopListeningEvent();
    void HandleResumeListeningEvent();
    void HandleNetworkConnectedEvent();
    void HandleNetworkDisconnectedEvent();
    void HandleActivationDoneEvent();
    void HandleWakeWordDetectedEvent();
    void ContinueOpenAudioChannel(ListeningMode mode);
    void ContinueWakeWordInvoke(const std::string& wake_word);
    void BeginWakeWordListeningSequence(ListeningMode mode);
    void PlayWakeWordAckAndEnterListening();

    // Activation task (runs in background)
    void ActivationTask();

    // Helper methods
    void CheckAssetsVersion();
    void CheckNewVersion();
    void InitializeProtocol();
    void ApplyRuntimeConfig();
    void SetListeningMode(ListeningMode mode);
    ListeningMode GetDefaultListeningMode() const;
    void ScheduleResumeListeningAfterGuard();
    void CancelPendingListeningResume();
    bool ShouldAcceptIncomingTtsAudio() const;
    const char* ResolveFaceForState(DeviceState state) const;
    void ApplyFaceForState(DeviceState state);
    void OpenTtsDownlinkWindow();
    void CloseTtsDownlinkWindow();
    void NoteIncomingTtsAudio(const AudioStreamPacket& packet);
    void DrainTtsPlaybackUntilQuiet();

    // Server event dispatch (JSON downlink)
    void HandleIncomingJsonMessage(const cJSON* root);
    void HandleIncomingTtsMessage(const cJSON* root);
    void HandleIncomingSttMessage(const cJSON* root);
    void HandleIncomingLlmMessage(const cJSON* root);
    void HandleIncomingMcpMessage(const cJSON* root);
    void HandleIncomingSystemMessage(const cJSON* root);
    void HandleWriteSdCardRuntimeConfig(const std::string& config_json, bool reboot_after_write);
    void HandleIncomingAlertMessage(const cJSON* root);
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
    void HandleIncomingCustomMessage(const cJSON* root);
#endif
    
    // State change handler called by state machine
    void OnStateChanged(DeviceState old_state, DeviceState new_state);
    AecMode ParseAecMode(const std::string& mode) const;

    bool continue_listening_after_tts_stop_ = true;
    int post_tts_listen_guard_ms_ = 0;
    int tts_downlink_drain_quiet_ms_ = 0;
    bool wake_word_detection_in_listening_ = false;
    bool xiaozhi_compat_mode_ = false;
};


class TaskPriorityReset {
public:
    TaskPriorityReset(BaseType_t priority) {
        original_priority_ = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, priority);
    }
    ~TaskPriorityReset() {
        vTaskPrioritySet(NULL, original_priority_);
    }

private:
    BaseType_t original_priority_;
};

#endif // _APPLICATION_H_
