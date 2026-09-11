#ifndef T_TESTS
#define T_TESTS

#include <JaszczurHAL.h>
#include <hal/timers/hal_soft_timer.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inject DTC_PCF8574_COMM_FAIL at startup for diagnostics testing
// #define START_TEST_ENABLE_DTC_INJECTION

// 0: engine start/idle/driver demand; 1: cyclic bench; 2: direct potentiometer
// bench. A build definition can override the local selection without editing
// this file.
#ifndef START_TEST_VP37_MODE
#define START_TEST_VP37_MODE 0
#endif
#if START_TEST_VP37_MODE == 1
#define START_TEST_ENABLE_VP37_CYCLIC
#elif START_TEST_VP37_MODE == 2
#define START_TEST_ENABLE_VP37_POTENTIOMETER
#elif START_TEST_VP37_MODE != 0
#error "Invalid START_TEST_VP37_MODE (expected 0, 1 or 2)"
#endif
#if defined(START_TEST_ENABLE_VP37_CYCLIC) &&                                  \
    defined(START_TEST_ENABLE_VP37_POTENTIOMETER)
#error "Select only one VP37 bench demand source"
#endif

#ifdef START_TEST_ENABLE_VP37_CYCLIC
#ifndef CYCLIC_DELAYTIME
#define CYCLIC_DELAYTIME 12
#endif
// Hold deadlines include setpoint slew; expiry selects zero demand.
#define VP37_BENCH_HOLD_MS 2000U
#define VP37_BENCH_HIGH_HOLD_MS 1000U
#define VP37_BENCH_HIGH_HOLD_PERCENT 80.0f
#define VP37_BENCH_INTEGRAL_LIMIT_MAX 360.0f

// Serial command buffer for runtime PID tuning
#define VP37_CMD_BUF_SIZE 64

typedef struct {
  uint32_t previousMillis;
  int increment;
  int value;
  float uv;
  char cmdBuf[VP37_CMD_BUF_SIZE];
  uint8_t cmdLen;
} CyclicTest;

#endif

/**
 * @brief Initialize optional built-in test helpers selected at compile time.
 * @return True when initialization finished.
 */
bool initTests(void);

/**
 * @brief Run one-shot startup test hooks.
 * @return True when startup test processing finished.
 */
bool startTests(void);

/**
 * @brief Execute one periodic step of enabled runtime tests.
 * @return None.
 */
void tickTests(void);

/**
 * @brief Forward one already-parsed serial command line to enabled test
 *        fixtures (e.g. VP37 PID tuner).
 *
 * Intended to be wired as the unknown-line callback for the HAL serial
 * session helper so that test fixtures consume serial commands only after
 * the bootstrap protocol parser (HELLO etc.) has had its chance to handle
 * them. Safe to call when no tests are compiled in - it becomes a no-op.
 *
 * @param line NUL-terminated command line (no trailing CR/LF).
 * @return None.
 */
void tickTestsHandleSerialLine(const char *line);

#ifdef __cplusplus
}
#endif

#endif
