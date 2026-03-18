#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "lvgl_display.h"
#include "gif/lvgl_gif.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>

#include <memory>
#include <string>

#define PREVIEW_IMAGE_DURATION_MS 5000


class LcdDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    
    lv_draw_buf_t draw_buf_;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    esp_timer_handle_t preview_timer_ = nullptr;
    esp_timer_handle_t speaking_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles
    bool power_save_mode_ = false;
    esp_timer_handle_t sleep_anim_timer_ = nullptr;
    int sleep_anim_frame_ = 0;
    void StartSleepAnimation();
    void StopSleepAnimation();

    esp_timer_handle_t idle_anim_timer_ = nullptr;
    int idle_anim_frame_ = 0;
    void StartIdleAnimation();
    void StopIdleAnimation();
    void AdvanceIdleFrame();

    esp_timer_handle_t wakeup_anim_timer_ = nullptr;
    int wakeup_anim_frame_ = 0;
    void StartWakeupAnimation();
    void StopWakeupAnimation();
    void AdvanceWakeupFrame();

    esp_timer_handle_t listening_anim_timer_ = nullptr;
    int listening_anim_frame_ = 0;
    void StartListeningAnimation();
    void StopListeningAnimation();
    bool speaking_animation_frame_ = false;
    bool current_emotion_has_speaking_variant_ = false;
    std::string current_emotion_ = "neutral";

    void InitializeLcdThemes();
    void ApplyEmojiScaleLocked(const lv_img_dsc_t* img_dsc);
    const LvglImage* FindThemeEmojiImageLocked(const char* emotion);
    void UpdateSleepOverlayLocked();
    void UpdateEmojiVisualLocked(const char* emotion);
    void UpdateSpeakingAnimation();
    void AdjustEmojiBoxForSubtitleLocked(bool subtitle_visible);
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height);
    
public:
    ~LcdDisplay();
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetPowerSaveMode(bool on) override;
    virtual bool HandleTap(int x, int y) override;
    virtual void SetupUI() override;
    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;
    
    // Set whether to hide chat messages/subtitles
    void SetHideSubtitle(bool hide);
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);
};

#endif // LCD_DISPLAY_H
