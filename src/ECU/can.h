#ifndef T_CAN
#define T_CAN

#include <tools_c.h>
#include "../common/canDefinitions/canDefinitions.h"

#include "sensors.h"
#include "config.h"
#include "rpm.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process pending CAN frames on the main CAN bus.
 * @return None.
 */
void canMainLoop(void);

/**
 * @brief Initialize the main CAN bus interface.
 * @param retries Number of initialization retries requested by the caller.
 * @return None.
 */
void canInit(int retries);

/**
 * @brief Send the full set of periodic CAN updates.
 * @return None.
 */
void CAN_sendAll(void);

/**
 * @brief Send a legacy throttle-position CAN update when needed.
 * @return None.
 * @note In the current diesel-oriented codebase this frame carries the G79/G185-like
 *       driver-demand signal, despite the historical throttle naming.
 */
void CAN_sendThrottleUpdate(void);

/**
 * @brief Send a turbo-pressure CAN update when needed.
 * @return None.
 */
void CAN_sendTurboUpdate(void);

/**
 * @brief Send the first group of ECU update frames.
 * @return None.
 */
void CAN_updaterecipients_01(void);

/**
 * @brief Send the second group of ECU update frames.
 * @return None.
 */
void CAN_updaterecipients_02(void);

/**
 * @brief Reserved legacy API for sending a single throttle frame.
 * @param value Driver-demand value to transmit using the historical throttle frame format.
 * @return None.
 * @note The payload is the legacy throttle-named signal, not a real throttle-plate angle.
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
 * @return None.
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
 * @return None.
 */
void canCheckConnection(void);

#ifdef UNIT_TEST
hal_can_t canTestGetCanHandle(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
