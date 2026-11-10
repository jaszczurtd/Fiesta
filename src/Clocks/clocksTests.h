#ifndef C_CLOCKS_TESTS
#define C_CLOCKS_TESTS

#include "../common/scDefinitions/sc_command_handlers.h"

#include <stdbool.h>

// Functional tests of the factory cluster: the charge lamp, the oil lamp, the
// tachometer, the speedometer and the buzzer, run from the configurator
// through the shared runner of src/common/test_runner. Core 0 is the
// controller role: it ticks the tests before the loop writes the outputs and
// shows every step through testOutputs. Core 1 is the session role: it serves
// the configurator and prints the steps while the debug log is audible (a
// configurator session mutes it; the SC status then carries the step). The
// measurements keep flowing to the display and to CAN while a test shows its
// values.

enum {
  CLOCKS_TEST_CHARGE = 1,
  CLOCKS_TEST_OIL,
  CLOCKS_TEST_RPM,
  CLOCKS_TEST_SPEED,
  CLOCKS_TEST_BUZZER
};

/** @brief Core 0: initialize the runner before core 1 serves the session. */
bool initClocksTests(void);
/**
 * @brief Core 0: one runner tick; call before CAN and the timers that write
 * the outputs, so a new step reaches them in the same pass.
 * @return Value of the runner tick; false, as no Clocks test drives a demand.
 */
bool tickClocksTests(void);
/**
 * @brief Core 1: print the lifecycle and step announcements.
 * @note While the debug log is muted they are dropped, not printed later.
 */
void clocksTestsEvents(void);
/** @brief Operations for the configurator session of core 1. */
const sc_command_test_ops_t *clocksTestsScOps(void);
/** @brief Core 1: the configurator session ended; its test stops. */
void clocksTestsScSessionEnded(void);
/** @return Name of the running test, or NULL; core 0 only. */
const char *clocksTestsActiveName(void);

#endif
