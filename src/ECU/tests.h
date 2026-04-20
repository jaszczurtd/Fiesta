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
//#define START_TEST_ENABLE_VP37_CYCLIC // Cyclic test: ramps VP37 throttle 0-100-0%, 
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

bool initTests(void);
bool startTests(void);
void tickTests(void);

#ifdef __cplusplus
}
#endif

#endif
