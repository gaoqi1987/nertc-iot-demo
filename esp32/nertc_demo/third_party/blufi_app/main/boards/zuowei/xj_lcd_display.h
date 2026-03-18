#ifndef XJ_LCD_DISPLAY_H
#define XJ_LCD_DISPLAY_H

#include "display/lcd_display.h"
#include "type.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class XjLcdDisplay : public LcdDisplay {
public:
    XjLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                 int width, int height, int offset_x, int offset_y,
                 bool mirror_x, bool mirror_y, bool swap_xy, AppType_t app_type);
};

#endif // XJ_LCD_DISPLAY_H
