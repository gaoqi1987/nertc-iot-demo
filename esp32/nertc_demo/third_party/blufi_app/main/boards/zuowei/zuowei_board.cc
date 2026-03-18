#include "zuowei_board.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "xj_lcd_display.h"
#include "zw_adc_button.h"
#include "type.h"
#include "esp_lcd_panel_st7789_fix.h"

static const char* TAG = "ZuoweiBoard";

ZuoweiBoard::ZuoweiBoard() {
    ESP_LOGI(TAG, "Initializing ZuoweiBoard");
}

ZuoweiBoard::~ZuoweiBoard() {
    for (int i = 0; i < kButtonCount; i++) {
        if (adc_buttons_[i] != nullptr) {
            delete adc_buttons_[i];
            adc_buttons_[i] = nullptr;
        }
    }
    if (display_ != nullptr) {
        delete display_;
        display_ = nullptr;
    }
    if (adc_handle_ != nullptr) {
        adc_oneshot_del_unit(adc_handle_);
        adc_handle_ = nullptr;
    }
}

Button* ZuoweiBoard::GetButton(int index) const {
    if (index >= 0 && index < kButtonCount) {
        return adc_buttons_[index];
    }
    return nullptr;
}

void ZuoweiBoard::InitializeButtons(ButtonClickCallback on_click) {
    ESP_LOGI(TAG, "Initializing ADC buttons");

    // 初始化 ADC
    const adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_KEY_UNIT_ID,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle_));

    // 配置 onoff 按钮 (index 0)
    button_adc_config_t adc_cfg = {};
    adc_cfg.adc_channel = ADC_KEY_CHANEL;
    adc_cfg.adc_handle = &adc_handle_;
    adc_cfg.button_index = 0;
    adc_cfg.min = KEY_ONOFF_ADC_MV;
    adc_cfg.max = KEY_ONOFF_ADC_MV + 275;
    adc_buttons_[0] = new ZwAdcButton(adc_cfg);

    // 配置 take 按钮 (index 1)
    adc_cfg.adc_handle = &adc_handle_;
    adc_cfg.button_index = 1;
    adc_cfg.min = KEY_TAKE_ADC_MV - 275;
    adc_cfg.max = KEY_TAKE_ADC_MV + 275;
    adc_buttons_[1] = new ZwAdcButton(adc_cfg);

    // 设置按钮回调
    adc_buttons_[0]->OnClick([on_click]() {
        if (on_click) on_click();
    });

    adc_buttons_[1]->OnClick([on_click]() {
        if (on_click) on_click();
    });

    ESP_LOGI(TAG, "ADC buttons initialized");
}

void ZuoweiBoard::InitializeSpi() {
    ESP_LOGI(TAG, "Initializing SPI bus");

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = LCD_SDA_GPIO;
    buscfg.miso_io_num = GPIO_NUM_NC;
    buscfg.sclk_io_num = LCD_CLK_GPIO;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = 64;
    buscfg.flags = SPICOMMON_BUSFLAG_SLAVE;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "SPI bus initialized");
}

void ZuoweiBoard::InitializeLcdDisplay() {
    ESP_LOGI(TAG, "Initializing LCD display");

    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;

    // 液晶屏控制IO初始化
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = GPIO_NUM_NC;
    io_config.dc_gpio_num = LCD_CD_GPIO;
    io_config.spi_mode = 3;
    io_config.pclk_hz = 80 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

    // 初始化液晶屏驱动芯片ST7789
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = LCD_RESET_GPIO;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789_fix(panel_io, &panel_config, &panel));

    // 复位两次
    esp_lcd_panel_reset(panel);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    esp_lcd_panel_reset(panel);

    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
    esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
    esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

    // 开背光
    gpio_reset_pin(LCD_BL_EN_GPIO);
    gpio_set_direction(LCD_BL_EN_GPIO, GPIO_MODE_OUTPUT);

#if DISPLAY_BACKLIGHT_OUTPUT_INVERT
    gpio_set_level(LCD_BL_EN_GPIO, 0);
#else
    gpio_set_level(LCD_BL_EN_GPIO, 1);
#endif
    ESP_LOGI(TAG, "Backlight turned on (GPIO %d)", LCD_BL_EN_GPIO);

    // 创建显示对象
    display_ = new XjLcdDisplay(
        panel_io, panel, 
        DISPLAY_WIDTH, DISPLAY_HEIGHT, 
        DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
        DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, 
        DISPLAY_SWAP_XY, g_app);

    ESP_LOGI(TAG, "LCD display initialized");
}

// 注册板子
DECLARE_BOARD(ZuoweiBoard)
