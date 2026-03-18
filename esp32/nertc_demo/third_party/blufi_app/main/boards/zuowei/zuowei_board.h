#ifndef _ZUOWEI_BOARD_H_
#define _ZUOWEI_BOARD_H_

#include "boards/board.h"
#include "config.h"
#include "esp_lcd_types.h"
#include "esp_adc/adc_oneshot.h"

class ZwAdcButton;

class ZuoweiBoard : public Board {
public:
    ZuoweiBoard();
    ~ZuoweiBoard() override;

    // 实现 Board 接口
    void InitializeButtons(ButtonClickCallback on_click) override;
    void InitializeSpi() override;
    void InitializeLcdDisplay() override;

    LcdDisplay* GetDisplay() const override { return display_; }
    Button* GetButton(int index) const override;

private:
    static constexpr int kButtonCount = 2;
    ZwAdcButton* adc_buttons_[kButtonCount] = {nullptr, nullptr};
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    LcdDisplay* display_ = nullptr;
};

#endif // _ZUOWEI_BOARD_H_
