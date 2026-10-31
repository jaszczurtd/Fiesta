#ifndef T_CAN
#define T_CAN

#include "../common/canDefinitions/canDefinitions.h"
#include <JaszczurHAL.h>

#include "config.h"
#include "rpm.h"
#include "sensors.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process received main-bus frames and send a due deferred update.
 */
void canMainLoop(void);

/**
 * @brief Send one due periodic snapshot without polling RX.
 * @note Call only from core 0, between I/O or console work. Keeps the same
 *       2 ms spacing and round-robin order as canMainLoop().
 */
void canServicePeriodic(void);

/**
 * @brief Initialize the main CAN bus interface.
 * @param retries Number of initialization retries requested by the caller.
 * @return HAL_OK, or the error of the last attempt (logged; CAN0 stays off
 *         and the CAN functions do nothing).
 */
hal_status_t canInit(int retries);

/**
 * @brief Send the full set of periodic CAN updates.
 */
void CAN_sendAll(void);

/**
 * @brief Queue a legacy throttle-position CAN update when needed.
 * @note In the current diesel-oriented codebase this frame carries the
 * G79/G185-like driver-demand signal, despite the historical throttle naming.
 */
void CAN_sendThrottleUpdate(void);

/**
 * @brief Queue a turbo-pressure CAN update when needed.
 */
void CAN_sendTurboUpdate(void);

/**
 * @brief Queue snapshots of the three periodic ECU status frames.
 * @note The core-0 loop sends one pending periodic frame at least 2 ms after
 *       its preceding transmission attempt. A newer snapshot replaces an
 *       unsent one; sequence numbers are assigned only at transmission.
 */
void CAN_updaterecipients_01(void);

/**
 * @brief Publish engine RPM and service a due periodic update.
 *
 * A changed value goes out at once and an unchanged one every
 * CAN_RPM_HEARTBEAT_INTERVAL_MS. A failed send is retried after
 * CAN_RPM_RETRY_INTERVAL_MS. The MCP2515 retries arbitration in hardware; the
 * HAL aborts a transmission that reaches its timeout. All of these are due
 * times checked once per core-0 loop, not latency guarantees.
 */
void CAN_updaterecipients_02(void);

/**
 * @brief Reserved legacy API for sending a single throttle frame.
 * @param value Driver-demand value to transmit using the historical throttle
 * frame format.
 * @note The payload is the legacy throttle-named signal, not a real
 * throttle-plate angle.
 */
void sendThrottleValueCAN(int value);

/**
 * @brief Check whether the DPF module is currently considered connected.
 * @return True when the DPF module is connected, otherwise false.
 */
bool isDPFConnected(void);

/**
 * @brief Check whether the EGT module is currently considered connected.
 * @return True when the EGT module is connected, otherwise false.
 */
bool isEGTConnected(void);

/**
 * @brief Whether F_EGT holds a measurement.
 * @return True when the last EGT frame marked the pre-DPF temperature valid
 *         and EGT frames still arrive (canCheckConnection()).
 */
bool isEGTValid(void);

/**
 * @brief Whether F_DPF_TEMP holds a measurement.
 * @return True when the last EGT frame marked the in-DPF temperature valid
 *         and EGT frames still arrive.
 */
bool isDPFTempValid(void);

/**
 * @brief Whether F_OIL_PRESSURE holds a measurement.
 * @return True when the last oil and speed frame marked the pressure valid
 *         and those frames still arrive (canCheckConnection()).
 */
bool isOilPressureValid(void);

/**
 * @brief Refresh connection state and DTCs for external CAN modules.
 */
void canCheckConnection(void);

#ifdef UNIT_TEST
hal_can_t canTestGetCanHandle(void);
void canTestResetRpmPublisher(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
