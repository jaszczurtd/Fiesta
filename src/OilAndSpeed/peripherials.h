#ifndef S_PERIPHERIALS_H
#define S_PERIPHERIALS_H

#include <Arduino.h>
#include <tools.h>
#include <SPI.h>
#include <canDefinitions.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "hardwareConfig.h"

#define IMPULSES_PER_ROTATION 48 

enum {NONE, RED, GREEN, YELLOW, WHITE, BLUE, PURPLE};

void setupOnboardLed(void);
void initSPI(void);
void initBasicPIO(void);
void setLEDColor(int ledColor);

#endif