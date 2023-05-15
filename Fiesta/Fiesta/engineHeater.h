#ifndef T_HEATER
#define T_HEATER

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

void heater(bool enable, int level);
void engineHeaterMainLoop(void);

#endif