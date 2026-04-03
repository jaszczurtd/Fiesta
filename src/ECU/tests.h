#ifndef T_TESTS
#define T_TESTS

#include <tools.h>

#ifdef __cplusplus
extern "C" {
#endif

//debug i2c only
//#define I2C_SCANNER

//for debug - display values on LCD
//debugFunc() function is invoked, no regular drawings
//#define DEBUG

//for serial debug
//#define DEBUG


bool initTests(void);
bool startTests(void);

#ifdef __cplusplus
}
#endif

#endif
