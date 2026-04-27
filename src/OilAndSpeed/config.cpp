#include "config.h"

#include <cstdio>
#include <cstring>
#include <hal/hal_serial_session.h>
#include <hal/hal_crypto.h>

#include "../common/scDefinitions/sc_param_handlers.h"
#include "../common/scDefinitions/sc_param_types.h"
#include "../common/scDefinitions/sc_protocol.h"
#include "../common/scDefinitions/sc_session_vocabulary.h"

static hal_serial_session_t s_configSession;

/* Read-only parameter catalog. Compile-time intervals exposed for the
 * configurator host catalog browser. min/max are the validation bounds
 * that would apply if/when these become writable. */
typedef struct {
  int16_t oil_pressure_read_interval_ms;
  int16_t thermocouple_read_interval_ms;
} oas_values_t;

static const oas_values_t k_oas_values = {
  .oil_pressure_read_interval_ms = (int16_t)OIL_PRESSURE_READ_INTERVAL,
  .thermocouple_read_interval_ms = (int16_t)THERMOCOUPLE_READ_INTERVAL,
};

static const sc_param_descriptor_t k_oas_params[] = {
  SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("oil_pressure_read_interval_ms",
                                       oas_values_t,
                                       oil_pressure_read_interval_ms,
                                       50, 500,
                                       (int16_t)OIL_PRESSURE_READ_INTERVAL, 1),
  SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("thermocouple_read_interval_ms",
                                       oas_values_t,
                                       thermocouple_read_interval_ms,
                                       500, 5000,
                                       (int16_t)THERMOCOUPLE_READ_INTERVAL, 1),
};
static const size_t k_oas_params_count =
    sizeof(k_oas_params) / sizeof(k_oas_params[0]);

static const char *configSessionSkipSpaces(const char *cursor) {
  if(cursor == nullptr) {
    return nullptr;
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
           MODULE_NAME,
           (unsigned)HAL_SERIAL_SESSION_PROTOCOL_VERSION,
           (unsigned long)configSessionId(),
           FW_VERSION,
           buildB64,
           uidHex[0] != '\0' ? uidHex : HAL_SERIAL_SESSION_UNKNOWN);
  hal_serial_session_println(&s_configSession, response);
}

static bool configSessionHandleScGetParamCommand(const char *line) {
  if(line == nullptr) {
    hal_serial_session_println(&s_configSession, SC_STATUS_BAD_REQUEST);
    return true;
  }

  const char *cursor = line + strlen(SC_CMD_GET_PARAM);
  cursor = configSessionSkipSpaces(cursor);
  if(cursor == nullptr || cursor[0] == '\0') {
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
  if(cursor == nullptr || cursor[0] != '\0') {
    hal_serial_session_println(&s_configSession,
        SC_STATUS_BAD_REQUEST " expected=" SC_CMD_GET_PARAM "_<param_id>");
    return true;
  }

  sc_param_reply_get_param(k_oas_params, k_oas_params_count,
                           &k_oas_values, paramId,
                           configSessionEmitThroughHal, &s_configSession);
  return true;
}

static bool configSessionHandleScCommand(const char *line) {
  if(line == nullptr || strncmp(line, "SC_", 3u) != 0) {
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
    sc_param_reply_get_param_list(k_oas_params, k_oas_params_count,
                                  configSessionEmitThroughHal,
                                  &s_configSession);
    return true;
  }

  if(strcmp(line, SC_CMD_GET_VALUES) == 0) {
    sc_param_reply_get_values_i16(k_oas_params, k_oas_params_count,
                                  &k_oas_values,
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

  hal_serial_session_println(&s_configSession, "ERR UNKNOWN");
}

void configSessionInit(void) {
  hal_serial_session_init_with_vocabulary(&s_configSession, MODULE_NAME,
                                          FW_VERSION, BUILD_ID,
                                          &fiesta_default_vocabulary);
  hal_serial_session_set_unknown_handler(&s_configSession,
                                         &configSession_onUnknownLine,
                                         nullptr);
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
