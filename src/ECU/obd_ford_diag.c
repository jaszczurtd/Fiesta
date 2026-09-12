/* UDS, KWP2000, and Ford EEC-V diagnostic services. */

#include "obd-2.h"
#include "obd_internal.h"
#include "obd_protocol.h"

#include <JaszczurHAL.h>
#include <hal/core/jh_endian.h>

#include "config.h"
#include "dtcManager.h"
#include "ecu_unit_testing.h"
#include "hardwareConfig.h"
#include "rpm.h"
#include "sensors.h"
#include "tests.h"
#include "vp37.h"

#ifdef UNIT_TEST
#include "tests/testable/obd2_testable.h"
#endif

static uint8_t s_udsSessionValue = UDS_SESSION_DEFAULT;

/**
 * @brief Validate minimum request length and send NRC 0x13 on failure.
 * @param response CAN identifier used for the negative response.
 * @param serviceId Service currently being processed.
 * @param numofBytes Request length encoded in the incoming frame.
 * @param minLen Minimum accepted payload length.
 * @return True when the request is long enough, otherwise false.
 */
static bool requireMinLength(obd_response_t *response, uint8_t serviceId,
                             uint8_t numofBytes, uint8_t minLen) {
  bool validLength = true;
  if (numofBytes < minLen) {
    obdResponseSetNegative(response, serviceId, NRC_INCORRECT_LENGTH);
    validLength = false;
  }
  return validLength;
}

#ifdef OBD_VERBOSE_IDENT_DEBUG
/**
 * @brief Check whether a DID belongs to the Ford identification subset.
 * @param did DID to classify.
 * @return True when the DID is part of Ford identification handling.
 */
static bool isFordDiagIdentificationDid(uint16_t did) {
  if (did == (uint16_t)DID_FORD_MODEL || did == (uint16_t)DID_PART_NUMBER ||
      did == (uint16_t)DID_SW_VERSION || did == (uint16_t)DID_VIN ||
      did == (uint16_t)DID_FORD_CATCH_CODE) {
    return true;
  }
  if (did == (uint16_t)DID_F4_MODEL_16 || did == (uint16_t)DID_F4_TYPE_ALT ||
      did == (uint16_t)DID_F4_SUBTYPE_ALT ||
      did == (uint16_t)DID_F4_CATCH_CODE_ALT ||
      did == (uint16_t)DID_F4_SW_DATE_ALT ||
      did == (uint16_t)DID_F4_CALIBRATION_ID_ALT ||
      did == (uint16_t)DID_F4_HARDWARE_ID_ALT ||
      did == (uint16_t)DID_F4_ROM_SIZE_ALT ||
      did == (uint16_t)DID_F4_PART_NUMBER_ALT ||
      did == (uint16_t)DID_F4_SW_VERSION ||
      did == (uint16_t)DID_F4_COPYRIGHT_ALT) {
    return true;
  }
  if (did >= (uint16_t)DID_F4_MODEL && did <= (uint16_t)DID_F4_COPYRIGHT) {
    return true;
  }
  if (did >= (uint16_t)DID_FORD_TYPE &&
      did <= (uint16_t)DID_FORD_VIN_CHUNK_LAST) {
    return true;
  }
  if (did == (uint16_t)DID_FORD_SW_DATE ||
      did == (uint16_t)DID_FORD_PARTNUM_MIDDLE ||
      did == (uint16_t)DID_FORD_PARTNUM_SUFFIX ||
      did == (uint16_t)DID_FORD_PARTNUM_PREFIX) {
    return true;
  }
  return false;
}

/**
 * @brief Check whether a KWP local ID is used by Ford identification flows.
 * @param localId Local identifier to classify.
 * @return True when the local ID is part of Ford identification handling.
 */
static bool isFordDiagIdentificationLocalId(uint8_t localId) {
  return (localId == KWP_LID_CALIB_BLOCK || localId == KWP_LID_COMPACT_IDENT ||
          localId == KWP_LID_SUPPORTED_LIST ||
          (localId >= KWP_LID_CALIBRATION_ID && localId <= KWP_LID_COPYRIGHT));
}
#endif

/**
 * @brief Initialize the service and DID bytes of a ReadDataByIdentifier
 * payload.
 * @param payload Writable buffer with room for at least three bytes.
 * @param did Data identifier to encode in big-endian order.
 */
static void initReadDidPayload(uint8_t *payload, uint16_t did) {
  payload[0] = UDS_RSP_READ_DATA_BY_ID;
  jh_store_be16(&payload[1], did);
}

/**
 * @brief Send a UDS 0x22 response containing a fixed-width ASCII field.
 * @param response CAN response identifier.
 * @param did DID being answered.
 * @param str Source string.
 * @param width Fixed payload width.
 * @return None.
 */
static void send22Field(obd_response_t *response, uint16_t did, const char *str,
                        int width) {
  if (width < 0) {
    width = 0;
  }
  if (width > 40) {
    width = 40;
  }

  uint8_t payload[3 + 40];
  (void)memset(payload, 0, sizeof(payload));
  initReadDidPayload(payload, did);
  hal_text_pack_field(&payload[3], str, width);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  if (isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident response DID=0x%04X width=%d", did, width);
    hal_deb_hex("UDS 0x22 ident resp payload", payload, 3 + width, 36);
  }
#endif
  obdResponseSetPayload(response, 3 + width, payload);
}

/**
 * @brief Send a Ford-style UDS 0x22 identification field using space padding.
 * @param response CAN response identifier.
 * @param did DID being answered.
 * @param str Source string.
 * @param width Fixed payload width.
 * @return None.
 */
static void send22IdentField(obd_response_t *response, uint16_t did,
                             const char *str, int width) {
  if (width < 0) {
    width = 0;
  }
  if (width > 40) {
    width = 40;
  }

  uint8_t payload[3 + 40];
  (void)memset(payload, 0, sizeof(payload));
  initReadDidPayload(payload, did);
  hal_text_pack_field_pad(&payload[3], str, width, FORD_IDENT_PAD);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  if (isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident response DID=0x%04X width=%d", did, width);
    hal_deb_hex("UDS 0x22 ident resp payload", payload, 3 + width, 36);
  }
#endif
  obdResponseSetPayload(response, 3 + width, payload);
}

/**
 * @brief Send a UDS 0x22 response containing one 32-bit big-endian value.
 * @param response CAN response identifier.
 * @param did DID being answered.
 * @param value Value to encode.
 * @return None.
 */
static void send22U32(obd_response_t *response, uint16_t did, uint32_t value) {
  uint8_t payload[7];
  (void)memset(payload, 0, sizeof(payload));
  initReadDidPayload(payload, did);
  (void)hal_u32_to_bytes_be(value, &payload[3]);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  if (isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident response DID=0x%04X U32=0x%08lX", did,
        (unsigned long)value);
    hal_deb_hex("UDS 0x22 ident resp payload", payload, (int)sizeof(payload),
                16);
  }
#endif
  obdResponseSetPayload(response, sizeof(payload), payload);
}

// ── Ford Part Number encoding for E217/E21A/E219 identification ──────
// Fordiag reads the Ford part number in three pieces:
//   E21A -> ASCII prefix      (e.g. "XS4A")
//   E217 -> binary middle     (e.g. "12A650" -> {0x12,0x0A,0x06,0x50})
//   E219 -> encoded suffix    (e.g. "AXB" -> 2-byte Ford encoding)
// It reconstructs PREFIX-MIDDLE-SUFFIX, looks it up in an internal
// database, and fills model/type/subtype/etc. fields from the match.

#define FORD_PARTNUM_CHARSET_LEN 22

/**
 * @brief Look up one Ford suffix character in the Fordiag charset.
 * @param c Character to encode.
 * @return Character index, or -1 when unsupported.
 */
static int fordPartCharIndex(char c) {
  // Ford part-number suffix character set (22 chars, no I/O/Q/W).
  static const char chars[] = "ABCDEFGHJKLMNPRSTUVXYZ";
  for (int i = 0; i < FORD_PARTNUM_CHARSET_LEN; i++) {
    if (chars[i] == c) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Encode one Ford part-number suffix fragment into a single byte.
 * @param s Pointer to the suffix fragment to encode.
 * @param len Number of characters to encode from @p s.
 * @return Encoded suffix byte.
 */
TESTABLE_STATIC uint8_t fordPartSuffixCharsToByte(const char *s, int len) {
  if (len == 1) {
    int idx = fordPartCharIndex(s[0]);
    return (uint8_t)(idx >= 0 ? idx : 0);
  }
  if (len >= 2) {
    int hi = fordPartCharIndex(s[0]);
    int lo = fordPartCharIndex(s[1]);
    if (hi < 0) {
      hi = 0;
    }
    if (lo < 0) {
      lo = 0;
    }
    return (uint8_t)((hi + 1) * FORD_PARTNUM_CHARSET_LEN + lo);
  }
  return 0;
}

/**
 * @brief Split a Ford part number into prefix, middle and suffix spans.
 * @param pn Ford part-number string.
 * @param prefixOut Output pointer receiving the prefix start.
 * @param prefixLen Output pointer receiving prefix length.
 * @param middleOut Output pointer receiving middle-section start.
 * @param middleLen Output pointer receiving middle-section length.
 * @param suffixOut Output pointer receiving suffix start.
 * @param suffixLen Output pointer receiving suffix length.
 * @return True when the string matches PREFIX-MIDDLE-SUFFIX format.
 */
TESTABLE_STATIC bool fordPartNumberSplit(const char *pn, const char **prefixOut,
                                         int *prefixLen, const char **middleOut,
                                         int *middleLen, const char **suffixOut,
                                         int *suffixLen) {
  if (pn == NULL) {
    return false;
  }
  const char *dash1 = NULL, *dash2 = NULL;
  for (const char *p = pn; *p != '\0'; p += 1) {
    if (*p == '-') {
      if (!dash1) {
        dash1 = p;
      } else if (!dash2) {
        dash2 = p;
      }
    }
  }
  if (!dash1 || !dash2) {
    return false;
  }
  *prefixOut = pn;
  *prefixLen = (int)(dash1 - pn);
  *middleOut = dash1 + 1;
  *middleLen = (int)(dash2 - dash1 - 1);
  *suffixOut = dash2 + 1;
  *suffixLen = (int)strlen(dash2 + 1);
  return true;
}

/**
 * @brief Send DID 0xE217 containing the binary middle section of the part
 * number.
 * @param response CAN response identifier.
 * @param did DID being answered.
 * @return None.
 */
static void sendE217PartNumMiddle(obd_response_t *response, uint16_t did) {
  static const uint8_t midBytes[] = {ecu_PartNumMiddleHex};
  int midLen = ecu_PartNumMiddleLen;
  if (midLen > 8) {
    midLen = 8;
  }
  uint8_t payload[3 + 8];
  (void)memset(payload, 0, sizeof(payload));
  initReadDidPayload(payload, did);
  (void)memcpy(&payload[3], midBytes, (size_t)midLen);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  deb("UDS 0x22 E217 partnum middle len=%d", midLen);
  hal_deb_hex("UDS 0x22 E217 response", payload, 3 + midLen, 16);
#endif
  obdResponseSetPayload(response, 3 + midLen, payload);
}

/**
 * @brief Send DID 0xE21A containing the ASCII part-number prefix.
 * @param response CAN response identifier.
 * @param did DID being answered.
 * @return None.
 */
static void sendE21APartNumPrefix(obd_response_t *response, uint16_t did) {
  const char *prefix, *middle, *suffix;
  int prefixLen, middleLen, suffixLen;
  if (!fordPartNumberSplit(ecu_PartNumber, &prefix, &prefixLen, &middle,
                           &middleLen, &suffix, &suffixLen)) {
    obdResponseSetNegative(response, UDS_SVC_READ_DATA_BY_ID,
                           NRC_CONDITIONS_NOT_CORRECT);
    return;
  }
  if (prefixLen > 16) {
    prefixLen = 16;
  }
  uint8_t payload[3 + 16];
  (void)memset(payload, 0, sizeof(payload));
  initReadDidPayload(payload, did);
  (void)memcpy(&payload[3], prefix, (size_t)prefixLen);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  deb("UDS 0x22 E21A partnum prefix len=%d", prefixLen);
  hal_deb_hex("UDS 0x22 E21A response", payload, 3 + prefixLen, 16);
#endif
  obdResponseSetPayload(response, 3 + prefixLen, payload);
}

/**
 * @brief Send DID 0xE219 containing the Ford-encoded part-number suffix.
 * @param response CAN response identifier.
 * @param did DID being answered.
 * @return None.
 */
static void sendE219PartNumSuffix(obd_response_t *response, uint16_t did) {
  const char *prefix, *middle, *suffix;
  int prefixLen, middleLen, suffixLen;
  if (!fordPartNumberSplit(ecu_PartNumber, &prefix, &prefixLen, &middle,
                           &middleLen, &suffix, &suffixLen)) {
    obdResponseSetNegative(response, UDS_SVC_READ_DATA_BY_ID,
                           NRC_CONDITIONS_NOT_CORRECT);
    return;
  }

  uint8_t leftByte = 0, rightByte = 0;
  if (suffixLen >= 3) {
    // 3-char suffix like "AXB": left="AX"(2 chars), right="B"(1 char)
    leftByte = fordPartSuffixCharsToByte(suffix, 2);
    rightByte = fordPartSuffixCharsToByte(&suffix[2], suffixLen - 2);
  } else if (suffixLen == 2) {
    // 2-char suffix like "FC": left="F"(1 char), right="C"(1 char)
    leftByte = fordPartSuffixCharsToByte(suffix, 1);
    rightByte = fordPartSuffixCharsToByte(&suffix[1], 1);
  } else if (suffixLen == 1) {
    rightByte = fordPartSuffixCharsToByte(suffix, 1);
  }
  // Left byte is always even (doubled) per Ford convention.
  leftByte = (uint8_t)(leftByte * 2);

  uint8_t payload[5];
  (void)memset(payload, 0, sizeof(payload));
  initReadDidPayload(payload, did);
  payload[3] = leftByte;
  payload[4] = rightByte;
#ifdef OBD_VERBOSE_IDENT_DEBUG
  deb("UDS 0x22 E219 partnum suffix left=0x%02X right=0x%02X", leftByte,
      rightByte);
  hal_deb_hex("UDS 0x22 E219 response", payload, (int)sizeof(payload), 8);
#endif
  obdResponseSetPayload(response, sizeof(payload), payload);
}

/**
 * @brief Send a KWP 0x12 response with a fixed-width space-padded field.
 * @param response CAN response identifier.
 * @param localId Local identifier being answered.
 * @param str Source string.
 * @param width Fixed payload width.
 * @return None.
 */
static void send12LocalField(obd_response_t *response, uint8_t localId,
                             const char *str, int width) {
  if (width < 0) {
    width = 0;
  }
  if (width > 60) {
    width = 60;
  }

  uint8_t payload[2 + 60];
  (void)memset(payload, 0, sizeof(payload));
  payload[0] = UDS_RSP_READ_DATA_BY_LOCAL_ID;
  payload[1] = localId;
  hal_text_pack_field_pad(&payload[2], str, width, FORD_IDENT_PAD);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  hal_deb_hex("KWP 0x12 local response", payload, 2 + width, 40);
#endif
  obdResponseSetPayload(response, 2 + width, payload);
}

/**
 * @brief Copy a string into a fixed window inside an ID block.
 * @param block Destination block buffer.
 * @param len Size of @p block.
 * @param offset Byte offset where the field starts.
 * @param width Fixed field width.
 * @param value String to copy.
 * @return None.
 */
static void writeAsciiField(uint8_t *block, int len, int offset, int width,
                            const char *value) {
  if (block == NULL || value == NULL || width <= 0 || offset < 0 ||
      (offset + width) > len) {
    return;
  }

  int n = (int)strlen(value);
  if (n > width) {
    n = width;
  }

  (void)memcpy(&block[offset], value, (size_t)n);
}

/**
 * @brief Build the synthetic Ford SCP identification block used by DMR reads.
 * @param block Output block buffer.
 * @param len Size of @p block in bytes.
 * @return None.
 */
static void buildScpIdBlock(uint8_t *block, int len) {
  if (block == NULL || len < SCP_IDBLOCK_SIZE) {
    return;
  }

  (void)memset(block, 0x00, (size_t)len);

  // Strategy-defined byte that indicates Flash ID block format (offset +0x13).
  block[0x13] = SCP_IDBLOCK_FMT_DEFAULT;

  // Store commonly requested identification values in a deterministic layout.
  // Bottom half: identification fields (not in checksummed range)
  writeAsciiField(block, len, 0x20, 16, ecu_Model);
  writeAsciiField(block, len, 0x30, 8, ecu_Type);
  writeAsciiField(block, len, 0x38, 8, ecu_SubType);
  writeAsciiField(block, len, 0x40, 8, ecu_CatchCode);
  writeAsciiField(block, len, 0x48, 8, ecu_SwDate);
  writeAsciiField(block, len, 0x50, 16, ecu_CalibrationId);
  writeAsciiField(block, len, 0x60, 16, ecu_PartNumber);
  writeAsciiField(block, len, 0x70, 8, ecu_HardwareId);
  block[0x78] = 0x00;
  block[0x79] = 0x08;
  block[0x7A] = 0x00;
  block[0x7B] = 0x00;

  // Upper half: checksummed - VIN at 0x85..0x95, Copyright at 0x97
  writeAsciiField(block, len, SCP_IDBLOCK_VIN_OFFSET, 17, vehicle_Vin);
  writeAsciiField(block, len, SCP_IDBLOCK_COPYRIGHT_OFS, 32, ecu_Copyright);

  // Vidblock checksum (CRAI8 §35.7.3.2): word-by-word sum of bytes
  // 128..255 must equal 0 (mod 65536). Compute correction word at 0xFE.
  uint32_t sum = 0;
  for (size_t i = 0x80; i < SCP_IDBLOCK_CHKSUM_OFS; i += 2) {
    sum += jh_load_be16(&block[i]);
    if (sum > 65535u) {
      sum -= 65536u;
    }
  }
  uint16_t correction = (uint16_t)((65536u - sum) & 0xFFFFu);
  jh_store_be16(&block[SCP_IDBLOCK_CHKSUM_OFS], correction);
}

/**
 * @brief Read one byte from the synthetic SCP DMR address space.
 * @param dmrType Ford DMR access type.
 * @param addr Requested address.
 * @param outValue Output pointer receiving the byte value.
 * @return True when the read was handled.
 */
static bool readScpDmrByte(uint8_t dmrType, uint16_t addr, uint8_t *outValue) {
  if (outValue == NULL) {
    return false;
  }

  uint8_t idBlock[SCP_IDBLOCK_SIZE];
  buildScpIdBlock(idBlock, (int)sizeof(idBlock));

  // Common Ford EEEC ID block base addresses (documented examples).
  const uint16_t idStarts[] = {SCP_IDBLOCK_ADDR, SCP_IDBLOCK_ADDR_ALT};
  for (size_t i = 0; i < COUNTOF(idStarts); i++) {
    uint16_t start = idStarts[i];
    if (addr >= start &&
        addr < (uint16_t)(start + (uint16_t)SCP_IDBLOCK_SIZE)) {
      *outValue = idBlock[(int)(addr - start)];
      return true;
    }
  }

  // Keep ECU responsive for valid DMR service values even when address is
  // outside mapped blocks.
  (void)dmrType;
  *outValue = 0x00;
  return true;
}

/**
 * @brief Send an SCP-style generic negative response frame.
 * @param response CAN response identifier.
 * @param requestMode Original service identifier.
 * @param arg1 First echoed argument byte.
 * @param arg2 Second echoed argument byte.
 * @param arg3 Third echoed argument byte.
 * @param responseCode Response or NRC code.
 * @return None.
 */
static void sendScpGeneralResponse(obd_response_t *response,
                                   uint8_t requestMode, uint8_t arg1,
                                   uint8_t arg2, uint8_t arg3,
                                   uint8_t responseCode) {
  const uint8_t rsp[8] = {0x06, UDS_RSP_NEGATIVE, requestMode, arg1, arg2,
                          arg3, responseCode,     PAD};
  obdResponseSetFrame(response, rsp);
}

/**
 * @brief Send one 4-byte SCP direct-memory response frame.
 * @param response CAN response identifier.
 * @param addr Requested base address.
 * @param dmrType Ford DMR access type.
 * @return None.
 */
static void sendScpDmrResponse(obd_response_t *response, uint16_t addr,
                               uint8_t dmrType) {
  uint8_t b0 = 0;
  uint8_t b1 = 0;
  uint8_t b2 = 0;
  uint8_t b3 = 0;
  (void)readScpDmrByte(dmrType, addr, &b0);
  (void)readScpDmrByte(dmrType, (uint16_t)(addr + 1u), &b1);
  (void)readScpDmrByte(dmrType, (uint16_t)(addr + 2u), &b2);
  (void)readScpDmrByte(dmrType, (uint16_t)(addr + 3u), &b3);

  uint8_t rsp[8];
  (void)memset(rsp, 0, sizeof(rsp));
  rsp[0] = 0x07u;
  rsp[1] = UDS_RSP_READ_MEMORY_BY_ADDR;
  jh_store_be16(&rsp[2], addr);
  rsp[4] = b0;
  rsp[5] = b1;
  rsp[6] = b2;
  rsp[7] = b3;
#ifdef OBD_VERBOSE_IDENT_DEBUG
  hal_deb_hex("SCP 0x23/0x63 response", rsp, (int)sizeof(rsp), 16);
#endif
  obdResponseSetFrame(response, rsp);
}

/**
 * @brief Encode one supported Ford SCP PID into raw response bytes.
 * @param pid Ford SCP PID to encode.
 * @param out Output buffer receiving up to 4 data bytes.
 * @return Number of data bytes written, or 0 when unsupported.
 */
static int encodeFordScpPid(uint16_t pid, uint8_t *out) {
  switch (pid) {
  case SCP_PID_RPM: { // N - Engine RPM, 0.25 rpm resolution, Word
    uint16_t raw = (uint16_t)(getGlobalValue(F_RPM) * 4.0f);
    jh_store_be16(out, raw);
    return 2;
  }
  case SCP_PID_VBAT: { // VBAT - Battery Voltage, 0.0625V resolution, Byte
    int32_t raw =
        hal_constrain((int32_t)(getGlobalValue(F_VOLTS) * 16.0f), 0, 255);
    out[0] = (uint8_t)raw;
    return 1;
  }
  case SCP_PID_TP_ENG: { // TP_ENG - Throttle Position A/D, 0.0156 count, Word
    float percent = hal_constrain((getGlobalValue(F_THROTTLE_POS) * 100.0f) /
                                      PWM_RESOLUTION,
                                  0.0f, 100.0f);
    // Ford 10-bit ADC (0-1023) scaled by Bin 6 (×64)
    uint16_t raw = (uint16_t)(percent * 1023.0f / 100.0f * 64.0f);
    jh_store_be16(out, raw);
    return 2;
  }
  case SCP_PID_ECT: { // ECT - Engine Coolant Temp, 2°F resolution, Byte Signed
    float tempF = getGlobalValue(F_COOLANT_TEMP) * 1.8f + 32.0f;
    int8_t raw = (int8_t)(tempF / 2.0f);
    out[0] = (uint8_t)raw;
    return 1;
  }
  case SCP_PID_ACT: { // ACT - Air Charge Temp, 2°F resolution, Byte Signed
    float tempF = getGlobalValue(F_INTAKE_TEMP) * 1.8f + 32.0f;
    int8_t raw = (int8_t)(tempF / 2.0f);
    out[0] = (uint8_t)raw;
    return 1;
  }
  case SCP_PID_LAMBSE1: // VP37: no lambda sensors -> NRC
  case SCP_PID_LAMBSE2:
  case SCP_PID_KAMRF1: // VP37: no adaptive fuel trim -> NRC
  case SCP_PID_KAMRF2:
    return 0;
  case SCP_PID_LOAD: { // LOAD - Engine Load, 1/32768 of std air charge, Word
    float loadPct =
        hal_constrain(getGlobalValue(F_CALCULATED_ENGINE_LOAD), 0.0f, 100.0f);
    uint16_t raw = (uint16_t)(loadPct / 100.0f * 32768.0f);
    jh_store_be16(out, raw);
    return 2;
  }
  case SCP_PID_VS: { // VS - Vehicle Speed, 0.001953 mph, Word
    float mph = hal_constrain(getGlobalValue(F_ABS_CAR_SPEED) * 0.621371f, 0.0f,
                              65535.0f / 512.0f);
    uint16_t raw = (uint16_t)(mph * 512.0f);
    jh_store_be16(out, raw);
    return 2;
  }
  case SCP_PID_BP: { // BP - Barometric Pressure, 0.125 inHg, Byte
    // Default sea level ≈ 29.92 inHg -> raw = 29.92/0.125 ≈ 239
    out[0] = 239;
    return 1;
  }
  // SCP_PID_IMAF removed - handled above with VMAF/MAF_RATE (return NRC)
  case SCP_PID_RATCH: { // RATCH - throttle ratchet count, 0.0156, Word
    out[0] = 0;
    out[1] = 0;
    return 2;
  }
  case SCP_PID_NORPM: { // NO - Neutral output RPM (same scale as N), Word
    uint16_t raw = (uint16_t)(getGlobalValue(F_RPM) * 4.0f);
    jh_store_be16(out, raw);
    return 2;
  }
  case SCP_PID_IDBLOCK_ADDR: { // idblock_adrs
    out[0] = SCP_IDBLOCK_BANK;
    jh_store_be16(&out[1], SCP_IDBLOCK_ADDR);
    out[3] = SCP_IDBLOCK_FMT_DEFAULT;
    return 4;
  }
  case SCP_PID_SECURITY_STATUS: { // Security Access Status, Byte
    out[0] = 0x00;
    return 1;
  }
  case SCP_PID_PATS_STATUS: { // PATS Status, Byte
    out[0] = 0x00;
    return 1;
  }
  case SCP_PID_TRIP_COUNT: { // TRIP_COUNT - OBDII trip counter, Byte
    out[0] = 0x01;
    return 1;
  }
  case SCP_PID_CODES_COUNT: { // CODES_COUNT - stored DTC count, Byte
    out[0] = dtcManagerCount(DTC_KIND_STORED);
    return 1;
  }
  case SCP_PID_EGRDC:   // VP37: no electronic EGR -> NRC
  case SCP_PID_FUELPW1: // VP37: no individual injector pulsewidth -> NRC
    return 0;
  case SCP_PID_VMAF:     // VMAF - MAF voltage (no MAF sensor)
  case SCP_PID_MAF_RATE: // j1979_01_10 - MAF rate (no MAF sensor)
  case SCP_PID_IMAF:     // IMAF - MAF sensor A/D (no MAF sensor)
    // Any positive response here makes Fordiag show an extra "MAF system".
    return 0;
  }
  return 0;
}

/**
 * @brief Handle a Ford SCP PID access request tunneled over UDS 0x22.
 * @param response CAN response identifier.
 * @param pid Requested Ford SCP PID.
 * @param txData Output frame buffer.
 * @param tx Output flag set when a single-frame response is ready.
 * @return True when the PID was handled.
 */
static bool handleScpPidAccess(obd_response_t *response, uint16_t pid,
                               uint8_t *txData, bool *tx) {
  (void)response; // Intentionally unused; kept for API consistency

  if (txData == NULL || tx == NULL) {
    return false;
  }

  uint8_t dataBytes[4] = {0};
  int dataLen = 0;

  // Try known Ford SCP PIDs with proper encoding.
  dataLen = encodeFordScpPid(pid, dataBytes);

  // Unknown PIDs: return false -> caller sends NRC.
  // Returning positive zero responses for unknown DIDs caused Fordiag
  // to show "MAP system / MAF system" (any positive = "sensor exists").
  if (dataLen == 0) {
    return false;
  }

  int pci = 3 + dataLen; // 0x62 + DID_H + DID_L + data
  if (pci > 7) {
    pci = 7;
  }
  txData[0] = (uint8_t)pci;
  txData[1] = UDS_RSP_READ_DATA_BY_ID;
  jh_store_be16(&txData[2], pid);
  for (int i = 0; i < 4; i++) {
    txData[4 + i] = (i < dataLen) ? dataBytes[i] : PAD;
  }
  *tx = true;
  return true;
}

/**
 * @brief Answer Ford E3xx identification DIDs used by Fordiag.
 * @param response CAN response identifier.
 * @param did Requested DID.
 * @return None.
 */
static void send22FordDiagE3xx(obd_response_t *response, uint16_t did) {
  if (did == (uint16_t)DID_FORD_TYPE) {
    // Null-padded: Fordiag treats E3xx as a VIN block and space-padding here
    // causes trailing 0x20 bytes to bleed into the VIN display as leading
    // spaces.
    send22Field(response, did, ecu_Type, 8);
    return;
  }

  if (did < (uint16_t)DID_FORD_VIN_CHUNK_BASE ||
      did > (uint16_t)DID_FORD_VIN_CHUNK_LAST) {
    return;
  }

  static const uint8_t chunkOffset[5] = {0, 3, 6, 9, 12};
  static const uint8_t chunkLen[5] = {3, 3, 3, 3, 5};

  uint8_t idx = (uint8_t)(did - DID_FORD_VIN_CHUNK_BASE);
  int off = (int)chunkOffset[idx];
  int width = (int)chunkLen[idx];
  int vinLen = (int)strlen(vehicle_Vin);

  char vinChunk[6] = {0};
  if (off < vinLen) {
    int copyLen = width;
    if (off + copyLen > vinLen) {
      copyLen = vinLen - off;
    }
    (void)memcpy(vinChunk, &vehicle_Vin[off], (size_t)copyLen);
  }

  deb("UDS 0x22 DID=0x%04X VIN chunk[%u]=%s", did, idx, vinChunk);
  send22Field(response, did, vinChunk, width);
}

static void handleUdsReadDtcInfo(uint8_t mode, uint8_t numofBytes,
                                 const uint8_t *data, obd_response_t *response,
                                 uint8_t *txData, bool *tx) {
#ifdef FORDIAG_COMPAT_NO_UDS_DTC
  (void)numofBytes;
  (void)data;
  (void)txData;
  (void)tx;
  // Fordiag author: "after connect do not response to command 19 = not support
  // UDS". Returning NRC here tells Fordiag the ECU is not a UDS ECU, steering
  // it toward the EEC-V identification path (E217/E21A/E219). DTCs remain
  // accessible via standard OBD Modes 0x03/0x07/0x0A.
  obdResponseSetNegative(response, mode, NRC_SERVICE_NOT_SUPPORTED);
#else
  // Accept 1-byte "probe" requests: Fordiag sends just service 0x19
  // to check DTC support before the full identification sequence.
  if (numofBytes < 2u) {
    uint8_t probe[] = {UDS_RSP_READ_DTC_INFO, 0x02, 0x2F};
    obdResponseSetPayload(response, sizeof(probe), probe);
  } else {
    uint8_t subFunction = data[2];
    deb("UDS 0x19 subFunction=0x%02X len=0x%02X", subFunction, numofBytes);
    if (subFunction == 0x02u) {
      uint8_t statusMask = (numofBytes > 3u) ? data[3] : 0xFFu;
      uint16_t activeCodes[8] = {0};
      uint8_t count = dtcManagerGetCodes(DTC_KIND_ACTIVE, activeCodes, 8);
      deb("UDS 0x19 reportDTCByStatusMask mask=0x%02X activeCount=%u",
          statusMask, count);

      uint8_t payload[40] = {0};
      int p = 0;
      payload[p] = UDS_RSP_READ_DTC_INFO;
      p++;
      payload[p] = 0x02;
      p++;
      payload[p] = 0x2F; // supported status mask
      p++;

      for (uint8_t i = 0; i < count && (p + 3) < (int)sizeof(payload); i++) {
        uint8_t dtcStatus = 0x01; // testFailed
        if ((dtcStatus & statusMask) == 0u) {
          continue;
        }

        payload[p] = 0x00;
        p++;
        jh_store_be16(&payload[p], activeCodes[i]);
        p += 2;
        payload[p] = dtcStatus;
        p++;
      }

      obdResponseSetPayload(response, p, payload);
    } else if (subFunction == 0x0Au) {
      // reportSupportedDTC
      uint8_t storedCount = dtcManagerCount(DTC_KIND_STORED);
      deb("UDS 0x19 reportSupportedDTC storedCount=%u", storedCount);
      txData[0] = 0x04;
      txData[1] = UDS_RSP_READ_DTC_INFO;
      txData[2] = 0x0A;
      txData[3] = storedCount;
      *tx = true;
    } else {
      obdResponseSetNegative(response, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
    }
  }
#endif
}

static void handleUdsReadDataById(uint32_t requestId, uint8_t mode,
                                  uint8_t numofBytes, const uint8_t *data,
                                  obd_response_t *response, uint8_t *txData,
                                  bool *tx) {
  (void)requestId;
  uint16_t did = jh_load_be16(&data[2]);
  deb("UDS 0x22 DID=0x%04X len=%d", did, numofBytes);
  // Detect multi-DID requests: service(1) + N*DID(2) means numofBytes > 3 for
  // N>1.
  if (numofBytes > 3u) {
#ifdef OBD_VERBOSE_IDENT_DEBUG
    int didCount = (numofBytes - 1) / 2;
    deb("UDS 0x22 MULTI-DID detected: %d DIDs in request (only 1st processed!)",
        didCount);
    hal_deb_hex("UDS 0x22 multi-DID raw", data, (int)numofBytes + 1, 16);
#endif
  }
#ifdef OBD_VERBOSE_IDENT_DEBUG
  if (isFordDiagIdentificationDid(did)) {
    deb("UDS 0x22 ident request DID=0x%04X reqId=0x%03lX", did,
        (unsigned long)requestId);
    hal_deb_hex("UDS 0x22 ident request raw", data, (int)numofBytes + 1, 16);
  }
#endif
  if (did == (uint16_t)DID_VIN) {
    uint8_t payload[20];
    (void)memset(payload, 0, sizeof(payload));
    size_t vinLength = strlen(vehicle_Vin);
    initReadDidPayload(payload, DID_VIN);
    if (vinLength > 17u) {
      vinLength = 17u;
    }
    (void)memcpy(&payload[3], vehicle_Vin, vinLength);
#ifdef OBD_VERBOSE_IDENT_DEBUG
    deb("UDS 0x22 F190 VIN='%s'", vehicle_Vin);
    hal_deb_hex("UDS 0x22 F190 payload", payload, 20, 24);
#endif
    obdResponseSetPayload(response, sizeof(payload), payload);
  } else if (did == (uint16_t)DID_ACTIVE_SESSION) {
    uint8_t udsRsp[8];
    (void)memset(udsRsp, 0, sizeof(udsRsp));
    udsRsp[0] = 0x04u;
    udsRsp[1] = UDS_RSP_READ_DATA_BY_ID;
    jh_store_be16(&udsRsp[2], DID_ACTIVE_SESSION);
    udsRsp[4] = s_udsSessionValue;
    obdResponseSetFrame(response, udsRsp);
  } else if (did == (uint16_t)DID_SPARE_PART_NUMBER) {
    send22Field(response, did, ecu_PartNumber, (int)strlen(ecu_PartNumber));
  } else if (did == (uint16_t)DID_SW_VERSION) {
    // Fordiag maps this DID as "SW version".
    send22Field(response, did, ecu_SwVersion, 4);
  } else if (did == (uint16_t)DID_SW_VERSION_ALT) {
    send22Field(response, did, ecu_SwVersion, (int)strlen(ecu_SwVersion));
  } else if (did == (uint16_t)DID_SUPPLIER_ID) {
    // System supplier identifier
    const char *supplier = "FORD EEC-V";
    send22Field(response, did, supplier, (int)strlen(supplier));
  } else if (did == (uint16_t)DID_MANUFACTURE_DATE) {
    send22Field(response, did, ecu_SwDate, (int)strlen(ecu_SwDate));
  } else if (did == (uint16_t)DID_SERIAL_NUMBER) {
    send22Field(response, did, vehicle_Vin, (int)strlen(vehicle_Vin));
  } else if (did == (uint16_t)DID_HW_VERSION) {
    send22Field(response, did, ecu_HardwareId, (int)strlen(ecu_HardwareId));
  } else if (did == (uint16_t)DID_SYSTEM_NAME) {
    // System name / engine type
    const char *sysName = "FORD 1.8 TDDI VP37";
    send22Field(response, did, sysName, (int)strlen(sysName));
  } else if (did == (uint16_t)DID_ODX_FILE_ID) {
    send22Field(response, did, ecu_Model, (int)strlen(ecu_Model));
  } else if (did == (uint16_t)DID_FORD_MODEL) {
    send22IdentField(response, did, ecu_Model, 8);
  } else if ((jh_u16_msb(did) == jh_u16_msb(DID_F4_MODEL)) &&
             did > (uint16_t)DID_F4_COPYRIGHT) {
    // F4xx live-data mirror takes priority over ALT ident DIDs.
    // ALT ident DIDs (F40B, F40C, ...F449) share the same address space,
    // so we must try live-data encoding first to avoid e.g. 0xF40F returning
    // the catch-code string instead of PID 0x0F (IAT) temperature data.
    uint8_t pid = jh_u16_lsb(did);
    uint8_t dataBytes[4] = {0};
    int dataLen = 0;
    if (encodeMode01PidData(pid, dataBytes, &dataLen)) {
      uint8_t payload[3 + 4];
      (void)memset(payload, 0, sizeof(payload));
      initReadDidPayload(payload, did);
      (void)memcpy(&payload[3], dataBytes, (size_t)dataLen);
      obdResponseSetPayload(response, 3 + dataLen, payload);
    } else if (did == (uint16_t)DID_F4_MODEL_16) {
      send22IdentField(response, did, ecu_Model, 16);
    } else if (did == (uint16_t)DID_F4_TYPE_ALT) {
      send22IdentField(response, did, ecu_Type, 8);
    } else if (did == (uint16_t)DID_F4_SUBTYPE_ALT) {
      send22IdentField(response, did, ecu_SubType, 8);
    } else if (did == (uint16_t)DID_F4_CATCH_CODE_ALT) {
      send22IdentField(response, did, ecu_CatchCode, 8);
    } else if (did == (uint16_t)DID_F4_SW_DATE_ALT) {
      send22IdentField(response, did, ecu_SwDate, 8);
    } else if (did == (uint16_t)DID_F4_CALIBRATION_ID_ALT) {
      send22IdentField(response, did, ecu_CalibrationId, 16);
    } else if (did == (uint16_t)DID_F4_HARDWARE_ID_ALT) {
      send22IdentField(response, did, ecu_HardwareId, 8);
    } else if (did == (uint16_t)DID_F4_ROM_SIZE_ALT) {
      send22U32(response, did, FORD_ROM_SIZE_512K);
    } else if (did == (uint16_t)DID_F4_PART_NUMBER_ALT) {
      send22IdentField(response, did, ecu_PartNumber, 16);
    } else if (did == (uint16_t)DID_F4_SW_VERSION) {
      send22IdentField(response, did, ecu_SwVersion, 4);
    } else if (did == (uint16_t)DID_F4_COPYRIGHT_ALT) {
      send22IdentField(response, did, ecu_Copyright, 16);
    } else {
      // Keep alive with a deterministic zero payload for unknown F4xx PIDs.
      uint8_t payload[4];
      (void)memset(payload, 0, sizeof(payload));
      initReadDidPayload(payload, did);
      obdResponseSetPayload(response, sizeof(payload), payload);
    }
  } else if (did >= (uint16_t)DID_F4_MODEL &&
             did <= (uint16_t)DID_F4_COPYRIGHT) {
    // Ford EEC-V identification DIDs used by identification screen.
    const char *val = ecu_SwVersion;
    int width = 4;
    bool isRomSizeDid = false;
    switch (did) {
    case DID_F4_MODEL:
      val = ecu_Model;
      width = 8;
      break;
    case DID_F4_TYPE:
      val = ecu_Type;
      width = 8;
      break;
    case DID_F4_SUBTYPE:
      val = ecu_SubType;
      width = 8;
      break;
    case DID_F4_CATCH_CODE:
      val = ecu_CatchCode;
      width = 8;
      break;
    case DID_F4_SW_DATE:
      val = ecu_SwDate;
      width = 8;
      break;
    case DID_F4_CALIBRATION_ID:
      val = ecu_CalibrationId;
      width = 16;
      break;
    case DID_F4_PART_NUMBER:
      val = ecu_PartNumber;
      width = 16;
      break;
    case DID_F4_HARDWARE_ID:
      val = ecu_HardwareId;
      width = 8;
      break;
    case DID_F4_ROM_SIZE:
      isRomSizeDid = true;
      break;
    case DID_F4_COPYRIGHT:
      val = ecu_Copyright;
      width = 16;
      break;
    default:
      break;
    }
    if (isRomSizeDid) {
      send22U32(response, did, FORD_ROM_SIZE_512K);
    } else {
      send22IdentField(response, did, val, width);
    }
  } else if (did == (uint16_t)DID_PART_NUMBER) {
    // Fordiag maps this DID as "Part number".
    send22Field(response, did, ecu_PartNumber, 16);
  } else if (did == (uint16_t)DID_BOOT_SW_ID) {
    send22Field(response, did, ecu_SwVersion, 4);
  } else if (did >= (uint16_t)DID_FORD_TYPE &&
             did <= (uint16_t)DID_FORD_VIN_CHUNK_LAST) {
    send22FordDiagE3xx(response, did);
  } else if (did == (uint16_t)DID_FORD_SW_DATE) {
    // E200: Fordiag decodes SW date as 3 binary bytes (NOT ASCII!):
    //   byte 0 = month (lower nibble), byte 1 = day, byte 2 = year-1900.
    // Per Fordiag author's ECU_ReadSWVersion FoxPro source.
    // Parse ecu_SwDate "YYYYMMDD" -> binary {month, day, year-1900}.
    int year = 0;
    int month = 0;
    int day = 0;
    if (strlen(ecu_SwDate) >= 8u) {
      year = (ecu_SwDate[0] - '0') * 1000 + (ecu_SwDate[1] - '0') * 100 +
             (ecu_SwDate[2] - '0') * 10 + (ecu_SwDate[3] - '0');
      month = (ecu_SwDate[4] - '0') * 10 + (ecu_SwDate[5] - '0');
      day = (ecu_SwDate[6] - '0') * 10 + (ecu_SwDate[7] - '0');
    }
    uint8_t payload[6];
    (void)memset(payload, 0, sizeof(payload));
    initReadDidPayload(payload, did);
    payload[3] = (uint8_t)(month & 0x0F);
    payload[4] = (uint8_t)day;
    payload[5] = (uint8_t)(year - 1900);
    deb("UDS 0x22 E200 SW date: %d-%02d-%02d -> {0x%02X,0x%02X,0x%02X}", year,
        month, day, payload[3], payload[4], payload[5]);
    obdResponseSetPayload(response, sizeof(payload), payload);
  } else if (did == (uint16_t)DID_FORD_PARTNUM_MIDDLE) {
    // E217: Fordiag reads binary middle bytes of Ford part number
    // (e.g. "12A650" -> {0x12, 0x0A, 0x06, 0x50}) for ECU identification.
    sendE217PartNumMiddle(response, did);
  } else if (did == (uint16_t)DID_FORD_PARTNUM_PREFIX) {
    // E21A: Fordiag reads ASCII prefix of Ford part number
    // (e.g. "XS4A") for ECU identification.
    sendE21APartNumPrefix(response, did);
  } else if (did == (uint16_t)DID_FORD_PARTNUM_SUFFIX) {
    // E219: Fordiag reads 2-byte encoded suffix of Ford part number
    // (e.g. "AXB" -> {0x52, 0x01}) for ECU identification.
    sendE219PartNumSuffix(response, did);
  } else if (did == (uint16_t)DID_FORD_CATCH_CODE) {
    send22IdentField(response, did, ecu_CatchCode, 8);
  } else if (did == (uint16_t)DID_FORD_PART_NUMBER) {
    send22IdentField(response, did, ecu_PartNumber, 16);
  } else if (did == (uint16_t)DID_FORD_TOTDIST) {
#ifdef OBD_ENABLE_TOTDIST
    // DD01: Total distance (odometer), 3 bytes big-endian unsigned km.
    // Per Fordiag author: "3bytova!" - unusual 3-byte format.
    uint32_t km = obdGetTotalDistanceKm();
    uint8_t payload[6];
    uint8_t encodedDistance[4];
    (void)memset(payload, 0, sizeof(payload));
    (void)memset(encodedDistance, 0, sizeof(encodedDistance));
    initReadDidPayload(payload, did);
    (void)hal_u32_to_bytes_be(km, encodedDistance);
    (void)memcpy(&payload[3], &encodedDistance[1], 3u);
    deb("UDS 0x22 DD01 TOTDIST=%lu km", (unsigned long)km);
    obdResponseSetPayload(response, sizeof(payload), payload);
#else
    obdResponseSetNegative(response, mode, NRC_REQUEST_OUT_OF_RANGE);
#endif
  } else if (did == (uint16_t)DID_FORD_OUTTMP) {
    // DD05: External temperature, 1 byte unsigned, value = raw - 40 °C.
    // ECU has no outside temp sensor; use intake temp as best proxy.
    uint8_t raw = obd_encodeTempByte(getGlobalValue(F_INTAKE_TEMP));
    uint8_t payload[4];
    (void)memset(payload, 0, sizeof(payload));
    initReadDidPayload(payload, did);
    payload[3] = raw;
    deb("UDS 0x22 DD05 OUTTMP raw=%u (%.1f°C)", (unsigned)raw,
        getGlobalValue(F_INTAKE_TEMP));
    obdResponseSetPayload(response, sizeof(payload), payload);
  } else if (did == (uint16_t)DID_FORD_FUEL_TEMP) {
    // DD02: Fuel temperature, 1 byte unsigned, value = raw - 40 °C.
    uint8_t raw = obd_encodeTempByte(getGlobalValue(F_FUEL_TEMP));
    uint8_t payload[4];
    (void)memset(payload, 0, sizeof(payload));
    initReadDidPayload(payload, did);
    payload[3] = raw;
    obdResponseSetPayload(response, sizeof(payload), payload);
  } else if (did == (uint16_t)DID_FORD_OIL_PRESSURE) {
    // DD03: Oil pressure, 2 bytes big-endian, kPa × 10.
    int32_t raw = hal_constrain(
        (int32_t)(getGlobalValue(F_OIL_PRESSURE) * 10.0f), 0, 65535);
    uint8_t payload[5];
    (void)memset(payload, 0, sizeof(payload));
    initReadDidPayload(payload, did);
    jh_store_be16(&payload[3], (uint16_t)raw);
    obdResponseSetPayload(response, sizeof(payload), payload);
  } else if (did == (uint16_t)DID_FORD_BOOST) {
    // DD04: Boost/intake pressure, 2 bytes big-endian, bar × 1000.
    int32_t raw = hal_constrain((int32_t)(getGlobalValue(F_PRESSURE) * 1000.0f),
                                0, 65535);
    uint8_t payload[5];
    (void)memset(payload, 0, sizeof(payload));
    initReadDidPayload(payload, did);
    jh_store_be16(&payload[3], (uint16_t)raw);
    obdResponseSetPayload(response, sizeof(payload), payload);
  } else if (did == (uint16_t)DID_FORD_DPF_PRESSURE) {
    // DD06: DPF differential pressure, 2 bytes big-endian, Pa.
    int32_t raw = hal_constrain(
        (int32_t)(getGlobalValue(F_DPF_PRESSURE) * 1000.0f), 0, 65535);
    uint8_t payload[5];
    (void)memset(payload, 0, sizeof(payload));
    initReadDidPayload(payload, did);
    jh_store_be16(&payload[3], (uint16_t)raw);
    obdResponseSetPayload(response, sizeof(payload), payload);
  } else if (did == (uint16_t)DID_FORD_BOOST_DESIRED) {
    // DD07: Desired boost pressure, 2 bytes big-endian, bar × 1000.
    int32_t raw = hal_constrain(
        (int32_t)(getGlobalValue(F_PRESSURE_DESIRED) * 1000.0f), 0, 65535);
    uint8_t payload[5];
    (void)memset(payload, 0, sizeof(payload));
    initReadDidPayload(payload, did);
    jh_store_be16(&payload[3], (uint16_t)raw);
    obdResponseSetPayload(response, sizeof(payload), payload);
  } else if (did == (uint16_t)DID_FORD_BOOST_PERCENT) {
    // DD08: Boost duty cycle percentage, 1 byte 0-100.
    int32_t pct =
        hal_constrain((int32_t)getGlobalValue(F_PRESSURE_PERCENTAGE), 0, 100);
    uint8_t payload[4];
    (void)memset(payload, 0, sizeof(payload));
    initReadDidPayload(payload, did);
    payload[3] = (uint8_t)pct;
    obdResponseSetPayload(response, sizeof(payload), payload);
  } else if (did == (uint16_t)DID_ECU_CAPABILITIES) {
    // DID 0x0200 collides with SCP_PID_CODES_COUNT - must return NRC here
    // so Fordiag falls back to Mode 01 PID 0x51 for diesel type detection.
    // A positive response causes Fordiag to show "MAP system / MAF system".
    obdResponseSetNegative(response, mode, NRC_REQUEST_OUT_OF_RANGE);
  } else if (handleScpPidAccess(response, did, txData, tx)) {
    // Compatibility with legacy Ford SCP "REQUEST_PID_ACCESS" style over CAN.
    // Positive response already prepared in txData.
  } else {
    obdResponseSetNegative(response, mode, NRC_REQUEST_OUT_OF_RANGE);
  }
}

static void handleUdsReadDataByLocalId(uint32_t requestId, uint8_t mode,
                                       uint8_t numofBytes, const uint8_t *data,
                                       obd_response_t *response) {
  (void)requestId;
#ifndef OBD_VERBOSE_IDENT_DEBUG
  (void)numofBytes;
#endif
  uint8_t localId = data[2];
  deb("KWP 0x12 localId=0x%02X", localId);
#ifdef OBD_VERBOSE_IDENT_DEBUG
  if (isFordDiagIdentificationLocalId(localId)) {
    deb("KWP 0x12 ident request localId=0x%02X reqId=0x%03lX", localId,
        (unsigned long)requestId);
    hal_deb_hex("KWP 0x12 ident request raw", data, (int)numofBytes + 1, 16);
  }
#endif

  switch (localId) {
  case KWP_LID_CALIBRATION_ID:
    send12LocalField(response, localId, ecu_CalibrationId, 16);
    break;
  case KWP_LID_SW_DATE:
    send12LocalField(response, localId, ecu_SwDate, 8);
    break;
  case KWP_LID_PART_NUMBER:
    send12LocalField(response, localId, ecu_PartNumber, 16);
    break;
  case KWP_LID_MODEL_16:
    send12LocalField(response, localId, ecu_Model, 16);
    break;
  case KWP_LID_VIN:
    send12LocalField(response, localId, vehicle_Vin, 17);
    break;
  case KWP_LID_MODEL:
    send12LocalField(response, localId, ecu_Model, 16);
    break;
  case KWP_LID_TYPE:
    send12LocalField(response, localId, ecu_Type, 8);
    break;
  case KWP_LID_SUBTYPE:
    send12LocalField(response, localId, ecu_SubType, 8);
    break;
  case KWP_LID_CATCH_CODE:
    send12LocalField(response, localId, ecu_CatchCode, 8);
    break;
  case KWP_LID_VIN_ALT:
    send12LocalField(response, localId, vehicle_Vin, 17);
    break;
  case KWP_LID_SW_VERSION:
    send12LocalField(response, localId, ecu_SwVersion, 4);
    break;
  case KWP_LID_SW_DATE_ALT:
    send12LocalField(response, localId, ecu_SwDate, 8);
    break;
  case KWP_LID_CALIBRATION_ALT:
    send12LocalField(response, localId, ecu_CalibrationId, 16);
    break;
  case KWP_LID_PART_NUMBER_ALT:
    send12LocalField(response, localId, ecu_PartNumber, 16);
    break;
  case KWP_LID_HARDWARE_ID:
    send12LocalField(response, localId, ecu_HardwareId, 8);
    break;
  case KWP_LID_COPYRIGHT:
    send12LocalField(response, localId, ecu_Copyright, 32);
    break;
  case KWP_LID_ROM_SIZE: {
    // ROM size: 512 KB = 0x00080000
    uint8_t rsp[6];
    (void)memset(rsp, 0, sizeof(rsp));
    rsp[0] = UDS_RSP_READ_DATA_BY_LOCAL_ID;
    rsp[1] = localId;
    (void)hal_u32_to_bytes_be(FORD_ROM_SIZE_512K, &rsp[2]);
    obdResponseSetPayload(response, sizeof(rsp), rsp);
    break;
  }

  // ---- ForDiag EEC-V identification blocks -------------------------
  case KWP_LID_CALIB_BLOCK: {
    // Software calibration block used by Fordiag:
    // SwVersion(4) + SwDate(8) + CalibId(16)
    uint8_t resp[2 + 16 + 4 + 8];
    resp[0] = UDS_RSP_READ_DATA_BY_LOCAL_ID;
    resp[1] = localId;
    // Ford EEC-V convention: space-padded ASCII fields.
    hal_text_pack_field_pad(&resp[2], ecu_SwVersion, 4, FORD_IDENT_PAD);
    hal_text_pack_field_pad(&resp[6], ecu_SwDate, 8, FORD_IDENT_PAD);
    hal_text_pack_field_pad(&resp[14], ecu_CalibrationId, 16, FORD_IDENT_PAD);
#ifdef OBD_VERBOSE_IDENT_DEBUG
    deb("KWP 0x12/0x33 fields: sw=%s swDate=%s cal=%s", ecu_SwVersion,
        ecu_SwDate, ecu_CalibrationId);
    hal_deb_hex("KWP 0x12/0x33 response", resp, (int)sizeof(resp), 40);
#endif
    obdResponseSetPayload(response, sizeof(resp), resp);
    break;
  }
  case KWP_LID_COMPACT_IDENT:
    // Disabled: our custom compact block format doesn't match what Fordiag
    // expects, causing it to show "MAP system / MAF system" instead of
    // "DIESEL engine". NRC forces fallback to standard PID 0x51 detection.
    obdResponseSetNegative(response, mode, NRC_REQUEST_OUT_OF_RANGE);
    break;
  case KWP_LID_SUPPORTED_LIST: {
    // Supported local identifiers list used by some scan tools to discover ID
    // fields.
    uint8_t resp[] = {UDS_RSP_READ_DATA_BY_LOCAL_ID,
                      localId,
                      KWP_LID_CALIB_BLOCK,
                      KWP_LID_CALIBRATION_ID,
                      KWP_LID_SW_DATE,
                      KWP_LID_PART_NUMBER,
                      KWP_LID_MODEL_16,
                      KWP_LID_VIN,
                      KWP_LID_MODEL,
                      KWP_LID_TYPE,
                      KWP_LID_SUBTYPE,
                      KWP_LID_CATCH_CODE,
                      KWP_LID_VIN_ALT,
                      KWP_LID_SW_VERSION,
                      KWP_LID_SW_DATE_ALT,
                      KWP_LID_CALIBRATION_ALT,
                      KWP_LID_PART_NUMBER_ALT,
                      KWP_LID_HARDWARE_ID,
                      KWP_LID_ROM_SIZE,
                      KWP_LID_COPYRIGHT};
#ifdef OBD_VERBOSE_IDENT_DEBUG
    hal_deb_hex("KWP 0x12/0xFF response", resp, (int)sizeof(resp), 8);
#endif
    obdResponseSetPayload(response, sizeof(resp), resp);
    break;
  }
    // ------------------------------------------------------------------

  default:
    obdResponseSetNegative(response, mode, NRC_REQUEST_OUT_OF_RANGE);
    break;
  }
}

/**
 * @brief Dispatch one UDS or KWP service request.
 * @param requestId CAN identifier on which the request was received.
 * @param mode Requested service identifier.
 * @param numofBytes Request length encoded in the incoming frame.
 * @param data Raw request buffer.
 * @param response Destination for the application response payload.
 * @return True when the service was recognized and handled.
 */
bool obdFordDiagHandleService(uint32_t requestId, uint8_t mode,
                              uint8_t numofBytes, const uint8_t *data,
                              obd_response_t *response) {
  bool handled = false;
  bool tx = false;
  const uint8_t pid = (numofBytes > 1u) ? data[2] : 0u;
  uint8_t txData[] = {0u,  (uint8_t)(UDS_POSITIVE_RESPONSE_OFFSET | mode),
                      pid, PAD,
                      PAD, PAD,
                      PAD, PAD};

  if (mode == (uint8_t)UDS_SVC_DIAGNOSTIC_SESSION) {
    handled = true;
    if (requireMinLength(response, mode, numofBytes, 2)) {
      uint8_t subFunction = data[2] & 0x7Fu;
      if ((subFunction == (uint8_t)UDS_SESSION_DEFAULT) ||
          (subFunction == (uint8_t)UDS_SESSION_PROGRAMMING) ||
          (subFunction == (uint8_t)UDS_SESSION_EXTENDED)) {
        s_udsSessionValue = subFunction;
        const uint8_t udsRsp[] = {0x06,        UDS_RSP_DIAGNOSTIC_SESSION,
                                  subFunction, 0x00,
                                  0x32,        0x01,
                                  0xF4,        PAD};
        obdResponseSetFrame(response, udsRsp);
      } else {
        obdResponseSetNegative(response, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
      }
    }
  } else if (mode == (uint8_t)UDS_SVC_ECU_RESET) {
    handled = true;
    if (requireMinLength(response, mode, numofBytes, 2)) {
      uint8_t subFunction = data[2] & 0x7Fu;
      txData[0] = 0x02;
      txData[1] = UDS_RSP_ECU_RESET;
      txData[2] = subFunction;
      tx = true;
    }
  } else if (mode == (uint8_t)UDS_SVC_CLEAR_DTC) {
    handled = true;
    // ISO 14229 request is 0x14 + 3-byte groupOfDTC, but some testers
    // send shortened variants (for example only service byte). For
    // compatibility we accept all variants and clear all DTCs.
    if (numofBytes >= 4u) {
      uint32_t group = ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 8) |
                       (uint32_t)data[4];
      deb("UDS 0x14 clearDTC group=0x%06lX", (unsigned long)group);
    } else if (numofBytes >= 2u) {
      uint32_t group = 0;
      for (uint8_t i = 2; i <= numofBytes; i++) {
        group = (group << 8) | (uint32_t)data[i];
      }
      deb("UDS 0x14 clearDTC shortGroup bytes=%u value=0x%06lX",
          (unsigned)(numofBytes - 1u), (unsigned long)group);
    } else {
      deb("UDS 0x14 clearDTC (no group bytes)");
    }

    if (dtcManagerClearAll()) {
      txData[0] = 0x01;
      txData[1] = UDS_RSP_CLEAR_DTC;
      tx = true;
    } else {
      obdResponseSetNegative(response, mode, NRC_CONDITIONS_NOT_CORRECT);
    }
  } else if (mode == (uint8_t)KWP_SVC_READ_DTC_BY_STATUS) {
    handled = true;
    // KWP2000 readDiagnosticTroubleCodesByStatus (ISO 14230-3)
    // Request: 18 statusOfDTC groupHi groupLo
    if (requireMinLength(response, mode, numofBytes, 4)) {
      uint8_t statusOfDtc = data[2];
      uint16_t group = jh_load_be16(&data[3]);
      (void)group; // FF00/FFFF = powertrain/all; we report all regardless

      // statusOfDtc is a bitmask: 0x00 = report all, 0x01 = testFailed
      // (active), 0x08 = confirmedDTC (stored). Treat 0x00 as "all stored".
      dtc_kind_t kind = DTC_KIND_STORED;
      if ((statusOfDtc & 0x01u) != 0u) {
        kind = DTC_KIND_ACTIVE;
      }

      uint16_t codes[8] = {0};
      uint8_t count = dtcManagerGetCodes(kind, codes, 8);
      deb("KWP 0x18 statusOfDtc=0x%02X group=0x%04X kind=%d count=%u",
          statusOfDtc, group, (int)kind, count);

      // Response (ISO 14230-3): 58 numberOfDTC [dtcHi dtcLo statusOfDTC]...
      uint8_t payload[40] = {0};
      int p = 0;
      payload[p] = KWP_RSP_READ_DTC_BY_STATUS;
      p++;
      payload[p] = count;
      p++;

      for (uint8_t i = 0; i < count && (p + 2) < (int)sizeof(payload); i++) {
        jh_store_be16(&payload[p], codes[i]);
        p += 2;
        payload[p] = (kind == DTC_KIND_ACTIVE) ? 0x01 : 0x08;
        p++;
      }

      obdResponseSetPayload(response, p, payload);
    }
  } else if (mode == (uint8_t)UDS_SVC_READ_DTC_INFO) {
    handled = true;
    handleUdsReadDtcInfo(mode, numofBytes, data, response, txData, &tx);
  } else if (mode == (uint8_t)UDS_SVC_READ_MEMORY_BY_ADDR) {
    handled = true;
    // Ford SCP-style Request DMR Access
    if (requireMinLength(response, mode, numofBytes, 4)) {
      uint8_t dmrType = data[2];
      uint16_t addr = jh_load_be16(&data[3]);
      deb("SCP 0x23 DMR type=0x%02X addr=0x%04X", dmrType, addr);

      bool validType = (dmrType == 0x00u || dmrType == 0x01u ||
                        dmrType == 0x08u || dmrType == 0x09u);
      if (!validType) {
        sendScpGeneralResponse(response, UDS_SVC_READ_MEMORY_BY_ADDR, data[2],
                               data[3], data[4], NRC_SUBFUNCTION_NOT_SUPPORTED);
      } else {
        sendScpDmrResponse(response, addr, dmrType);
      }
    }
  } else if (mode == (uint8_t)UDS_SVC_READ_DATA_BY_ID) {
    handled = true;
    if (requireMinLength(response, mode, numofBytes, 3)) {
      handleUdsReadDataById(requestId, mode, numofBytes, data, response, txData,
                            &tx);
    }
  } else if (mode == (uint8_t)UDS_SVC_READ_DATA_BY_LOCAL_ID) {
    handled = true;
    // KWP2000 service 0x12 - ReadDataByLocalIdentifier
    if (requireMinLength(response, mode, numofBytes, 2)) {
      handleUdsReadDataByLocalId(requestId, mode, numofBytes, data, response);
    }
  } else if (mode == (uint8_t)UDS_SVC_TESTER_PRESENT) {
    handled = true;
    if (requireMinLength(response, mode, numofBytes, 2)) {
      uint8_t subFunction = data[2];

      // 0x80 means suppress positive response.
      if ((subFunction & (uint8_t)UDS_SUPPRESS_POSITIVE_RSP) == 0u) {
        // Accept any sub-function value (Fordiag sends 0x01 in addition to
        // 0x00).
        txData[0] = 0x02;
        txData[1] = UDS_RSP_TESTER_PRESENT;
        txData[2] = (uint8_t)(subFunction & 0x7Fu);
        tx = true;
      }
    }
  }

  // ── Services listed in Ford ISO-15765 table but not fully implemented ──
  // Respond with proper NRC so Ford diagnostic tools see the ECU as aware
  // of these services rather than treating it as a communication failure.

  else if (mode == (uint8_t)UDS_SVC_SECURITY_ACCESS) {
    handled = true;
    // SecurityAccess: no seed/key implemented on this emulated ECU.
    obdResponseSetNegative(response, mode, NRC_CONDITIONS_NOT_CORRECT);
  } else if (mode == (uint8_t)UDS_SVC_COMM_CONTROL) {
    handled = true;
    if (requireMinLength(response, mode, numofBytes, 2)) {
      uint8_t subFunction = data[2] & 0x7Fu;
      if (subFunction == 0x00u) {
        // enableRxAndTx - acknowledge default state.
        txData[0] = 0x02;
        txData[1] = UDS_RSP_COMM_CONTROL;
        txData[2] = subFunction;
        tx = true;
      } else {
        obdResponseSetNegative(response, mode, NRC_CONDITIONS_NOT_CORRECT);
      }
    }
  } else if (mode == (uint8_t)UDS_SVC_WRITE_DATA_BY_ID) {
    handled = true;
    // No writable DIDs on this ECU.
    obdResponseSetNegative(response, mode, NRC_REQUEST_OUT_OF_RANGE);
  } else if (mode == (uint8_t)UDS_SVC_IO_CONTROL) {
    handled = true;
    // No controllable I/O on this ECU.
    obdResponseSetNegative(response, mode, NRC_REQUEST_OUT_OF_RANGE);
  } else if (mode == (uint8_t)UDS_SVC_ROUTINE_CONTROL) {
    handled = true;
    // No supported routines.
    obdResponseSetNegative(response, mode, NRC_REQUEST_OUT_OF_RANGE);
  } else if (mode == (uint8_t)UDS_SVC_CONTROL_DTC_SETTING) {
    handled = true;
    if (requireMinLength(response, mode, numofBytes, 2)) {
      uint8_t subFunction = data[2] & 0x7Fu;
      // Accept both DTCSettingOn (0x01) and DTCSettingOff (0x02).
      if (subFunction == 0x01u || subFunction == 0x02u) {
        txData[0] = 0x02;
        txData[1] = UDS_RSP_CONTROL_DTC_SETTING;
        txData[2] = subFunction;
        tx = true;
      } else {
        obdResponseSetNegative(response, mode, NRC_SUBFUNCTION_NOT_SUPPORTED);
      }
    }
  }

  if (tx) {
    obdResponseSetFrame(response, txData);
  }

  return handled;
}
