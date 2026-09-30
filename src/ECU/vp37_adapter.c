// The ECU side of the VP37 module: board services, start-up and the global
// values taken from the published status. The pump itself knows nothing of the
// PCF8574, the DTC store or the ECU's global values.

#include "vp37_adapter.h"

#include "config.h"
#include "dtcManager.h"
#include "hardwareConfig.h"
#include "sensors.h"
#include "tests.h"

#include <utils/multicoreWatchdog.h>

static void vp37AdapterWriteQuantity(int32_t command) {
  (void)pwmWrite(PIO_VP37_RPM, command);
}

static void vp37AdapterWriteTiming(int32_t command) {
  (void)pwmWrite(PIO_VP37_ANGLE, command);
}

static void vp37AdapterSetEnabled(bool enabled) {
  pcf8574_write(PCF8574_O_VP37_ENABLE, enabled);
}

static bool vp37AdapterEnabled(void) {
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

static hal_status_t vp37AdapterAdjustometerTransfer(uint8_t reg, uint8_t *data,
                                                    size_t len) {
  return i2cReadRegisters(ADJUSTOMETER_I2C_ADDR, reg, data, len);
}

static bool vp37AdapterDriveAllowed(void) {
  const bool allowed = (int32_t)getGlobalValue(F_RPM) <= RPM_MAX_EVER;
  if (!allowed) {
    derr("VP37 disabled: RPM too high");
  }
  return allowed;
}

static const VP37Callbacks k_vp37AdapterCallbacks = {
    .writeQuantityPwm = vp37AdapterWriteQuantity,
    .writeTimingPwm = vp37AdapterWriteTiming,
    .setDriveEnabled = vp37AdapterSetEnabled,
    .driveEnabled = vp37AdapterEnabled,
    .adjustometerTransfer = vp37AdapterAdjustometerTransfer,
    .driveAllowed = vp37AdapterDriveAllowed,
    .feedWatchdog = watchdog_feed,
    .activeTestName = testsActiveName,
    .cyclicDelayMs = testsCyclicDelayMs,
};

void vp37AdapterPublish(const VP37Pump *pump) {
  /* Good Adjustometer frames already copied to the global values; core 0. */
  static uint32_t s_vp37PublishedReads = 0U;
  VP37Status status;
  if ((VP37_readStatus(pump, &status) == HAL_OK) &&
      (status.readCount != s_vp37PublishedReads)) {
    s_vp37PublishedReads = status.readCount;
    setGlobalValue(F_FUEL_TEMP, status.fuelTempC);
    setGlobalValue(F_VOLTS, status.supplyVolts);
  }
}

const VP37Callbacks *vp37AdapterCallbacks(void) {
  return &k_vp37AdapterCallbacks;
}

VP37InitStatus vp37AdapterStart(VP37Pump *pump) {
  VP37InitStatus status = VP37_INIT_OUTPUT_UNAVAILABLE;
  const bool outputs =
      pwmChannelReady(PIO_VP37_RPM) && pwmChannelReady(PIO_VP37_ANGLE);
  dtcManagerSetActive(DTC_PWM_CHANNEL_NOT_INIT, !outputs);
  if (!outputs) {
    derr("VP37 drive outputs are not initialized");
  } else {
    /* Both refuse only a pump started before, which keeps its services;
     * VP37_init() reports the rest. */
    (void)VP37_setCallbacks(pump, &k_vp37AdapterCallbacks);
    (void)VP37_setAdjustometerFastFeedback(pump, true);
    status = VP37_init(pump);
    vp37AdapterPublish(pump);
  }
  return status;
}
