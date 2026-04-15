#ifndef T_LED
#define T_LED

#include <libConfig.h>
#include "config.h"

#include <tools_c.h>
#include "hardwareConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

void initLed(void);
void updateLed(void);

#ifdef __cplusplus
}
#endif

#endif
