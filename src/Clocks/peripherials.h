#ifndef C_PERIPHERIALS
#define C_PERIPHERIALS

#include <tools.h>
#include <canDefinitions.h>

#include "config.h"
#include "hardwareConfig.h"
#include <hal/hal.h>
#include "buzzer.h"

extern float valueFields[];

#define INITIAL_BRIGHTNESS ((1 << PWM_WRITE_RESOLUTION) - 1)

void setupOnboardLed(void);
void initSPI(void);
void initBasicPIO(void);
void enableOilLamp(bool enable);
int getThrottlePercentage(void);
void lcdBrightness(int val);

#endif
