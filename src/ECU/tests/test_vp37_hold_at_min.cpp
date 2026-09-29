// The release of the actuator at zero demand is a build option. This binary
// compiles the VP37 units with VP37_PWM_DISABLE_AT_MIN_POSITION at 0 and
// checks that zero demand is then held at the calibrated bottom under drive
// instead of being released.
#include "dtcManager.h"
#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
#include "testable/vp37_test_fixture.h"
#include "unity.h"
#include "vp37_internal.h"
#include <string.h>

// ── Fixture ─────────────────────────────────────────────────────────────────

void setUp(void) { setUpVp37Fixture(); }

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

  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 50.0f));
  trackDemand(pump, &ms, 400U);
  TEST_ASSERT_GREATER_THAN_INT32(0, pump->output.finalPWM);

  // Descend to zero demand and let the slew finish; the drive stays on.
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 0.0f));
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
