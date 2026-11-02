#ifndef XIAOZHI_SERVO_CONTROLLER_H
#define XIAOZHI_SERVO_CONTROLLER_H

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "mcp_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class ServoController {
private:
    static constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
    static constexpr ledc_timer_t kTimer = LEDC_TIMER_3;
    static constexpr ledc_channel_t kChannel = LEDC_CHANNEL_3;

    // 50 Hz 对应 20000 微秒一个周期
    static constexpr uint32_t kPeriodUs = 20000;

    // 使用 14 位分辨率，一个周期对应 16384 个计数
    static constexpr uint32_t kPeriodCounts = 1U << 14;

    // 按商品给出的参数映射角度；实际机械角度仍需校准
    static constexpr uint32_t kMinPulseUs = 500;
    static constexpr uint32_t kMaxPulseUs = 2500;

    // 首轮保留端点余量，确认无卡顿后再按实物调整
    static constexpr int kMinAngle = 10;
    static constexpr int kMaxAngle = 170;

    enum class CommandType { SetAngle, StartShake, Stop };
    struct Command {
        CommandType type;
        int angle;
    };
    QueueHandle_t commands_ = nullptr;

    static const char* CommandName(CommandType type) {
        switch (type) {
            case CommandType::SetAngle: return "SetAngle";
            case CommandType::StartShake: return "StartShake";
            case CommandType::Stop: return "Stop";
        }
        return "Unknown";
    }

    void SendCommand(CommandType type, int angle = 90) {
        const Command command{type, angle};
        // 入队前记录，接收任务可能在入队后立即被调度
        ESP_LOGI("Servo", "[QUEUE SEND] 指令=%s，angle=%d（仅SetAngle使用）",
                 CommandName(type), angle);
        // 单格邮箱保留最新指令，尚未处理的旧指令可能被覆盖
        ESP_ERROR_CHECK(xQueueOverwrite(commands_, &command) == pdPASS
                            ? ESP_OK : ESP_FAIL);
    }

    static void TaskEntry(void* context) {
        static_cast<ServoController*>(context)->Run();
    }

    void Run() {
        // 状态仅由本任务访问，其他任务通过队列发送请求
        int current = 90;
        int target = 90;
        int phase = 0;
        bool shaking = false;
        const int positions[] = {90, 60, 120, 90};
        ESP_LOGI("Servo", "[TASK READY] 舵机任务启动，初始指令角度=90");

        while (true) {
            Command command{};
            const bool moving = shaking || current != target;
            // 静止时等待指令；运动时约每20毫秒更新一步
            // 新消息可以提前唤醒任务，不必等整轮摇头结束
            if (xQueueReceive(commands_, &command,
                    moving ? pdMS_TO_TICKS(20) : portMAX_DELAY) == pdTRUE) {
                ESP_LOGI("Servo", "[QUEUE RECV] 指令=%s，当前指令角度=%d",
                         CommandName(command.type), current);
                if (command.type == CommandType::Stop) {
                    shaking = false;
                    target = current;
                    ESP_LOGI("Servo", "[STOP] 取消剩余动作，保持指令角度=%d，PWM继续输出", current);
                } else if (command.type == CommandType::StartShake) {
                    if (!shaking) {
                        shaking = true;
                        phase = 0;
                        target = positions[phase];
                        ESP_LOGI("Servo", "[SHAKE START] 循环执行90→60→120→90度");
                    } else {
                        ESP_LOGI("Servo", "[SHAKE START] 已在摇头，忽略重复开始");
                    }
                } else {
                    shaking = false;
                    target = command.angle;
                    ESP_LOGI("Servo", "[MOVE] 取消摇头，目标角度=%d", target);
                }
                continue;
            }

            // 每步最多变化2度；只有本任务更新PWM
            if (current < target) {
                current += (target - current > 2) ? 2 : target - current;
            } else if (current > target) {
                current -= (current - target > 2) ? 2 : current - target;
            }
            ApplyAngle(current);
            if (shaking && current == target) {
                phase = (phase + 1) % 4;
                target = positions[phase];
                ESP_LOGI("Servo", "[SHAKE NEXT] 当前指令角度=%d，下一目标=%d", current, target);
            }
        }
    }

public:
    explicit ServoController(gpio_num_t gpio) {
        // 配置舵机使用的 PWM 定时器
        ledc_timer_config_t timer = {};
        timer.speed_mode = kMode;
        timer.timer_num = kTimer;
        timer.duty_resolution = LEDC_TIMER_14_BIT;
        timer.freq_hz = 50;
        timer.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&timer));

        // 初始输出 1500 微秒脉冲，使舵机回到中间位置
        ledc_channel_config_t channel = {};
        channel.gpio_num = gpio;
        channel.speed_mode = kMode;
        channel.channel = kChannel;
        channel.intr_type = LEDC_INTR_DISABLE;
        channel.timer_sel = kTimer;
        channel.duty = 1500U * kPeriodCounts / kPeriodUs;
        channel.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&channel));

        commands_ = xQueueCreate(1, sizeof(Command));
        ESP_ERROR_CHECK(commands_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
        // 任务常驻；ESP-IDF的栈大小单位为字节
        ESP_ERROR_CHECK(xTaskCreate(TaskEntry, "servo_motion", 4096,
                                   this, 2, nullptr) == pdPASS
                            ? ESP_OK : ESP_ERR_NO_MEM);

        // 工具参数范围与底层限制保持一致
        auto& server = McpServer::GetInstance();
        server.AddTool(
            "self.servo.set_angle",
            "设置舵机的绝对目标角度，允许10到170度，"
            "90度为中间位置，会取消摇头并平滑移动。"
            "角度为估算值，返回成功仅表示指令已接收。",
            PropertyList({
                Property("angle", kPropertyTypeInteger, kMinAngle, kMaxAngle)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                SetAngle(properties["angle"].value<int>());
                return true;
            });

        server.AddTool(
            "self.servo.start_shake",
            "开始持续摇头，循环执行90、60、120、90度，直到用户要求停止。"
            "重复调用不会叠加动作。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                SendCommand(CommandType::StartShake);
                return true;
            });
        server.AddTool(
            "self.servo.stop",
            "停止动作、停止摇头，取消舵机后续动作并保持最后输出角度，"
            "不会自动回中。返回成功仅表示指令已接收。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                SendCommand(CommandType::Stop);
                return true;
            });
    }

    // 本对象由开发板静态持有，任务和工具回调在运行期间持续使用
    ServoController(const ServoController&) = delete;
    ServoController& operator=(const ServoController&) = delete;

    void SetAngle(int angle) {
        // 限制机械活动范围，避免直接冲向两端
        if (angle < kMinAngle) {
            angle = kMinAngle;
        }
        if (angle > kMaxAngle) {
            angle = kMaxAngle;
        }

        SendCommand(CommandType::SetAngle, angle);
    }

private:
    void ApplyAngle(int angle) {
        // 按 0～180 度对应 500～2500 微秒进行线性映射
        const uint32_t pulse_us =
            kMinPulseUs + static_cast<uint32_t>(angle) *
            (kMaxPulseUs - kMinPulseUs) / 180U;

        // 将高电平持续时间换算为 PWM 占空比计数
        const uint32_t duty =
            pulse_us * kPeriodCounts / kPeriodUs;

        ESP_ERROR_CHECK(ledc_set_duty(kMode, kChannel, duty));
        ESP_ERROR_CHECK(ledc_update_duty(kMode, kChannel));
        // 逐步PWM日志使用DEBUG级别，避免串口每秒打印50次
        ESP_LOGD("Servo", "目标角度=%d，脉宽=%lu 微秒，占空比计数=%lu",
                 angle, static_cast<unsigned long>(pulse_us),
                 static_cast<unsigned long>(duty));
    }
};

#endif
