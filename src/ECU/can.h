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
 * @brief Process pending CAN frames on the main CAN bus.
 */
void canMainLoop(void);

/**
 * @brief Initialize the main CAN bus interface.
 * @param retries Number of initialization retries requested by the caller.
 */
void canInit(int retries);

/**
 * @brief Send the full set of periodic CAN updates.
 */
void CAN_sendAll(void);

/**
 * @brief Send a legacy throttle-position CAN update when needed.
 * @note In the current diesel-oriented codebase this frame carries the
 * G79/G185-like driver-demand signal, despite the historical throttle naming.
 */
void CAN_sendThrottleUpdate(void);

/**
 * @brief Send a turbo-pressure CAN update when needed.
 */
void CAN_sendTurboUpdate(void);

/**
 * @brief Send the first group of ECU update frames.
 */
void CAN_updaterecipients_01(void);

/**
 * @brief Publish engine RPM on the main CAN bus.
 *
 * A changed value goes out at once and an unchanged one every
 * CAN_RPM_HEARTBEAT_INTERVAL_MS. A failed send is retried after
 * CAN_RPM_RETRY_INTERVAL_MS. The MCP2515 runs in one-shot mode, so an
 * unacknowledged frame never holds one of its three TX buffers. All of these
 * are due times checked once per core-0 loop, not latency guarantees.
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
 * @brief Pack GPS date and time into the project CAN payload format.
 * @param dateYYMMDD Date encoded as YYMMDD.
 * @param timeHHMM Time encoded as HHMM.
 * @return Packed 24-bit date/time value, or 0 on invalid input.
 */
uint32_t CAN_packGpsDateTime(uint32_t dateYYMMDD, uint32_t timeHHMM);

/**
 * @brief Build the extended GPS latitude CAN frame payload.
 * @param frameNo Frame sequence number to insert.
 * @param outBuf Output buffer receiving the frame payload.
 * @param outLen Size of the output buffer in bytes.
 * @return True on success, otherwise false.
 */
bool CAN_buildGpsLatFrame(uint8_t frameNo, uint8_t *outBuf, int outLen);

/**
 * @brief Build the extended GPS longitude/time CAN frame payload.
 * @param frameNo Frame sequence number to insert.
 * @param outBuf Output buffer receiving the frame payload.
 * @param outLen Size of the output buffer in bytes.
 * @return True on success, otherwise false.
 */
bool CAN_buildGpsLonTimeFrame(uint8_t frameNo, uint8_t *outBuf, int outLen);

/**
 * @brief Send the pair of extended GPS CAN frames.
 */
void CAN_sendGpsExtended(void);

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
