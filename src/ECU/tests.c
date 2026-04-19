
#include "tests.h"
#include "dtcManager.h"
#include "sensors.h"
#include "vp37.h"
#include "ecuContext.h"

//=============================================================================
// Global state
//=============================================================================

static bool s_testsInitialized = false;

//=============================================================================
// VP37 Cyclic Test (START_TEST_ENABLE_VP37_CYCLIC)
//=============================================================================
#ifdef START_TEST_ENABLE_VP37_CYCLIC

static PIDValues s_pidVal;
static CyclicTest s_ct;

/// @brief Generate cyclic throttle ramp for VP37 (0-100-0%)
/// Allows observation of PID response during tuning
static int VP37cyclicTest(void) {
  uint32_t currentMillis = hal_millis();

  if(currentMillis - s_ct.previousMillis >= CYCLIC_DELAYTIME) {
    s_ct.previousMillis = currentMillis;
    s_ct.value += s_ct.increment;

    if(s_ct.value >= 100 || s_ct.value <= 0) {
      s_ct.increment = -s_ct.increment;
    }
  }
  return s_ct.value;
}

#endif // START_TEST_ENABLE_VP37_CYCLIC

//=============================================================================
// Public Test Interface
//=============================================================================

/// @brief Initialize tests based on enabled directives
bool initTests(void) {
#ifdef START_TEST_ENABLE_VP37_CYCLIC

  ecu_context_t *ctx = getECUContext();
  VP37_getVP37PIDValues(&ctx->injectionPump, &s_pidVal.kP, &s_pidVal.kI, &s_pidVal.kD);
  s_ct.increment = 1;
  s_ct.uv = 0.001f;
  s_ct.value = 0;
  s_ct.previousMillis = hal_millis();

  deb("TEST: VP37 cyclic test initialized (Kp=%.4f Ki=%.4f Kd=%.4f)", s_pidVal.kP, s_pidVal.kI, s_pidVal.kD);
#endif

  s_testsInitialized = true;
  return true;
}

/// @brief Startup tests (run once at system startup)
bool startTests(void) {
#ifdef START_TEST_ENABLE_DTC_INJECTION
  static bool dtcInjected = false;
  if(!dtcInjected) {
    const uint16_t code = (uint16_t)DTC_PCF8574_COMM_FAIL;
    dtcManagerSetActive(code, true);
    deb("TEST: startup DTC injected: 0x%04X (%s)", (unsigned)code, getDtcName(code));
    dtcInjected = true;
  }
#endif

  return true;
}

/// @brief Periodic test tick (call from looper1 if START_TEST_ENABLE_VP37_CYCLIC enabled)
void tickTests(void) {
#ifdef START_TEST_ENABLE_VP37_CYCLIC
  if(!s_testsInitialized) {
    return;
  }

  int thr = VP37cyclicTest();
  ecu_context_t *ctx = getECUContext();
  VP37_setVP37Throttle(&ctx->injectionPump, thr);

#endif
}
