#include "firmware_entry.h"

extern "C" void app_start(void) { initialization(); }

extern "C" void app_task0(void) { looper(); }

#if defined(FIESTA_ENABLE_CORE1) && FIESTA_ENABLE_CORE1
extern "C" void app_task1(void) {
  static bool initialized = false;
  if (!initialized) {
    initialization1();
    initialized = true;
  }
  looper1();
}
#endif
