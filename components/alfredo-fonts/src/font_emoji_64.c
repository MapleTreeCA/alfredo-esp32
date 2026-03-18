#include "lvgl.h"

extern const lv_image_dsc_t alfred_face_calm_64; // calm
extern const lv_image_dsc_t alfred_face_smile_64; // smile
extern const lv_image_dsc_t alfred_face_laugh_64; // laugh
extern const lv_image_dsc_t alfred_face_worried_64; // worried
extern const lv_image_dsc_t alfred_face_angry_64; // angry
extern const lv_image_dsc_t alfred_face_shy_64; // shy
extern const lv_image_dsc_t alfred_face_surprised_64; // surprised
extern const lv_image_dsc_t alfred_face_wink_64; // wink
extern const lv_image_dsc_t alfred_face_sleepy_64; // sleepy
extern const lv_image_dsc_t alfred_face_talk_64; // talk
extern const lv_image_dsc_t alfred_face_sleeping_64; // sleeping
extern const lv_image_dsc_t alfred_face_sleeping0_64; // sleeping0
extern const lv_image_dsc_t alfred_face_sleeping1_64; // sleeping1
extern const lv_image_dsc_t alfred_face_sleeping2_64; // sleeping2
extern const lv_image_dsc_t alfred_face_sleeping3_64; // sleeping3
extern const lv_image_dsc_t alfred_face_thinking_64; // thinking
extern const lv_image_dsc_t alfred_face_listening_64; // listening
extern const lv_image_dsc_t alfred_face_listening1_64; // listening1
extern const lv_image_dsc_t alfred_face_listening2_64; // listening2
extern const lv_image_dsc_t alfred_face_listening3_64; // listening3
extern const lv_image_dsc_t alfred_face_noconnection_64; // noconnection
extern const lv_image_dsc_t alfred_face_grieved_64; // grieved
extern const lv_image_dsc_t alfred_face_wakeup1_64; // wakeup1
extern const lv_image_dsc_t alfred_face_wakeup2_64; // wakeup2

typedef struct emoji_64 {
    const lv_image_dsc_t* emoji;
    uint32_t unicode;
} emoji_64_t;

static const void* get_imgfont_path(const lv_font_t * font, uint32_t unicode, uint32_t unicode_next, int32_t * offset_y, void * user_data) {
    static const emoji_64_t emoji_64_table[] = {
        { &alfred_face_calm_64, 0x1f636 }, // neutral
        { &alfred_face_smile_64, 0x1f642 }, // happy
        { &alfred_face_laugh_64, 0x1f606 }, // laughing
        { &alfred_face_laugh_64, 0x1f602 }, // funny
        { &alfred_face_worried_64, 0x1f614 }, // sad
        { &alfred_face_angry_64, 0x1f620 }, // angry
        { &alfred_face_worried_64, 0x1f62d }, // crying
        { &alfred_face_shy_64, 0x1f60d }, // loving
        { &alfred_face_shy_64, 0x1f633 }, // embarrassed
        { &alfred_face_surprised_64, 0x1f62f }, // surprised
        { &alfred_face_surprised_64, 0x1f631 }, // shocked
        { &alfred_face_thinking_64, 0x1f914 }, // thinking
        { &alfred_face_wink_64, 0x1f609 }, // winking
        { &alfred_face_calm_64, 0x1f60e }, // cool
        { &alfred_face_sleepy_64, 0x1f60c }, // relaxed
        { &alfred_face_talk_64, 0x1f924 }, // delicious
        { &alfred_face_shy_64, 0x1f618 }, // kissy
        { &alfred_face_calm_64, 0x1f60f }, // confident
        { &alfred_face_sleepy_64, 0x1f634 }, // sleepy
        { &alfred_face_wink_64, 0x1f61c }, // silly
        { &alfred_face_worried_64, 0x1f644 }, // confused
        { &alfred_face_wakeup1_64, 0x1f971 }, // wakeup1
        { &alfred_face_wakeup2_64, 0x1f604 }, // wakeup2
        { &alfred_face_sleeping_64, 0x1f4a4 }, // sleeping
        { &alfred_face_sleeping0_64, 0x1f550 }, // sleeping0
        { &alfred_face_sleeping1_64, 0x1f551 }, // sleeping1
        { &alfred_face_sleeping2_64, 0x1f552 }, // sleeping2
        { &alfred_face_sleeping3_64, 0x1f553 }, // sleeping3
        { &alfred_face_listening_64, 0x1f3a7 }, // listening
        { &alfred_face_listening1_64, 0x1f554 }, // listening1
        { &alfred_face_listening2_64, 0x1f555 }, // listening2
        { &alfred_face_listening3_64, 0x1f556 }, // listening3
        { &alfred_face_noconnection_64, 0x1f4f5 }, // noconnection
        { &alfred_face_grieved_64, 0x1f622 }, // grieved
    };

    (void)font;
    (void)unicode_next;
    (void)offset_y;
    (void)user_data;

    for (size_t i = 0; i < sizeof(emoji_64_table) / sizeof(emoji_64_table[0]); i++) {
        if (emoji_64_table[i].unicode == unicode) {
            return emoji_64_table[i].emoji;
        }
    }
    return NULL;
}

const lv_font_t* font_emoji_64_init(void) {
    static lv_font_t* font = NULL;
    if (font == NULL) {
        font = lv_imgfont_create(64, get_imgfont_path, NULL);
        if (font == NULL) {
            LV_LOG_ERROR("Failed to allocate memory for emoji font");
            return NULL;
        }
        font->base_line = 0;
        font->fallback = NULL;
    }
    return font;
}
