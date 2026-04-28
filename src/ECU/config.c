
#include "config.h"
#include "tests.h"
#include "ecu_unit_testing.h"
#include <hal/hal_crypto.h>

#include <hal/hal_kv.h>
#include <hal/hal_serial_session.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../common/scDefinitions/sc_param_handlers.h"
#include "../common/scDefinitions/sc_param_types.h"
#include "../common/scDefinitions/sc_protocol.h"
#include "../common/scDefinitions/sc_session_vocabulary.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"

const char *err = "ERR";

#define ECU_PARAMS_SCHEMA_V1    1u
#define ECU_PARAMS_SCHEMA_V2    2u
#define ECU_PARAMS_BLOB_SIZE_V1 16u
#define ECU_PARAMS_BLOB_SIZE_V2 18u
#define ECU_PARAMS_BLOB_KEY     0xDA10u

/* Wire-visible parameter catalogue. R1.2 routes every SC reply for these
 * params through the descriptor-driven helpers in src/common/scDefinitions/.
 * The schema_since column drives V1 (5 fields, 16-byte blob) vs V2 (6
 * fields, 18-byte blob) backwards compatibility. */
static const sc_param_descriptor_t k_ecu_params[] = {
    SC_PARAM_SCALAR_I16("fan_coolant_start_c", ecu_params_values_t,
                        fanCoolantStartC,
                        ECU_PARAMS_COOLANT_START_MIN,
                        ECU_PARAMS_COOLANT_START_MAX,
                        TEMP_FAN_START, 1),
    SC_PARAM_SCALAR_I16("fan_coolant_stop_c", ecu_params_values_t,
                        fanCoolantStopC,
                        ECU_PARAMS_COOLANT_STOP_MIN,
                        ECU_PARAMS_COOLANT_STOP_MAX,
                        TEMP_FAN_STOP, 1),
    SC_PARAM_SCALAR_I16("fan_air_start_c", ecu_params_values_t,
                        fanAirStartC,
                        ECU_PARAMS_AIR_START_MIN,
                        ECU_PARAMS_AIR_START_MAX,
                        AIR_TEMP_FAN_START, 1),
    SC_PARAM_SCALAR_I16("fan_air_stop_c", ecu_params_values_t,
                        fanAirStopC,
                        ECU_PARAMS_AIR_STOP_MIN,
                        ECU_PARAMS_AIR_STOP_MAX,
                        AIR_TEMP_FAN_STOP, 1),
    SC_PARAM_SCALAR_I16("heater_stop_c", ecu_params_values_t,
                        heaterStopC,
                        ECU_PARAMS_HEATER_STOP_MIN,
                        ECU_PARAMS_HEATER_STOP_MAX,
                        TEMP_HEATER_STOP, 1),
    SC_PARAM_SCALAR_I16("nominal_rpm", ecu_params_values_t,
                        nominalRpm,
                        ECU_PARAMS_NOMINAL_RPM_MIN,
                        ECU_PARAMS_NOMINAL_RPM_MAX,
                        NOMINAL_RPM_VALUE, 2),
};
static const size_t k_ecu_params_count =
    sizeof(k_ecu_params) / sizeof(k_ecu_params[0]);

/* Forward-declared session state used by every SC_* reply helper below.
 * `hal_serial_session_println(&s_configSession, ...)` automatically wraps the
 * payload into the `$SC,<seq>,<payload>*<crc>` frame when the in-flight
 * request was framed; otherwise it falls back to plain text. */
static hal_serial_session_t s_configSession;

static ecu_params_values_t s_active = {
  .fanCoolantStartC = TEMP_FAN_START,
  .fanCoolantStopC = TEMP_FAN_STOP,
  .fanAirStartC = AIR_TEMP_FAN_START,
  .fanAirStopC = AIR_TEMP_FAN_STOP,
  .heaterStopC = TEMP_HEATER_STOP,
  .nominalRpm = NOMINAL_RPM_VALUE
};

static ecu_params_values_t s_staging = {
  .fanCoolantStartC = TEMP_FAN_START,
  .fanCoolantStopC = TEMP_FAN_STOP,
  .fanAirStartC = AIR_TEMP_FAN_START,
  .fanAirStopC = AIR_TEMP_FAN_STOP,
  .heaterStopC = TEMP_HEATER_STOP,
  .nominalRpm = NOMINAL_RPM_VALUE
};

static bool s_initialized = false;

TESTABLE_STATIC void ecuParamsLoadDefaults(ecu_params_values_t *outValues) {
  if(outValues == NULL) {
    return;
  }
  (void)sc_param_load_defaults(k_ecu_params, k_ecu_params_count, outValues);
}

TESTABLE_STATIC bool ecuParamsValidate(const ecu_params_values_t *candidate, const char **reason) {
  if(reason != NULL) {
    *reason = NULL;
  }

  if(candidate == NULL) {
    if(reason != NULL) {
      *reason = "null";
    }
    return false;
  }

  if(candidate->fanCoolantStartC < ECU_PARAMS_COOLANT_START_MIN
    || candidate->fanCoolantStartC > ECU_PARAMS_COOLANT_START_MAX) {
    if(reason != NULL) {
      *reason = "fan_coolant_start_range";
    }
    return false;
  }

  if(candidate->fanCoolantStopC < ECU_PARAMS_COOLANT_STOP_MIN
    || candidate->fanCoolantStopC > ECU_PARAMS_COOLANT_STOP_MAX) {
    if(reason != NULL) {
      *reason = "fan_coolant_stop_range";
    }
    return false;
  }

  if(candidate->fanAirStartC < ECU_PARAMS_AIR_START_MIN
    || candidate->fanAirStartC > ECU_PARAMS_AIR_START_MAX) {
    if(reason != NULL) {
      *reason = "fan_air_start_range";
    }
    return false;
  }

  if(candidate->fanAirStopC < ECU_PARAMS_AIR_STOP_MIN
    || candidate->fanAirStopC > ECU_PARAMS_AIR_STOP_MAX) {
    if(reason != NULL) {
      *reason = "fan_air_stop_range";
    }
    return false;
  }

  if(candidate->heaterStopC < ECU_PARAMS_HEATER_STOP_MIN
    || candidate->heaterStopC > ECU_PARAMS_HEATER_STOP_MAX) {
    if(reason != NULL) {
      *reason = "heater_stop_range";
    }
    return false;
  }

  if(candidate->nominalRpm < ECU_PARAMS_NOMINAL_RPM_MIN
    || candidate->nominalRpm > ECU_PARAMS_NOMINAL_RPM_MAX) {
    if(reason != NULL) {
      *reason = "nominal_rpm_range";
    }
    return false;
  }

  if(candidate->fanCoolantStopC >= candidate->fanCoolantStartC) {
    if(reason != NULL) {
      *reason = "fan_coolant_hysteresis";
    }
    return false;
  }

  if(candidate->fanAirStopC >= candidate->fanAirStartC) {
    if(reason != NULL) {
      *reason = "fan_air_hysteresis";
    }
    return false;
  }

  if(candidate->heaterStopC >= candidate->fanCoolantStartC) {
    if(reason != NULL) {
      *reason = "heater_vs_fan_order";
    }
    return false;
  }

  return true;
}

TESTABLE_STATIC bool ecuParamsStage(const ecu_params_values_t *candidate, const char **reason) {
  if(!ecuParamsValidate(candidate, reason)) {
    return false;
  }
  s_staging = *candidate;
  return true;
}

TESTABLE_STATIC void ecuParamsApply(void) {
  s_active = s_staging;
}

TESTABLE_STATIC bool ecuParamsLoadPersisted(ecu_params_values_t *outValues) {
  if(outValues == NULL) {
    return false;
  }

  uint16_t blobLen = 0u;
  if(!hal_kv_get_blob(ECU_PARAMS_BLOB_KEY, NULL, 0u, &blobLen)) {
    return false;
  }
  if(blobLen != ECU_PARAMS_BLOB_SIZE_V1 && blobLen != ECU_PARAMS_BLOB_SIZE_V2) {
    return false;
  }

  uint8_t blob[ECU_PARAMS_BLOB_SIZE_V2] = {0};
  if(!hal_kv_get_blob(ECU_PARAMS_BLOB_KEY, blob, (uint16_t)sizeof(blob), &blobLen)) {
    return false;
  }

  /* Pre-load defaults so V2-only fields keep a sane value when a V1
   * blob (5 fields) is being decoded into a V2-shaped struct. */
  ecu_params_values_t decoded;
  ecuParamsLoadDefaults(&decoded);

  uint16_t schema = 0u;
  if(!sc_param_blob_decode(k_ecu_params, k_ecu_params_count, &decoded,
                           blob, blobLen, &schema)) {
    return false;
  }

  if(!ecuParamsValidate(&decoded, NULL)) {
    return false;
  }

  *outValues = decoded;
  return true;
}

#ifdef UNIT_TEST
TESTABLE_STATIC bool ecuParamsPersist(const ecu_params_values_t *values) {
  if(!ecuParamsValidate(values, NULL)) {
    return false;
  }

  uint8_t blob[ECU_PARAMS_BLOB_SIZE_V2] = {0};
  const size_t written = sc_param_blob_encode(
      k_ecu_params, k_ecu_params_count, values, ECU_PARAMS_SCHEMA_V2,
      blob, sizeof(blob));
  if(written != ECU_PARAMS_BLOB_SIZE_V2) {
    return false;
  }
  return hal_kv_set_blob(ECU_PARAMS_BLOB_KEY, blob, (uint16_t)sizeof(blob));
}

TESTABLE_STATIC uint16_t ecuParamsBlobKeyForTest(void) {
  return ECU_PARAMS_BLOB_KEY;
}

TESTABLE_STATIC void ecuParamsResetRuntimeStateForTest(void) {
  ecuParamsLoadDefaults(&s_active);
  s_staging = s_active;
  s_initialized = false;
}
#endif

void ecuParamsInit(void) {
  if(s_initialized) {
    return;
  }

  ecuParamsLoadDefaults(&s_staging);
  ecuParamsApply();

  ecu_params_values_t loaded = {0};
  if(ecuParamsLoadPersisted(&loaded) && ecuParamsStage(&loaded, NULL)) {
    ecuParamsApply();
  }

  s_initialized = true;
}

const ecu_params_values_t *ecuParamsActive(void) {
  return &s_active;
}

int16_t ecuParamsFanCoolantStart(void) {
  return s_active.fanCoolantStartC;
}

int16_t ecuParamsFanCoolantStop(void) {
  return s_active.fanCoolantStopC;
}

int16_t ecuParamsFanAirStart(void) {
  return s_active.fanAirStartC;
}

int16_t ecuParamsFanAirStop(void) {
  return s_active.fanAirStopC;
}

int16_t ecuParamsHeaterStop(void) {
  return s_active.heaterStopC;
}

int16_t ecuParamsNominalRpm(void) {
  return s_active.nominalRpm;
}

TESTABLE_INLINE_STATIC const char *configSessionSkipSpaces(const char *cursor) {
  if(cursor == NULL) {
    return NULL;
  }

  while(*cursor == ' ') {
    cursor++;
  }
  return cursor;
}

/* Adapter that bridges the descriptor-driven `sc_emit_fn` callback to
 * the framed reply helper from JaszczurHAL. The session pointer is
 * passed as the opaque emit_user parameter. */
static void configSessionEmitThroughHal(const char *payload, void *user) {
  hal_serial_session_println((hal_serial_session_t *)user, payload);
}

static void configSessionReplyGetMeta(void) {
  char uidHex[HAL_DEVICE_UID_HEX_BUF_SIZE] = {0};
  if(!hal_get_device_uid_hex(uidHex, sizeof(uidHex))) {
    uidHex[0] = '\0';
  }

  char buildB64[32];
  size_t buildB64Len = 0u;
  const uint8_t *b = (const uint8_t *)(BUILD_ID);
  hal_base64_encode(b, strlen((const char *)b), buildB64, sizeof(buildB64),
                    &buildB64Len);

  char response[256] = {0};
  snprintf(response, sizeof(response),
           SC_REPLY_META_FMT,
           SC_MODULE_TOKEN_ECU,
           (unsigned)HAL_SERIAL_SESSION_PROTOCOL_VERSION,
           (unsigned long)configSessionId(),
           FW_VERSION,
           buildB64,
           uidHex[0] != '\0' ? uidHex : HAL_SERIAL_SESSION_UNKNOWN);
  hal_serial_session_println(&s_configSession, response);
}

static bool configSessionHandleScGetParamCommand(const char *line) {
  if(line == NULL) {
    hal_serial_session_println(&s_configSession, SC_STATUS_BAD_REQUEST);
    return true;
  }

  const char *cursor = line + strlen(SC_CMD_GET_PARAM);
  cursor = configSessionSkipSpaces(cursor);
  if(cursor == NULL || cursor[0] == '\0') {
    hal_serial_session_println(&s_configSession,
        SC_STATUS_BAD_REQUEST " expected=" SC_CMD_GET_PARAM "_<param_id>");
    return true;
  }

  char paramId[SC_PARAM_ID_MAX] = {0};
  size_t idLen = 0u;
  while(cursor[idLen] != '\0' && cursor[idLen] != ' ' && idLen + 1u < sizeof(paramId)) {
    paramId[idLen] = cursor[idLen];
    idLen++;
  }
  paramId[idLen] = '\0';

  if(cursor[idLen] != '\0' && cursor[idLen] != ' ') {
    hal_serial_session_println(&s_configSession,
        SC_STATUS_BAD_REQUEST " param_id_too_long");
    return true;
  }

  cursor += idLen;
  cursor = configSessionSkipSpaces(cursor);
  if(cursor == NULL || cursor[0] != '\0') {
    hal_serial_session_println(&s_configSession,
        SC_STATUS_BAD_REQUEST " expected=" SC_CMD_GET_PARAM "_<param_id>");
    return true;
  }

  sc_param_reply_get_param(k_ecu_params, k_ecu_params_count, &s_active,
                           paramId,
                           configSessionEmitThroughHal, &s_configSession);
  return true;
}

static bool configSessionHandleScCommand(const char *line) {
  if(line == NULL || strncmp(line, "SC_", 3u) != 0) {
    return false;
  }

  if(!configSessionActive()) {
    hal_serial_session_println(&s_configSession,
        SC_REPLY_NOT_READY_HELLO_REQUIRED);
    return true;
  }

  if(strcmp(line, SC_CMD_GET_META) == 0) {
    configSessionReplyGetMeta();
    return true;
  }

  if(strcmp(line, SC_CMD_GET_PARAM_LIST) == 0) {
    sc_param_reply_get_param_list(k_ecu_params, k_ecu_params_count,
                                  configSessionEmitThroughHal,
                                  &s_configSession);
    return true;
  }

  if(strcmp(line, SC_CMD_GET_VALUES) == 0) {
    sc_param_reply_get_values_i16(k_ecu_params, k_ecu_params_count,
                                  ecuParamsActive(),
                                  configSessionEmitThroughHal,
                                  &s_configSession);
    return true;
  }

  if(strncmp(line, SC_CMD_GET_PARAM, strlen(SC_CMD_GET_PARAM)) == 0) {
    return configSessionHandleScGetParamCommand(line);
  }

  hal_serial_session_println(&s_configSession, SC_STATUS_UNKNOWN_CMD);
  return true;
}

static void configSession_onUnknownLine(const char *line, void *user) {
  (void)user;
  if(configSessionHandleScCommand(line)) {
    return;
  }

  /* Forward non-protocol command lines (PID tuning, etc.) to test fixtures.
   * This keeps the bootstrap session parser as the sole consumer of raw
   * serial bytes; secondary consumers receive whole lines only after HELLO
   * and friends have been handled. */
  tickTestsHandleSerialLine(line);
}

void configSessionInit(void) {
  hal_serial_session_init_with_vocabulary(&s_configSession, SC_MODULE_TOKEN_ECU,
                                          FW_VERSION, BUILD_ID,
                                          &fiesta_default_vocabulary);
  hal_serial_session_set_unknown_handler(&s_configSession,
                                         &configSession_onUnknownLine,
                                         NULL);
}

void configSessionTick(void) {
  hal_serial_session_poll(&s_configSession);
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_configSession);
}

uint32_t configSessionId(void) {
  return hal_serial_session_id(&s_configSession);
}
