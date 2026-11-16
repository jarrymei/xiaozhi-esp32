#ifndef BREAD_COMPACT_FOUR_SERVO_SELF_TEST_H
#define BREAD_COMPACT_FOUR_SERVO_SELF_TEST_H

#include <array>
#include "config.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 每次启动四路一起小幅往返一轮，不依赖联网或MCP。
class FourServoSelfTest {
private:
    static constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
    static constexpr ledc_timer_t kTimer = LEDC_TIMER_3;
    static constexpr uint32_t kPeriodUs = 20000;
    static constexpr uint32_t kCounts = 1U << 14;
    const std::array<gpio_num_t, 4> pins_ = {
        FOUR_SERVO_J1_GPIO, FOUR_SERVO_J2_GPIO, FOUR_SERVO_J3_GPIO, FOUR_SERVO_J4_GPIO};
    // 通道0/定时器0保留给LCD背光。
    const std::array<ledc_channel_t, 4> channels_ = {
        LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3, LEDC_CHANNEL_4};

    static void TaskEntry(void* context) {
        static_cast<FourServoSelfTest*>(context)->Run();
        vTaskDelete(nullptr);
    }

    void Run() {
        ESP_LOGI("FourServoTest", "[WAIT] 5秒后J1到J4一起自动测试一轮，请空载观察供电与动作");
        // 延时只发生在独立测试任务，不阻塞主循环、显示或音频路径。
        vTaskDelay(pdMS_TO_TICKS(5000));
        constexpr uint32_t pulses[] = {1500, 1400, 1500, 1600, 1500};
        ESP_LOGI("FourServoTest", "[BEGIN] 四路同时动作测试：GPIO%d/%d/%d/%d",
                 static_cast<int>(pins_[0]), static_cast<int>(pins_[1]),
                 static_cast<int>(pins_[2]), static_cast<int>(pins_[3]));
        for (int step = 0; step < 5; ++step) {
            const uint32_t duty = pulses[step] * kCounts / kPeriodUs;
            // 先为四路写入相同占空比，再紧邻更新；软件调用不保证硬件同沿生效。
            for (int i = 0; i < 4; ++i) {
                ESP_ERROR_CHECK(ledc_set_duty(kMode, channels_[i], duty));
            }
            for (int i = 0; i < 4; ++i) {
                ESP_ERROR_CHECK(ledc_update_duty(kMode, channels_[i]));
            }
            ESP_LOGI("FourServoTest", "[STEP %d/5] 四路共同脉宽指令=%lu微秒，占空比计数=%lu，保持2秒",
                     step + 1, static_cast<unsigned long>(pulses[step]), static_cast<unsigned long>(duty));
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        // 一轮结束后四路全部归零，不持续输出保持脉冲。
        for (int i = 0; i < 4; ++i) {
            ESP_ERROR_CHECK(ledc_set_duty(kMode, channels_[i], 0));
        }
        for (int i = 0; i < 4; ++i) {
            ESP_ERROR_CHECK(ledc_update_duty(kMode, channels_[i]));
            ESP_LOGI("FourServoTest", "[RELEASE] J%d GPIO%d PWM归零", i + 1, static_cast<int>(pins_[i]));
        }
        ESP_LOGI("FourServoTest", "[DONE] 四路同时测试一轮完成，全部PWM归零，不循环；复位后重跑，换线须断电。日志不是实测结果");
    }

public:
    FourServoSelfTest() {
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
            ESP_LOGI("FourServoTest", "[READY] J%d GPIO%d 定时器3/通道%d，50Hz，初始无脉冲",
                     i + 1, static_cast<int>(pins_[i]), static_cast<int>(channels_[i]));
        }
        // 静态实例仅创建一个任务，一轮完成后任务自删。
        ESP_ERROR_CHECK(xTaskCreate(TaskEntry, "four_servo_test", 3072, this, 2, nullptr) == pdPASS
                            ? ESP_OK : ESP_ERR_NO_MEM);
    }

    FourServoSelfTest(const FourServoSelfTest&) = delete;
    FourServoSelfTest& operator=(const FourServoSelfTest&) = delete;
};

#endif
