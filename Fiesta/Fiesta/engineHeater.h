#ifndef T_HEATER
#define T_HEATER

#include "utils.h"
#include "config.h"
#include "start.h"

void heater(bool enable, int level);
void engineHeaterMainLoop(void);

#endif