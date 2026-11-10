#ifndef C_TEST_OUTPUTS
#define C_TEST_OUTPUTS

#include <stdbool.h>
#include <stdint.h>

// Test values that stand in for the cluster outputs while a functional test
// runs. Core 0 owns the state: a test step sets it and the output updaters
// read it in the same loop pass. The measurements stay untouched, so the value
// a test shows never reaches canValues, the local snapshot or a CAN frame.

typedef enum {
  TEST_OUTPUT_RPM = 0,
  TEST_OUTPUT_SPEED,
  TEST_OUTPUT_CHARGE,
  TEST_OUTPUT_OIL,
  TEST_OUTPUT_BUZZER,
  TEST_OUTPUT_COUNT
} clocks_test_output_t;

typedef enum {
  TEST_LAMP_OFF = 0,
  TEST_LAMP_LIT,
  TEST_LAMP_BLINK
} clocks_test_lamp_t;

/** @brief Release every output: the measurements drive them again. */
void testOutputsReset(void);
/** @brief Release one output. */
void testOutputsRelease(clocks_test_output_t output);
/** @return True while a test owns @p output. */
bool testOutputsOwned(clocks_test_output_t output);
/** @brief Show @p value (rpm or km/h) on a gauge instead of the measurement. */
void testOutputsSetGauge(clocks_test_output_t output, unsigned int value);
/**
 * @brief Test value of a gauge.
 * @param outValue Receives the value when the gauge is under test.
 * @return True when @p outValue replaces the measurement.
 */
bool testOutputsGauge(clocks_test_output_t output, unsigned int *outValue);
/**
 * @brief Show @p state on a lamp; blinking counts its phase from @p nowMs
 * with the half period of that lamp.
 */
void testOutputsSetLamp(clocks_test_output_t output, clocks_test_lamp_t state,
                        uint32_t nowMs);
/**
 * @brief Test state of a lamp at @p nowMs.
 * @param outLit Receives the level when the lamp is under test.
 * @return True when @p outLit replaces the lamp logic.
 */
bool testOutputsLamp(clocks_test_output_t output, uint32_t nowMs, bool *outLit);
/** @brief Hand the buzzer to the test, or back to the start beep and alarms. */
void testOutputsSetBuzzer(bool owned);

#endif
