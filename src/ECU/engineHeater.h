#ifndef T_HEATER
#define T_HEATER

#include <tools_c.h>

#include "config.h"
#include "sensors.h"
#include "tests.h"
#include "engineFan.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool heaterLoEnabled;
  bool heaterHiEnabled;
  bool lastHeaterLoEnabled;
  bool lastHeaterHiEnabled;
} engineHeater;

void engineHeater_init(engineHeater *self);
void engineHeater_process(engineHeater *self);
void engineHeater_showDebug(engineHeater *self);
void engineHeater_heater(engineHeater *self, bool enable, int32_t level);

engineHeater *getHeaterInstance(void);
void createHeater(void);

#ifdef __cplusplus
}
#endif

#endif
