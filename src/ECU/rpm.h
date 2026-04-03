
#ifndef T_RPM
#define T_RPM

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

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
  volatile int rpmValue;
  volatile unsigned long shortPulse;
  volatile unsigned long lastPulse;
  volatile unsigned long previousMillis;
  volatile long rpmAliveTime;
  volatile int RPMpulses;
  volatile int snapshotPulses;
  volatile bool rpmReady;

  int currentRPMSolenoid;
  bool rpmCycle;
#ifndef VP37
  int rpmPercentValue;
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
void RPM_setAccelRPMPercentage(RPM *self, int percentage);
int RPM_getCurrentRPMSolenoid(RPM *self);
void RPM_interrupt(RPM *self);
void RPM_resetRPMCycle(RPM *self);
int RPM_getCurrentRPM(RPM *self);

RPM *getRPMInstance(void);
void createRPM(void);


#endif
