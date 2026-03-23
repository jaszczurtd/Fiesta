#include "peripherials.h"

static volatile float valueFields[F_LAST];
m_mutex_def(valueFieldsMutex);

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
  m_mutex_init(valueFieldsMutex);
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

void setGlobalValue(int idx, float val) {
  m_mutex_enter_blocking(valueFieldsMutex);
  valueFields[idx] = val;
  m_mutex_exit(valueFieldsMutex);
}

float getGlobalValue(int idx) {
  m_mutex_enter_blocking(valueFieldsMutex);
  float v = valueFields[idx];
  m_mutex_exit(valueFieldsMutex);
  return v;
}

