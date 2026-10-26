
#include "TFTExtension.h"
#include "engineFuel.h"
#include "hardwareConfig.h"
#include "pressureGauge.h"
#include "simpleGauge.h"
#include "tempGauge.h"
#include <hal/hal.h>

void initTFT(void) {
  hal_gpio_set_mode(JH_HW_NODE_TFT_PIN_RESET, HAL_GPIO_OUTPUT);
  hal_gpio_write(JH_HW_NODE_TFT_PIN_RESET, false);
  hal_delay_ms(100);
  hal_gpio_write(JH_HW_NODE_TFT_PIN_RESET, true);

  const hal_display_ili9341_config_t tftConfig = JH_HW_NODE_TFT_CONFIG_INIT;
  hal_display_init_ili9341_ex(&tftConfig);
  hal_display_configure(SCREEN_W, SCREEN_H,
                        HAL_DISPLAY_ROTATION(JH_HW_NODE_TFT_PROP_ROTATION),
                        HAL_DISPLAY_INVERT_OFF, HAL_DISPLAY_COLOR_ORDER_RGB);
}

void redrawAllGauges(void) {
  redrawFuel();
  redrawTempGauges();
  redrawSimpleGauges();
  redrawPressureGauges();
}
