// The ECU side of the VP37 module: board services, start-up and the global
// values taken from the published status. The pump itself knows nothing of the
// PCF8574, the DTC store or the ECU's global values.

#include "vp37_adapter.h"

#include "config.h"
#include "dtcManager.h"
#include "hardwareConfig.h"
#include "sensors.h"
#include "tests.h"

#include <string.h>
#include <utils/multicoreWatchdog.h>

bool vp37AdapterCanServiceCore1(const VP37Pump *pump, uint32_t nowUs) {
  return (pump != NULL) &&
         (!VP37_controlDueWithin(pump, nowUs, CORE1_VP37_GUARD_US) ||
          (pump->controlStarted && VP37_controlDueWithin(pump, nowUs, 0U)));
}

static void vp37AdapterWriteQuantity(int32_t command) {
  (void)pwmWrite(JH_HW_NODE_VP37_PIN_QUANTITY_PWM, command);
}

static void vp37AdapterWriteTiming(int32_t command) {
  (void)pwmWrite(JH_HW_NODE_VP37_PIN_TIMING_PWM, command);
}

static void vp37AdapterSetEnabled(bool enabled) {
  pcf8574_write(JH_HW_NODE_VP37_PIN_ENABLE_INDEX, enabled);
}

static bool vp37AdapterEnabled(void) {
  return pcf8574_read(JH_HW_NODE_VP37_PIN_ENABLE_INDEX);
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

/* What core 0 took from the published status; no other core touches it. */
typedef struct {
  uint32_t reads;        /* good Adjustometer frames already copied */
  uint32_t faultSinceMs; /* first good frame of the current fault run */
  bool fuelTempValid;    /* F_FUEL_TEMP holds a measurement */
  bool faultRun;         /* every good frame since faultSinceMs had the fault */
  bool faultConfirmed;   /* DTC_ADJ_FUEL_TEMP_BROKEN is reported active */
} VP37AdapterPublished;

static VP37AdapterPublished s_vp37Published;

/**
 * @brief Raise or clear DTC_ADJ_FUEL_TEMP_BROKEN from one new good frame.
 * @param fault The frame reported a fuel temperature sensor fault.
 * @param nowMs Current time [ms].
 * @note Frames lost on the bus neither confirm nor clear the fault. A
 * confirmed fault is asserted again with every faulty frame, so it returns
 * after a diagnostic clear while the sensor stays broken.
 */
static void vp37AdapterTrackFuelTempFault(bool fault, uint32_t nowMs) {
  if (fault && !s_vp37Published.faultRun) {
    s_vp37Published.faultSinceMs = nowMs;
  }
  s_vp37Published.faultRun = fault;
  const bool confirmed =
      fault && hal_elapsed_u32(nowMs, s_vp37Published.faultSinceMs,
                               ECU_FUEL_TEMP_FAULT_CONFIRM_MS);
  if (confirmed || s_vp37Published.faultConfirmed) {
    dtcManagerSetActive(DTC_ADJ_FUEL_TEMP_BROKEN, confirmed);
  }
  s_vp37Published.faultConfirmed = confirmed;
}

void vp37AdapterPublish(const VP37Pump *pump) {
  VP37Status status;
  if (VP37_readStatus(pump, &status) != HAL_OK) {
    return;
  }
  /* A lost link invalidates the fuel temperature at once, without a frame. */
  bool fuelTempChanged = status.fuelTempValid != s_vp37Published.fuelTempValid;
  if (status.readCount != s_vp37Published.reads) {
    s_vp37Published.reads = status.readCount;
    vp37AdapterTrackFuelTempFault(status.fuelTempFault, hal_millis());
    setGlobalValue(F_VOLTS, status.supplyVolts);
    fuelTempChanged = true;
  }
  if (fuelTempChanged) {
    s_vp37Published.fuelTempValid = status.fuelTempValid;
    setGlobalValue(F_FUEL_TEMP, status.fuelTempValid ? status.fuelTempC : 0.0f);
  }
}

bool isFuelTempValid(void) { return s_vp37Published.fuelTempValid; }

#ifdef UNIT_TEST
void vp37AdapterResetForTest(void) {
  (void)memset(&s_vp37Published, 0, sizeof(s_vp37Published));
}
#endif

const VP37Callbacks *vp37AdapterCallbacks(void) {
  return &k_vp37AdapterCallbacks;
}

VP37InitStatus vp37AdapterStart(VP37Pump *pump) {
  VP37InitStatus status = VP37_INIT_OUTPUT_UNAVAILABLE;
  const bool outputs = pwmChannelReady(JH_HW_NODE_VP37_PIN_QUANTITY_PWM) &&
                       pwmChannelReady(JH_HW_NODE_VP37_PIN_TIMING_PWM);
  dtcManagerSetActive(DTC_PWM_CHANNEL_NOT_INIT, !outputs);
  if (!outputs) {
    derr("VP37 drive outputs are not initialized");
  } else {
    /* A pump started before keeps its services; VP37_init() reports the rest.
     */
    (void)VP37_setCallbacks(pump, &k_vp37AdapterCallbacks);
    status = VP37_init(pump);
    vp37AdapterPublish(pump);
  }
  return status;
}
