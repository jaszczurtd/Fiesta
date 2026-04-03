#ifndef T_HEATER
#define T_HEATER

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"
#include "engineFan.h"

typedef struct {
  bool heaterLoEnabled;
  bool heaterHiEnabled;
  bool lastHeaterLoEnabled;
  bool lastHeaterHiEnabled;
} engineHeater;

void engineHeater_init(engineHeater *self);
void engineHeater_process(engineHeater *self);
void engineHeater_showDebug(engineHeater *self);
void engineHeater_heater(engineHeater *self, bool enable, int level);

engineHeater *getHeaterInstance(void);
void createHeater(void);

#endif
