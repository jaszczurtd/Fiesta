// The bench side of the VP37 module: board services on the test stand's own
// hardware. The power stage is the twin of the ECU one, the drive enable is a
// plain GPIO, and the Adjustometer transfer owns the I2C bus alone, so it
// needs no bus mutex.

#include "vp37_bench_adapter.h"

#include "hardwareConfig.h"

#include <utils/multicoreWatchdog.h>

static hal_pwm_freq_channel_t s_quantityChannel;
static hal_pwm_freq_channel_t s_timingChannel;

/** @brief The command as the power stage takes it: active low, like valToPWM
 * writes it on the ECU. */
static void vp37BenchWrite(hal_pwm_freq_channel_t channel, int32_t command) {
  if (channel != NULL) {
    hal_pwm_freq_write(channel, (VP37_PWM_RESOLUTION - command));
  }
}

static void vp37BenchWriteQuantity(int32_t command) {
  vp37BenchWrite(s_quantityChannel, command);
}

static void vp37BenchWriteTiming(int32_t command) {
  vp37BenchWrite(s_timingChannel, command);
}

static void vp37BenchSetEnabled(bool enabled) {
  hal_gpio_write(PIN_VP37_ENABLE, enabled);
}

static bool vp37BenchEnabled(void) { return hal_gpio_read(PIN_VP37_ENABLE); }

static hal_status_t vp37BenchAdjustometerTransfer(uint8_t reg, uint8_t *data,
                                                  size_t length) {
  return hal_i2c_write_read_bus_ex(0, ADJUSTOMETER_I2C_ADDR, &reg, 1U, data,
                                   length);
}

static const VP37Callbacks k_vp37BenchCallbacks = {
    .writeQuantityPwm = vp37BenchWriteQuantity,
    .writeTimingPwm = vp37BenchWriteTiming,
    .setDriveEnabled = vp37BenchSetEnabled,
    .driveEnabled = vp37BenchEnabled,
    .adjustometerTransfer = vp37BenchAdjustometerTransfer,
    .feedWatchdog = watchdog_feed,
};

const VP37Callbacks *vp37BenchCallbacks(void) { return &k_vp37BenchCallbacks; }

VP37InitStatus vp37BenchAdapterStart(VP37Pump *pump) {
  VP37InitStatus status = VP37_INIT_OUTPUT_UNAVAILABLE;

  if (VP37_isReady(pump)) {
    /* VP37_init() would refuse this start anyway; refuse it before the
     * drive outputs are torn down, so a running pump keeps running. */
    return VP37_INIT_ALREADY_INITIALIZED;
  }

  hal_gpio_set_mode(PIN_VP37_ENABLE, HAL_GPIO_OUTPUT);
  hal_gpio_write(PIN_VP37_ENABLE, false);

  // Reinitialization replaces channels; do not abandon their pool slots.
  if (s_quantityChannel != NULL) {
    hal_pwm_freq_destroy(s_quantityChannel);
  }
  if (s_timingChannel != NULL) {
    hal_pwm_freq_destroy(s_timingChannel);
  }
  s_quantityChannel = hal_pwm_freq_create(
      VP37_QUANTITY_PWM_PIN, VP37_PWM_FREQUENCY_HZ, VP37_PWM_RESOLUTION);
  s_timingChannel = hal_pwm_freq_create(
      VP37_TIMING_PWM_PIN, VP37_TIMING_PWM_FREQUENCY_HZ, VP37_PWM_RESOLUTION);

  if ((s_quantityChannel != NULL) && (s_timingChannel != NULL)) {
    // The scan belongs to this core, like on the ECU: its completion
    // interrupt must run where the blocks are consumed.
    const hal_status_t scanStatus = VP37_currentScanStart();
    if (scanStatus != HAL_OK) {
      derr("VP37 bench scan start failed: %s",
           hal_status_to_string(scanStatus));
    }
    (void)VP37_setCallbacks(pump, &k_vp37BenchCallbacks);
    (void)VP37_setAdjustometerFastFeedback(pump, true);
    status = VP37_init(pump);
  } else {
    derr("VP37 bench drive outputs are not initialized");
  }
  return status;
}

#ifdef UNIT_TEST
hal_pwm_freq_channel_t vp37BenchQuantityChannel(void) {
  return s_quantityChannel;
}
hal_pwm_freq_channel_t vp37BenchTimingChannel(void) { return s_timingChannel; }
#endif

float vp37BenchDemandPercent(void) {
  const int raw = hal_adc_read(VP37_SCAN_AUX_ADC_PIN);
  const float span = 4095.0f;
  float percent = ((float)raw * 100.0f) / span;
  percent = hal_constrain(percent, 0.0f, 100.0f);
  return percent;
}
