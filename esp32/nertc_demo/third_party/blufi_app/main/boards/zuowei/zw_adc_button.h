#ifndef _ZW_ADC_BUTTON_H_
#define _ZW_ADC_BUTTON_H_

#include "button/button.h"
#include "iot_button.h"

class ZwAdcButton : public Button {
public:
    ZwAdcButton(const button_adc_config_t& adc_config);
};

#endif // _ZW_ADC_BUTTON_H_
