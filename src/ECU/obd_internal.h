#ifndef ECU_OBD_INTERNAL_H
#define ECU_OBD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OBD_MAX_RESPONSE_PAYLOAD 160u

/** Application payload produced by a diagnostic service. */
typedef struct {
  uint8_t data[OBD_MAX_RESPONSE_PAYLOAD];
  size_t length;
} obd_response_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validate and dispatch one received diagnostic CAN frame.
 * @param requestId CAN identifier on which the request was received.
 * @param dlc Number of valid bytes in @p data.
 * @param data Raw CAN frame data.
 */
void obdReqWithDlc(uint32_t requestId, uint8_t dlc, const uint8_t *data);

/**
 * @brief Build a response for a standard SAE J1979 service.
 * @param mode Requested OBD mode.
 * @param pid Requested PID when the mode uses one.
 * @param response Destination for the application payload.
 * @return True when the service belongs to J1979 handling.
 */
bool obdJ1979HandleService(uint8_t mode, uint8_t pid, obd_response_t *response);

/**
 * @brief Look up the diagnostic label for a J1979 PID.
 * @param pid PID number.
 * @return Static label, or "unknown" for an out-of-range PID.
 */
const char *obdJ1979GetPidName(uint8_t pid);

/**
 * @brief Encode the data bytes for one Mode 01 PID.
 * @param pid PID to encode.
 * @param out Destination for up to four encoded bytes.
 * @param outLen Destination for the encoded byte count.
 * @return True when the PID has an encoder.
 */
bool encodeMode01PidData(uint8_t pid, uint8_t *out, int *outLen);

/**
 * @brief Build a response for Ford UDS, KWP2000, or SCP services.
 * @param requestId CAN identifier on which the request was received.
 * @param mode Requested diagnostic service.
 * @param numBytes Application-byte count declared by the request.
 * @param data Raw CAN frame data.
 * @param response Destination for the application payload.
 * @return True when the service belongs to Ford diagnostic handling.
 */
bool obdFordDiagHandleService(uint32_t requestId, uint8_t mode,
                              uint8_t numBytes, const uint8_t *data,
                              obd_response_t *response);

/**
 * @brief Store the application bytes from a legacy single-frame buffer.
 * @param response Destination response.
 * @param frame Eight-byte buffer whose first byte is the payload length.
 */
void obdResponseSetFrame(obd_response_t *response, const uint8_t *frame);

/**
 * @brief Store an application payload for later ISO-TP transmission.
 * @param response Destination response.
 * @param length Number of bytes to copy.
 * @param data Payload bytes.
 */
void obdResponseSetPayload(obd_response_t *response, size_t length,
                           const uint8_t *data);

/**
 * @brief Build a three-byte UDS negative-response payload.
 * @param response Destination response.
 * @param mode Rejected service identifier.
 * @param reason UDS negative-response code.
 */
void obdResponseSetNegative(obd_response_t *response, uint8_t mode,
                            uint8_t reason);

/**
 * @brief Encode an OBD temperature byte with the standard 40-degree offset.
 * @param tempC Temperature in degrees Celsius.
 * @return Saturated wire value in the range 0..255.
 */
uint8_t obd_encodeTempByte(float tempC);

#ifdef __cplusplus
}
#endif

#endif
