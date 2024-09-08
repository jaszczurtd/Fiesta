#ifndef C_PERIPHERIALS
#define C_PERIPHERIALS

#include <Arduino.h>
#include <tools.h>
#include <SPI.h>
#include <canDefinitions.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "hardwareConfig.h"
#include "buzzer.h"

enum {NONE, RED, GREEN, YELLOW, WHITE, BLUE};

extern float valueFields[];

//in miliseconds, print values into serial
#define DEBUG_UPDATE 3 * SECOND

#define INITIAL_BRIGHTNESS ((1 << PWM_WRITE_RESOLUTION) - 1)

void setupOnboardLed(void);
void initSPI(void);
void initBasicPIO(void);
int getThrottlePercentage(void);
void lcdBrightness(int val);
void setLEDColor(int ledColor);

#endif
