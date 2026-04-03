#ifndef T_FAN
#define T_FAN

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"
#include "canDefinitions.h"

enum {
  FAN_REASON_NONE, FAN_REASON_COOLANT, FAN_REASON_AIR
};

typedef struct {
  int fanEnabled;
  int lastFanStatus;
} engineFan;

void engineFan_init(engineFan *self);
void engineFan_process(engineFan *self);
void engineFan_showDebug(engineFan *self);
bool engineFan_isFanEnabled(engineFan *self);
void engineFan_fan(engineFan *self, bool enable);

engineFan *getFanInstance(void);
void createFan(void);

#endif
