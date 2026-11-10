#include "oilLamp.h"
#include "clocksState.h"
#include "lampBlink.h"
#include "testOutputs.h"

#include <JaszczurHAL.h>

OilLamp::OilLamp() { reset(); }

void OilLamp::reset() {
  faulty = false;
  faultSinceMs = 0u;
}

bool OilLamp::process(const OilLampInput &input) {
  // A faulty sender has no pressure to tell: the lamp blinks from the first
  // sample that found the fault.
  if (input.senderFault) {
    if (!faulty) {
      faulty = true;
      faultSinceMs = input.nowMs;
    }
    return lampBlinkLit(input.nowMs, faultSinceMs, OIL_LAMP_BLINK_HALF_MS);
  }
  faulty = false;
  // Lit before the first reading, as at power-up, and without pressure.
  return !input.valid || (input.bar < MIN_OIL_PRESSURE);
}

static OilLamp oilLamp;

void initOilLamp(void) {
  oilLamp.reset();
  hal_gpio_set_mode(JH_HW_NODE_OIL_LAMP_PIN_OUT, HAL_GPIO_OUTPUT);
  hal_gpio_write(JH_HW_NODE_OIL_LAMP_PIN_OUT, true);
}

void updateOilLamp(void) {
  clocks_local_snapshot_t local = {};
  clocksStateCopyLocal(&local);
  OilLampInput input;
  input.nowMs = hal_millis();
  input.senderFault = local.oilSenderFault;
  input.valid = local.oilValid;
  input.bar = local.oilBar;
  // The sender evaluation keeps running; a test replaces only the level.
  bool lit = oilLamp.process(input);
  bool testLit = false;
  if (testOutputsLamp(TEST_OUTPUT_OIL, input.nowMs, &testLit)) {
    lit = testLit;
  }
  hal_gpio_write(JH_HW_NODE_OIL_LAMP_PIN_OUT, lit);
}
