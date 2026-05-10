#include "unity.h"
#include "engine_operation.h"
#include "ecuContext.h"
#include "sensors.h"
#include "rpm.h"
#include "dtcManager.h"
#include "hal/impl/.mock/hal_mock.h"

static void setupPumpForEngineOperation(VP37Pump *pump) {
    memset(pump, 0, sizeof(*pump));
    pump->vp37Initialized = true;
    pump->calibrationDone = true;
    pump->VP37_ADJUST_MIN = 100;
    pump->VP37_ADJUST_MAX = 9100;
    pump->VP37_ADJUST_MIDDLE = (pump->VP37_ADJUST_MAX + pump->VP37_ADJUST_MIN) / 2;
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
}

void test_engine_operation_cranking_uses_start_demand(void) {
    ecu_context_t *ctx = getECUContext();
    setupPumpForEngineOperation(&ctx->injectionPump);
    engineOperation_init(&ctx->engineOp);

    setGlobalValue(F_THROTTLE_POS, 0.0f);
    getRPMInstance()->rpmValue = ENGINE_OP_CRANKING_RPM_MIN + 1;

    hal_mock_set_millis(0);
    engineOperation_process(&ctx->engineOp);

    int32_t expectedTarget = (int32_t)mapfloat((float)ENGINE_OP_START_DEMAND_MIN,
                                               VP37_PERCENT_MIN,
                                               VP37_PERCENT_MAX,
                                               (float)ctx->injectionPump.VP37_ADJUST_MIN,
                                               (float)ctx->injectionPump.VP37_ADJUST_MAX);
    TEST_ASSERT_EQUAL_INT32(expectedTarget, ctx->injectionPump.desiredAdjustometerTarget);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_engine_operation_cranking_uses_start_demand);

    return UNITY_END();
}
