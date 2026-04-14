#include "peripherials.h"

void setupOnboardLed(void) {
  hal_rgb_led_init(PIN_RGB, NUMPIXELS);
  hal_rgb_led_set_color(HAL_RGB_LED_BLUE);
}

void initSPI(void) {
  hal_gpio_set_mode(CAN_CS, HAL_GPIO_OUTPUT);
  hal_gpio_write(CAN_CS, true);

  hal_spi_init(0, PIN_MISO, PIN_MOSI, PIN_SCK);

  hal_gpio_set_mode(OIL_OUTPUT_PIN, HAL_GPIO_OUTPUT);
  enableOilLamp(true);
}

void enableOilLamp(bool enable) {
  hal_gpio_write(OIL_OUTPUT_PIN, enable);
}

void initBasicPIO(void) {
  hal_pwm_set_resolution(PWM_WRITE_RESOLUTION);
  lcdBrightness(INITIAL_BRIGHTNESS);

  Buzzer::initHardware(BUZZER);
  initBuzzers();
}

void lcdBrightness(int val) {
  valueFields[F_CLOCK_BRIGHTNESS] = val;
  hal_pwm_write(PIN_BRIGHTNESS, ((1 << PWM_WRITE_RESOLUTION) - 1) - val);
}

int getThrottlePercentage(void) {
  int currentVal = int(valueFields[F_THROTTLE_POS]);
  float percent = (currentVal * 100) / PWM_RESOLUTION;
  return percentToGivenVal(percent, 100);
}

