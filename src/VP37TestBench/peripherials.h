#ifndef C_PERIPHERIALS
#define C_PERIPHERIALS

#include <JaszczurHAL.h>
#include <hal/display/hal_display.h>

#include "config.h"
#include "hardwareConfig.h"

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 320

void initI2C(void);
void initSPI(void);
void setupOnboardLed(void);

#endif
