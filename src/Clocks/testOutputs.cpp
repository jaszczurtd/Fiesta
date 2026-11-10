#include "testOutputs.h"
#include "config.h"
#include "lampBlink.h"

namespace {
struct TestOutput {
  bool owned;
  unsigned int value;       // gauge: rpm or km/h
  clocks_test_lamp_t state; // lamp
  uint32_t sinceMs;         // lamp: start of the blink phase
};
TestOutput s_outputs[TEST_OUTPUT_COUNT];

bool valid(clocks_test_output_t output) {
  return (unsigned int)output < (unsigned int)TEST_OUTPUT_COUNT;
}

uint32_t blinkHalfMs(clocks_test_output_t output) {
  return output == TEST_OUTPUT_CHARGE ? (uint32_t)CHARGE_LAMP_BLINK_HALF_MS
                                      : (uint32_t)OIL_LAMP_BLINK_HALF_MS;
}
} // namespace

void testOutputsReset(void) {
  for (unsigned int i = 0u; i < (unsigned int)TEST_OUTPUT_COUNT; i++) {
    s_outputs[i] = TestOutput{};
  }
}

void testOutputsRelease(clocks_test_output_t output) {
  if (valid(output)) {
    s_outputs[output] = TestOutput{};
  }
}

bool testOutputsOwned(clocks_test_output_t output) {
  return valid(output) && s_outputs[output].owned;
}

void testOutputsSetGauge(clocks_test_output_t output, unsigned int value) {
  if (valid(output)) {
    s_outputs[output].owned = true;
    s_outputs[output].value = value;
  }
}

bool testOutputsGauge(clocks_test_output_t output, unsigned int *outValue) {
  if (!testOutputsOwned(output)) {
    return false;
  }
  *outValue = s_outputs[output].value;
  return true;
}

void testOutputsSetLamp(clocks_test_output_t output, clocks_test_lamp_t state,
                        uint32_t nowMs) {
  if (valid(output)) {
    s_outputs[output].owned = true;
    s_outputs[output].state = state;
    s_outputs[output].sinceMs = nowMs;
  }
}

bool testOutputsLamp(clocks_test_output_t output, uint32_t nowMs,
                     bool *outLit) {
  if (!testOutputsOwned(output)) {
    return false;
  }
  const TestOutput &lamp = s_outputs[output];
  switch (lamp.state) {
  case TEST_LAMP_LIT:
    *outLit = true;
    break;
  case TEST_LAMP_BLINK:
    *outLit = lampBlinkLit(nowMs, lamp.sinceMs, blinkHalfMs(output));
    break;
  case TEST_LAMP_OFF:
  default:
    *outLit = false;
    break;
  }
  return true;
}

void testOutputsSetBuzzer(bool owned) {
  s_outputs[TEST_OUTPUT_BUZZER].owned = owned;
}
