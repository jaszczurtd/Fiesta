/* This code implements ISO 14229, ISO 14230, and ISO 15765 diagnostic
   communication standards. It enables communication via diagnostic interfaces
   that support or use the CAN bus.

   The implementation emulates the behavior of an original Ford EEC-V ECU used
   in the Ford Fiesta 1.8 DI, allowing diagnostic tools to recognize and
   communicate with it as if they were connected to the factory engine control
   unit.
*/

#include "obd-2.h"

#include <JaszczurHAL.h>
#include <hal/system/hal_system.h>

#include "config.h"
#include "dtcManager.h"
#include "ecu_unit_testing.h"
#include "hardwareConfig.h"
#include "obd_internal.h"
#include "obd_protocol.h"
#include <utils/multicoreWatchdog.h>

#ifdef UNIT_TEST
#include "tests/testable/obd2_testable.h"
#endif

#define ISO_TP_FC_TIMEOUT_MS 1000u
#define ISO_TP_TX_RETRY_TIMEOUT_MS 1000u

static void iso_tp_process(void);
static void unsupportedServicePrint(uint8_t mode);
static bool obdCanSendFrame(uint32_t responseId, const uint8_t *data);
static bool obdTransportSendPayload(uint32_t responseId, uint32_t requestId,
                                    const uint8_t *data, size_t length);

typedef enum {
  ISO_TP_IDLE = 0,
  ISO_TP_WAIT_FC,
  ISO_TP_SEND_CF,
} iso_tp_state_t;

typedef struct {
  uint8_t data[OBD_MAX_RESPONSE_PAYLOAD];
  size_t len;
  size_t offset;
  uint32_t responseId;
  uint32_t requestId;
  uint8_t index;
  uint8_t stMin;
  uint8_t blockSize;
  uint8_t blockSent;
  bool cfSent;
  uint32_t fcWaitStart;
  uint32_t lastCfTime;
  uint32_t txRetryStart;
  iso_tp_state_t state;
} iso_tp_ctx_t;

typedef struct {
  hal_can_t canHandle;
  uint32_t rxIdValue;
  uint8_t dlcValue;
  uint8_t rxBufValue[HAL_CAN_MAX_DATA_LEN];
  iso_tp_ctx_t isoTpState;
  bool initializedFlag;
#ifdef OBD_ENABLE_TOTDIST
  uint32_t totalDistanceKmValue;
#endif
} obd_state_t;

static obd_state_t s_obdState = {.canHandle = NULL,
                                 .rxIdValue = 0u,
                                 .dlcValue = 0u,
                                 .rxBufValue = {0},
                                 .isoTpState = {.data = {0},
                                                .len = 0u,
                                                .offset = 0u,
                                                .responseId = 0u,
                                                .requestId = 0u,
                                                .index = 0u,
                                                .stMin = 0u,
                                                .blockSize = 0u,
                                                .blockSent = 0u,
                                                .cfSent = false,
                                                .fcWaitStart = 0u,
                                                .lastCfTime = 0u,
                                                .txRetryStart = 0u,
                                                .state = ISO_TP_IDLE},
                                 .initializedFlag = false
#ifdef OBD_ENABLE_TOTDIST
                                 ,
                                 .totalDistanceKmValue =
                                     ecu_TotalDistanceKmDefault
#endif
};

#ifdef UNIT_TEST
TESTABLE_STATIC hal_can_t obdTestGetCanHandle(void) {
  return s_obdState.canHandle;
}

void obdTestResetTransport(void) {
  (void)memset(&s_obdState.isoTpState, 0, sizeof(s_obdState.isoTpState));
  s_obdState.isoTpState.state = ISO_TP_IDLE;
}
#endif

#ifdef OBD_ENABLE_TOTDIST

/**
 * @brief Return the emulated total-distance value for Ford-specific DIDs.
 * @return Odometer value in kilometers.
 */
uint32_t obdGetTotalDistanceKm(void) { return s_obdState.totalDistanceKmValue; }

/**
 * @brief Update the emulated total-distance value for Ford-specific DIDs.
 * @param km New odometer value in kilometers.
 * @return None.
 */
void obdSetTotalDistanceKm(uint32_t km) {
  s_obdState.totalDistanceKmValue = km;
}
#endif

/**
 * @brief Initialize the CAN-based OBD/UDS responder.
 * @param retries Number of CAN initialization retries to request.
 * @return None.
 */
void obdInit(int retries) {

  hal_can_config_t canCfg = hal_can_default_config();
  canCfg.mcp2515.cs_pin = CAN1_GPIO;

  s_obdState.canHandle = hal_can_create_with_retry(
      &canCfg, CAN1_INT, NULL, retries > 0 ? retries - 1 : 0, watchdog_feed);
  s_obdState.initializedFlag =
      (s_obdState.canHandle != NULL) &&
      hal_can_set_std_filters(s_obdState.canHandle, LISTEN_ID, FUNCTIONAL_ID);

  if (s_obdState.initializedFlag) {
    deb("OBD-2 CAN Shield init ok!");
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, false);
  } else {
    if (s_obdState.canHandle != NULL) {
      hal_can_destroy(s_obdState.canHandle);
      s_obdState.canHandle = NULL;
    }
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);
  }
}

/**
 * @brief Poll CAN for requests and advance any active ISO-TP transmission.
 * @return None.
 */
void obdLoop(void) {
  if (!s_obdState.initializedFlag) {
    return;
  }

  iso_tp_process();

  // Block new requests while a multi-frame transfer is in progress.
  if (s_obdState.isoTpState.state != ISO_TP_IDLE) {
    return;
  }

  if (!hal_gpio_read(CAN1_INT)) {
    if (hal_can_receive(s_obdState.canHandle, &s_obdState.rxIdValue,
                        &s_obdState.dlcValue, s_obdState.rxBufValue)) {
      if ((s_obdState.rxIdValue == (uint32_t)FUNCTIONAL_ID) ||
          (s_obdState.rxIdValue == (uint32_t)LISTEN_ID)) {
        obdReqWithDlc(s_obdState.rxIdValue, s_obdState.dlcValue,
                      s_obdState.rxBufValue);
      }
    }
  }
}

/**
 * @brief Convert ISO-TP STmin encoding into scheduler-friendly milliseconds.
 * @param stMin Raw ISO-TP STmin byte.
 * @return Millisecond delay used between consecutive frames.
 */
TESTABLE_STATIC uint8_t stMinToMs(uint8_t stMin) {
  uint8_t msDelay = 0u;
  if (stMin <= 0x7Fu) {
    msDelay = stMin;
  } else {
    // 0xF1..0xF9 are 100us..900us; clamp to 1ms granularity for current
    // scheduler.
    if ((stMin >= 0xF1u) && (stMin <= 0xF9u)) {
      msDelay = 1u;
    }
  }
  return msDelay;
}

void obdResponseSetPayload(obd_response_t *response, size_t length,
                           const uint8_t *data) {
  if ((response == NULL) || (data == NULL) || (length == 0u) ||
      (length > OBD_MAX_RESPONSE_PAYLOAD)) {
    if (response != NULL) {
      response->length = 0u;
    }
    return;
  }

  (void)memcpy(response->data, data, length);
  response->length = length;
}

void obdResponseSetFrame(obd_response_t *response, const uint8_t *frame) {
  if ((response == NULL) || (frame == NULL) ||
      (frame[0] > (uint8_t)(HAL_CAN_MAX_DATA_LEN - 1u))) {
    if (response != NULL) {
      response->length = 0u;
    }
    return;
  }

  obdResponseSetPayload(response, frame[0], &frame[1]);
}

void obdResponseSetNegative(obd_response_t *response, uint8_t mode,
                            uint8_t reason) {
  const uint8_t payload[] = {UDS_RSP_NEGATIVE, mode, reason};
  obdResponseSetPayload(response, sizeof(payload), payload);
}

/**
 * @brief Parse and answer one incoming OBD/UDS CAN request frame.
 * @param requestId CAN identifier of the incoming request.
 * @param dlc Number of valid bytes in @p data.
 * @param data Raw request buffer.
 * @return None.
 */
void obdReqWithDlc(uint32_t requestId, uint8_t dlc, const uint8_t *data) {
  if ((data == NULL) || (dlc == 0u) || (dlc > 8u)) {
    return;
  }

  uint8_t numofBytes = data[0];

#ifdef OBD_VERBOSE_RX_DEBUG
  hal_deb_hex("RX raw", data, (int)((dlc < 8u) ? dlc : 8u), 16);
#endif

  // Ignore ISO-TP control/segmentation frames arriving as normal OBD requests.
  if (numofBytes > 8u) {
    deb("RX dropped: ISO-TP multi-frame PCI=0x%02X (no MF rx support)",
        data[0]);
    return;
  }
  if (numofBytes < 1u) {
    return;
  }
  if ((uint8_t)(numofBytes + 1u) > dlc) {
    deb("RX dropped: payload length %u exceeds DLC %u", (unsigned)numofBytes,
        (unsigned)dlc);
    return;
  }

  const uint32_t responseId = REPLY_ID;
  uint8_t mode = data[1];
  uint8_t pid = ((numofBytes > 1u) && (dlc > 2u)) ? data[2] : 0u;
  bool handled = false;
  obd_response_t response;
  (void)memset(&response, 0, sizeof(response));

  if (mode == OBD_MODE_CURRENT_DATA && pid <= PID_LAST) {
    deb("OBD-2 pid:0x%02x (%s) length:0x%02x mode:0x%02x", pid,
        obdJ1979GetPidName(pid), numofBytes, mode);
  } else {
    deb("OBD/UDS service:0x%02x length:0x%02x reqId=0x%03lX", mode, numofBytes,
        (unsigned long)requestId);
  }

  handled = obdJ1979HandleService(mode, pid, &response);
  if (!handled) {
    handled =
        obdFordDiagHandleService(requestId, mode, numofBytes, data, &response);
  }

  if (!handled) {
    obdResponseSetNegative(&response, mode, NRC_SERVICE_NOT_SUPPORTED);
    unsupportedServicePrint(mode);
  }

  if ((response.length > 0u) &&
      !obdTransportSendPayload(responseId, requestId, response.data,
                               response.length)) {
    derr("OBD response TX start failed id=0x%03lX", (unsigned long)responseId);
  }
}

/**
 * @brief Log an unsupported service request.
 * @param mode Unsupported service identifier.
 * @return None.
 */
static void unsupportedServicePrint(uint8_t mode) {
  deb("Unsupported service $%02X requested!", mode);
}

static bool obdCanSendFrame(uint32_t responseId, const uint8_t *data) {
  const bool sent = hal_can_send(s_obdState.canHandle, responseId,
                                 HAL_CAN_MAX_DATA_LEN, data);
  if (!sent) {
    derr("OBD CAN TX failed id=0x%03lX", (unsigned long)responseId);
  }
  return sent;
}

/**
 * @brief Start a non-blocking ISO-TP response transmission.
 * @param responseId CAN response identifier.
 * @param requestId CAN request identifier expected on flow-control frames.
 * @param data Payload bytes to transmit.
 * @param length Number of payload bytes to send.
 * @return True when the single frame or first frame was queued.
 */
static bool obdTransportSendPayload(uint32_t responseId, uint32_t requestId,
                                    const uint8_t *data, size_t length) {
  bool started = false;

  if ((data == NULL) || (length == 0u) || (length > OBD_MAX_RESPONSE_PAYLOAD) ||
      (s_obdState.isoTpState.state != ISO_TP_IDLE)) {
    return false;
  }

  if (length <= (size_t)(HAL_CAN_MAX_DATA_LEN - 1u)) {
    uint8_t singleFrame[HAL_CAN_MAX_DATA_LEN];
    (void)memset(singleFrame, 0, sizeof(singleFrame));
    singleFrame[0] = (uint8_t)length;
    (void)memcpy(&singleFrame[1], data, length);
    started = obdCanSendFrame(responseId, singleFrame);
  } else {
    uint8_t firstFrame[HAL_CAN_MAX_DATA_LEN];
    (void)memset(firstFrame, 0, sizeof(firstFrame));
    const size_t firstPayloadLength = (size_t)(HAL_CAN_MAX_DATA_LEN - 2u);

    firstFrame[0] = (uint8_t)(0x10u | (uint8_t)((length >> 8u) & 0x0Fu));
    firstFrame[1] = (uint8_t)(length & 0xFFu);
    (void)memcpy(&firstFrame[2], data, firstPayloadLength);

    if (obdCanSendFrame(responseId, firstFrame)) {
      (void)memcpy(s_obdState.isoTpState.data, data, length);
      s_obdState.isoTpState.len = length;
      s_obdState.isoTpState.offset = firstPayloadLength;
      s_obdState.isoTpState.responseId = responseId;
      s_obdState.isoTpState.requestId = requestId;
      s_obdState.isoTpState.index = 1u;
      s_obdState.isoTpState.stMin = 0u;
      s_obdState.isoTpState.blockSize = 0u;
      s_obdState.isoTpState.blockSent = 0u;
      s_obdState.isoTpState.cfSent = false;
      s_obdState.isoTpState.lastCfTime = 0u;
      s_obdState.isoTpState.txRetryStart = hal_millis();
      s_obdState.isoTpState.fcWaitStart = hal_millis();
      s_obdState.isoTpState.state = ISO_TP_WAIT_FC;
      started = true;
    }
  }

  return started;
}

/**
 * @brief Advance the ISO-TP transmit state machine by one scheduler step.
 * @return None.
 */
static void iso_tp_process(void) {
  if (s_obdState.isoTpState.state == ISO_TP_IDLE) {
    return;
  }

  if (s_obdState.isoTpState.state == ISO_TP_WAIT_FC) {
    if (hal_millis_deadline_expired(s_obdState.isoTpState.fcWaitStart,
                                    ISO_TP_FC_TIMEOUT_MS)) {
      derr("ISO-TP timeout waiting for FC (len=%u)",
           (unsigned)s_obdState.isoTpState.len);
      s_obdState.isoTpState.state = ISO_TP_IDLE;
      return;
    }
    // Drain up to several frames per call so that stale/non-OBD traffic
    // cannot keep the MCP2515 RX buffers occupied while we wait for FC.
    for (size_t drain = 0u; drain < (size_t)HAL_CAN_MAX_DATA_LEN; drain++) {
      if (hal_gpio_read(CAN1_INT)) {
        break; // no more frames
      }
      bool gotFrame =
          hal_can_receive(s_obdState.canHandle, &s_obdState.rxIdValue,
                          &s_obdState.dlcValue, s_obdState.rxBufValue);
      if (!gotFrame) {
        break;
      }
      bool idMatches =
          (s_obdState.rxIdValue == s_obdState.isoTpState.requestId) ||
          (s_obdState.rxIdValue == (uint32_t)LISTEN_ID) ||
          (s_obdState.rxIdValue == (uint32_t)FUNCTIONAL_ID);
      if (gotFrame && idMatches) {
        if ((s_obdState.dlcValue >= 3u) &&
            ((s_obdState.rxBufValue[0] & 0xF0u) == 0x30u)) {
          const uint8_t fcType = s_obdState.rxBufValue[0] & 0x0Fu;
          if (fcType == 0x00u) {
            s_obdState.isoTpState.blockSize = s_obdState.rxBufValue[1];
            s_obdState.isoTpState.stMin = stMinToMs(s_obdState.rxBufValue[2]);
            s_obdState.isoTpState.blockSent = 0u;
            s_obdState.isoTpState.lastCfTime = 0u;
            s_obdState.isoTpState.cfSent = false;
            s_obdState.isoTpState.txRetryStart = hal_millis();
            s_obdState.isoTpState.state = ISO_TP_SEND_CF;
            return;
          } else if (fcType == 0x01u) {
            s_obdState.isoTpState.fcWaitStart =
                hal_millis(); // extend wait window
            return;
          } else if (fcType == 0x02u) {
            derr("ISO-TP FC abort from tester");
            s_obdState.isoTpState.state = ISO_TP_IDLE;
            return;
          }
        } else {
          // Non-FC frame arrived while waiting for FC - tester sent a new
          // request.
          derr("ISO-TP WAIT_FC: non-FC frame LOST s_obdState.rxIdValue=0x%03lX "
               "PCI=0x%02X",
               (unsigned long)s_obdState.rxIdValue, s_obdState.rxBufValue[0]);
          hal_deb_hex("ISO-TP lost frame", s_obdState.rxBufValue,
                      (s_obdState.dlcValue < HAL_CAN_MAX_DATA_LEN)
                          ? s_obdState.dlcValue
                          : HAL_CAN_MAX_DATA_LEN,
                      8);
        }
      }
      // Non-matching ID or non-FC: discard and try next frame.
    }
    return;
  }

  // ISO_TP_SEND_CF: send at most one CF per call, respecting stMin.
  const uint32_t now = hal_millis();
  if (s_obdState.isoTpState.cfSent &&
      !hal_elapsed_u32(now, s_obdState.isoTpState.lastCfTime,
                       s_obdState.isoTpState.stMin)) {
    return;
  }

  uint8_t tpData[HAL_CAN_MAX_DATA_LEN];
  (void)memset(tpData, 0, sizeof(tpData));
  size_t nextOffset = s_obdState.isoTpState.offset;
  tpData[0] = (uint8_t)(0x20u | (s_obdState.isoTpState.index & 0x0Fu));

  for (size_t i = 1u; i < (size_t)HAL_CAN_MAX_DATA_LEN; i++) {
    if (nextOffset < s_obdState.isoTpState.len) {
      tpData[i] = s_obdState.isoTpState.data[nextOffset];
      nextOffset++;
    }
  }

  if (!obdCanSendFrame(s_obdState.isoTpState.responseId, tpData)) {
    if (hal_millis_deadline_expired(s_obdState.isoTpState.txRetryStart,
                                    ISO_TP_TX_RETRY_TIMEOUT_MS)) {
      derr("ISO-TP timeout retrying consecutive frame");
      s_obdState.isoTpState.state = ISO_TP_IDLE;
    }
    return;
  }

  s_obdState.isoTpState.offset = nextOffset;
  s_obdState.isoTpState.index =
      (uint8_t)((s_obdState.isoTpState.index + 1u) & 0x0Fu);
  s_obdState.isoTpState.lastCfTime = now;
  s_obdState.isoTpState.cfSent = true;
  s_obdState.isoTpState.txRetryStart = now;

  if (s_obdState.isoTpState.offset >= s_obdState.isoTpState.len) {
    deb("ISO-TP TX done %u bytes", (unsigned)s_obdState.isoTpState.len);
    s_obdState.isoTpState.state = ISO_TP_IDLE;
    return;
  }

  if (s_obdState.isoTpState.blockSize != 0u) {
    s_obdState.isoTpState.blockSent++;
    if (s_obdState.isoTpState.blockSent >= s_obdState.isoTpState.blockSize) {
      s_obdState.isoTpState.blockSent = 0u;
      s_obdState.isoTpState.fcWaitStart = hal_millis();
      s_obdState.isoTpState.state = ISO_TP_WAIT_FC;
    }
  }
}
