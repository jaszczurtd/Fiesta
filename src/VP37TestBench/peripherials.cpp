#include "peripherials.h"

void setupOnboardLed(void) {
  hal_gpio_set_mode(PIN_ONBOARD_LED, HAL_GPIO_OUTPUT);
}

void initI2C(void) { hal_i2c_init(PIN_SDA, PIN_SCL, HAL_I2C_CLOCK_FAST_HZ); }

void initSPI(void) {
  hal_spi_init(0, PIN_MISO, PIN_MOSI, PIN_SCK);

  hal_display_init(TFT_CS, TFT_DC, TFT_RST);
  // ST7796S native panel is 320x480 (portrait); rotate 90 deg to land at
  // 480x320 landscape. BGR colour order is required by the panel; inversion
  // stays off (matches the reference doomConsole ST7796S configuration).
  hal_display_configure(320, 480, HAL_DISPLAY_ROTATION_90,
                        HAL_DISPLAY_INVERT_OFF, HAL_DISPLAY_COLOR_ORDER_BGR);
  hal_display_fill_screen(0);
}
