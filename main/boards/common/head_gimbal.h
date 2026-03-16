#ifndef HEAD_GIMBAL_H
#define HEAD_GIMBAL_H

#include <driver/gpio.h>
#include <driver/ledc.h>

#include <mutex>
#include <string>

#include <cJSON.h>

class HeadGimbal {
public:
    HeadGimbal(gpio_num_t pan_pin, gpio_num_t tilt_pin, ledc_timer_t timer = LEDC_TIMER_2,
               ledc_channel_t pan_channel = LEDC_CHANNEL_6,
               ledc_channel_t tilt_channel = LEDC_CHANNEL_7,
               const std::string& settings_ns = "head_gimbal");
    ~HeadGimbal();

    bool IsReady() const;
    void RegisterMcpTools(const std::string& tool_prefix = "self.head");

    void Center(int duration_ms = 0);
    void MoveTo(int pan_angle, int tilt_angle, int duration_ms = 0);
    void MoveBy(int delta_pan, int delta_tilt, int duration_ms = 0);
    void LookAt(int x_percent, int y_percent, int duration_ms = 150);
    void Shake(int cycles = 2, int amplitude = 30, int period_ms = 600);
    void Nod(int cycles = 2, int amplitude = 15, int period_ms = 500);

    void SetTrim(const std::string& axis_name, int trim);
    void SetInverted(const std::string& axis_name, bool inverted);
    void SetLimits(const std::string& axis_name, int min_angle, int max_angle);

    cJSON* CreateTrimJson() const;
    cJSON* CreateStatusJson() const;

private:
    struct AxisConfig {
        gpio_num_t pin = GPIO_NUM_NC;
        ledc_channel_t channel = LEDC_CHANNEL_0;
        int trim = 0;
        int min_angle = 20;
        int max_angle = 160;
        bool inverted = false;
        int logical_angle = 90;
        int physical_angle = 90;
    };

    std::string settings_ns_;
    ledc_timer_t timer_;
    AxisConfig pan_;
    AxisConfig tilt_;
    bool ready_ = false;
    mutable std::mutex mutex_;

    void InitializePwm();
    void LoadSettings();
    void SaveAxisSettings(const char* axis_name, const AxisConfig& axis);

    AxisConfig& GetAxis(const std::string& axis_name);
    const AxisConfig& GetAxis(const std::string& axis_name) const;

    void MoveUnlocked(int pan_angle, int tilt_angle, int duration_ms);
    void WriteAxis(AxisConfig& axis, int logical_angle);
    int ApplyAxisConfig(const AxisConfig& axis, int logical_angle) const;

    cJSON* CreateAxisJson(const char* axis_name, const AxisConfig& axis) const;

    static int ClampAngle(int angle);
    static uint32_t AngleToDuty(int angle);
};

#endif  // HEAD_GIMBAL_H
