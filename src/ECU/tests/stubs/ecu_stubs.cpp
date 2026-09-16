/*
 * ECU test stubs - stub implementations for modules excluded from the test
 * build.
 *
 * start.cpp is excluded because it uses the multicoreWatchdog timer dependency.
 *
 * Some compiled ECU modules (can/obd-2/vp37) call watchdog_feed(), so we
 * provide a no-op stub for host-unit tests.
 */

#include "ecuContext.h"
#include "rpm.h"

#include <utils/multicoreWatchdog.h>

#include <cstdint>

// ── Central context stub (start.cpp is excluded in tests) ────────────────────

static ecu_context_t s_ctx;

ecu_context_t *getECUContext(void) { return &s_ctx; }

void watchdog_feed(void) {}

// ── tests.c stubs: tests.c is excluded from the host library so a test
//    binary can compile its own copy with ECU_FUNCTIONAL_TESTS_ENABLED.
//    config.c routes unknown serial lines here, so the stub records the
//    last one and lets unit tests assert the routing. vp37.c asks the test
//    layer which test is running; without tests the answer is none.
static char s_lastForwardedLine[128] = {0};
static unsigned s_forwardedCount = 0;

extern "C" __attribute__((weak)) void
tickTestsHandleSerialLine(const char *line) {
  s_forwardedCount++;
  if (line == nullptr) {
    s_lastForwardedLine[0] = '\0';
    return;
  }
  size_t i = 0;
  while (line[i] != '\0' && i + 1u < sizeof(s_lastForwardedLine)) {
    s_lastForwardedLine[i] = line[i];
    i++;
  }
  s_lastForwardedLine[i] = '\0';
}

extern "C" const char *test_stubs_last_forwarded_serial_line(void) {
  return s_lastForwardedLine;
}

extern "C" unsigned test_stubs_forwarded_serial_count(void) {
  return s_forwardedCount;
}

extern "C" void test_stubs_reset_forwarded_serial(void) {
  s_lastForwardedLine[0] = '\0';
  s_forwardedCount = 0;
}

extern "C" __attribute__((weak)) const char *testsActiveName(void) {
  return nullptr;
}

extern "C" __attribute__((weak)) uint32_t testsCyclicDelayMs(void) {
  return 0u;
}
