#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#if BREAD_COMPACT_USE_WAVESHARE_LCD
#include "display/lcd_display.h"
#include "waveshare_lcd_test_display.h"
#include "robot_eye_display.h"
#include <driver/spi_master.h>
#else
#include "display/oled_display.h"
#endif
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include "servo_controller.h"

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "CompactWifiBoard"

class CompactWifiBoard : public WifiBoard {
private:
#if !BREAD_COMPACT_USE_WAVESHARE_LCD
    i2c_master_bus_handle_t display_i2c_bus_;
#endif
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button touch_button_;
    Button volume_up_button_;
    Button volume_down_button_;

#if BREAD_COMPACT_USE_WAVESHARE_LCD
    void InitializeWaveshareLcd() {
        ESP_LOGI(TAG, "[LCD INIT] 微雪1.69寸 ST7789V2，%dx%d，SPI=%dHz",
                 DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_SPI_CLOCK_HZ);
        ESP_LOGI(TAG, "[LCD PINS] CLK=%d DIN=%d CS=%d DC=%d RST=%d BL=%d",
                 DISPLAY_CLK_PIN, DISPLAY_MOSI_PIN, DISPLAY_CS_PIN,
                 DISPLAY_DC_PIN, DISPLAY_RST_PIN, DISPLAY_BACKLIGHT_PIN);

        // 背光先关闭，屏幕初始化和清屏完成后再恢复亮度。
        auto backlight = GetBacklight();

        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = DISPLAY_MOSI_PIN;
        bus_config.miso_io_num = GPIO_NUM_NC;
        bus_config.sclk_io_num = DISPLAY_CLK_PIN;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        // 偏移只在面板驱动设置一次，LVGL中不再重复添加20行。
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y));

#if BREAD_COMPACT_USE_ROBOT_EYES
        display_ = new RobotEyeDisplay(panel_io_, panel_,
#else
        display_ = new WaveshareLcdTestDisplay(panel_io_, panel_,
#endif
            DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        backlight->RestoreBrightness();
        ESP_LOGI(TAG, "[LCD READY] 驱动初始化完成；SPI无读回线，实际显示需观察屏幕确认");
    }
#else
    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

#endif

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }

            ESP_LOGI(TAG, "我的第一次修改：拿下了BOT键");

            GetDisplay()->ShowNotification("您好，主人");

            app.ToggleChatState();
        });
        touch_button_.OnPressDown([this]() {
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([this]() {
            Application::GetInstance().StopListening();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
        // 学习测试：启动时将 LED 设置为 10% 亮度
        // lamp.SetBrightness(10);

        // 使用 GPIO17 控制舵机，启动时回到中间位置
        static ServoController servo(GPIO_NUM_17);
#if BREAD_COMPACT_USE_WAVESHARE_LCD && BREAD_COMPACT_USE_ROBOT_EYES
        // 显式视觉入口：只控制屏幕表演，不把普通TTS当成音乐播放事件。
        McpServer::GetInstance().AddTool(
            "self.screen.set_singing_animation",
            "开启或关闭屏幕上拿麦克风唱歌的动画。用户要求演示唱歌动画、拿麦克风表演时使用。"
            "此工具只改变屏幕，不播放或停止歌曲，也不能证明歌曲正在播放。",
            PropertyList({
                Property("enabled", kPropertyTypeBoolean),
                Property("duration_seconds", kPropertyTypeInteger, 15, 1, 600)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                return static_cast<RobotEyeDisplay*>(display_)->SetSingingAnimation(
                    properties["enabled"].value<bool>(), properties["duration_seconds"].value<int>());
            });
#endif
    }

public:
    CompactWifiBoard() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
#if BREAD_COMPACT_USE_WAVESHARE_LCD
        InitializeWaveshareLcd();
#else
        InitializeDisplayI2c();
        InitializeSsd1306Display();
#endif
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    void SetNetworkEventCallback(NetworkEventCallback callback) override {
        // 仅转发本板联网显示阶段，应用原回调及其网络处理保持完整执行。
        WifiBoard::SetNetworkEventCallback([this, callback = std::move(callback)](NetworkEvent event, const std::string& data) {
#if BREAD_COMPACT_USE_WAVESHARE_LCD && BREAD_COMPACT_USE_ROBOT_EYES
            switch (event) {
                case NetworkEvent::Connected:
                    static_cast<RobotEyeDisplay*>(display_)->SetNetworkConnecting(false);
                    break;
                case NetworkEvent::Scanning:
                case NetworkEvent::Connecting:
                case NetworkEvent::Disconnected:
                case NetworkEvent::WifiConfigModeEnter:
                case NetworkEvent::WifiConfigModeExit:
                    static_cast<RobotEyeDisplay*>(display_)->SetNetworkConnecting(true);
                    break;
                default:
                    break;
            }
#endif
            if (callback) callback(event, data);
        });
    }

    virtual Backlight* GetBacklight() override {
#if BREAD_COMPACT_USE_WAVESHARE_LCD
        // 公共背光类使用定时器0、通道0，不占LED的2和舵机的3。
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, false);
        return &backlight;
#else
        return nullptr;
#endif
    }
};

DECLARE_BOARD(CompactWifiBoard);
