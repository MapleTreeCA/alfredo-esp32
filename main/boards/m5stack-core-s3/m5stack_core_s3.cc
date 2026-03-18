#include "wifi_board.h"
#include "cores3_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "config.h"
#include "power_save_timer.h"
#include "i2c_device.h"
#include "axp2101.h"
#include "head_gimbal.h"
#include "led/circular_strip.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_rom_sys.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_ili9341.h>
#include <esp_timer.h>
#include "esp_video.h"
#include <algorithm>
#include <cstdio>

#define TAG "M5StackCoreS3Board"

namespace {
constexpr uint16_t ComposeNecCode(uint8_t command) {
    return (static_cast<uint16_t>(command) << 8) | static_cast<uint8_t>(~command);
}

class IrNecTransmitter {
public:
    explicit IrNecTransmitter(gpio_num_t gpio) : gpio_(gpio) {
        gpio_config_t config = {};
        config.pin_bit_mask = 1ULL << gpio_;
        config.mode = GPIO_MODE_OUTPUT;
        config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        config.pull_up_en = GPIO_PULLUP_DISABLE;
        config.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&config));
        gpio_set_level(gpio_, 0);
    }

    void SendNec(uint16_t address, uint8_t command) const {
        SendNecFrame(address, ComposeNecCode(command));
    }

    void SendNecFrame(uint16_t address, uint16_t code) const {
        const uint8_t address_high = static_cast<uint8_t>((address >> 8) & 0xFF);
        const uint8_t address_low = static_cast<uint8_t>(address & 0xFF);
        const uint8_t code_high = static_cast<uint8_t>((code >> 8) & 0xFF);
        const uint8_t code_low = static_cast<uint8_t>(code & 0xFF);
        SendLeader();
        SendByte(address_high);
        SendByte(address_low);
        SendByte(code_high);
        SendByte(code_low);
        SendMark(kBitMarkUs);
        SendSpace(0);
    }

private:
    static constexpr uint32_t kCarrierHighUs = 13;
    static constexpr uint32_t kCarrierLowUs = 13;
    static constexpr uint32_t kLeaderMarkUs = 9000;
    static constexpr uint32_t kLeaderSpaceUs = 4500;
    static constexpr uint32_t kBitMarkUs = 560;
    static constexpr uint32_t kZeroSpaceUs = 560;
    static constexpr uint32_t kOneSpaceUs = 1690;

    gpio_num_t gpio_;

    void SendLeader() const {
        SendMark(kLeaderMarkUs);
        SendSpace(kLeaderSpaceUs);
    }

    void SendByte(uint8_t value) const {
        for (int bit = 0; bit < 8; ++bit) {
            const bool is_one = (value >> bit) & 0x01;
            SendMark(kBitMarkUs);
            SendSpace(is_one ? kOneSpaceUs : kZeroSpaceUs);
        }
    }

    void SendMark(uint32_t duration_us) const {
        uint32_t elapsed_us = 0;
        while (elapsed_us + kCarrierHighUs + kCarrierLowUs <= duration_us) {
            gpio_set_level(gpio_, 1);
            esp_rom_delay_us(kCarrierHighUs);
            gpio_set_level(gpio_, 0);
            esp_rom_delay_us(kCarrierLowUs);
            elapsed_us += kCarrierHighUs + kCarrierLowUs;
        }
        if (elapsed_us < duration_us) {
            gpio_set_level(gpio_, 1);
            esp_rom_delay_us(duration_us - elapsed_us);
            gpio_set_level(gpio_, 0);
        }
    }

    void SendSpace(uint32_t duration_us) const {
        gpio_set_level(gpio_, 0);
        if (duration_us > 0) {
            esp_rom_delay_us(duration_us);
        }
    }
};
}  // namespace

class Pmic : public Axp2101 {
public:
    // Power Init
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        uint8_t data = ReadReg(0x90);
        data |= 0b10110100;
        WriteReg(0x90, data);
        WriteReg(0x99, (0b11110 - 5));
        WriteReg(0x97, (0b11110 - 2));
        WriteReg(0x69, 0b00000100); // disable CHGLED pin; keep PMIC LED dark
        WriteReg(0x30, 0b111111);
        WriteReg(0x90, 0xBF);
        WriteReg(0x94, 33 - 5);
        WriteReg(0x95, 33 - 5);
    }

    void SetBrightness(uint8_t brightness) {
        brightness = ((brightness + 641) >> 5);
        WriteReg(0x99, brightness);
    }

};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(Pmic *pmic) : pmic_(pmic) {}

    void SetBrightnessImpl(uint8_t brightness) override {
        pmic_->SetBrightness(target_brightness_);
        brightness_ = target_brightness_;
    }

private:
    Pmic *pmic_;
};

class Aw9523 : public I2cDevice {
public:
    // Exanpd IO Init
    Aw9523(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x02, 0b00000111);  // P0
        WriteReg(0x03, 0b10001111);  // P1
        WriteReg(0x04, 0b00011000);  // CONFIG_P0
        WriteReg(0x05, 0b00001100);  // CONFIG_P1
        WriteReg(0x11, 0b00010000);  // GCR P0 port is Push-Pull mode.
        WriteReg(0x12, 0b11111111);  // LEDMODE_P0
        WriteReg(0x13, 0b11111111);  // LEDMODE_P1
    }

    void ResetAw88298() {
        ESP_LOGI(TAG, "Reset AW88298");
        WriteReg(0x02, 0b00000011);
        vTaskDelay(pdMS_TO_TICKS(10));
        WriteReg(0x02, 0b00000111);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    void ResetIli9342() {
        ESP_LOGI(TAG, "Reset IlI9342");
        WriteReg(0x03, 0b10000001);
        vTaskDelay(pdMS_TO_TICKS(20));
        WriteReg(0x03, 0b10000011);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
};

class Ft6336 : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    
    Ft6336(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        uint8_t chip_id = ReadReg(0xA3);
        ESP_LOGI(TAG, "Get chip ID: 0x%02X", chip_id);
        read_buffer_ = new uint8_t[6];
    }

    ~Ft6336() {
        delete[] read_buffer_;
    }

    void UpdateTouchPoint() {
        ReadRegs(0x02, read_buffer_, 6);
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    inline const TouchPoint_t& GetTouchPoint() {
        return tp_;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
};

class ChargingStrip : public CircularStrip {
public:
    ChargingStrip(gpio_num_t gpio, uint16_t max_leds)
        : CircularStrip(gpio, max_leds) {
    }

    void OnWakeWordDetected() override {
        BlinkTimes(kIndicatorColor, kBlinkIntervalMs, 3);
    }

    void SetExternalPower(bool external_power) {
        if (external_power_ == external_power) {
            return;
        }
        external_power_ = external_power;
        if (external_power_) {
            BlinkTimes(kIndicatorColor, kBlinkIntervalMs, 10);
            return;
        }
        SetAllColor({0, 0, 0});
    }

    void OnStateChanged() override {
        // Power and wake indications are one-shot events; device state changes
        // should not restart them.
    }

private:
    bool external_power_ = false;
    static constexpr StripColor kIndicatorColor = {32, 12, 0};
    static constexpr int kBlinkIntervalMs = 200;
};

class M5StackCoreS3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Pmic* pmic_;
    Aw9523* aw9523_;
    Ft6336* ft6336_;
    LcdDisplay* display_;
    EspVideo* camera_;
    ChargingStrip* charging_strip_ = nullptr;
    HeadGimbal* head_gimbal_ = nullptr;
    esp_timer_handle_t touchpad_timer_;
    PowerSaveTimer* power_save_timer_;
    bool manual_sleep_face_ = false;
    bool external_power_connected_ = false;

    void ApplyExternalPowerState(bool external_power, bool force = false) {
        if (!force && external_power_connected_ == external_power) {
            return;
        }

        external_power_connected_ = external_power;
        ESP_LOGI(TAG, "External power %s, auto sleep/shutdown %s",
                 external_power_connected_ ? "connected" : "disconnected",
                 external_power_connected_ ? "disabled" : "enabled");

        if (power_save_timer_ != nullptr) {
            power_save_timer_->SetEnabled(!external_power_connected_);
        }
        if (charging_strip_ != nullptr) {
            charging_strip_->SetExternalPower(external_power_connected_);
        }
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(10);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            pmic_->PowerOff();
        });
        ApplyExternalPowerState(pmic_->IsVbusGood(), true);
    }

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void I2cDetect() {
        uint8_t address;
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                fflush(stdout);
                address = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
                if (ret == ESP_OK) {
                    printf("%02x ", address);
                } else if (ret == ESP_ERR_TIMEOUT) {
                    printf("UU ");
                } else {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
    }

    void InitializeAxp2101() {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(i2c_bus_, 0x34);
        ESP_LOGI(TAG, "AXP2101 vbus_good=%d battery_present=%d charging=%d discharging=%d",
                 pmic_->IsVbusGood(), pmic_->IsBatteryPresent(),
                 pmic_->IsCharging(), pmic_->IsDischarging());
    }

    void InitializeChargingStrip() {
        ESP_LOGI(TAG, "Init M5GO RGB strip on GPIO%d (%d LEDs)", M5GO_RGB_LED_GPIO, M5GO_RGB_LED_COUNT);
        charging_strip_ = new ChargingStrip(M5GO_RGB_LED_GPIO, M5GO_RGB_LED_COUNT);
        ApplyExternalPowerState(external_power_connected_, true);
    }

    void InitializeAw9523() {
        ESP_LOGI(TAG, "Init AW9523");
        aw9523_ = new Aw9523(i2c_bus_, 0x58);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    void SendProjectorNecFrameInternal(const char* reason,
                                       uint16_t address,
                                       uint16_t code) {
        ESP_LOGI(TAG,
                 "%s projector NEC frame on GPIO%d: address=0x%04X code=0x%04X count=%d",
                 reason,
                 static_cast<int>(M5GO_IR_LED_GPIO),
                 address,
                 code,
                 PROJECTOR_IR_AUTO_SEND_COUNT);
        IrNecTransmitter transmitter(M5GO_IR_LED_GPIO);
        for (int attempt = 0; attempt < PROJECTOR_IR_AUTO_SEND_COUNT; ++attempt) {
            transmitter.SendNecFrame(address, code);
            if (attempt + 1 < PROJECTOR_IR_AUTO_SEND_COUNT) {
                vTaskDelay(pdMS_TO_TICKS(PROJECTOR_IR_AUTO_SEND_GAP_MS));
            }
        }
    }

    void PollTouchpad() {
        static bool was_touched = false;
        static int64_t touch_start_time = 0;
        static int release_x = -1;
        static int release_y = -1;
        constexpr int64_t SHORT_TOUCH_THRESHOLD_MS = 500;
        constexpr int64_t LONG_TOUCH_THRESHOLD_MS = 700;
        constexpr int kVolumeStep = 8;
        constexpr int kMinVolume = 1;
        constexpr int kMaxVolume = 100;
        constexpr int kVolumeZoneX = DISPLAY_WIDTH * 2 / 3;
        
        ft6336_->UpdateTouchPoint();
        auto& touch_point = ft6336_->GetTouchPoint();
        
        // Touch begin / move
        if (touch_point.num > 0) {
            if (!was_touched) {
                was_touched = true;
                touch_start_time = esp_timer_get_time() / 1000;
            }
            release_x = touch_point.x;
            release_y = touch_point.y;
            return;
        }

        // Touch release
        if (!was_touched) {
            return;
        }

        was_touched = false;
        int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time;
        int x = release_x;
        int y = release_y;
        release_x = -1;
        release_y = -1;

        auto& app = Application::GetInstance();

        if (touch_duration >= LONG_TOUCH_THRESHOLD_MS) {
            manual_sleep_face_ = true;
            GetDisplay()->SetPowerSaveMode(true);
            GetDisplay()->ShowNotification("Sleep face", 1200);
            return;
        }

        if (touch_duration >= SHORT_TOUCH_THRESHOLD_MS) {
            return;
        }

        // Any short touch wakes manual sleepy face first.
        if (manual_sleep_face_) {
            manual_sleep_face_ = false;
            GetDisplay()->SetPowerSaveMode(false);
            return;
        }

        power_save_timer_->WakeUp();

        if (auto* display = GetDisplay(); display != nullptr && display->HandleTap(x, y)) {
            return;
        }

        if (app.GetDeviceState() == kDeviceStateStarting) {
            EnterWifiConfigMode();
            return;
        }

        // Right-top / right-bottom short tap adjusts output volume.
        if (x >= kVolumeZoneX) {
            auto* codec = GetAudioCodec();
            if (codec != nullptr) {
                int current = codec->output_volume();
                int next = current;
                if (y < DISPLAY_HEIGHT / 2) {
                    next = std::min(kMaxVolume, current + kVolumeStep);
                } else {
                    next = std::max(kMinVolume, current - kVolumeStep);
                }

                if (next != current) {
                    codec->SetOutputVolume(next);
                }

                char notification[32];
                std::snprintf(notification, sizeof(notification), "Volume: %d", codec->output_volume());
                GetDisplay()->ShowNotification(notification, 1200);
            }
            return;
        }

        app.ToggleChatState();
    }

    void InitializeFt6336TouchPad() {
        ESP_LOGI(TAG, "Init FT6336");
        ft6336_ = new Ft6336(i2c_bus_, 0x38);
        
        // 创建定时器，20ms 间隔
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                M5StackCoreS3Board* board = (M5StackCoreS3Board*)arg;
                board->PollTouchpad();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touchpad_timer",
            .skip_unhandled_events = true,
        };
        
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touchpad_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touchpad_timer_, 20 * 1000));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_37;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_36;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeIli9342Display() {
        ESP_LOGI(TAG, "Init IlI9342");

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_3;
        io_config.dc_gpio_num = GPIO_NUM_35;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        aw9523_->ResetIli9342();

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

     void InitializeCamera() {
        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = CAMERA_PIN_D0,
                [1] = CAMERA_PIN_D1,
                [2] = CAMERA_PIN_D2,
                [3] = CAMERA_PIN_D3,
                [4] = CAMERA_PIN_D4,
                [5] = CAMERA_PIN_D5,
                [6] = CAMERA_PIN_D6,
                [7] = CAMERA_PIN_D7,
            },
            .vsync_io = CAMERA_PIN_VSYNC,
            .de_io = CAMERA_PIN_HREF,
            .pclk_io = CAMERA_PIN_PCLK,
            .xclk_io = CAMERA_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_bus_,
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAMERA_PIN_RESET,
            .pwdn_pin = CAMERA_PIN_PWDN,
            .dvp_pin = dvp_pin_config,
            .xclk_freq = XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new EspVideo(video_config);
        camera_->SetHMirror(false);
    }

public:
    M5StackCoreS3Board() {
        InitializeI2c();
        InitializeAxp2101();
        InitializePowerSaveTimer();
        InitializeChargingStrip();
        InitializeAw9523();
        I2cDetect();
        InitializeSpi();
        InitializeIli9342Display();
        InitializeCamera();
        InitializeFt6336TouchPad();
        GetBacklight()->RestoreBrightness();

        head_gimbal_ = new HeadGimbal(HEAD_GIMBAL_PAN_PIN, HEAD_GIMBAL_TILT_PIN,
                                      LEDC_TIMER_3, LEDC_CHANNEL_4, LEDC_CHANNEL_5);
        if (head_gimbal_->IsReady()) {
            head_gimbal_->RegisterMcpTools();
        }
    }

    virtual AudioCodec* GetAudioCodec() override {
        static CoreS3AudioCodec audio_codec(i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_AW88298_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    virtual bool SendProjectorPowerCode() override {
        SendProjectorNecFrameInternal("Manual-sending", PROJECTOR_IR_NEC_ADDRESS,
                                      ComposeNecCode(PROJECTOR_IR_NEC_COMMAND));
        return true;
    }

    virtual bool SendProjectorPowerCode(uint16_t address) override {
        SendProjectorNecFrameInternal("Manual-sending", address,
                                      ComposeNecCode(PROJECTOR_IR_NEC_COMMAND));
        return true;
    }

    virtual bool SendProjectorNecFrame(uint16_t address, uint16_t code) override {
        SendProjectorNecFrameInternal("Manual-sending", address, code);
        return true;
    }

    virtual bool SendProjectorNecCode(uint16_t address, uint8_t command) override {
        SendProjectorNecFrameInternal("Manual-sending", address, ComposeNecCode(command));
        return true;
    }

    virtual Led* GetLed() override {
        if (charging_strip_ != nullptr) {
            return charging_strip_;
        }
        return Board::GetLed();
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_charging = !pmic_->IsCharging(); // force sync on first call
        static bool last_vbus_good = !pmic_->IsVbusGood(); // force sync on first call
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        bool vbus_good = pmic_->IsVbusGood();
        if (vbus_good != last_vbus_good) {
            ApplyExternalPowerState(vbus_good);
            last_vbus_good = vbus_good;
        }
        if (charging != last_charging) {
            last_charging = charging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual Backlight *GetBacklight() override {
        static CustomBacklight backlight(pmic_);
        return &backlight;
    }

    virtual void OnDeviceStateChanged(DeviceState new_state, DeviceState old_state) override {
        ESP_LOGI("CoreS3Board", "OnDeviceStateChanged: %d -> %d (gimbal=%s)",
                 (int)old_state, (int)new_state,
                 (head_gimbal_ && head_gimbal_->IsReady()) ? "ready" : "not ready");
        if (new_state != kDeviceStateConnecting || old_state != kDeviceStateIdle) return;
        if (!head_gimbal_ || !head_gimbal_->IsReady()) return;

        // Run wake-up animation in background so listening starts immediately.
        BaseType_t ret = xTaskCreate([](void* arg) {
            auto* gimbal = static_cast<HeadGimbal*>(arg);
            gimbal->MoveTo(75, 100, 1200);  // look left-down (heavy head drooping)
            gimbal->MoveTo(105, 95, 1400);  // sweep right, still slightly down
            gimbal->MoveTo(90, 80, 1000);   // look up toward center
            gimbal->MoveTo(90, 90, 700);    // settle to center
            vTaskDelete(nullptr);
        }, "gimbal_wake", 3072, head_gimbal_, 3, nullptr);
        ESP_LOGI("CoreS3Board", "gimbal_wake task create: %s", ret == pdPASS ? "OK" : "FAIL");
    }
};

DECLARE_BOARD(M5StackCoreS3Board);
