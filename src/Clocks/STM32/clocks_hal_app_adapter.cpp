#include <hal/hal_app.h>

#include "logic.h"

extern "C" void app_start(void) {
  initialization();
  initialization1();
}

extern "C" void app_task0(void) { looper(); }

extern "C" void app_task1(void) { looper1(); }
