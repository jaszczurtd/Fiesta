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
#include "ecuContext.h"

// ── Central context stub (start.cpp is excluded in tests) ────────────────────

static ecu_context_t s_ctx;

ecu_context_t *getECUContext(void) {
    return &s_ctx;
}

// ── RPM stub implementations ─────────────────────────────────────────────────

void RPM_init(RPM *self) {
    self->rpmValue = 0;
    self->shortPulse = 0;
    self->lastPulse = 0;
    self->previousMillis = 0;
    self->rpmAliveTime = 0;
    self->RPMpulses = 0;
    self->snapshotPulses = 0;
    self->rpmReady = false;
    self->currentRPMSolenoid = 0;
    self->rpmCycle = false;
#ifndef VP37
    self->rpmPercentValue = 0;
#endif
}

void RPM_process(RPM *self) {
    (void)self;
}

void RPM_showDebug(RPM *self) {
    (void)self;
}

void RPM_setAccelMaxRPM(RPM *self) {
    (void)self;
}

void RPM_resetRPMEngine(RPM *self) {
    (void)self;
}

void RPM_interrupt(RPM *self) {
    (void)self;
}

void RPM_resetRPMCycle(RPM *self) {
    if (self) {
        self->rpmCycle = false;
    }
}

void RPM_setAccelRPMPercentage(RPM *self, int percentage) {
    (void)self;
    (void)percentage;
}

int RPM_getCurrentRPMSolenoid(RPM *self) {
    return self->currentRPMSolenoid;
}

int RPM_getCurrentRPM(RPM *self) {
    return self->rpmValue;
}

bool RPM_isEngineRunning(RPM *self) {
    return RPM_getCurrentRPM(self) != 0;
}

#ifndef VP37
void RPM_stabilizeRPM(RPM *self) {
    (void)self;
}
#endif

void watchdog_feed(void) {}

RPM *getRPMInstance(void) {
    return &getECUContext()->rpm;
}

void createRPM(void) {
    RPM_init(getRPMInstance());
}
