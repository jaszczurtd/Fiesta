#include "peripherials.h"

static int lcdBrightnessLevel = 0;

void setupOnboardLed(void) {
  (void)hal_rgb_led_init_board_status();
  hal_rgb_led_set_color(HAL_RGB_LED_BLUE);
}

void initSPI(void) {
  hal_gpio_set_mode(JH_HW_NODE_CAN_PIN_CS, HAL_GPIO_OUTPUT);
  hal_gpio_write(JH_HW_NODE_CAN_PIN_CS, true);
  // Core 1 sets up the TFT later; until then its chip select must not let
  // the display read the MCP2515 traffic on the shared bus.
  hal_gpio_set_mode(JH_HW_NODE_TFT_PIN_CS, HAL_GPIO_OUTPUT);
  hal_gpio_write(JH_HW_NODE_TFT_PIN_CS, true);

  hal_spi_init(JH_HW_BUS_SPI_INDEX, JH_HW_BUS_SPI_PIN_MISO,
               JH_HW_BUS_SPI_PIN_MOSI, JH_HW_BUS_SPI_PIN_SCK);

  hal_gpio_set_mode(JH_HW_NODE_OIL_LAMP_PIN_OUT, HAL_GPIO_OUTPUT);
  enableOilLamp(true);
}

void enableOilLamp(bool enable) {
  hal_gpio_write(JH_HW_NODE_OIL_LAMP_PIN_OUT, enable);
}

void initBasicPIO(void) {
  hal_pwm_set_resolution(PWM_WRITE_RESOLUTION);
  lcdBrightness(INITIAL_BRIGHTNESS);

  Buzzer::initHardware(JH_HW_NODE_BUZZER_PIN_OUT);
  initBuzzers();
}

void lcdBrightness(int val) {
  lcdBrightnessLevel = val;
  hal_pwm_write(JH_HW_NODE_BACKLIGHT_PIN_OUT,
                ((1 << PWM_WRITE_RESOLUTION) - 1) - val);
}

int getLcdBrightness(void) { return lcdBrightnessLevel; }

int getThrottlePercentage(void) {
  int currentVal = int(valueFields[F_THROTTLE_POS]);
  float percent = (currentVal * 100) / PWM_RESOLUTION;
  return hal_math_percent_to_value(percent, 100);
}
