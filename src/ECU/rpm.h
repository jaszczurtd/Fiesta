
#ifndef T_RPM
#define T_RPM

#include <tools_c.h>

#include "config.h"
#include "sensors.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

//is it really needed? To evaluate later
#define RPM_CORRECTION_VAL 50

//tweakable value:
//by how much percentage should the solenoid position change from RPM?
#define RPM_PERCENTAGE_CORRECTION_VAL 5

//since solenoid has some time lag we have to compensate it
//values defined as ms
#define RPM_TIME_TO_POSITIVE_CORRECTION_RPM_PERCENTAGE 500
#define RPM_TIME_TO_NEGATIVE_CORRECTION_RPM_PERCENTAGE 600

typedef struct {
  volatile int32_t rpmValue;
  volatile unsigned long shortPulse;
  volatile unsigned long lastPulse;
  volatile unsigned long previousMillis;
  volatile long rpmAliveTime;
  volatile int32_t RPMpulses;
  volatile int32_t snapshotPulses;
  volatile bool rpmReady;

  int32_t currentRPMSolenoid;
  bool rpmCycle;
#ifndef VP37
  int32_t rpmPercentValue;
  hal_soft_timer_t rpmCycleTimer;
#endif
} RPM;

void RPM_init(RPM *self);
void RPM_process(RPM *self);
void RPM_showDebug(RPM *self);
void RPM_setAccelMaxRPM(RPM *self);
void RPM_resetRPMEngine(RPM *self);
#ifndef VP37
void RPM_stabilizeRPM(RPM *self);
#endif
bool RPM_isEngineRunning(RPM *self);
void RPM_setAccelRPMPercentage(RPM *self, int32_t percentage);
int32_t RPM_getCurrentRPMSolenoid(RPM *self);
void RPM_interrupt(RPM *self);
void RPM_resetRPMCycle(RPM *self);
int32_t RPM_getCurrentRPM(RPM *self);

RPM *getRPMInstance(void);
void RPM_create(void);

#ifdef __cplusplus
}
#endif


#endif
