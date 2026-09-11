
#include "start.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "led.h"
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/i2c/hal_i2c_slave.h>
#include <hal/system/hal_system.h>
#include <hal/timers/hal_soft_timer.h>
#include <limits.h>
#include <utils/multicoreWatchdog.h>

#include "telemetry.h"

#if DEBUG_DEEP
static uint32_t lastPeriodicLogMs = 0U;
#endif
static uint32_t lastAuxiliaryMs = 0U;
static uint32_t lastChipTempReadMs = 0U;
static int16_t chipTempDeciC = INT16_MIN;

/**
 * @brief Initialize core-0 services, sensors and status reporting.
 * @return None.
 */
static void initializeCore0(void) {

  hal_debug_init_default();
  hal_debug_set_module_prefix(SC_MODULE_TOKEN_ADJUSTOMETER);

  setupWatchdog(NULL, WATCHDOG_TIME);

  initI2C();

  hal_delay_ms(ADJUSTOMETER_WARMUP_MS);
  initSensors();
  initLed();

  setStartedCore0();

  deb("Fiesta Adjustometer started: %s\n",
      isEnvironmentStarted() ? "yes" : "no");
}

/**
 * @brief Run one iteration of the idle core-0 maintenance loop.
 * @return None.
 */
static void runCore0(void) {
  updateWatchdogCore0();

  if (!isEnvironmentStarted()) {
    hal_idle();
    return;
  }

  updateAdjustometerCapture();

  static uint32_t lastNumber = UINT32_MAX;
  static uint32_t lastPublishMs = 0U;
  static uint8_t lastStatus = UINT8_MAX;
  adjustometer_feedback_t sample;
  const uint32_t nowMs = hal_millis();
  if (getAdjustometerFeedback(&sample) == HAL_OK &&
      (sample.status != lastStatus ||
       ((sample.number != lastNumber ||
         hal_elapsed_u32(nowMs, lastPublishMs, 5U)) &&
        hal_elapsed_u32(nowMs, lastPublishMs,
                        ADJUSTOMETER_FEEDBACK_MIN_PUBLISH_MS)))) {
    publishAdjustometerFeedback(&sample);
    lastNumber = sample.number;
    lastPublishMs = nowMs;
    lastStatus = sample.status;
  }
  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}

/**
 * @brief Initialize the second core used for auxiliary sensors and diagnostics.
 * @return None.
 */
static void initializeCore1(void) {
  setStartedCore1();

  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

/**
 * @brief Run one iteration of the core-1 Adjustometer loop.
 * @return None.
 */
static void runCore1(void) {

  updateWatchdogCore1();

  if (!isEnvironmentStarted()) {
    hal_idle();
    return;
  }

  if (hal_millis_interval_elapsed_now(&lastAuxiliaryMs,
                                      ADJUSTOMETER_EXT_UPDATE_MS)) {
    updateAuxiliarySensors();
    if (chipTempDeciC == INT16_MIN ||
        hal_millis_interval_elapsed_now(&lastChipTempReadMs, 250U)) {
      const float chipTemp = hal_read_chip_temp();
      chipTempDeciC = chipTemp >= -50.0f && chipTemp <= 150.0f
                          ? (int16_t)(chipTemp * 10.0f)
                          : INT16_MIN;
    }
    adjustometer_feedback_t sample;
    if (getAdjustometerFeedback(&sample) == HAL_OK) {
      publishAdjustometerExtension(&sample, chipTempDeciC);
#if DEBUG_DEEP
      const uint32_t nowMs = hal_millis();
      if (hal_elapsed_u32(nowMs, lastPeriodicLogMs, DEBUG_UPDATE)) {
        lastPeriodicLogMs = nowMs;
        deb("rev:3 p:%d raw:%lu f:%lu age:%u v:%u ft:%u s:%u bl:%lu ready:%d",
            sample.pulseHz, (unsigned long)sample.rawHz,
            (unsigned long)sample.filteredHz, sample.ageUs, sample.voltage,
            sample.fuelTemp, sample.status, (unsigned long)sample.baselineHz,
            isAdjustometerReady());
      }
#endif
    }
    updateLed();
  }

  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}

void app_start(void) {
  initializeCore0();
#if !HAL_TARGET_IS_RP
  initializeCore1();
#endif
}

void app_task0(void) { runCore0(); }

void app_task1(void) {
#if HAL_TARGET_IS_RP
  static bool initialized = false;
  if (!initialized) {
    initializeCore1();
    initialized = true;
  }
#endif
  runCore1();
}
