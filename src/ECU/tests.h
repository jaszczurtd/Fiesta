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
//#define START_TEST_ENABLE_VP37_CYCLIC // Cyclic test: ramps VP37 throttle 0-100-0%, allows PID tuning via keyboard pins 0-7

//miliseconds
#define ENGINE_KEYBOARD_UPDATE 50

#ifdef START_TEST_ENABLE_VP37_CYCLIC
#define CYCLIC_DELAYTIME 3

typedef struct {
  uint32_t previousMillis;
  int increment;
  int value;
  float uv;
} CyclicTest;

typedef struct {
  float kP;
  float kI;
  float kD;
} PIDValues;

#endif

bool initTests(void);
bool startTests(void);
void tickTests(void);

#ifdef __cplusplus
}
#endif

#endif
