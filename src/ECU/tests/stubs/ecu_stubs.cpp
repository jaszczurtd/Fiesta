/*
 * ECU test stubs - stub implementations for modules excluded from the test build.
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

void RPM_setAccelRPMPercentage(RPM *self, int32_t percentage) {
    (void)self;
    (void)percentage;
}

int32_t RPM_getCurrentRPMSolenoid(const RPM *self) {
    return self->currentRPMSolenoid;
}

int32_t RPM_getCurrentRPM(const RPM *self) {
    return self->rpmValue;
}

bool RPM_isEngineRunning(const RPM *self) {
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

void RPM_create(void) {
    RPM_init(getRPMInstance());
}

// ── tests.c stub: tests.c is excluded from the host build, but config.c
//    references tickTestsHandleSerialLine() via the HAL session unknown-line
//    callback. Provide an observable no-op implementation that records the
//    last forwarded line so unit tests can assert routing behaviour.
static char s_lastForwardedLine[128] = {0};
static unsigned s_forwardedCount = 0;

extern "C" void tickTestsHandleSerialLine(const char *line) {
    s_forwardedCount++;
    if (line == nullptr) {
        s_lastForwardedLine[0] = '\0';
        return;
    }
    size_t i = 0;
    while (line[i] != '\0' && i + 1u < sizeof(s_lastForwardedLine)) {
        s_lastForwardedLine[i] = line[i];
        i++;
    }
    s_lastForwardedLine[i] = '\0';
}

extern "C" const char *test_stubs_last_forwarded_serial_line(void) {
    return s_lastForwardedLine;
}

extern "C" unsigned test_stubs_forwarded_serial_count(void) {
    return s_forwardedCount;
}

extern "C" void test_stubs_reset_forwarded_serial(void) {
    s_lastForwardedLine[0] = '\0';
    s_forwardedCount = 0;
}
