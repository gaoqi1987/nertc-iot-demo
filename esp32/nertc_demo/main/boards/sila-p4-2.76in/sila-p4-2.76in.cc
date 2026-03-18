#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "application.h"
#include "display/lcd_display.h"
#include "button.h"
#include "config.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "settings.h"
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <esp_lvgl_port.h>
#include <esp_lcd_touch.h>
#include "esp_lcd_touch_cst826.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_netif.h>
#if CONFIG_USE_ACOUSTIC_WIFI_PROVISIONING
#include "afsk_demod.h"
#endif

// SD card scanner not available, SD card initialization removed

#define TAG "SilaP4Ball"

LV_FONT_DECLARE(font_puhui_30_4);
LV_FONT_DECLARE(font_awesome_30_4);
extern const lv_font_t* font_emoji_64_init();

static const st7701_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x10, 0x0C}, 2, 0},
    {0xC2, (uint8_t[]){0x07, 0x0A}, 2, 0},
    {0xC7, (uint8_t[]){0x04}, 1, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xB0, (uint8_t[]){0x05, 0x12, 0x98, 0x0E, 0x0F, 0x07, 0x07, 0x09, 0x09, 0x23, 0x05, 0x52, 0x0F, 0x67, 0x2C, 0x11}, 16, 0},
    {0xB1, (uint8_t[]){0x0B, 0x11, 0x97, 0x0C, 0x12, 0x06, 0x06, 0x08, 0x08, 0x22, 0x03, 0x51, 0x11, 0x66, 0x2B, 0x0F}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x5D}, 1, 0},
    {0xB1, (uint8_t[]){0x2D}, 1, 0},
    {0xB2, (uint8_t[]){0x81}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x4E}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x20}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x06, 0x30, 0x08, 0x30, 0x05, 0x30, 0x07, 0x30, 0x00, 0x33, 0x33}, 11, 0},
    {0xE2, (uint8_t[]){0x11, 0x11, 0x33, 0x33, 0xF4, 0x00, 0x00, 0x00, 0xF4, 0x00, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x0D, 0xF5, 0x30, 0xF0, 0x0F, 0xF7, 0x30, 0xF0, 0x09, 0xF1, 0x30, 0xF0, 0x0B, 0xF3, 0x30, 0xF0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x0C, 0xF4, 0x30, 0xF0, 0x0E, 0xF6, 0x30, 0xF0, 0x08, 0xF0, 0x30, 0xF0, 0x0A, 0xF2, 0x30, 0xF0}, 16, 0},
    {0xE9, (uint8_t[]){0x36, 0x01}, 2, 0},
    {0xEB, (uint8_t[]){0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40}, 7, 0},
    {0xED, (uint8_t[]){0xFF, 0x10, 0xAF, 0x76, 0x54, 0x2B, 0xCF, 0xFF, 0xFF, 0xFC, 0xB2, 0x45, 0x67, 0xFA, 0x01, 0xFF}, 16, 0},
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120}, // 退出睡眠模式
    {0x3A, (uint8_t[]){0x55}, 1, 0},  // RGB565格式
    {0x36, (uint8_t[]){0x00}, 1, 0},  // MADCTL: 初始值，后续会重新设置
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x29, (uint8_t[]){0x00}, 0, 20},  // 显示开启
};

/**
 * @brief 专用于 Sila P4 2.76in ST7701 屏幕的 MIPI 显示类
 * 
 * 此类直接继承自 LcdDisplay，绕过 MipiLcdDisplay 的默认 LVGL 配置，
 * 使用与参考项目 esp32p4_2.76_st7701_mipi 相同的配置，
 * 避免软件旋转与硬件初始化命令的冲突导致镜像问题。
 */
class SilaP4MipiDisplay : public LcdDisplay {
public:
    SilaP4MipiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                      int width, int height, int offset_x, int offset_y)
        : LcdDisplay(panel_io, panel, width, height) {

        ESP_LOGI(TAG, "Initialize LVGL library for Sila P4 2.76in ST7701");
        lv_init();

        ESP_LOGI(TAG, "Initialize LVGL port");
        // 使用与参考项目 esp32p4_2.76_st7701_mipi 相同的配置
        lvgl_port_cfg_t port_cfg = {
            .task_priority = 2,
            .task_stack = 8 * 1024,
            .task_max_sleep_ms = 500,
            .timer_period_ms = 2
        };
        lvgl_port_init(&port_cfg);

        ESP_LOGI(TAG, "Adding MIPI DSI display");
        // 关键：不设置 .rotation 字段，避免软件旋转与硬件初始化命令冲突
        // 此屏幕的镜像完全由 lcd_init_cmds 中的 MADCTL (0x36) 硬件命令控制
        const lvgl_port_display_cfg_t disp_cfg = {
            .io_handle = panel_io,
            .panel_handle = panel,
            .buffer_size = static_cast<uint32_t>(width_ * 200),
            .double_buffer = true,
            .hres = static_cast<uint32_t>(width_),
            .vres = static_cast<uint32_t>(height_),
            // 注意：不设置 .rotation，使用默认值（全部为 false）
            // 注意：不设置 .sw_rotate，避免软件旋转干扰硬件设置
            .flags = {
                .buff_spiram = true,
            },
        };

        const lvgl_port_display_dsi_cfg_t dsi_cfg = {
            .flags = {
                .avoid_tearing = false,
            }
        };

        display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
        if (display_ == nullptr) {
            ESP_LOGE(TAG, "Failed to add display");
            return;
        }

        if (offset_x != 0 || offset_y != 0) {
            lv_display_set_offset(display_, offset_x, offset_y);
        }

        SetupUI();
        ESP_LOGI(TAG, "Sila P4 MIPI display initialized successfully");
    }
};

class SilaP4Ball : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;           // 音频编解码器I2C总线
    Button boot_button_;
    SilaP4MipiDisplay *display_;
    esp_lcd_panel_io_handle_t io_;
    esp_lcd_panel_handle_t panel_;
    esp_lcd_touch_handle_t touch_handle_;
    esp_lcd_panel_io_handle_t touch_io_;
    TaskHandle_t touch_button_task_handle_;

    void InitializeCodecI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    static esp_err_t bsp_enable_dsi_phy_power(void) {
#if MIPI_DSI_PHY_PWR_LDO_CHAN > 0
        static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
        ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif
        return ESP_OK;
    }

    void InitializeLCD() {
        gpio_config_t rst_io_conf = {
            .pin_bit_mask = (1ULL << PIN_NUM_LCD_RST),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_io_conf);

        gpio_set_level(PIN_NUM_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(PIN_NUM_LCD_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(PIN_NUM_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(20));

        bsp_enable_dsi_phy_power();

        esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
        esp_lcd_dsi_bus_config_t bus_config = {
            .bus_id = 0,
            .num_data_lanes = 2,
            .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
            .lane_bit_rate_mbps = 400,  // 降低位速率提高稳定性
        };
        esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);

        esp_lcd_dbi_io_config_t dbi_config = ST7701_PANEL_IO_DBI_CONFIG();
        esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io_);

        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 20,  // 降低时钟频率提高稳定性
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = DISPLAY_WIDTH,
                .v_size = DISPLAY_HEIGHT,
                .hsync_pulse_width = 20,
                .hsync_back_porch = 40,
                .hsync_front_porch = 40,
                .vsync_pulse_width = 20,
                .vsync_back_porch = 30,
                .vsync_front_porch = 30,
            },
            .flags = {
                .use_dma2d = true,
            },
        };
        st7701_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(st7701_lcd_init_cmd_t),
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
            },
            .flags = {
                .use_mipi_interface = 1,
            },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_st7701(io_, &lcd_dev_config, &panel_);
        esp_lcd_panel_reset(panel_);
        esp_lcd_panel_init(panel_);
        esp_lcd_panel_disp_on_off(panel_, true);
        ESP_LOGI(TAG, "LCD面板初始化完成");
    }

    void InitializeLvglDisplay() {
        // 使用专用的 SilaP4MipiDisplay，避免软件旋转与硬件初始化命令冲突
        display_ = new SilaP4MipiDisplay(io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                         DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y);
    }

    void InitializeTouch() {
        ESP_LOGI(TAG, "初始化触摸控制器");
        
        // 先手动复位触摸控制器
        if (TP_RST_PIN != GPIO_NUM_NC) {
            gpio_config_t rst_gpio_config = {
                .pin_bit_mask = (1ULL << TP_RST_PIN),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE,
            };
            gpio_config(&rst_gpio_config);
            gpio_set_level(TP_RST_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_set_level(TP_RST_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(200));  // 增加等待时间，确保触摸控制器完全稳定
            ESP_LOGI(TAG, "触摸控制器硬件复位完成");
        }
        
        ESP_LOGI(TAG, "开始创建触摸 I2C IO");
        // 创建触摸 I2C IO (使用独立的触摸I2C总线)
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST826_CONFIG();
        tp_io_config.scl_speed_hz = 100000;  // 降低I2C速度以提高稳定性
        esp_err_t ret = esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &touch_io_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "创建触摸 I2C IO 失败: %s，触摸功能将不可用", esp_err_to_name(ret));
            touch_io_ = nullptr;
            touch_handle_ = nullptr;
            return;
        }
        ESP_LOGI(TAG, "触摸 I2C IO 创建成功");
        
        // 额外等待，确保I2C通信稳定
        vTaskDelay(pdMS_TO_TICKS(100));
        
        ESP_LOGI(TAG, "开始配置触摸控制器");
        // 配置触摸控制器
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = TP_RST_PIN,  // 保持复位引脚配置，但内部复位会再次执行
            .int_gpio_num = TP_INT_PIN,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
        };
        
        ESP_LOGI(TAG, "开始初始化触摸控制器驱动（这可能需要几秒钟）");
        // 尝试初始化触摸控制器，如果失败则记录警告但不崩溃
        // 注意：这个函数内部会尝试读取芯片ID，如果I2C通信失败可能会卡住
        ret = esp_lcd_touch_new_i2c_cst826(touch_io_, &tp_cfg, &touch_handle_);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "触摸控制器初始化失败: %s，将继续运行但触摸功能不可用", esp_err_to_name(ret));
            touch_handle_ = nullptr;
            // 清理I2C IO
            if (touch_io_) {
                esp_lcd_panel_io_del(touch_io_);
                touch_io_ = nullptr;
            }
            return;
        }
        ESP_LOGI(TAG, "触摸控制器驱动初始化成功");
        
        ESP_LOGI(TAG, "开始注册触摸到 LVGL");
        // 注册触摸到 LVGL
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = touch_handle_,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "触摸面板初始化成功");
    }

    static void TouchButtonTask(void* arg) {
        SilaP4Ball* board = static_cast<SilaP4Ball*>(arg);
        uint16_t x[2], y[2];
        uint16_t strength[2];
        uint8_t point_num;
        bool last_touch_state = false;
        TickType_t touch_start_time = 0;
        const TickType_t touch_debounce_ms = pdMS_TO_TICKS(50);
        
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(20));
            
            if (board->touch_handle_ == nullptr) {
                continue;
            }
            
            esp_err_t ret = board->touch_handle_->read_data(board->touch_handle_);
            bool has_touch = (ret == ESP_OK);
            
            if (has_touch) {
                board->touch_handle_->get_xy(board->touch_handle_, x, y, strength, (uint8_t*)&point_num, 2);
            }
            
            bool current_touch_state = has_touch && (point_num > 0);
            
            if (current_touch_state && !last_touch_state) {
                touch_start_time = xTaskGetTickCount();
                last_touch_state = true;
            }
            
            if (!current_touch_state && last_touch_state) {
                TickType_t touch_duration = xTaskGetTickCount() - touch_start_time;
                
                if (touch_duration >= touch_debounce_ms) {
                    ESP_LOGI(TAG, "触摸按键触发 (持续时间: %lu ms)", touch_duration * portTICK_PERIOD_MS);
                    
                    auto& app = Application::GetInstance();
                    if (app.GetDeviceState() == kDeviceStateStarting) {
                        board->EnterWifiConfigMode();
                    }
                    app.ToggleChatState();
                }
                
                last_touch_state = false;
            }
        }
    }

    void InitializeTouchButton() {
        xTaskCreate(TouchButtonTask, "touch_button", 4096, this, 5, &touch_button_task_handle_);
        ESP_LOGI(TAG, "触摸按键监听任务已创建");
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
            }
            app.ToggleChatState(); });
    }

public:
    SilaP4Ball() :
        boot_button_(BOOT_BUTTON_GPIO),
        touch_handle_(nullptr),
        touch_io_(nullptr),
        touch_button_task_handle_(nullptr) {
        InitializeCodecI2c();
        InitializeLCD();
        InitializeLvglDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeTouchButton();
        GetBacklight()->RestoreBrightness();
        ESP_LOGI(TAG, "✅ Sila P4 2.76in Ball (ST7701) 初始化完成");
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(SilaP4Ball);