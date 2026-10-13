#include "peripherials.h"

void setupOnboardLed(void) {
  hal_gpio_set_mode(JH_HW_NODE_BOARD_STATUS_LED_PIN_OUT, HAL_GPIO_OUTPUT);
}

void initI2C(void) {
  hal_i2c_init(JH_HW_BUS_I2C_PIN_SDA, JH_HW_BUS_I2C_PIN_SCL,
               JH_HW_BUS_I2C_FREQUENCY_HZ);
}

void initSPI(void) {
  hal_spi_init(JH_HW_BUS_SPI_INDEX, JH_HW_BUS_SPI_PIN_MISO,
               JH_HW_BUS_SPI_PIN_MOSI, JH_HW_BUS_SPI_PIN_SCK);

  hal_display_init(JH_HW_NODE_TFT_PIN_CS, JH_HW_NODE_TFT_DC_PIN_OUT,
                   JH_HW_NODE_TFT_RESET_PIN_OUT);
  // ST7796S native panel is 320x480 (portrait); rotate 90 deg to land at
  // 480x320 landscape. BGR colour order is required by the panel; inversion
  // stays off (matches the reference doomConsole ST7796S configuration).
  hal_display_configure(320, 480, HAL_DISPLAY_ROTATION_90,
                        HAL_DISPLAY_INVERT_OFF, HAL_DISPLAY_COLOR_ORDER_BGR);
  hal_display_fill_screen(0);
}
