#ifndef T_TESTS
#define T_TESTS

#include <tools_c.h>
#include <hal/hal_soft_timer.h>

#ifdef __cplusplus
extern "C" {
#endif

//Inject DTC_PCF8574_COMM_FAIL at startup for diagnostics testing
//#define START_TEST_ENABLE_DTC_INJECTION 

//enable functional tests for VP37 cyclic control via keyboard
#define START_TEST_ENABLE_VP37_CYCLIC // Cyclic test: ramps VP37 throttle 0-100-0%, 
//allows PID tuning via Serial commands (send '?' for help)


#ifdef START_TEST_ENABLE_VP37_CYCLIC
#define CYCLIC_DELAYTIME 8

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
