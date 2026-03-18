#include "emoji_collection.h"

#include <esp_log.h>
#include <unordered_map>
#include <string>

#define TAG "EmojiCollection"

void EmojiCollection::AddEmoji(const std::string& name, LvglImage* image) {
    emoji_collection_[name] = image;
}

const LvglImage* EmojiCollection::FindEmojiImage(const char* name) const {
    auto it = emoji_collection_.find(name);
    if (it != emoji_collection_.end()) {
        return it->second;
    }
    return nullptr;
}

const LvglImage* EmojiCollection::GetEmojiImage(const char* name) {
    auto image = FindEmojiImage(name);
    if (image != nullptr) {
        return image;
    }

    ESP_LOGW(TAG, "Emoji not found: %s", name);
    return nullptr;
}

EmojiCollection::~EmojiCollection() {
    for (auto it = emoji_collection_.begin(); it != emoji_collection_.end(); ++it) {
        delete it->second;
    }
    emoji_collection_.clear();
}

// These are generated from Alfredo SVG faces in the font component's src/emoji.
extern const lv_image_dsc_t alfred_face_calm_32; // neutral
extern const lv_image_dsc_t alfred_face_smile_32; // happy
extern const lv_image_dsc_t alfred_face_worried_32; // sad
extern const lv_image_dsc_t alfred_face_sleeping_32; // sleeping
extern const lv_image_dsc_t alfred_face_thinking_32; // thinking
extern const lv_image_dsc_t alfred_face_listening_32; // listening
extern const lv_image_dsc_t alfred_face_noconnection_32; // noconnection
extern const lv_image_dsc_t alfred_face_grieved_32; // grieved
extern const lv_image_dsc_t alfred_face_wakeup1_32; // wakeup1 (groggy)
extern const lv_image_dsc_t alfred_face_wakeup2_32; // wakeup2 (happy awake)
extern const lv_image_dsc_t alfred_face_sleeping0_32;
extern const lv_image_dsc_t alfred_face_sleeping1_32;
extern const lv_image_dsc_t alfred_face_sleeping2_32;
extern const lv_image_dsc_t alfred_face_sleeping3_32;
extern const lv_image_dsc_t alfred_face_listening1_32;
extern const lv_image_dsc_t alfred_face_listening2_32;
extern const lv_image_dsc_t alfred_face_listening3_32;

Twemoji32::Twemoji32() {
    AddEmoji("neutral", new LvglSourceImage(&alfred_face_calm_32));
    AddEmoji("happy", new LvglSourceImage(&alfred_face_smile_32));
    AddEmoji("sad", new LvglSourceImage(&alfred_face_worried_32));
    AddEmoji("sleeping", new LvglSourceImage(&alfred_face_sleeping_32));
    AddEmoji("thinking", new LvglSourceImage(&alfred_face_thinking_32));
    AddEmoji("listening", new LvglSourceImage(&alfred_face_listening_32));
    AddEmoji("noconnection", new LvglSourceImage(&alfred_face_noconnection_32));
    AddEmoji("grieved", new LvglSourceImage(&alfred_face_grieved_32));
    AddEmoji("wakeup1", new LvglSourceImage(&alfred_face_wakeup1_32));
    AddEmoji("wakeup2", new LvglSourceImage(&alfred_face_wakeup2_32));
    AddEmoji("sleeping0", new LvglSourceImage(&alfred_face_sleeping0_32));
    AddEmoji("sleeping1", new LvglSourceImage(&alfred_face_sleeping1_32));
    AddEmoji("sleeping2", new LvglSourceImage(&alfred_face_sleeping2_32));
    AddEmoji("sleeping3", new LvglSourceImage(&alfred_face_sleeping3_32));
    AddEmoji("listening1", new LvglSourceImage(&alfred_face_listening1_32));
    AddEmoji("listening2", new LvglSourceImage(&alfred_face_listening2_32));
    AddEmoji("listening3", new LvglSourceImage(&alfred_face_listening3_32));
}


// These are generated from Alfredo SVG faces in the font component's src/emoji.
extern const lv_image_dsc_t alfred_face_calm_64; // neutral
extern const lv_image_dsc_t alfred_face_smile_64; // happy
extern const lv_image_dsc_t alfred_face_worried_64; // sad
extern const lv_image_dsc_t alfred_face_sleeping_64; // sleeping
extern const lv_image_dsc_t alfred_face_thinking_64; // thinking
extern const lv_image_dsc_t alfred_face_listening_64; // listening
extern const lv_image_dsc_t alfred_face_noconnection_64; // noconnection
extern const lv_image_dsc_t alfred_face_grieved_64; // grieved
extern const lv_image_dsc_t alfred_face_wakeup1_64; // wakeup1 (groggy)
extern const lv_image_dsc_t alfred_face_wakeup2_64; // wakeup2 (happy awake)
extern const lv_image_dsc_t alfred_face_sleeping0_64;
extern const lv_image_dsc_t alfred_face_sleeping1_64;
extern const lv_image_dsc_t alfred_face_sleeping2_64;
extern const lv_image_dsc_t alfred_face_sleeping3_64;
extern const lv_image_dsc_t alfred_face_listening1_64;
extern const lv_image_dsc_t alfred_face_listening2_64;
extern const lv_image_dsc_t alfred_face_listening3_64;

Twemoji64::Twemoji64() {
    AddEmoji("neutral", new LvglSourceImage(&alfred_face_calm_64));
    AddEmoji("happy", new LvglSourceImage(&alfred_face_smile_64));
    AddEmoji("sad", new LvglSourceImage(&alfred_face_worried_64));
    AddEmoji("sleeping", new LvglSourceImage(&alfred_face_sleeping_64));
    AddEmoji("thinking", new LvglSourceImage(&alfred_face_thinking_64));
    AddEmoji("listening", new LvglSourceImage(&alfred_face_listening_64));
    AddEmoji("noconnection", new LvglSourceImage(&alfred_face_noconnection_64));
    AddEmoji("grieved", new LvglSourceImage(&alfred_face_grieved_64));
    AddEmoji("wakeup1", new LvglSourceImage(&alfred_face_wakeup1_64));
    AddEmoji("wakeup2", new LvglSourceImage(&alfred_face_wakeup2_64));
    AddEmoji("sleeping0", new LvglSourceImage(&alfred_face_sleeping0_64));
    AddEmoji("sleeping1", new LvglSourceImage(&alfred_face_sleeping1_64));
    AddEmoji("sleeping2", new LvglSourceImage(&alfred_face_sleeping2_64));
    AddEmoji("sleeping3", new LvglSourceImage(&alfred_face_sleeping3_64));
    AddEmoji("listening1", new LvglSourceImage(&alfred_face_listening1_64));
    AddEmoji("listening2", new LvglSourceImage(&alfred_face_listening2_64));
    AddEmoji("listening3", new LvglSourceImage(&alfred_face_listening3_64));
}
