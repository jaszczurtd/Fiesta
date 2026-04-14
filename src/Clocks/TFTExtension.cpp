
#include "TFTExtension.h"
#include <hal/hal.h>
#include "hardwareConfig.h"
#include "engineFuel.h"
#include "tempGauge.h"
#include "simpleGauge.h"
#include "pressureGauge.h"

void initTFT(void) {
    hal_gpio_set_mode(TFT_RST, HAL_GPIO_OUTPUT);
    hal_gpio_write(TFT_RST, false);
    hal_delay_ms(100);
    hal_gpio_write(TFT_RST, true);

    hal_display_init(TFT_CS, TFT_DC, TFT_RST);
    hal_display_configure(
        SCREEN_W,
        SCREEN_H,
        HAL_DISPLAY_ROTATION(90),
        HAL_DISPLAY_INVERT_OFF,
        HAL_DISPLAY_COLOR_ORDER_RGB);
}

void softInitDisplay(void) {
    hal_display_soft_init(75);
    hal_display_set_rotation(HAL_DISPLAY_ROTATION(90));
}

void redrawAllGauges(void) {
    redrawFuel();
    redrawTempGauges();
    redrawSimpleGauges();
    redrawPressureGauges();
}

