#include "head_gimbal.h"

#include "mcp_server.h"
#include "settings.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
constexpr const char* TAG = "HeadGimbal";
constexpr int kCenterAngle = 90;
constexpr int kLookAtPanRange = 40;
constexpr int kLookAtTiltRange = 20;
constexpr int kMotionStepMs = 20;
}  // namespace

HeadGimbal::HeadGimbal(gpio_num_t pan_pin, gpio_num_t tilt_pin, ledc_timer_t timer,
                       ledc_channel_t pan_channel, ledc_channel_t tilt_channel,
                       const std::string& settings_ns)
    : settings_ns_(settings_ns), timer_(timer) {
    pan_.pin = pan_pin;
    pan_.channel = pan_channel;
    tilt_.pin = tilt_pin;
    tilt_.channel = tilt_channel;
    tilt_.min_angle = 60;
    tilt_.max_angle = 120;

    LoadSettings();
    InitializePwm();

    if (ready_) {
        Center();
    }
}

HeadGimbal::~HeadGimbal() {
    if (pan_.pin != GPIO_NUM_NC) {
        ledc_stop(LEDC_LOW_SPEED_MODE, pan_.channel, 0);
    }
    if (tilt_.pin != GPIO_NUM_NC) {
        ledc_stop(LEDC_LOW_SPEED_MODE, tilt_.channel, 0);
    }
}

bool HeadGimbal::IsReady() const {
    return ready_;
}

void HeadGimbal::RegisterMcpTools(const std::string& tool_prefix) {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(tool_prefix + ".center", "Move the head back to center position",
                       PropertyList(), [this](const PropertyList&) -> ReturnValue {
                           Center();
                           return true;
                       });

    mcp_server.AddTool(
        tool_prefix + ".move",
        "Move the head to absolute pan/tilt angles in degrees. Both values are logical angles before trim/invert.",
        PropertyList({Property("pan", kPropertyTypeInteger, 90, 0, 180),
                      Property("tilt", kPropertyTypeInteger, 90, 0, 180),
                      Property("duration_ms", kPropertyTypeInteger, 200, 0, 3000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            MoveTo(properties["pan"].value<int>(), properties["tilt"].value<int>(),
                   properties["duration_ms"].value<int>());
            return true;
        });

    mcp_server.AddTool(
        tool_prefix + ".move_by",
        "Move the head by relative delta angles in degrees.",
        PropertyList({Property("delta_pan", kPropertyTypeInteger, 0, -90, 90),
                      Property("delta_tilt", kPropertyTypeInteger, 0, -90, 90),
                      Property("duration_ms", kPropertyTypeInteger, 150, 0, 3000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            MoveBy(properties["delta_pan"].value<int>(), properties["delta_tilt"].value<int>(),
                   properties["duration_ms"].value<int>());
            return true;
        });

    mcp_server.AddTool(
        tool_prefix + ".look_at",
        "Map normalized image offsets to pan/tilt motion. x_percent: -100 means target is far left, 100 means far right. y_percent: -100 means target is high, 100 means low.",
        PropertyList({Property("x_percent", kPropertyTypeInteger, 0, -100, 100),
                      Property("y_percent", kPropertyTypeInteger, 0, -100, 100),
                      Property("duration_ms", kPropertyTypeInteger, 150, 0, 3000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            LookAt(properties["x_percent"].value<int>(), properties["y_percent"].value<int>(),
                   properties["duration_ms"].value<int>());
            return true;
        });

    mcp_server.AddTool(
        tool_prefix + ".set_trim",
        "Set pan or tilt trim in degrees and save it to NVS.",
        PropertyList({Property("axis", kPropertyTypeString, "pan"),
                      Property("trim", kPropertyTypeInteger, 0, -90, 90)}),
        [this](const PropertyList& properties) -> ReturnValue {
            SetTrim(properties["axis"].value<std::string>(), properties["trim"].value<int>());
            return CreateTrimJson();
        });

    mcp_server.AddTool(tool_prefix + ".get_trim", "Get current pan/tilt trim values", PropertyList(),
                       [this](const PropertyList&) -> ReturnValue { return CreateTrimJson(); });

    mcp_server.AddTool(
        tool_prefix + ".set_direction",
        "Invert or restore one servo axis. Use this if the physical installation makes the axis move in the opposite direction.",
        PropertyList({Property("axis", kPropertyTypeString, "pan"),
                      Property("inverted", kPropertyTypeBoolean, false)}),
        [this](const PropertyList& properties) -> ReturnValue {
            SetInverted(properties["axis"].value<std::string>(),
                        properties["inverted"].value<bool>());
            return CreateStatusJson();
        });

    mcp_server.AddTool(
        tool_prefix + ".set_limit",
        "Set servo limits in degrees for one axis and save them to NVS.",
        PropertyList({Property("axis", kPropertyTypeString, "pan"),
                      Property("min_angle", kPropertyTypeInteger, 20, 0, 180),
                      Property("max_angle", kPropertyTypeInteger, 160, 0, 180)}),
        [this](const PropertyList& properties) -> ReturnValue {
            SetLimits(properties["axis"].value<std::string>(),
                      properties["min_angle"].value<int>(),
                      properties["max_angle"].value<int>());
            return CreateStatusJson();
        });

    mcp_server.AddTool(
        tool_prefix + ".shake",
        "Shake the head left and right (no/disagreement gesture).",
        PropertyList({Property("cycles", kPropertyTypeInteger, 2, 1, 5),
                      Property("amplitude", kPropertyTypeInteger, 30, 5, 60),
                      Property("period_ms", kPropertyTypeInteger, 600, 200, 2000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            Shake(properties["cycles"].value<int>(), properties["amplitude"].value<int>(),
                  properties["period_ms"].value<int>());
            return true;
        });

    mcp_server.AddTool(
        tool_prefix + ".nod",
        "Nod the head up and down (yes/agreement gesture).",
        PropertyList({Property("cycles", kPropertyTypeInteger, 2, 1, 5),
                      Property("amplitude", kPropertyTypeInteger, 15, 5, 40),
                      Property("period_ms", kPropertyTypeInteger, 500, 200, 2000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            Nod(properties["cycles"].value<int>(), properties["amplitude"].value<int>(),
                properties["period_ms"].value<int>());
            return true;
        });

    mcp_server.AddTool(tool_prefix + ".get_status",
                       "Get current head servo status, trims, limits and pins", PropertyList(),
                       [this](const PropertyList&) -> ReturnValue { return CreateStatusJson(); });
}

void HeadGimbal::Center(int duration_ms) {
    MoveTo(kCenterAngle, kCenterAngle, duration_ms);
}

void HeadGimbal::MoveTo(int pan_angle, int tilt_angle, int duration_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    MoveUnlocked(pan_angle, tilt_angle, duration_ms);
}

void HeadGimbal::MoveBy(int delta_pan, int delta_tilt, int duration_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    MoveUnlocked(pan_.logical_angle + delta_pan, tilt_.logical_angle + delta_tilt, duration_ms);
}

void HeadGimbal::Shake(int cycles, int amplitude, int period_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_) return;
    int half = std::max(kMotionStepMs, period_ms / 2);
    int current_tilt = tilt_.logical_angle;
    for (int i = 0; i < cycles; ++i) {
        MoveUnlocked(kCenterAngle - amplitude, current_tilt, half);
        MoveUnlocked(kCenterAngle + amplitude, current_tilt, half);
    }
    MoveUnlocked(kCenterAngle, current_tilt, half / 2);
}

void HeadGimbal::Nod(int cycles, int amplitude, int period_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_) return;
    int half = std::max(kMotionStepMs, period_ms / 2);
    int current_pan = pan_.logical_angle;
    for (int i = 0; i < cycles; ++i) {
        MoveUnlocked(current_pan, kCenterAngle + amplitude, half);
        MoveUnlocked(current_pan, kCenterAngle - amplitude, half);
    }
    MoveUnlocked(current_pan, kCenterAngle, half / 2);
}

void HeadGimbal::LookAt(int x_percent, int y_percent, int duration_ms) {
    int pan_angle = kCenterAngle + std::lround((x_percent / 100.0f) * kLookAtPanRange);
    int tilt_angle = kCenterAngle + std::lround((y_percent / 100.0f) * kLookAtTiltRange);
    MoveTo(pan_angle, tilt_angle, duration_ms);
}

void HeadGimbal::SetTrim(const std::string& axis_name, int trim) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& axis = GetAxis(axis_name);
    axis.trim = trim;
    SaveAxisSettings(axis_name.c_str(), axis);
    WriteAxis(axis, axis.logical_angle);
}

void HeadGimbal::SetInverted(const std::string& axis_name, bool inverted) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& axis = GetAxis(axis_name);
    axis.inverted = inverted;
    SaveAxisSettings(axis_name.c_str(), axis);
    WriteAxis(axis, axis.logical_angle);
}

void HeadGimbal::SetLimits(const std::string& axis_name, int min_angle, int max_angle) {
    if (min_angle > max_angle) {
        throw std::runtime_error("min_angle must be <= max_angle");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& axis = GetAxis(axis_name);
    axis.min_angle = ClampAngle(min_angle);
    axis.max_angle = ClampAngle(max_angle);
    SaveAxisSettings(axis_name.c_str(), axis);
    WriteAxis(axis, axis.logical_angle);
}

cJSON* HeadGimbal::CreateTrimJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "pan_trim", pan_.trim);
    cJSON_AddNumberToObject(root, "tilt_trim", tilt_.trim);
    return root;
}

cJSON* HeadGimbal::CreateStatusJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ready", ready_);
    cJSON_AddItemToObject(root, "pan", CreateAxisJson("pan", pan_));
    cJSON_AddItemToObject(root, "tilt", CreateAxisJson("tilt", tilt_));
    return root;
}

static bool ProbeServoPin(gpio_num_t pin) {
    // SG90 signal line floats high-impedance; use internal pull-down.
    // If an external pull-up (e.g. from servo PCB) drives the line HIGH,
    // the pin reads 1.  This is a best-effort heuristic, not guaranteed.
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    vTaskDelay(pdMS_TO_TICKS(5));
    int level = gpio_get_level(pin);

    // Clear pull-down before LEDC takes over; a lingering pull-down
    // would drag the PWM signal low and prevent the servo from responding.
    gpio_pulldown_dis(pin);

    ESP_LOGI("HeadGimbal", "Probe pin %d: level=%d (%s)", pin, level,
             level ? "servo likely connected" : "no pull-up detected");
    return level == 1;
}

void HeadGimbal::InitializePwm() {
    ready_ = pan_.pin != GPIO_NUM_NC || tilt_.pin != GPIO_NUM_NC;
    if (!ready_) {
        ESP_LOGW(TAG, "Head gimbal disabled: no pins configured");
        return;
    }

    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = LEDC_TIMER_13_BIT;
    ledc_timer.timer_num       = timer_;
    ledc_timer.freq_hz         = 50;
    ledc_timer.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    if (pan_.pin != GPIO_NUM_NC) {
        ledc_channel_config_t c = {};
        c.gpio_num   = pan_.pin;
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel    = pan_.channel;
        c.timer_sel  = timer_;
        c.duty       = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&c));
    }

    if (tilt_.pin != GPIO_NUM_NC) {
        ledc_channel_config_t c = {};
        c.gpio_num   = tilt_.pin;
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel    = tilt_.channel;
        c.timer_sel  = timer_;
        c.duty       = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&c));
    }

    ESP_LOGI(TAG, "Head gimbal PWM ready: pan=%d tilt=%d", pan_.pin, tilt_.pin);
}

void HeadGimbal::LoadSettings() {
    Settings settings(settings_ns_);
    pan_.trim = settings.GetInt("pan_trim", 0);
    pan_.min_angle = settings.GetInt("pan_min", 20);
    pan_.max_angle = settings.GetInt("pan_max", 160);
    pan_.inverted = settings.GetBool("pan_inv", false);

    tilt_.trim = settings.GetInt("tilt_trim", 0);
    tilt_.min_angle = settings.GetInt("tilt_min", 60);
    tilt_.max_angle = settings.GetInt("tilt_max", 120);
    tilt_.inverted = settings.GetBool("tilt_inv", false);
}

void HeadGimbal::SaveAxisSettings(const char* axis_name, const AxisConfig& axis) {
    Settings settings(settings_ns_, true);
    settings.SetInt(std::string(axis_name) + "_trim", axis.trim);
    settings.SetInt(std::string(axis_name) + "_min", axis.min_angle);
    settings.SetInt(std::string(axis_name) + "_max", axis.max_angle);
    settings.SetBool(std::string(axis_name) + "_inv", axis.inverted);
}

HeadGimbal::AxisConfig& HeadGimbal::GetAxis(const std::string& axis_name) {
    if (axis_name == "pan") {
        return pan_;
    }
    if (axis_name == "tilt") {
        return tilt_;
    }
    throw std::runtime_error("axis must be pan or tilt");
}

const HeadGimbal::AxisConfig& HeadGimbal::GetAxis(const std::string& axis_name) const {
    if (axis_name == "pan") {
        return pan_;
    }
    if (axis_name == "tilt") {
        return tilt_;
    }
    throw std::runtime_error("axis must be pan or tilt");
}

void HeadGimbal::MoveUnlocked(int pan_angle, int tilt_angle, int duration_ms) {
    if (!ready_) {
        ESP_LOGW(TAG, "Ignoring move request because head gimbal is not ready");
        return;
    }

    pan_angle = ClampAngle(pan_angle);
    tilt_angle = ClampAngle(tilt_angle);

    if (duration_ms <= 0) {
        WriteAxis(pan_, pan_angle);
        WriteAxis(tilt_, tilt_angle);
        return;
    }

    int start_pan = pan_.logical_angle;
    int start_tilt = tilt_.logical_angle;
    int steps = std::max(1, duration_ms / kMotionStepMs);
    for (int i = 1; i <= steps; ++i) {
        float ratio = static_cast<float>(i) / static_cast<float>(steps);
        int next_pan = start_pan + std::lround((pan_angle - start_pan) * ratio);
        int next_tilt = start_tilt + std::lround((tilt_angle - start_tilt) * ratio);
        WriteAxis(pan_, next_pan);
        WriteAxis(tilt_, next_tilt);
        vTaskDelay(pdMS_TO_TICKS(kMotionStepMs));
    }
}

void HeadGimbal::WriteAxis(AxisConfig& axis, int logical_angle) {
    axis.logical_angle = ClampAngle(logical_angle);
    if (axis.pin == GPIO_NUM_NC) {
        return;
    }
    axis.physical_angle = ApplyAxisConfig(axis, axis.logical_angle);
    uint32_t duty = AngleToDuty(axis.physical_angle);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, axis.channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, axis.channel));
}

int HeadGimbal::ApplyAxisConfig(const AxisConfig& axis, int logical_angle) const {
    int adjusted = ClampAngle(logical_angle);
    if (axis.inverted) {
        adjusted = 180 - adjusted;
    }
    adjusted += axis.trim;
    adjusted = std::max(axis.min_angle, adjusted);
    adjusted = std::min(axis.max_angle, adjusted);
    return adjusted;
}

cJSON* HeadGimbal::CreateAxisJson(const char* axis_name, const AxisConfig& axis) const {
    auto json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "axis", axis_name);
    cJSON_AddNumberToObject(json, "pin", axis.pin);
    cJSON_AddNumberToObject(json, "logical_angle", axis.logical_angle);
    cJSON_AddNumberToObject(json, "physical_angle", axis.physical_angle);
    cJSON_AddNumberToObject(json, "trim", axis.trim);
    cJSON_AddNumberToObject(json, "min_angle", axis.min_angle);
    cJSON_AddNumberToObject(json, "max_angle", axis.max_angle);
    cJSON_AddBoolToObject(json, "inverted", axis.inverted);
    return json;
}

int HeadGimbal::ClampAngle(int angle) {
    return std::max(0, std::min(180, angle));
}

uint32_t HeadGimbal::AngleToDuty(int angle) {
    angle = ClampAngle(angle);
    return static_cast<uint32_t>(((angle / 180.0f) * 2.0f + 0.5f) * 8191 / 20.0f);
}
