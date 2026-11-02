#pragma once

#include "waveshare_lcd_test_display.h"
#include <esp_random.h>
#include <cstring>
#include <cmath>

// 本板专用双眼界面：复用原有状态栏、字幕、图片预览和开机测试层。
class RobotEyeDisplay : public WaveshareLcdTestDisplay {
public:
    using WaveshareLcdTestDisplay::WaveshareLcdTestDisplay;

    ~RobotEyeDisplay() override {
        DisplayLockGuard lock(this);
        if (animation_timer_) lv_timer_delete(animation_timer_);
    }

    void SetupUI() override {
        if (IsSetupUICalled()) return;
        WaveshareLcdTestDisplay::SetupUI();
        DisplayLockGuard lock(this);
        // 双眼放在原表情容器中，图片预览仍可整体隐藏它。
        lv_obj_set_size(emoji_box_, 224, 160);
        lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, -8);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(emoji_label_);
        face_ = lv_obj_create(emoji_box_);
        lv_obj_remove_style_all(face_);
        lv_obj_set_size(face_, 224, 160);
        lv_obj_remove_flag(face_, LV_OBJ_FLAG_SCROLLABLE);
        for (auto& eye : eyes_) {
            eye = lv_obj_create(face_);
            lv_obj_remove_style_all(eye);
            lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
            lv_obj_remove_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
        }
        ApplyFaceTheme();
        ActivateEyes();
        next_blink_ = lv_tick_get() + 1800;
        animation_timer_ = lv_timer_create([](lv_timer_t* timer) {
            // 回调已持有 LVGL 锁，不在这里重复加锁或执行阻塞操作。
            static_cast<RobotEyeDisplay*>(lv_timer_get_user_data(timer))->DrawEyes();
        }, 50, this);
        if (!animation_timer_) ESP_LOGE("RobotEyes", "无法创建动画定时器");
        DrawEyes();
    }

    void SetInteractionState(DeviceState state) override {
        DisplayLockGuard lock(this);
        state_ = state;
        // 连接、升级和报错仍允许显示原来的图标及提示。
        if (face_ && (state == kDeviceStateIdle || state == kDeviceStateListening ||
                      state == kDeviceStateSpeaking)) {
            ActivateEyes();
        }
    }

    void SetEmotion(const char* emotion) override {
        if (!emotion) return;
        // 系统图标继续走原来的显示逻辑；云端情绪使用双眼呈现。
        const bool use_eyes = std::strcmp(emotion, "neutral") == 0 ||
            std::strcmp(emotion, "happy") == 0 || std::strcmp(emotion, "laughing") == 0 ||
            std::strcmp(emotion, "loving") == 0 || std::strcmp(emotion, "sleepy") == 0 ||
            std::strcmp(emotion, "sad") == 0;
        if (!use_eyes) {
            WaveshareLcdTestDisplay::SetEmotion(emotion);
            DisplayLockGuard lock(this);
            if (face_) lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        DisplayLockGuard lock(this);
        happy_ = std::strcmp(emotion, "happy") == 0 ||
                 std::strcmp(emotion, "laughing") == 0 ||
                 std::strcmp(emotion, "loving") == 0;
        sleepy_ = std::strcmp(emotion, "sleepy") == 0 ||
                  std::strcmp(emotion, "sad") == 0;
        if (face_) ActivateEyes();
    }

    void SetTheme(Theme* theme) override {
        WaveshareLcdTestDisplay::SetTheme(theme);
        DisplayLockGuard lock(this);
        if (face_) ApplyFaceTheme();
    }

private:
    lv_obj_t* face_ = nullptr;
    lv_obj_t* eyes_[2] = {};
    lv_timer_t* animation_timer_ = nullptr;
    DeviceState state_ = kDeviceStateIdle;
    uint32_t next_blink_ = 0;
    uint32_t blink_start_ = 0;
    bool blinking_ = false;
    bool happy_ = false;
    bool sleepy_ = false;
    float eye_height_ = 68;

    void ActivateEyes() {
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(face_, LV_OBJ_FLAG_HIDDEN);
    }

    void ApplyFaceTheme() {
        // 固定黑色脸部，切换主题后仍保证青色眼睛和字幕的对比度。
        auto screen = lv_display_get_screen_active(display_);
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
        lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
        lv_obj_set_style_bg_color(face_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(face_, LV_OPA_COVER, 0);
        for (auto bar : {top_bar_, status_bar_, bottom_bar_}) {
            if (bar) {
                lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
                lv_obj_set_style_text_color(bar, lv_color_white(), 0);
            }
        }
        for (auto label : {status_label_, notification_label_, network_label_,
                           mute_label_, battery_label_, chat_message_label_, emoji_label_}) {
            if (label) lv_obj_set_style_text_color(label, lv_color_white(), 0);
        }
    }

    void DrawEyes() {
        const uint32_t now = lv_tick_get();
        // 有符号差值允许毫秒计数器回绕；闭眼约 250 毫秒。
        if (!blinking_ && static_cast<int32_t>(now - next_blink_) >= 0) {
            blinking_ = true;
            blink_start_ = now;
        }
        float openness = 1;
        if (blinking_) {
            const uint32_t elapsed = now - blink_start_;
            if (elapsed < 100) openness = 1 - elapsed / 100.0f;
            else if (elapsed < 150) openness = 0;
            else if (elapsed < 250) openness = (elapsed - 150) / 100.0f;
            else {
                blinking_ = false;
                next_blink_ = now + 2800 + esp_random() % 3000;
            }
        }
        if (lv_obj_has_flag(face_, LV_OBJ_FLAG_HIDDEN) ||
            lv_obj_has_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN)) return;

        const bool listening = state_ == kDeviceStateListening;
        const bool speaking = state_ == kDeviceStateSpeaking;
        const float phase = (now % 12000) * 0.001f;
        float target = listening ? 88 : speaking ? 70 + 7 * std::sin(phase * 9) : 68;
        if (happy_) target = 42;
        if (sleepy_) target = 34;
        // 插值缓和状态切换；说话律动是视觉动画，不代表实时音量。
        eye_height_ += (target - eye_height_) * 0.25f;
        const int height = 4 + static_cast<int>((eye_height_ - 4) * openness);
        const int gaze = listening ? 0 : static_cast<int>(5 * std::sin(phase));
        const int vertical = speaking ? static_cast<int>(2 * std::sin(phase * 9)) : 0;
        for (int i = 0; i < 2; ++i) {
            lv_obj_set_size(eyes_[i], 68, height);
            lv_obj_set_pos(eyes_[i], 26 + i * 104 + gaze, 80 - height / 2 + vertical);
            lv_obj_set_style_radius(eyes_[i], happy_ ? 22 : 15, 0);
            lv_obj_set_style_bg_color(eyes_[i], lv_color_hex(listening ? 0x38E8FF : 0x00CFFF), 0);
        }
    }
};
