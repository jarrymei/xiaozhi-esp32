#pragma once

#include "config.h"
#include "display/lcd_display.h"

// 仅为这块板增加开机测试覆盖层，不修改其他开发板共用的显示实现。
class WaveshareLcdTestDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    void SetupUI() override {
        if (IsSetupUICalled()) {
            return;
        }
        SpiLcdDisplay::SetupUI();

#if DISPLAY_STARTUP_TEST_MS > 0
        DisplayLockGuard lock(this);
        if (display_ == nullptr) {
            ESP_LOGE("WaveshareLCD", "[LCD TEST] LVGL显示对象不存在");
            return;
        }

        // 覆盖层不替换小智主界面，定时移除后立即露出正常状态与表情。
        auto overlay = lv_obj_create(lv_display_get_layer_top(display_));
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, width_, height_);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        const uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF};
        const char* labels[] = {"R", "G", "B"};
        for (int i = 0; i < 3; ++i) {
            auto strip = lv_obj_create(overlay);
            lv_obj_remove_style_all(strip);
            lv_obj_set_pos(strip, i * width_ / 3, 0);
            lv_obj_set_size(strip, (i + 1) * width_ / 3 - i * width_ / 3, height_);
            lv_obj_set_style_bg_color(strip, lv_color_hex(colors[i]), 0);
            lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
            auto name = lv_label_create(strip);
            lv_label_set_text(name, labels[i]);
            lv_obj_set_style_text_color(name, lv_color_black(), 0);
            lv_obj_set_style_bg_color(name, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(name, LV_OPA_COVER, 0);
            lv_obj_align(name, LV_ALIGN_CENTER, 0, 0);
        }

        // 四边白框及上下标签用于检查裁切、方向和20行显存偏移。
        auto border = lv_obj_create(overlay);
        lv_obj_remove_style_all(border);
        lv_obj_set_pos(border, 6, 6);
        lv_obj_set_size(border, width_ - 12, height_ - 12);
        lv_obj_set_style_border_width(border, 2, 0);
        lv_obj_set_style_border_color(border, lv_color_white(), 0);
        auto title = lv_label_create(overlay);
        lv_label_set_text(title, "TOP  240x280");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);
        auto footer = lv_label_create(overlay);
        lv_label_set_text(footer, "BOTTOM  ST7789");
        lv_obj_set_style_text_color(footer, lv_color_white(), 0);
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -24);

        auto timer = lv_timer_create([](lv_timer_t* timer) {
            // LVGL定时器回调已在图形任务中执行，无需再次申请显示锁。
            auto overlay = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
            lv_obj_delete(overlay);
            ESP_LOGI("WaveshareLCD", "[LCD TEST DONE] 测试层已移除，进入小智界面");
        }, DISPLAY_STARTUP_TEST_MS, overlay);
        if (timer == nullptr) {
            lv_obj_delete(overlay);
            ESP_LOGE("WaveshareLCD", "[LCD TEST] 无法创建测试定时器，直接使用小智界面");
            return;
        }
        lv_timer_set_repeat_count(timer, 1);
        ESP_LOGI("WaveshareLCD", "[LCD TEST] 显示红绿蓝及边框%u毫秒，请观察是否正确",
                 static_cast<unsigned>(DISPLAY_STARTUP_TEST_MS));
#endif
    }
};
