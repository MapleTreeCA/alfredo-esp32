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

// These are generated from Alfred SVG faces in the font component's src/emoji.
extern const lv_image_dsc_t alfred_face_calm_32; // neutral/cool/confident
extern const lv_image_dsc_t alfred_face_smile_32; // happy
extern const lv_image_dsc_t alfred_face_laugh_32; // laughing/funny
extern const lv_image_dsc_t alfred_face_worried_32; // sad/crying/thinking/confused
extern const lv_image_dsc_t alfred_face_angry_32; // angry
extern const lv_image_dsc_t alfred_face_shy_32; // loving/embarrassed/kissy
extern const lv_image_dsc_t alfred_face_surprised_32; // surprised/shocked
extern const lv_image_dsc_t alfred_face_wink_32; // winking/silly
extern const lv_image_dsc_t alfred_face_sleepy_32; // relaxed/sleepy
extern const lv_image_dsc_t alfred_face_talk_32; // delicious

Twemoji32::Twemoji32() {
    AddEmoji("neutral", new LvglSourceImage(&alfred_face_calm_32));
    AddEmoji("happy", new LvglSourceImage(&alfred_face_smile_32));
    AddEmoji("laughing", new LvglSourceImage(&alfred_face_laugh_32));
    AddEmoji("funny", new LvglSourceImage(&alfred_face_laugh_32));
    AddEmoji("sad", new LvglSourceImage(&alfred_face_worried_32));
    AddEmoji("angry", new LvglSourceImage(&alfred_face_angry_32));
    AddEmoji("crying", new LvglSourceImage(&alfred_face_worried_32));
    AddEmoji("loving", new LvglSourceImage(&alfred_face_shy_32));
    AddEmoji("embarrassed", new LvglSourceImage(&alfred_face_shy_32));
    AddEmoji("surprised", new LvglSourceImage(&alfred_face_surprised_32));
    AddEmoji("shocked", new LvglSourceImage(&alfred_face_surprised_32));
    AddEmoji("thinking", new LvglSourceImage(&alfred_face_worried_32));
    AddEmoji("winking", new LvglSourceImage(&alfred_face_wink_32));
    AddEmoji("cool", new LvglSourceImage(&alfred_face_calm_32));
    AddEmoji("relaxed", new LvglSourceImage(&alfred_face_sleepy_32));
    AddEmoji("delicious", new LvglSourceImage(&alfred_face_talk_32));
    AddEmoji("kissy", new LvglSourceImage(&alfred_face_shy_32));
    AddEmoji("confident", new LvglSourceImage(&alfred_face_calm_32));
    AddEmoji("sleepy", new LvglSourceImage(&alfred_face_sleepy_32));
    AddEmoji("silly", new LvglSourceImage(&alfred_face_wink_32));
    AddEmoji("confused", new LvglSourceImage(&alfred_face_worried_32));
}


// These are generated from Alfred SVG faces in the font component's src/emoji.
extern const lv_image_dsc_t alfred_face_calm_64; // neutral/cool/confident
extern const lv_image_dsc_t alfred_face_smile_64; // happy
extern const lv_image_dsc_t alfred_face_laugh_64; // laughing/funny
extern const lv_image_dsc_t alfred_face_worried_64; // sad/crying/thinking/confused
extern const lv_image_dsc_t alfred_face_angry_64; // angry
extern const lv_image_dsc_t alfred_face_shy_64; // loving/embarrassed/kissy
extern const lv_image_dsc_t alfred_face_surprised_64; // surprised/shocked
extern const lv_image_dsc_t alfred_face_wink_64; // winking/silly
extern const lv_image_dsc_t alfred_face_sleepy_64; // relaxed/sleepy
extern const lv_image_dsc_t alfred_face_talk_64; // delicious

Twemoji64::Twemoji64() {
    AddEmoji("neutral", new LvglSourceImage(&alfred_face_calm_64));
    AddEmoji("happy", new LvglSourceImage(&alfred_face_smile_64));
    AddEmoji("laughing", new LvglSourceImage(&alfred_face_laugh_64));
    AddEmoji("funny", new LvglSourceImage(&alfred_face_laugh_64));
    AddEmoji("sad", new LvglSourceImage(&alfred_face_worried_64));
    AddEmoji("angry", new LvglSourceImage(&alfred_face_angry_64));
    AddEmoji("crying", new LvglSourceImage(&alfred_face_worried_64));
    AddEmoji("loving", new LvglSourceImage(&alfred_face_shy_64));
    AddEmoji("embarrassed", new LvglSourceImage(&alfred_face_shy_64));
    AddEmoji("surprised", new LvglSourceImage(&alfred_face_surprised_64));
    AddEmoji("shocked", new LvglSourceImage(&alfred_face_surprised_64));
    AddEmoji("thinking", new LvglSourceImage(&alfred_face_worried_64));
    AddEmoji("winking", new LvglSourceImage(&alfred_face_wink_64));
    AddEmoji("cool", new LvglSourceImage(&alfred_face_calm_64));
    AddEmoji("relaxed", new LvglSourceImage(&alfred_face_sleepy_64));
    AddEmoji("delicious", new LvglSourceImage(&alfred_face_talk_64));
    AddEmoji("kissy", new LvglSourceImage(&alfred_face_shy_64));
    AddEmoji("confident", new LvglSourceImage(&alfred_face_calm_64));
    AddEmoji("sleepy", new LvglSourceImage(&alfred_face_sleepy_64));
    AddEmoji("silly", new LvglSourceImage(&alfred_face_wink_64));
    AddEmoji("confused", new LvglSourceImage(&alfred_face_worried_64));
}
