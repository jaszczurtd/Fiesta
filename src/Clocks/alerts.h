#ifndef C_ALERTS
#define C_ALERTS

#include "clocksState.h"

// Temperature alarms on the buzzer, from the ECU state of core 0 and the
// local measurements of core 1. Core 0 only.

/** @brief Forget every alarm and arm the start beep, as after power-up. */
void resetBuzzerAlerts(void);
/**
 * @brief Sound the start beep once the environment is up; call every pass.
 * @note Waits while the buzzer test owns the buzzer and sounds after it.
 */
void processStartBeep(bool environmentStarted);
/**
 * @brief Evaluate the alarms and sound the one due; call every 500 ms.
 * @note Skipped entirely while the buzzer test owns the buzzer, so an alarm
 * due in the meantime is neither consumed nor delayed by the test.
 */
void processTemperatureBuzzerAlerts(const clocks_local_snapshot_t *local);

#endif
