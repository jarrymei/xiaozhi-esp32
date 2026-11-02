#ifndef __LAMP_CONTROLLER_H__
#define __LAMP_CONTROLLER_H__

#include <string>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "mcp_server.h"

class LampController {
private:
    gpio_num_t gpio_num_;

    // 当前亮度：0 表示熄灭，100 表示最大亮度
    int brightness_ = 0;

    // 记住上一次非零亮度，重新开灯时恢复
    int last_brightness_ = 50;

    // 使用 10 位分辨率，占空比数值取 0～1023
    static constexpr uint32_t kMaxDuty = 1023;
    static constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
    static constexpr ledc_timer_t kTimer = LEDC_TIMER_2;
    static constexpr ledc_channel_t kChannel = LEDC_CHANNEL_2;

public:
    explicit LampController(gpio_num_t gpio_num)
        : gpio_num_(gpio_num) {
        // 配置定时器：决定 PWM 频率和占空比分辨率
        ledc_timer_config_t timer_config = {};
        timer_config.speed_mode = kMode;
        timer_config.duty_resolution = LEDC_TIMER_10_BIT;
        timer_config.timer_num = kTimer;
        timer_config.freq_hz = 5000;
        timer_config.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

        // 将 PWM 通道连接到 GPIO，初始占空比为零
        ledc_channel_config_t channel_config = {};
        channel_config.gpio_num = gpio_num_;
        channel_config.speed_mode = kMode;
        channel_config.channel = kChannel;
        channel_config.intr_type = LEDC_INTR_DISABLE;
        channel_config.timer_sel = kTimer;
        channel_config.duty = 0;
        channel_config.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

        auto& server = McpServer::GetInstance();

        // 查询实际记录的开关状态和亮度
        server.AddTool(
            "self.lamp.get_state",
            "查询灯的开关状态和亮度，亮度范围为0到100。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                return std::string("{\"power\":") +
                    (brightness_ > 0 ? "true" : "false") +
                    ",\"brightness\":" +
                    std::to_string(brightness_) + "}";
            });

        // 开灯时恢复上一次的非零亮度
        server.AddTool(
            "self.lamp.turn_on",
            "打开灯，恢复上一次的亮度。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                SetBrightness(last_brightness_);
                return true;
            });

        // 关灯，但保留上一次亮度记录
        server.AddTool(
            "self.lamp.turn_off",
            "关闭灯。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                SetBrightness(0);
                return true;
            });

        // 将大模型传入的百分比转换为 PWM 占空比
        server.AddTool(
            "self.lamp.set_brightness",
            "设置灯的亮度，范围为0到100，0为关闭，100为最亮。",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                SetBrightness(
                    properties["brightness"].value<int>());
                return true;
            });
    }

    void SetBrightness(int percent) {
        // 限制参数范围，避免超出允许的占空比
        if (percent < 0) {
            percent = 0;
        }
        if (percent > 100) {
            percent = 100;
        }

        // 将百分比映射到 0～1023
        const uint32_t duty = kMaxDuty * percent / 100;

        // 设置新占空比，再通知硬件生效
        ESP_ERROR_CHECK(ledc_set_duty(kMode, kChannel, duty));
        ESP_ERROR_CHECK(ledc_update_duty(kMode, kChannel));

        brightness_ = percent;
        if (percent > 0) {
            last_brightness_ = percent;
        }
    }
};

#endif