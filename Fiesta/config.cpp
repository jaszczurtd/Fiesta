
#include <Arduino.h>
#include "config.h"
#include "start.h"

#ifndef MESSAGE_DISPLAYED
#ifdef INC_FREERTOS_H
#warning "this is FreeRTOS build"
#else
#warning "this is normal Arduino, 2 cores build"
#endif
#define MESSAGE_DISPLAYED
#endif

const char *err = (char*)F("ERR");
