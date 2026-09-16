// The release of the actuator at zero demand is a build option. This binary
// compiles the VP37 units with VP37_PWM_DISABLE_AT_MIN_POSITION at 0 and
// checks that zero demand is then held at the calibrated bottom under drive
// instead of being released.
#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
#include "unity.h"
#include "vp37_internal.h"
#include <string.h>

// ── Fixture, the same as test_vp37.cpp's ────────────────────────────────────

static void injectLocalSupplyVoltage(float volts) {
  const float ratio =
      ((float)V_DIVIDER_R1 + (float)V_DIVIDER_R2) / (float)V_DIVIDER_R2;
  int adc = (int)((volts / ratio) * (4095.0f / 3.3f) + 0.5f);
  adc = hal_constrain(adc, 0, 4095);
  hal_mock_adc_inject(ADC_VOLT_PIN, adc);
}

static void injectAdjRegisterData(int16_t pulseHz, uint8_t voltage,
                                  uint8_t fuelTemp, uint8_t status) {
  uint8_t buf[5];
  buf[0] = (uint8_t)((uint16_t)pulseHz >> 8);
  buf[1] = (uint8_t)((uint16_t)pulseHz & 0xFF);
  buf[2] = voltage;
  buf[3] = fuelTemp;
  buf[4] = status;
  hal_mock_i2c_inject_rx(buf, 5);
  injectLocalSupplyVoltage((float)voltage * 0.1f);
}

static void setupPumpForProcessTests(VP37Pump *pump) {
  memset(pump, 0, sizeof(*pump));
  pump->pid.controller = hal_pid_controller_create();
  pump->vp37Initialized = true;
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;
  pump->feedback.adjustMiddle =
      (pump->feedback.adjustMax + pump->feedback.adjustMin) / 2;
  pump->demand.target = -1;
  pump->demand.desired = -1;
  pump->demand.lastThrottle = -1.0f;
  pump->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  pump->pid.integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  pump->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST;
  pump->feedforward.motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  pump->thermal.temperatureCorrection = 1.0f;
  pump->thermal.temperatureCompensationWeight = 1.0f;
  pump->supply.heldVolts = NOMINAL_VOLTAGE;
  pump->supply.ready = false;
  pump->demand.throttleRampLastMs = hal_millis();
  pump->feedback.lastStatus = ADJ_STATUS_OK;
  VP37_setVP37PID(pump, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, false);
  hal_pid_controller_set_tf(pump->pid.controller, VP37_PID_TF);
  hal_pid_controller_set_max_integral(pump->pid.controller,
                                      VP37_PID_MAX_INTEGRAL);
  setGlobalValue(F_RPM, 1000.0f);
  setGlobalValue(F_VOLTS, 14.0f);
  injectLocalSupplyVoltage(14.0f);
  setGlobalValue(F_THROTTLE_POS, 0.0f);
  hal_mock_i2c_set_busy(false);
}

void setUp(void) {
  hal_mock_set_millis(0);
  hal_i2c_init(4, 5, 400000);
  initSensors();
  initI2C();
  dtcManagerInit();
  dtcManagerClearAll();
}

void tearDown(void) {
  hal_mock_i2c_set_busy(false);
  ecu_context_t *ctx = getECUContext();
  VP37Pump *pump = &ctx->injectionPump;
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
}

// ── Zero demand under the hold option ────────────────────────────────────────

static void trackDemand(VP37Pump *pump, uint32_t *ms, uint32_t steps) {
  for (uint32_t i = 0U; i < steps; i++) {
    *ms += 5U;
    hal_mock_set_millis(*ms);
    const int16_t position =
        (int16_t)(pump->demand.desired < 0 ? pump->feedback.adjustMin
                                           : pump->demand.desired);
    injectAdjRegisterData(position, 144, 29, ADJ_STATUS_OK);
    VP37_process(pump);
  }
}

void test_zero_demand_is_held_at_the_bottom_under_drive(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  setupPumpForProcessTests(pump);
  uint32_t ms = 0U;

  VP37_setVP37Throttle(pump, 50);
  trackDemand(pump, &ms, 400U);
  TEST_ASSERT_GREATER_THAN_INT32(0, pump->output.finalPWM);

  // Descend to zero demand and let the slew finish; the drive stays on.
  VP37_setVP37Throttle(pump, 0);
  trackDemand(pump, &ms, 800U);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.desired);
  TEST_ASSERT_FALSE(VP37_demandAtRest(pump));
  TEST_ASSERT_FALSE(pump->demand.atRest);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, pump->feedforward.pwm);
  TEST_ASSERT_GREATER_THAN_INT32(0, pump->output.finalPWM);
  TEST_ASSERT_TRUE(pump->vp37Initialized);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_zero_demand_is_held_at_the_bottom_under_drive);
  return UNITY_END();
}
