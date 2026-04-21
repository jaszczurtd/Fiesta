
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

/**
 * @brief Initialize RPM measurement state and related hardware resources.
 * @param self RPM controller instance to initialize.
 * @return None.
 * @note The Hall input used here is the project's G28-like engine-speed reference.
 */
void RPM_init(RPM *self);

/**
 * @brief Update RPM value and optional non-VP37 idle-control logic.
 * @param self RPM controller instance to process.
 * @return None.
 * @note The underlying speed signal is derived from the project's G28-like crank path.
 */
void RPM_process(RPM *self);

/**
 * @brief Print the current RPM and actuator state for diagnostics.
 * @param self RPM controller instance to report.
 * @return None.
 */
void RPM_showDebug(RPM *self);

/**
 * @brief Set the acceleration solenoid command to its maximum configured value.
 * @param self RPM controller instance to update.
 * @return None.
 */
void RPM_setAccelMaxRPM(RPM *self);

/**
 * @brief Reserved legacy API for resetting RPM engine state.
 * @param self RPM controller instance to reset.
 * @return None.
 */
void RPM_resetRPMEngine(RPM *self);
#ifndef VP37
/**
 * @brief Reserved legacy API for historical non-VP37 idle stabilization.
 * @param self RPM controller instance to update.
 * @return None.
 */
void RPM_stabilizeRPM(RPM *self);
#endif

/**
 * @brief Check whether the engine is currently considered running.
 * @param self RPM controller instance to inspect.
 * @return True when RPM is non-zero, otherwise false.
 */
bool RPM_isEngineRunning(const RPM *self);

/**
 * @brief Set the acceleration solenoid command as a PWM percentage.
 * @param self RPM controller instance to update.
 * @param percentage Requested percentage of `PWM_RESOLUTION`.
 * @return None.
 */
void RPM_setAccelRPMPercentage(RPM *self, int32_t percentage);

/**
 * @brief Get the current solenoid command used by the RPM controller.
 * @param self RPM controller instance to inspect.
 * @return Current RPM solenoid command value.
 */
int32_t RPM_getCurrentRPMSolenoid(const RPM *self);

/**
 * @brief Handle one Hall-sensor edge for G28-like RPM measurement.
 * @param self RPM controller instance to update from the interrupt.
 * @return None.
 */
void RPM_interrupt(RPM *self);

/**
 * @brief Clear the temporary RPM correction-cycle state.
 * @param self RPM controller instance to update.
 * @return None.
 */
void RPM_resetRPMCycle(RPM *self);

/**
 * @brief Get the most recently calculated engine RPM.
 * @param self RPM controller instance to inspect.
 * @return Current RPM value.
 * @note The returned speed comes from the project's G28-like crank-signal path.
 */
int32_t RPM_getCurrentRPM(const RPM *self);

/**
 * @brief Get the shared RPM controller instance from ECU context.
 * @return Pointer to the global RPM controller instance.
 */
RPM *getRPMInstance(void);

/**
 * @brief Create and initialize the shared RPM controller instance.
 * @return None.
 */
void RPM_create(void);

#ifdef __cplusplus
}
#endif


#endif
