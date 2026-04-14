#ifndef S_PERIPHERIALS_H
#define S_PERIPHERIALS_H

#include <tools.h>
#include <canDefinitions.h>

#include "config.h"
#include "hardwareConfig.h"


enum {NONE, RED, GREEN, YELLOW, WHITE, BLUE, PURPLE};

void  setGlobalValue(int idx, float val);
float getGlobalValue(int idx);
void setupOnboardLed(void);
void initSPI(void);
void initBasicPIO(void);
void setLEDColor(int ledColor);

#endif
