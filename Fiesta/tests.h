#ifndef T_TESTS
#define T_TESTS

#include <Arduino.h>
#include <tools.h>
#include <arduino-timer.h>
#include <unity.h>
#include <unity_config.h>
#include <pidController.h>

#include "vp37.h"
#include "turbo.h"
#include "engineOperation.h"

//miliseconds
#define ENGINE_KEYBOARD_UPDATE 50

typedef struct {
  unsigned char key;
  unsigned char lastKeyState; 
  bool keyPressed;
} Keyboard;

//just enable some test by uncomment it (for now)
//#define TEST_CYCLIC



#ifdef TEST_CYCLIC
#define CYCLIC_DELAYTIME 3
typedef struct {
  unsigned long previousMillis; 
  int increment; 
  int value;
  float uv;
} CyclicTest;
#endif

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
void tickTests(void);

#endif
