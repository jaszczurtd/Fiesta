#include "peripherials.h"

void setupOnboardLed(void) {
  hal_rgb_led_init(PIN_RGB, NUMPIXELS);
  setLEDColor(BLUE);
}

void initSPI(void) {
  hal_gpio_set_mode(CAN_CS, HAL_GPIO_OUTPUT);
  hal_gpio_write(CAN_CS, true);

  hal_spi_init(0, PIN_MISO, PIN_MOSI, PIN_SCK);
}

void initBasicPIO(void) {
  hal_pwm_set_resolution(PWM_WRITE_RESOLUTION);
}

void setLEDColor(int ledColor) {
  switch (ledColor) {
    case NONE:   hal_rgb_led_set_color(HAL_RGB_LED_NONE);   break;
    case RED:    hal_rgb_led_set_color(HAL_RGB_LED_RED);    break;
    case GREEN:  hal_rgb_led_set_color(HAL_RGB_LED_GREEN);  break;
    case BLUE:   hal_rgb_led_set_color(HAL_RGB_LED_BLUE);   break;
    case YELLOW: hal_rgb_led_set_color(HAL_RGB_LED_YELLOW); break;
    case WHITE:  hal_rgb_led_set_color(HAL_RGB_LED_WHITE);  break;
    case PURPLE: hal_rgb_led_set_color(HAL_RGB_LED_PURPLE); break;
    default:     break;
  }
}
