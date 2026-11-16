#ifndef BREAD_COMPACT_SERVO17_SELF_TEST_H
#define BREAD_COMPACT_SERVO17_SELF_TEST_H

#include "config.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 排障期间每次启动只执行一轮，不依赖联网、语音或MCP调用。
class Servo17SelfTest {
private:
    static constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
    static constexpr ledc_timer_t kTimer = LEDC_TIMER_3;
    static constexpr ledc_channel_t kChannel = LEDC_CHANNEL_3;
    static constexpr uint32_t kPeriodUs = 20000;
    static constexpr uint32_t kCounts = 1U << 14;

    static void Run(void*) {
        ESP_LOGI("Servo17Test", "[WAIT] GPIO%d本地自检，5秒后自动动作，空载且只接一颗",
                 static_cast<int>(SERVO17_SELF_TEST_GPIO));
        // 只阻塞独立测试任务，不阻塞显示、音频或应用状态处理。
        vTaskDelay(pdMS_TO_TICKS(5000));
        constexpr uint32_t pulses[] = {1500, 1400, 1500, 1600, 1500};
        for (int i = 0; i < 5; ++i) {
            const uint32_t duty = pulses[i] * kCounts / kPeriodUs;
            ESP_ERROR_CHECK(ledc_set_duty(kMode, kChannel, duty));
            ESP_ERROR_CHECK(ledc_update_duty(kMode, kChannel));
            ESP_LOGI("Servo17Test", "[STEP %d/5] GPIO%d 50Hz 脉宽指令=%lu微秒，占空比计数=%lu，保持2秒",
                     i + 1, static_cast<int>(SERVO17_SELF_TEST_GPIO),
                     static_cast<unsigned long>(pulses[i]), static_cast<unsigned long>(duty));
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        ESP_ERROR_CHECK(ledc_set_duty(kMode, kChannel, 0));
        ESP_ERROR_CHECK(ledc_update_duty(kMode, kChannel));
        ESP_LOGI("Servo17Test", "[DONE] 一轮完成，PWM归零；换舵机须断电，重新复位才再测试。日志不代表实测动作或波形");
        vTaskDelete(nullptr);
    }

public:
    Servo17SelfTest() {
        ledc_timer_config_t timer = {};
        timer.speed_mode = kMode;
        timer.timer_num = kTimer;
        timer.duty_resolution = LEDC_TIMER_14_BIT;
        timer.freq_hz = 50;
        timer.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&timer));

        ledc_channel_config_t channel = {};
        channel.gpio_num = SERVO17_SELF_TEST_GPIO;
        channel.speed_mode = kMode;
        channel.channel = kChannel;
        channel.timer_sel = kTimer;
        channel.intr_type = LEDC_INTR_DISABLE;
        channel.duty = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&channel));
        ESP_LOGI("Servo17Test", "[READY] GPIO%d 定时器3/通道3，初始无脉冲；其他舵机接口未初始化",
                 static_cast<int>(SERVO17_SELF_TEST_GPIO));
        // 静态实例仅创建一次，任务结束自删，不周期创建后台任务。
        ESP_ERROR_CHECK(xTaskCreate(Run, "servo17_test", 3072, nullptr, 2, nullptr) == pdPASS
                            ? ESP_OK : ESP_ERR_NO_MEM);
    }

    Servo17SelfTest(const Servo17SelfTest&) = delete;
    Servo17SelfTest& operator=(const Servo17SelfTest&) = delete;
};

#endif
