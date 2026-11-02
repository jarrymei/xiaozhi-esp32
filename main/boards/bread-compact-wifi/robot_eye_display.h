#pragma once

#include "waveshare_lcd_test_display.h"
#include "assets/singing_microphone.h"
#include <esp_random.h>
#include <cstring>
#include <cmath>
#include <algorithm>

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
        // 清爽陪伴：脸部固定在y=33～206，底部字幕另留独立空间。
        lv_obj_set_size(emoji_box_, 224, 174);
        lv_obj_align(emoji_box_, LV_ALIGN_TOP_MID, 0, 33);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(emoji_label_);
        face_ = lv_obj_create(emoji_box_);
        lv_obj_remove_style_all(face_);
        lv_obj_set_size(face_, 224, 174);
        lv_obj_remove_flag(face_, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < 2; ++i) {
            auto& eye = eyes_[i];
            eye = lv_obj_create(face_);
            lv_obj_remove_style_all(eye);
            lv_obj_set_size(eye, 84, 120);
            lv_obj_set_pos(eye, 22 + i * 96, 20);
            lv_obj_remove_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(eye, [](lv_event_t* event) {
                auto self = static_cast<RobotEyeDisplay*>(lv_event_get_user_data(event));
                const int index = lv_event_get_target(event) == self->eyes_[0] ? 0 : 1;
                self->PaintEye(lv_event_get_layer(event), index);
            }, LV_EVENT_DRAW_MAIN, this);
        }
        // 麦克风和小手只存在于屏幕内；不驱动实体舵机或修改音频播放。
        singing_overlay_ = lv_obj_create(face_);
        lv_obj_remove_style_all(singing_overlay_);
        lv_obj_set_size(singing_overlay_, 224, 90);
        lv_obj_set_pos(singing_overlay_, 0, 84);
        lv_obj_remove_flag(singing_overlay_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(singing_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(singing_overlay_, [](lv_event_t* event) {
            static_cast<RobotEyeDisplay*>(lv_event_get_user_data(event))->PaintSinging(lv_event_get_layer(event));
        }, LV_EVENT_DRAW_MAIN, this);
        // 打瞌睡装饰复用脸部容器，图片预览和系统提示仍能整体隐藏。
        drowsy_overlay_ = lv_obj_create(face_);
        lv_obj_remove_style_all(drowsy_overlay_);
        lv_obj_set_size(drowsy_overlay_, 64, 76);
        lv_obj_set_pos(drowsy_overlay_, 156, 8);
        lv_obj_remove_flag(drowsy_overlay_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(drowsy_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(drowsy_overlay_, [](lv_event_t* event) {
            static_cast<RobotEyeDisplay*>(lv_event_get_user_data(event))->PaintDrowsy(lv_event_get_layer(event));
        }, LV_EVENT_DRAW_MAIN, this);
        ConfigureCompanionLayout();
        ApplyFaceTheme();
        ActivateEyes();
        motion_started_ = state_started_ = last_motion_tick_ = lv_tick_get();
        next_blink_ = motion_started_ + 1700;
        animation_timer_ = lv_timer_create([](lv_timer_t* timer) {
            // 回调已持有 LVGL 锁，不在这里重复加锁或执行阻塞操作。
            static_cast<RobotEyeDisplay*>(lv_timer_get_user_data(timer))->DrawEyes();
        }, 50, this);
        if (!animation_timer_) ESP_LOGE("RobotEyes", "无法创建动画定时器");
        DrawEyes();
    }

    void SetInteractionState(DeviceState state) override {
        DisplayLockGuard lock(this);
        if (state_ != state) {
            state_started_ = lv_tick_get();
        }
        state_ = state;
        UpdateStatusPlacement();
        // 唤醒、断网或升级等状态优先；演示不会覆盖正常聆听和系统处理。
        if (singing_follows_music_ && state == kDeviceStateIdle) BeginSingingFinish();
        else if (state != kDeviceStateSpeaking && state != kDeviceStateIdle) StopSinging();
        if (state == kDeviceStateIdle || state == kDeviceStateListening) emotion_ = Emotion::Neutral;
        // 连接、升级和报错仍允许显示原来的图标及提示。
        if (face_ && (state == kDeviceStateIdle || state == kDeviceStateListening ||
                      state == kDeviceStateSpeaking)) {
            ActivateEyes();
        } else if (face_) {
            lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void SetNetworkConnecting(bool connecting) {
        DisplayLockGuard lock(this);
        network_connecting_ = connecting;
        if (connecting) StopSinging();
        UpdateStatusPlacement();
    }

    void SetEmotion(const char* emotion) override {
        if (!emotion) return;
        ESP_LOGI("RobotEyes", "[情绪 RX] %s", emotion);
        // 只有已核对的系统提示图标允许走基础显示；其他输入永远使用自定义双眼。
        if (IsSystemIcon(emotion)) {
            WaveshareLcdTestDisplay::SetEmotion(emotion);
            DisplayLockGuard lock(this);
            StopSinging();
            if (face_) lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
            if (NeedsCenteredStatus()) HideStartupGraphics();
            return;
        }
        DisplayLockGuard lock(this);
        emotion_ = MapEmotion(emotion);
        if (face_) ActivateEyes();
    }

    void SetTheme(Theme* theme) override {
        WaveshareLcdTestDisplay::SetTheme(theme);
        DisplayLockGuard lock(this);
        if (face_) {
            ConfigureCompanionLayout();
            ApplyFaceTheme();
        }
    }

    void SetChatMessage(const char* role, const char* content) override {
        // 恢复原字幕更新、字体和单行滚动，仍保留播放标记的动画联动。
        WaveshareLcdTestDisplay::SetChatMessage(role, content);
        if (!role || !content || std::strcmp(role, "assistant")) return;
        const std::string message(content);
        const auto first = message.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return;
        const auto last = message.find_last_not_of(" \t\r\n");
        // 该标记来自用户提供的实际日志；搜索音乐不触发，不猜其他服务的格式。
        if (message.compare(first, last - first + 1, "% play_music...") == 0) {
            SetSingingAnimation(true, 600, true);
        }
    }

    bool SetSingingAnimation(bool enabled, int duration_seconds, bool follow_music = false) {
        DisplayLockGuard lock(this);
        if (!singing_overlay_) return false;
        if (enabled && !CanShowEyes()) return false;
        if (!enabled) {
            StopSinging();
        } else {
            singing_ = true;
            singing_finishing_ = false;
            singing_started_ = lv_tick_get();
            singing_follows_music_ = follow_music;
            singing_until_ = lv_tick_get() + std::clamp(duration_seconds, 1, 600) * 1000;
            singing_step_ = -1;
            ActivateEyes();
            lv_obj_remove_flag(singing_overlay_, LV_OBJ_FLAG_HIDDEN);
        }
        ESP_LOGI("RobotEyes", "[唱歌动画] %s，来源=%s，最长%d秒；非音量同步",
                 enabled ? "开启" : "关闭", follow_music ? "云端play_music标记" : "MCP演示",
                 enabled ? duration_seconds : 0);
        return true;
    }

private:
    lv_obj_t* face_ = nullptr;
    lv_obj_t* eyes_[2] = {};
    lv_obj_t* singing_overlay_ = nullptr;
    lv_obj_t* drowsy_overlay_ = nullptr;
    bool drowsy_ = false;
    uint32_t drowsy_started_ = 0;
    uint32_t last_z_spawn_ = 0;
    struct ZParticle {
        bool active = false;
        uint32_t started = 0;
    } z_particles_[5];
    bool singing_ = false;
    bool singing_follows_music_ = false;
    bool singing_finishing_ = false;
    uint32_t singing_started_ = 0;
    uint32_t singing_finish_start_ = 0;
    uint32_t singing_until_ = 0;
    int singing_step_ = -1;
    lv_timer_t* animation_timer_ = nullptr;
    DeviceState state_ = kDeviceStateStarting;
    bool network_connecting_ = true;
    bool centered_status_ = false;
    bool status_placement_initialized_ = false;
    uint32_t next_blink_ = 0;
    uint32_t blink_start_ = 0;
    bool blinking_ = false;
    uint32_t motion_started_ = 0;
    uint32_t state_started_ = 0;
    uint32_t last_motion_tick_ = 0;
    enum class Emotion { Neutral, Happy, Confused, Sleepy, Sad, Thinking, Angry, Surprised };
    Emotion emotion_ = Emotion::Neutral;
    struct EyeShape {
        float width = 62;
        float height = 72;
        float y = 0;
        float lid = 0;
        float smile = 0;
        float tilt = 0;
        float drowsy = 0;
    } shapes_[2];
    struct EyeFrame {
        int width = 62;
        int height = -1;
        int y = 0;
        int lid = 0;
        int gaze = 0;
        int smile = 0;
        int tilt = 0;
        int openness = 100;
        int drowsy = 0;
    } frames_[2];

    struct DrowsyPose {
        float height;
        float y;
    };

    static DrowsyPose GetDrowsyPose(uint32_t elapsed) {
        struct Key { uint32_t time; float height; float y; };
        // 对齐认可预览：半睁、点头合眼、迷糊惊醒，再慢慢犯困；左右眼始终同步。
        static constexpr Key keys[] = {
            {0, 27, 4}, {1600, 24, 5}, {3100, 6, 10}, {4200, 6, 10},
            {4450, 40, -2}, {4900, 28, 3}, {6500, 24, 5}, {7600, 6, 10},
            {8600, 6, 9}, {9600, 27, 4}
        };
        const uint32_t phase = elapsed % 9600;
        for (size_t i = 1; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            if (phase <= keys[i].time) {
                const auto& a = keys[i - 1];
                const auto& b = keys[i];
                const float blend = Ease((phase - a.time) / static_cast<float>(b.time - a.time));
                return {a.height + (b.height - a.height) * blend, a.y + (b.y - a.y) * blend};
            }
        }
        return {27, 4};
    }

    void UpdateDrowsy(uint32_t now, bool enabled, const DrowsyPose& pose) {
        if (enabled != drowsy_) {
            drowsy_ = enabled;
            drowsy_started_ = now;
            last_z_spawn_ = now - 550;
            for (auto& particle : z_particles_) particle.active = false;
            blinking_ = false;
            next_blink_ = now + 1700;
            if (enabled) lv_obj_remove_flag(drowsy_overlay_, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(drowsy_overlay_, LV_OBJ_FLAG_HIDDEN);
        }
        if (!enabled) return;
        bool changed = false;
        for (auto& particle : z_particles_) {
            if (!particle.active) continue;
            changed = true;
            if (now - particle.started >= 2500) particle.active = false;
        }
        if (pose.height < 15 && now - last_z_spawn_ >= 550) {
            for (auto& particle : z_particles_) {
                if (particle.active) continue;
                particle = {true, now};
                last_z_spawn_ = now;
                changed = true;
                break;
            }
        }
        if (changed) lv_obj_invalidate(drowsy_overlay_);
    }

    void PaintDrowsy(lv_layer_t* layer) {
        lv_area_t bounds;
        lv_obj_get_coords(drowsy_overlay_, &bounds);
        const uint32_t now = lv_tick_get();
        for (const auto& particle : z_particles_) {
            if (!particle.active) continue;
            const float progress = std::clamp((now - particle.started) / 2500.0f, 0.0f, 1.0f);
            const float alpha = Ease(progress / 0.16f) * (1 - Ease((progress - 0.65f) / 0.35f));
            const float grow = Ease(progress);
            const float x = bounds.x1 + 24 + 13 * progress;
            const float y = bounds.y1 + 55 - 43 * progress;
            const float w = 6 + 6 * grow;
            const float h = 8 + 10 * grow;
            // 三条线组成小z，向右上方飘动、逐渐变大再淡出，不新增字体或图片。
            lv_draw_line_dsc_t line;
            lv_draw_line_dsc_init(&line);
            line.color = lv_color_hex(0x6AAEB4);
            line.opa = static_cast<lv_opa_t>(alpha * 191);
            line.width = progress < 0.5f ? 1 : 2;
            line.round_start = line.round_end = true;
            const lv_point_t points[] = {
                {static_cast<int32_t>(x - w / 2), static_cast<int32_t>(y - h / 2)},
                {static_cast<int32_t>(x + w / 2), static_cast<int32_t>(y - h / 2)},
                {static_cast<int32_t>(x - w / 2), static_cast<int32_t>(y + h / 2)},
                {static_cast<int32_t>(x + w / 2), static_cast<int32_t>(y + h / 2)}
            };
            for (int i = 0; i < 3; ++i) {
                line.p1 = {points[i].x, points[i].y};
                line.p2 = {points[i + 1].x, points[i + 1].y};
                lv_draw_line(layer, &line);
            }
        }
    }

    bool NeedsCenteredStatus() const {
        return network_connecting_ || state_ == kDeviceStateUnknown ||
            state_ == kDeviceStateStarting || state_ == kDeviceStateWifiConfiguring ||
            state_ == kDeviceStateActivating;
    }

    bool CanShowEyes() const {
        return !NeedsCenteredStatus() && (state_ == kDeviceStateIdle ||
            state_ == kDeviceStateListening || state_ == kDeviceStateSpeaking);
    }

    void HideStartupGraphics() {
        // 启动和联网阶段只显示原状态/通知文字，不让后续neutral或系统图标盖住文字。
        if (face_) lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_label_) lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_image_) lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }

    void UpdateStatusPlacement(bool force = false) {
        if (!face_ || !status_bar_) return;
        const bool centered = NeedsCenteredStatus();
        if (!force && status_placement_initialized_ && centered == centered_status_) return;
        const bool was_centered = centered_status_;
        centered_status_ = centered;
        status_placement_initialized_ = true;
        lv_obj_set_size(status_bar_, centered ? 204 : 132, centered ? 64 : 22);
        lv_obj_align(status_bar_, centered ? LV_ALIGN_CENTER : LV_ALIGN_TOP_MID, 0, centered ? 0 : 12);
        lv_obj_set_size(status_label_, centered ? 204 : 132, LV_SIZE_CONTENT);
        lv_label_set_long_mode(status_label_, centered ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_align(status_label_, centered ? LV_ALIGN_CENTER : LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_size(notification_label_, 204, centered ? LV_SIZE_CONTENT : 24);
        lv_label_set_long_mode(notification_label_, centered ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_bg_opa(notification_label_, centered ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_align(notification_label_, centered ? LV_ALIGN_CENTER : LV_ALIGN_TOP_MID, 0, centered ? 0 : 39);
        if (centered) HideStartupGraphics();
        else if (was_centered && CanShowEyes()) ActivateEyes();
    }

    void ConfigureCompanionLayout() {
        // 只修改本板实例，其他板型及公共LCD实现保持默认兼容。
        lv_obj_set_height(top_bar_, 34);
        lv_obj_set_style_pad_top(top_bar_, 12, 0);
        lv_obj_set_style_pad_bottom(top_bar_, 0, 0);
        lv_obj_set_style_pad_left(top_bar_, 18, 0);
        lv_obj_set_style_pad_right(top_bar_, 18, 0);
        lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(status_bar_, 132, 22);
        lv_obj_set_style_pad_all(status_bar_, 0, 0);
        lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 12);
        lv_obj_set_width(status_label_, 132);
        lv_obj_align(status_label_, LV_ALIGN_TOP_MID, 0, 0);

        // 通知使用独立的单行提示条，复用原显示/隐藏定时器，避免和网络图标争位。
        lv_obj_set_parent(notification_label_, lv_display_get_screen_active(display_));
        lv_obj_set_size(notification_label_, 204, 24);
        lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_pad_top(notification_label_, 3, 0);
        lv_obj_set_style_pad_bottom(notification_label_, 3, 0);
        lv_obj_set_style_pad_left(notification_label_, 4, 0);
        lv_obj_set_style_pad_right(notification_label_, 4, 0);
        lv_obj_set_style_bg_color(notification_label_, lv_color_hex(0x16272B), 0);
        lv_obj_set_style_bg_opa(notification_label_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(notification_label_, 7, 0);
        lv_obj_align(notification_label_, LV_ALIGN_TOP_MID, 0, 39);

        // 底部字幕完全沿用基础LCD的布局、字体、滚动及清空行为。
        UpdateStatusPlacement(true);
    }

    static bool IsSystemIcon(const char* name) {
        // 与application.cc现有激活、升级及错误提示对应，不能把任意未识别情绪放行。
        for (const char* icon : {"microchip_ai", "triangle_exclamation", "circle_xmark",
                                 "cloud_arrow_down", "cloud_slash", "link", "download"}) {
            if (!std::strcmp(name, icon)) return true;
        }
        return false;
    }

    static Emotion MapEmotion(const char* name) {
        struct Mapping { const char* name; Emotion emotion; };
        // 覆盖工程内全部21种对话情绪，同类情绪复用青色眼形，不加载旧图片或GIF。
        static constexpr Mapping mappings[] = {
            {"neutral", Emotion::Neutral}, {"happy", Emotion::Happy},
            {"laughing", Emotion::Happy}, {"funny", Emotion::Happy},
            {"sad", Emotion::Sad}, {"angry", Emotion::Angry},
            {"crying", Emotion::Sad}, {"loving", Emotion::Happy},
            {"embarrassed", Emotion::Confused}, {"surprised", Emotion::Surprised},
            {"shocked", Emotion::Surprised}, {"thinking", Emotion::Thinking},
            {"winking", Emotion::Happy}, {"cool", Emotion::Neutral},
            {"relaxed", Emotion::Neutral}, {"delicious", Emotion::Happy},
            {"kissy", Emotion::Happy}, {"confident", Emotion::Neutral},
            {"sleepy", Emotion::Sleepy}, {"silly", Emotion::Happy},
            {"confused", Emotion::Confused},
        };
        for (const auto& mapping : mappings) {
            if (!std::strcmp(name, mapping.name)) return mapping.emotion;
        }
        ESP_LOGW("RobotEyes", "未映射情绪'%s'，使用自定义正常双眼", name);
        return Emotion::Neutral;
    }

    static float Ease(float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3 - 2 * value);
    }

    void StopSinging() {
        singing_ = false;
        singing_finishing_ = false;
        singing_follows_music_ = false;
        if (singing_overlay_) lv_obj_add_flag(singing_overlay_, LV_OBJ_FLAG_HIDDEN);
    }

    void BeginSingingFinish() {
        if (!singing_ || singing_finishing_) return;
        singing_finishing_ = true;
        singing_finish_start_ = lv_tick_get();
        singing_step_ = -1;
        // 先用250毫秒提麦，再停留500毫秒；仅自然结束或演示到时走收尾。
    }

    void PaintSinging(lv_layer_t* layer) {
        lv_area_t bounds;
        lv_obj_get_coords(singing_overlay_, &bounds);
        const uint32_t now = lv_tick_get();
        const float phase = (now - singing_started_) * 0.001f;
        const int lift = singing_finishing_ ?
            static_cast<int>(6 * Ease((now - singing_finish_start_) / 250.0f)) :
            static_cast<int>(std::lround(1.5f * std::sin(phase * 4)));
        lv_draw_image_dsc_t microphone;
        lv_draw_image_dsc_init(&microphone);
        microphone.src = &kSingingMicrophoneImage;
        microphone.pivot = {0, 0};
        microphone.scale_x = microphone.scale_y = 219;  // 84像素素材约缩至72像素，给字幕留出空间。
        microphone.rotation = singing_finishing_ ? 0 : static_cast<int>(20 * std::sin(phase * 4));
        microphone.antialias = true;
        lv_area_t sprite_area = {bounds.x1 + 126, bounds.y1 + 12 - lift,
                                bounds.x1 + 209, bounds.y1 + 95 - lift};
        lv_draw_image(layer, &microphone, &sprite_area);
        auto rect = [&](int x, int y, int w, int h, int radius, uint32_t color) {
            lv_draw_rect_dsc_t dsc;
            lv_draw_rect_dsc_init(&dsc);
            dsc.bg_color = lv_color_hex(color);
            dsc.bg_opa = LV_OPA_COVER;
            dsc.radius = radius;
            lv_area_t area = {bounds.x1 + x, bounds.y1 + y + lift,
                              bounds.x1 + x + w - 1, bounds.y1 + y + lift + h - 1};
            lv_draw_rect(layer, &dsc, &area);
        };
        auto line = [&](int x1, int y1, int x2, int y2, int width, uint32_t color) {
            lv_draw_line_dsc_t dsc;
            lv_draw_line_dsc_init(&dsc);
            dsc.color = lv_color_hex(color);
            dsc.width = width;
            dsc.round_start = true;
            dsc.round_end = true;
            dsc.p1 = {bounds.x1 + x1, bounds.y1 + y1 + lift};
            dsc.p2 = {bounds.x1 + x2, bounds.y1 + y2 + lift};
            lv_draw_line(layer, &dsc);
        };
        // 小手与麦克风为同一透明素材，保持已确认的手背朝外、向内握持关系。
        if (singing_finishing_) return;
        // 音符是装饰节奏，不读取音量，也不宣称与歌曲节拍同步。
        const int float_y = (singing_step_ % 12) * 2;
        const int note_x = singing_step_ % 24 < 12 ? 39 : 89;
        rect(note_x, 49 - float_y, 12, 8, 4, 0x2CE5ED);
        line(note_x + 10, 51 - float_y, note_x + 10, 29 - float_y, 3, 0x2CE5ED);
        line(note_x + 10, 29 - float_y, note_x + 18, 32 - float_y, 3, 0x2CE5ED);
    }

    void PaintEye(lv_layer_t* layer, int index) {
        const auto& f = frames_[index];
        if (f.height < 0) return;
        lv_area_t bounds;
        lv_obj_get_coords(eyes_[index], &bounds);
        const int x = bounds.x1 + 42 + f.gaze;
        const int y = bounds.y1 + 60 + f.y;
        const float openness = std::max(0.055f, f.openness / 100.0f);
        const int height = std::max(4, static_cast<int>(f.height * openness));
        const float shut = f.drowsy / 100.0f * (1 - Ease((f.height - 6) / 9.0f));
        lv_draw_rect_dsc_t rect;
        lv_draw_rect_dsc_init(&rect);
        rect.bg_color = lv_color_hex(0x2CE5ED);
        rect.bg_opa = static_cast<lv_opa_t>(255 * (100 - f.smile) / 100 * (1 - shut));
        rect.radius = std::min(18, height / 2);
        lv_area_t area = {x - f.width / 2, y - height / 2,
                          x - f.width / 2 + f.width - 1, y - height / 2 + height - 1};
        if (rect.bg_opa) lv_draw_rect(layer, &rect, &area);
        if (f.drowsy && height > 12 && rect.bg_opa) {
            // 保留圆润下眼缘，用少量黑色三角形拼出缓缓垂下的弧形上眼睑。
            lv_draw_triangle_dsc_t lid;
            lv_draw_triangle_dsc_init(&lid);
            lid.color = lv_color_black();
            lid.opa = static_cast<lv_opa_t>(rect.bg_opa * f.drowsy / 100);
            for (int i = 0; i < 8; ++i) {
                const float a = i / 8.0f;
                const float b = (i + 1) / 8.0f;
                const int x1 = area.x1 + static_cast<int>(f.width * a);
                const int x2 = area.x1 + static_cast<int>(f.width * b);
                const int y1 = area.y1 + static_cast<int>(2 + 20 * a * (1 - a));
                const int y2 = area.y1 + static_cast<int>(2 + 20 * b * (1 - b));
                lid.p[0] = {x1, area.y1}; lid.p[1] = {x2, area.y1}; lid.p[2] = {x2, y2};
                lv_draw_triangle(layer, &lid);
                lid.p[0] = {x1, area.y1}; lid.p[1] = {x2, y2}; lid.p[2] = {x1, y1};
                lv_draw_triangle(layer, &lid);
            }
        }
        if (shut > 0.005f) {
            // 轻柔下弯的闭眼线区别于开心笑眼，和半睁眼形交叉过渡。
            lv_draw_line_dsc_t line;
            lv_draw_line_dsc_init(&line);
            line.color = rect.bg_color;
            line.opa = static_cast<lv_opa_t>(255 * shut);
            line.width = 6;
            line.round_start = line.round_end = true;
            for (int i = 0; i < 12; ++i) {
                const float a = i / 12.0f;
                const float b = (i + 1) / 12.0f;
                line.p1 = {static_cast<int32_t>(x - 23 + 46 * a),
                           static_cast<int32_t>(y - 2 + 18 * a * (1 - a))};
                line.p2 = {static_cast<int32_t>(x - 23 + 46 * b),
                           static_cast<int32_t>(y - 2 + 18 * b * (1 - b))};
                lv_draw_line(layer, &line);
            }
        }
        if (f.lid && height > 12 && rect.bg_opa) {
            // 待机眼睑只轻压上沿，仍保持已认可的对称轮廓。
            lv_draw_rect_dsc_t lid;
            lv_draw_rect_dsc_init(&lid);
            lid.bg_color = lv_color_black();
            lid.bg_opa = rect.bg_opa;
            lv_area_t lid_area = {area.x1, area.y1, area.x2,
                                 area.y1 + std::max(1, static_cast<int>(f.lid * openness)) - 1};
            lv_draw_rect(layer, &lid, &lid_area);
        }
        if (f.tilt && height > 12 && rect.bg_opa) {
            // 黑色三角眼睑覆盖上沿，左右镜像表达委屈或疑惑。
            lv_draw_triangle_dsc_t lid;
            lv_draw_triangle_dsc_init(&lid);
            lid.color = lv_color_black();
            lid.opa = rect.bg_opa;
            lid.p[0] = {area.x1, area.y1};
            lid.p[1] = {area.x2, area.y1};
            lid.p[2] = {f.tilt > 0 ? area.x1 : area.x2,
                        area.y1 + static_cast<int>(std::abs(f.tilt) * openness)};
            lv_draw_triangle(layer, &lid);
        }
        if (f.smile) {
            // 用固定数量的短线逼近弯月轮廓，不创建逐帧对象或图片。
            lv_draw_line_dsc_t line;
            lv_draw_line_dsc_init(&line);
            line.color = rect.bg_color;
            line.opa = static_cast<lv_opa_t>(255 * f.smile / 100);
            line.width = std::max(4, static_cast<int>(12 * openness));
            line.round_start = true;
            line.round_end = true;
            for (int segment = 0; segment < 20; ++segment) {
                const float a = segment / 20.0f;
                const float b = (segment + 1) / 20.0f;
                line.p1 = {static_cast<int32_t>(x - 26 + 52 * a),
                           static_cast<int32_t>(y + (8 - 80 * a * (1 - a)) * openness)};
                line.p2 = {static_cast<int32_t>(x - 26 + 52 * b),
                           static_cast<int32_t>(y + (8 - 80 * b * (1 - b)) * openness)};
                lv_draw_line(layer, &line);
            }
        }
    }

    void ActivateEyes() {
        if (!CanShowEyes()) {
            if (NeedsCenteredStatus()) HideStartupGraphics();
            else if (face_) lv_obj_add_flag(face_, LV_OBJ_FLAG_HIDDEN);
            return;
        }
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
        // 用实际经过时间计算平滑系数，定时器略有延迟时仍保持相近的过渡速度。
        const float step = std::min<uint32_t>(now - last_motion_tick_, 150) / 50.0f;
        last_motion_tick_ = now;
        const float shape_blend = 1 - std::pow(0.70f, step);
        if (singing_ && !singing_finishing_ && static_cast<int32_t>(now - singing_until_) >= 0)
            BeginSingingFinish();
        if (singing_finishing_ && now - singing_finish_start_ >= 750) StopSinging();
        const bool visible = !lv_obj_has_flag(face_, LV_OBJ_FLAG_HIDDEN) &&
            !lv_obj_has_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        const bool sleeping = state_ == kDeviceStateIdle && !singing_ && CanShowEyes() && visible;
        const uint32_t sleep_elapsed = sleeping && drowsy_ ? now - drowsy_started_ : 0;
        const auto sleep_pose = GetDrowsyPose(sleep_elapsed);
        UpdateDrowsy(now, sleeping, sleep_pose);
        // 打盹本身控制开合，不再叠加常规眨眼；唤醒后重新安排下一次眨眼。
        if (!sleeping && !blinking_ && static_cast<int32_t>(now - next_blink_) >= 0) {
            blinking_ = true;
            blink_start_ = now;
        }
        float openness = 1;
        if (blinking_) {
            const uint32_t elapsed = now - blink_start_;
            if (elapsed < 75) openness = 1 - Ease(elapsed / 75.0f);
            else if (elapsed < 100) openness = 0;
            else if (elapsed < 245) openness = Ease((elapsed - 100) / 145.0f);
            else {
                blinking_ = false;
                next_blink_ = now + 4200 + esp_random() % 1400;
            }
        }
        if (!visible) return;

        const bool listening = state_ == kDeviceStateListening;
        const bool speaking = state_ == kDeviceStateSpeaking;
        const float phase = (now - motion_started_) * 0.001f;
        const uint32_t state_elapsed = now - state_started_;
        const float focus = listening && state_elapsed < 650 ?
            std::sin(state_elapsed / 650.0f * 3.14159265f) : 0;
        const float phrase = (state_elapsed % 3600) * 0.001f;
        const float envelope = speaking && phrase < 2.5f ?
            std::pow(std::sin(phrase * 3.14159265f / 2.5f), 2) : 0;
        for (int i = 0; i < 2; ++i) {
            EyeShape target;
            target.width = listening ? 63 : speaking ? 62 + 0.5f * envelope : 62;
            target.height = listening ? 80 + 3 * focus : speaking ?
                71 + 2.4f * envelope * std::sin(state_elapsed * 0.0073f) :
                71 + 1.3f * std::sin(phase * 1.1f);
            target.y = listening ? -3 : speaking ? -0.6f * envelope : 0;
            target.lid = state_ == kDeviceStateIdle && emotion_ == Emotion::Neutral ? 1.3f : 0;
            if (emotion_ == Emotion::Happy) target.smile = 100;
            else if (emotion_ == Emotion::Sleepy) { target.height = 21; target.y = 9; }
            else if (emotion_ == Emotion::Confused) {
                target.height = i ? 80 : 45;
                target.y = i ? -9 : 8;
                target.tilt = i ? -7 : 9;
            } else if (emotion_ == Emotion::Sad) {
                target.height = 48;
                target.y = 5;
                target.tilt = i ? -12 : 12;
            } else if (emotion_ == Emotion::Angry) {
                target.height = 48;
                target.tilt = i ? 12 : -12;
            } else if (emotion_ == Emotion::Surprised) {
                target.height = 88;
                target.y = -3;
            } else if (emotion_ == Emotion::Thinking) { target.y = -8; }
            if (sleeping) {
                // 空闲时用打瞌睡取代巡视；这只是屏幕动画，不改变唤醒、音频或供电状态。
                target.width = 58;
                target.height = sleep_pose.height;
                target.y = sleep_pose.y + std::sin(sleep_elapsed * 0.001f * 6.2831853f / 4.8f);
                target.lid = target.smile = target.tilt = 0;
                target.drowsy = 100;
            }
            if (singing_) {
                target.width = 62;
                target.lid = 0;
                const uint32_t elapsed = now - singing_started_;
                const float singing_phase = elapsed * 0.001f;
                target.height = 58 + 2 * std::sin(singing_phase * 4);
                target.y = -12 + std::sin(singing_phase * 4);
                // 每十秒短暂陶醉一次；双眼始终对称，不采用被否决的wink与歪脸。
                const uint32_t moment = elapsed % 10000;
                target.smile = singing_finishing_ || (moment >= 5500 && moment < 6800) ? 100 : 0;
                target.tilt = 0;
            }
            auto& shape = shapes_[i];
            // 普通眼形使用较柔和的过渡；保留已确认的唱歌动作收敛速度。
            const float blend = sleeping || (!sleeping && shape.drowsy > 0.5f) ?
                1 - std::pow(0.45f, step) : singing_ ? 1 - std::pow(0.55f, step) : shape_blend;
            shape.width += (target.width - shape.width) * blend;
            shape.height += (target.height - shape.height) * blend;
            shape.y += (target.y - shape.y) * blend;
            shape.lid += (target.lid - shape.lid) * blend;
            shape.smile += (target.smile - shape.smile) * blend;
            shape.tilt += (target.tilt - shape.tilt) * blend;
            shape.drowsy += (target.drowsy - shape.drowsy) * blend;
            EyeFrame frame;
            frame.width = std::lround(shape.width);
            frame.height = std::lround(shape.height);
            frame.y = std::lround(shape.y);
            frame.lid = std::lround(shape.lid);
            frame.smile = std::lround(shape.smile);
            frame.tilt = std::lround(shape.tilt);
            frame.gaze = 0;
            frame.openness = std::lround(openness * 100);
            frame.drowsy = std::lround(shape.drowsy);
            const auto& old = frames_[i];
            // 只有可见几何变化才失效局部区域，待机不重复刷新整张脸。
            if (frame.width != old.width || frame.lid != old.lid ||
                frame.height != old.height || frame.y != old.y || frame.smile != old.smile ||
                frame.tilt != old.tilt || frame.gaze != old.gaze || frame.openness != old.openness ||
                frame.drowsy != old.drowsy) {
                frames_[i] = frame;
                lv_obj_invalidate(eyes_[i]);
            }
        }
        if (singing_) {
            // 装饰层每100毫秒更新，复用原定时器，不新增任务。
            const int step = (now / 100) % 24;
            if (step != singing_step_) {
                singing_step_ = step;
                lv_obj_invalidate(singing_overlay_);
            }
        }
    }
};
