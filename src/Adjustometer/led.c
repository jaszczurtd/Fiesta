#include "led.h"
#include "sensors.h"
#include <hal/hal_rgb_led.h>
#include <hal/hal_i2c_slave.h>

#define LED_BLINK_NO_OSCILLATION_MS  (SECOND / 8)   // 4x per second = toggle every 125 ms
#define LED_BLINK_STATUS_MS          (SECOND / 2)   // status cycle step every 500 ms
#define LED_BRIGHTNESS_FULL          30
#define LED_BRIGHTNESS_HALF          15

#define LED_SEQ_MAX  4
#define LED_I2C_TIMEOUT_MS 2000U

static uint32_t ledLastToggleMs = 0;
static uint32_t lastI2CTransactionCount = 0;
static uint32_t lastI2CSeenMs = 0;

// Color sequence built each cycle from active conditions
static hal_rgb_led_color_t ledSeq[LED_SEQ_MAX];
static uint8_t ledSeqLen = 0;
static uint8_t ledSeqIdx = 0;

void initLed(void) {
  ledLastToggleMs = 0;
  ledSeqLen = 0;
  ledSeqIdx = 0;
  hal_rgb_led_init(PIN_RGB, NUMPIXELS);
  hal_rgb_led_set_brightness(LED_BRIGHTNESS_FULL);
  hal_rgb_led_off();
  lastI2CTransactionCount = hal_i2c_slave_get_transaction_count();
  lastI2CSeenMs = hal_millis();
}

void updateLed(void) {
  uint32_t now = hal_millis();
  uint8_t status = getAdjustometerStatus();

  // Signal lost — red blink 4x/s, overrides everything
  if (status & ADJ_STATUS_SIGNAL_LOST) {
    if ((now - ledLastToggleMs) >= LED_BLINK_NO_OSCILLATION_MS) {
      ledLastToggleMs = now;
      ledSeqIdx = !ledSeqIdx;
      hal_rgb_led_set_brightness(LED_BRIGHTNESS_FULL);
      if (ledSeqIdx) {
        hal_rgb_led_set_color(HAL_RGB_LED_RED);
      } else {
        hal_rgb_led_off();
      }
    }
    return;
  }

  // Build color sequence from active conditions.
  // Use time-based I2C activity detection to avoid rapid LED toggling:
  // updateLed() runs every ~1 ms but I2C transactions arrive every ~10-100 ms,
  // so per-frame comparison would falsely report "no I2C" most of the time.
  uint32_t txnCount = hal_i2c_slave_get_transaction_count();
  if (txnCount != lastI2CTransactionCount) {
    lastI2CTransactionCount = txnCount;
    lastI2CSeenMs = now;
  }
  bool noI2C = (now - lastI2CSeenMs) >= LED_I2C_TIMEOUT_MS;
  bool fuelBroken = (status & ADJ_STATUS_FUEL_TEMP_BROKEN) != 0;
  bool voltageBad = (status & ADJ_STATUS_VOLTAGE_BAD) != 0;

  if (!noI2C && !fuelBroken && !voltageBad) {
    // All OK: steady green at 50% brightness
    ledSeqLen = 0;
    hal_rgb_led_set_brightness(LED_BRIGHTNESS_HALF);
    hal_rgb_led_set_color(HAL_RGB_LED_GREEN);
    return;
  }

  // Build cycling sequence: [purple] [yellow] [red] green
  uint8_t len = 0;
  if (fuelBroken) {
    ledSeq[len++] = HAL_RGB_LED_PURPLE;
  }
  if (voltageBad) {
    ledSeq[len++] = HAL_RGB_LED_YELLOW;
  }
  if (noI2C) {
    ledSeq[len++] = HAL_RGB_LED_RED;
  }
  ledSeq[len++] = HAL_RGB_LED_GREEN;

  // Reset index if sequence changed length
  if (len != ledSeqLen) {
    ledSeqLen = len;
    ledSeqIdx = 0;
    ledLastToggleMs = now;
    hal_rgb_led_set_brightness(LED_BRIGHTNESS_FULL);
    hal_rgb_led_set_color(ledSeq[0]);
    return;
  }

  if ((now - ledLastToggleMs) >= LED_BLINK_STATUS_MS) {
    ledLastToggleMs = now;
    ledSeqIdx++;
    if (ledSeqIdx >= ledSeqLen) {
      ledSeqIdx = 0;
    }
    hal_rgb_led_set_brightness(LED_BRIGHTNESS_FULL);
    hal_rgb_led_set_color(ledSeq[ledSeqIdx]);
  }
}
