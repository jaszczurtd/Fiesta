/*
 * ECU test stubs — stub implementations for modules excluded from the test build.
 *
 * rpm.cpp is excluded because it uses the arduino-timer Timer class.
 * start.cpp is excluded because it uses multicoreWatchdog (arduino-timer dep).
 *
 * Some compiled ECU modules (can/obd-2/vp37) call watchdog_feed(), so we
 * provide a no-op stub for host-unit tests.
 */

#include "rpm.h"

// ── RPM class stub implementations ────────────────────────────────────────────

RPM::RPM()
    : rpmValue(0)
    , shortPulse(0)
    , lastPulse(0)
    , previousMillis(0)
    , rpmAliveTime(0)
    , RPMpulses(0)
    , snapshotPulses(0)
    , rpmReady(false)
    , currentRPMSolenoid(0)
    , rpmCycle(false)
#ifndef VP37
    , rpmPercentValue(0)
#endif
{}

void RPM::init()                         {}
void RPM::process()                      {}
void RPM::showDebug()                    {}
void RPM::setAccelMaxRPM()               {}
void RPM::resetRPMEngine()               {}
void RPM::interrupt()                    {}
void RPM::resetRPMCycle()                {}
void RPM::setAccelRPMPercentage(int)     {}
int  RPM::getCurrentRPMSolenoid()        { return 0; }
int  RPM::getCurrentRPM()               { return rpmValue; }
bool RPM::isEngineRunning()              { return rpmValue != 0; }
bool RPM::isEngineThrottlePressed()      { return false; }
#ifndef VP37
void RPM::stabilizeRPM()                 {}
#endif

void watchdog_feed(void) {}

static RPM s_engineRPM;
RPM  *getRPMInstance(void) { return &s_engineRPM; }
void  createRPM(void)      { s_engineRPM.init(); }
