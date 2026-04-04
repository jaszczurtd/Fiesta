
#include "tests.h"
#include "dtcManager.h"

//#ifndef START_TEST_ENABLE_DTC_INJECTION
//#define START_TEST_ENABLE_DTC_INJECTION 1
//#endif

bool initTests(void) {

  //tbd
  return true;
}

bool startTests(void) {
#if START_TEST_ENABLE_DTC_INJECTION
  static bool dtcInjected = false;
  if(!dtcInjected) {
    const uint16_t code = (uint16_t)DTC_PCF8574_COMM_FAIL;
    dtcManagerSetActive(code, true);
    deb("TEST: startup DTC injected: 0x%04X (%s)", (unsigned)code, getDtcName(code));
    dtcInjected = true;
  }
#endif

  //tbd
  return true;
}
