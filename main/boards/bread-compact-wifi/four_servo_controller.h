#ifndef BREAD_COMPACT_FOUR_SERVO_CONTROLLER_H
#define BREAD_COMPACT_FOUR_SERVO_CONTROLLER_H

#include <array>
#include <mutex>
#include <string>
#include "config.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mcp_server.h"

// 四颗空载舵机的单路或共同角度控制，不包含步态、自动扫动或上电回中。
class FourServoController {
private:
    static constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
    static constexpr ledc_timer_t kTimer = LEDC_TIMER_3;
    static constexpr uint32_t kPeriodUs = 20000;
    static constexpr uint32_t kCounts = 1U << 14;
    // 语音控制已确认后逐步放宽测试范围，仍保留机械端点余量，实际行程待校准。
    static constexpr int kMinAngle = 60;
    static constexpr int kMaxAngle = 120;
    const std::array<gpio_num_t, 4> pins_ = {
        FOUR_SERVO_J1_GPIO, FOUR_SERVO_J2_GPIO, FOUR_SERVO_J3_GPIO, FOUR_SERVO_J4_GPIO};
    // 背光独占通道0/定时器0；四路共用50Hz定时器3，各有独立占空比。
    const std::array<ledc_channel_t, 4> channels_ = {
        LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3, LEDC_CHANNEL_4};
    std::array<bool, 4> enabled_{};
    std::array<int, 4> last_angle_ = {-1, -1, -1, -1};
    std::mutex mutex_;

    ReturnValue SetAngle(int servo, int angle) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (servo < 1 || servo > 4 || angle < kMinAngle || angle > kMaxAngle) {
            return std::string("参数超出当前测试范围：接口1到4，角度60到120");
        }
        const int i = servo - 1;
        // 暂按常见500～2500微秒映射估算角度，90度为1500微秒；实际角度待校准。
        const uint32_t pulse_us = 500U + static_cast<uint32_t>(angle) * 2000U / 180U;
        const uint32_t duty = pulse_us * kCounts / kPeriodUs;
        ESP_ERROR_CHECK(ledc_set_duty(kMode, channels_[i], duty));
        ESP_ERROR_CHECK(ledc_update_duty(kMode, channels_[i]));
        enabled_[i] = true;
        last_angle_[i] = angle;
        ESP_LOGI("FourServo", "[SET] J%d GPIO%d 指令角度=%d 脉宽=%lu微秒（非实测角度）",
                 servo, static_cast<int>(pins_[i]), angle, static_cast<unsigned long>(pulse_us));
        return std::string("已向J") + std::to_string(servo) + "输出角度指令；其余接口保持原状，实际位置未测量";
    }

    ReturnValue SetAllAngle(int angle) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (angle < kMinAngle || angle > kMaxAngle) {
            return std::string("参数超出当前测试范围：角度60到120");
        }
        const uint32_t pulse_us = 500U + static_cast<uint32_t>(angle) * 2000U / 180U;
        const uint32_t duty = pulse_us * kCounts / kPeriodUs;
        // 先写四路再紧邻更新，不保证硬件在同一边沿生效或实际转速一致。
        for (int i = 0; i < 4; ++i) {
            ESP_ERROR_CHECK(ledc_set_duty(kMode, channels_[i], duty));
        }
        for (int i = 0; i < 4; ++i) {
            ESP_ERROR_CHECK(ledc_update_duty(kMode, channels_[i]));
            enabled_[i] = true;
            last_angle_[i] = angle;
        }
        ESP_LOGI("FourServo", "[SET ALL] J1到J4 指令角度=%d 脉宽=%lu微秒（非实测角度）",
                 angle, static_cast<unsigned long>(pulse_us));
        return std::string("四路已输出相同角度指令并持续保持脉冲；实际位置未测量");
    }

    ReturnValue Release(int servo) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (servo < 0 || servo > 4) return std::string("接口应为0到4，0表示全部");
        for (int i = 0; i < 4; ++i) {
            if (servo != 0 && servo != i + 1) continue;
            // 占空比归零保留LEDC通道，可由下一次明确角度指令重新启用。
            ESP_ERROR_CHECK(ledc_set_duty(kMode, channels_[i], 0));
            ESP_ERROR_CHECK(ledc_update_duty(kMode, channels_[i]));
            enabled_[i] = false;
            ESP_LOGI("FourServo", "[RELEASE] J%d GPIO%d PWM已归零，舵机电源仍接通",
                     i + 1, static_cast<int>(pins_[i]));
        }
        return std::string("指定接口已停止发送脉冲；不保证保持位置，未切断舵机电源");
    }

    ReturnValue GetState() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string result = "{\"servos\":[";
        for (int i = 0; i < 4; ++i) {
            if (i) result += ",";
            result += "{\"servo\":" + std::to_string(i + 1) +
                      ",\"gpio\":" + std::to_string(static_cast<int>(pins_[i])) +
                      ",\"pwm_enabled\":" + (enabled_[i] ? "true" : "false") +
                      ",\"last_command_angle\":" +
                      (last_angle_[i] < 0 ? std::string("null") : std::to_string(last_angle_[i])) + "}";
        }
        return result + "],\"position_measured\":false}";
    }

public:
    FourServoController() {
        ledc_timer_config_t timer = {};
        timer.speed_mode = kMode;
        timer.timer_num = kTimer;
        timer.duty_resolution = LEDC_TIMER_14_BIT;
        timer.freq_hz = 50;
        timer.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&timer));
        for (int i = 0; i < 4; ++i) {
            ledc_channel_config_t channel = {};
            channel.gpio_num = pins_[i];
            channel.speed_mode = kMode;
            channel.channel = channels_[i];
            channel.timer_sel = kTimer;
            channel.intr_type = LEDC_INTR_DISABLE;
            channel.duty = 0;
            ESP_ERROR_CHECK(ledc_channel_config(&channel));
            ESP_LOGI("FourServo", "[READY] J%d GPIO%d 通道%d，启动无脉冲，不自动回中",
                     i + 1, static_cast<int>(pins_[i]), static_cast<int>(channels_[i]));
        }
        auto& server = McpServer::GetInstance();
        server.AddTool("self.servos.set_angle",
            "空载台架测试：只在用户明确要求控制编号舵机时使用。servo为J1到J4的编号1到4；"
            "angle仅60到120度，90为估算中位。首次启用可能直接转到目标，其他路保持原状。"
            "到位后持续保持脉冲，不代表测得实际角度；不要用来执行行走。",
            PropertyList({Property("servo", kPropertyTypeInteger, 1, 4),
                          Property("angle", kPropertyTypeInteger, kMinAngle, kMaxAngle)}),
            [this](const PropertyList& p) -> ReturnValue {
                return SetAngle(p["servo"].value<int>(), p["angle"].value<int>());
            });
        server.AddTool("self.servos.set_all_angles",
            "在用户明确要求四个舵机一起转到同一角度或全部回中时使用。angle仅60到120度，"
            "90为估算中位；四路输出相同目标并保持脉冲，不执行循环摆动或步态。实际角度未测量。",
            PropertyList({Property("angle", kPropertyTypeInteger, kMinAngle, kMaxAngle)}),
            [this](const PropertyList& p) -> ReturnValue { return SetAllAngle(p["angle"].value<int>()); });
        server.AddTool("self.servos.release",
            "停止指定舵机的PWM脉冲，servo为1到4，0为全部。测试结束或要求释放舵机时使用。"
            "不是保持角度的停止，不切断5V电源，不保证消除堵转；异常需物理断电。",
            PropertyList({Property("servo", kPropertyTypeInteger, 0, 4)}),
            [this](const PropertyList& p) -> ReturnValue { return Release(p["servo"].value<int>()); });
        server.AddTool("self.servos.get_state",
            "查询四个接口的GPIO、PWM启用状态和最后指令角度；不是实测位置或到位检测。",
            PropertyList(), [this](const PropertyList&) -> ReturnValue { return GetState(); });
    }

    FourServoController(const FourServoController&) = delete;
    FourServoController& operator=(const FourServoController&) = delete;
};

#endif
