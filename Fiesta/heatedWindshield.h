#ifndef T_HEATED_WINDSHIELD
#define T_HEATED_WINDSHIELD

#include <tools.h>

#include "config.h"
#include "start.h"
#include "tests.h"

void heatedWindow(bool enable, int side);
void initHeatedWindow(void);
bool isHeatedWindowEnabled(void);
void heatedWindowMainLoop(void);

#endif