#ifndef T_TESTS
#define T_TESTS

#include <Arduino.h>
#include <tools.h>
#include <arduino-timer.h>
#include <unity.h>
#include <unity_config.h>

//debug i2c only
//#define I2C_SCANNER

//for debug - display values on LCD
//debugFunc() function is invoked, no regular drawings
//#define DEBUG

//for serial debug
//#define DEBUG


#ifdef DEBUG_SCREEN
void debugFunc(void);
#endif

bool initTests(void);
bool startTests(void);

#endif
