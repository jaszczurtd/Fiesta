#ifndef T_FAN
#define T_FAN

#include <tools_c.h>

#include "config.h"
#include "sensors.h"
#include "tests.h"
#include "canDefinitions.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  FAN_REASON_NONE, FAN_REASON_COOLANT, FAN_REASON_AIR
};

typedef struct {
  int32_t fanEnabled;
  int32_t lastFanStatus;
} engineFan;

void engineFan_init(engineFan *self);
void engineFan_process(engineFan *self);
void engineFan_showDebug(const engineFan *self);
bool engineFan_isFanEnabled(const engineFan *self);
void engineFan_fan(engineFan *self, bool enable);

engineFan *getFanInstance(void);
void createFan(void);

#ifdef __cplusplus
}
#endif

#endif
