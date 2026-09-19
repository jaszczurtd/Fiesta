#include "dtcManager.h"
#include "ecuContext.h"
#include "engine_operation.h"
#include "hal/impl/.mock/hal_mock.h"
#include "rpm.h"
#include "sensors.h"
#include "unity.h"

static void setupPumpForEngineOperation(VP37Pump *pump) {
  memset(pump, 0, sizeof(*pump));
  pump->vp37Initialized = true;
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;
  pump->feedback.adjustMiddle =
      (pump->feedback.adjustMax + pump->feedback.adjustMin) / 2;
}

void setUp(void) {
  hal_mock_set_millis(0);
  hal_i2c_init(4, 5, 400000);
  initSensors();
  initI2C();
  dtcManagerInit();
  dtcManagerClearAll();
}

void tearDown(void) { hal_mock_i2c_set_busy(false); }

static void sampleDriverDemand(int raw) {
  hal_mock_adc_inject(ADC_SENSORS_PIN, raw);
  readThrottleValues();
}

void test_engine_operation_cranking_uses_start_demand(void) {
  ecu_context_t *ctx = getECUContext();
  setupPumpForEngineOperation(&ctx->injectionPump);
  engineOperation_init(&ctx->engineOp);

  sampleDriverDemand(4095);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getDriverDemandPercent());
  getRPMInstance()->rpmValue = ENGINE_OP_CRANKING_RPM_MIN + 1;

  hal_mock_set_millis(0);
  engineOperation_process(&ctx->engineOp);

  int32_t expectedTarget = (int32_t)hal_math_map_f32(
      (float)ENGINE_OP_START_DEMAND_MIN, VP37_PERCENT_MIN, VP37_PERCENT_MAX,
      (float)ctx->injectionPump.feedback.adjustMin,
      (float)ctx->injectionPump.feedback.adjustMax);
  TEST_ASSERT_EQUAL_INT32(expectedTarget, ctx->injectionPump.demand.target);
}

void test_engine_operation_closed_throttle_keeps_engine_start_and_idle_demand(
    void) {
  ecu_context_t *ctx = getECUContext();
  setupPumpForEngineOperation(&ctx->injectionPump);
  engineOperation_init(&ctx->engineOp);
  sampleDriverDemand(4095);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getDriverDemandPercent());
  setGlobalValue(F_COOLANT_TEMP, 90.0f);
  getRPMInstance()->rpmValue = 0;
  engineOperation_process(&ctx->engineOp);
  TEST_ASSERT_EQUAL_INT(ENGINE_OP_STATE_STOPPED, ctx->engineOp.state);
  TEST_ASSERT_EQUAL_FLOAT(ENGINE_OP_START_DEMAND_MIN,
                          ctx->injectionPump.demand.requestedPercent);

  getRPMInstance()->rpmValue = 900;
  hal_mock_set_millis(50);
  engineOperation_process(&ctx->engineOp);
  TEST_ASSERT_EQUAL_INT(ENGINE_OP_STATE_IDLE, ctx->engineOp.state);
  TEST_ASSERT_GREATER_THAN_FLOAT(0, ctx->injectionPump.demand.requestedPercent);

  // A rising idle correction reaches the actuator entry immediately; the
  // analog-input filter must not delay the outer regulator's computed demand.
  const float idleDemand = ctx->injectionPump.demand.requestedPercent;
  TEST_ASSERT_GREATER_THAN_FLOAT((float)ENGINE_OP_IDLE_DEMAND_MIN, idleDemand);
  getRPMInstance()->rpmValue = 850;
  hal_mock_set_millis(51);
  engineOperation_process(&ctx->engineOp);
  TEST_ASSERT_FLOAT_WITHIN(.001f, idleDemand + (50.0f * ENGINE_OP_IDLE_P_GAIN),
                           ctx->injectionPump.demand.requestedPercent);
}

void test_fractional_engine_driver_demand_matches_common_position_entry(void) {
  ecu_context_t *ctx = getECUContext();
  setupPumpForEngineOperation(&ctx->injectionPump);
  engineOperation_init(&ctx->engineOp);
  sampleDriverDemand(2700);
  const float demand = getDriverDemandPercent();
  TEST_ASSERT_GREATER_THAN_FLOAT(
      (float)ACCELERATE_MIN_PERCENTAGE_THROTTLE_VALUE, demand);
  TEST_ASSERT_GREATER_THAN_FLOAT(.001f, demand - (float)(int32_t)demand);
  setGlobalValue(F_COOLANT_TEMP, 90.0f);
  getRPMInstance()->rpmValue = 900;
  engineOperation_process(&ctx->engineOp);

  TEST_ASSERT_EQUAL_INT(ENGINE_OP_STATE_DRIVER, ctx->engineOp.state);
  TEST_ASSERT_EQUAL_FLOAT(demand, ctx->injectionPump.demand.requestedPercent);
  VP37Pump direct;
  setupPumpForEngineOperation(&direct);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemand(&direct, demand));
  TEST_ASSERT_EQUAL_INT32(direct.demand.target,
                          ctx->injectionPump.demand.target);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_engine_operation_cranking_uses_start_demand);
  RUN_TEST(
      test_engine_operation_closed_throttle_keeps_engine_start_and_idle_demand);
  RUN_TEST(test_fractional_engine_driver_demand_matches_common_position_entry);

  return UNITY_END();
}
