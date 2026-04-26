#include "config.h"

#include <cstdio>
#include <cstring>
#include <hal/hal_serial_session.h>
#include <hal/hal_crypto.h>

static const char *SC_STATUS_OK = "SC_OK";
static const char *SC_STATUS_UNKNOWN_CMD = "SC_UNKNOWN_CMD";
static const char *SC_STATUS_BAD_REQUEST = "SC_BAD_REQUEST";
static const char *SC_STATUS_NOT_READY = "SC_NOT_READY";
static const char *SC_STATUS_INVALID_PARAM_ID = "SC_INVALID_PARAM_ID";

static hal_serial_session_t s_configSession;

/* Read-only parameter catalog for the configurator host.
 *
 * These are compile-time thresholds today (no runtime calibration layer
 * on Clocks yet, unlike ECU's `ecuParams`). They are exposed read-only so
 * the desktop catalog browser is meaningful for this module; min/max are
 * the validation bounds that would apply if/when this becomes writable.
 */
typedef int16_t (*clocks_sc_param_getter_t)(void);

static int16_t configSessionParamCoolantWarn(void) { return (int16_t)TEMP_OK_HI; }
static int16_t configSessionParamCoolantMax(void)  { return (int16_t)TEMP_MAX; }
static int16_t configSessionParamOilWarn(void)     { return (int16_t)TEMP_OIL_OK_HI; }
static int16_t configSessionParamOilMax(void)      { return (int16_t)TEMP_OIL_MAX; }
static int16_t configSessionParamEgtWarn(void)     { return (int16_t)TEMP_EGT_OK_HI; }
static int16_t configSessionParamEgtMax(void)      { return (int16_t)TEMP_EGT_MAX; }

struct ClocksScParam {
  const char *id;
  clocks_sc_param_getter_t getter;
  int16_t defaultValue;
  int16_t minValue;
  int16_t maxValue;
};

static const ClocksScParam s_scParams[] = {
  { "coolant_warn_c", &configSessionParamCoolantWarn, (int16_t)TEMP_OK_HI,     80,  120 },
  { "coolant_max_c",  &configSessionParamCoolantMax,  (int16_t)TEMP_MAX,      100,  140 },
  { "oil_warn_c",     &configSessionParamOilWarn,     (int16_t)TEMP_OIL_OK_HI, 90,  130 },
  { "oil_max_c",      &configSessionParamOilMax,      (int16_t)TEMP_OIL_MAX,  120,  160 },
  { "egt_warn_c",     &configSessionParamEgtWarn,     (int16_t)TEMP_EGT_OK_HI,700, 1100 },
  { "egt_max_c",      &configSessionParamEgtMax,      (int16_t)TEMP_EGT_MAX, 1300, 1800 },
};
static const size_t s_scParamCount = sizeof(s_scParams) / sizeof(s_scParams[0]);

static const ClocksScParam *configSessionFindParamById(const char *id) {
  if(id == nullptr || id[0] == '\0') {
    return nullptr;
  }
  for(size_t i = 0u; i < s_scParamCount; ++i) {
    if(strcmp(s_scParams[i].id, id) == 0) {
      return &s_scParams[i];
    }
  }
  return nullptr;
}

static const char *configSessionSkipSpaces(const char *cursor) {
  if(cursor == nullptr) {
    return nullptr;
  }

  while(*cursor == ' ') {
    cursor++;
  }
  return cursor;
}

static void configSessionReplyGetMeta(void) {
  char uidHex[HAL_DEVICE_UID_HEX_BUF_SIZE] = {0};
  if(!hal_get_device_uid_hex(uidHex, sizeof(uidHex))) {
    uidHex[0] = '\0';
  }

  char out[32];
  size_t out_len = 0u;
  const uint8_t *b = (const uint8_t *)(BUILD_ID);

  hal_base64_encode(b, strlen((const char*)b), out, sizeof(out), &out_len);

  char response[256] = {0};
  snprintf(response,
           sizeof(response),
           "%s META module=%s proto=%u session=%lu fw=%s build=%s uid=%s",
           SC_STATUS_OK,
           MODULE_NAME,
           (unsigned)HAL_SERIAL_SESSION_PROTOCOL_VERSION,
           (unsigned long)configSessionId(),
           FW_VERSION,
           out,
           uidHex[0] != '\0' ? uidHex : HAL_SERIAL_SESSION_UNKNOWN);
  hal_serial_session_println(&s_configSession, response);
}

static void configSessionReplyGetParamList(void) {
  char response[256] = {0};
  size_t used = (size_t)snprintf(response, sizeof(response), "%s PARAM_LIST", SC_STATUS_OK);
  if(used >= sizeof(response)) {
    response[sizeof(response) - 1u] = '\0';
    hal_serial_session_println(&s_configSession, response);
    return;
  }

  for(size_t i = 0u; i < s_scParamCount; ++i) {
    const char *separator = (i == 0u) ? " " : ",";
    int written = snprintf(response + used,
                           sizeof(response) - used,
                           "%s%s",
                           separator,
                           s_scParams[i].id);
    if(written < 0) {
      break;
    }
    size_t chunk = (size_t)written;
    if(chunk >= (sizeof(response) - used)) {
      used = sizeof(response) - 1u;
      break;
    }
    used += chunk;
  }

  response[sizeof(response) - 1u] = '\0';
  hal_serial_session_println(&s_configSession, response);
}

static void configSessionReplyGetParamValue(const ClocksScParam *param) {
  if(param == nullptr || param->getter == nullptr) {
    hal_serial_session_println(&s_configSession, SC_STATUS_BAD_REQUEST);
    return;
  }
  char response[160] = {0};
  snprintf(response,
           sizeof(response),
           "%s PARAM id=%s value=%d min=%d max=%d default=%d",
           SC_STATUS_OK,
           param->id,
           (int)param->getter(),
           (int)param->minValue,
           (int)param->maxValue,
           (int)param->defaultValue);
  hal_serial_session_println(&s_configSession, response);
}

static void configSessionReplyGetValues(void) {
  char response[256] = {0};
  size_t used = (size_t)snprintf(response, sizeof(response), "%s PARAM_VALUES", SC_STATUS_OK);
  if(used >= sizeof(response)) {
    response[sizeof(response) - 1u] = '\0';
    hal_serial_session_println(&s_configSession, response);
    return;
  }

  for(size_t i = 0u; i < s_scParamCount; ++i) {
    int written = snprintf(response + used,
                           sizeof(response) - used,
                           " %s=%d",
                           s_scParams[i].id,
                           (int)s_scParams[i].getter());
    if(written < 0) {
      break;
    }
    size_t chunk = (size_t)written;
    if(chunk >= (sizeof(response) - used)) {
      used = sizeof(response) - 1u;
      break;
    }
    used += chunk;
  }

  response[sizeof(response) - 1u] = '\0';
  hal_serial_session_println(&s_configSession, response);
}

static bool configSessionHandleScGetParamCommand(const char *line) {
  if(line == nullptr) {
    hal_serial_session_println(&s_configSession, SC_STATUS_BAD_REQUEST);
    return true;
  }

  const char *cursor = line + strlen("SC_GET_PARAM");
  cursor = configSessionSkipSpaces(cursor);
  if(cursor == nullptr || cursor[0] == '\0') {
    hal_serial_session_println(&s_configSession, "SC_BAD_REQUEST expected=SC_GET_PARAM_<param_id>");
    return true;
  }

  char paramId[32] = {0};
  size_t idLen = 0u;
  while(cursor[idLen] != '\0' && cursor[idLen] != ' ' && idLen + 1u < sizeof(paramId)) {
    paramId[idLen] = cursor[idLen];
    idLen++;
  }
  paramId[idLen] = '\0';

  if(cursor[idLen] != '\0' && cursor[idLen] != ' ') {
    hal_serial_session_println(&s_configSession, "SC_BAD_REQUEST param_id_too_long");
    return true;
  }

  cursor += idLen;
  cursor = configSessionSkipSpaces(cursor);
  if(cursor == nullptr || cursor[0] != '\0') {
    hal_serial_session_println(&s_configSession, "SC_BAD_REQUEST expected=SC_GET_PARAM_<param_id>");
    return true;
  }

  const ClocksScParam *param = configSessionFindParamById(paramId);
  if(param == nullptr) {
    char response[96] = {0};
    snprintf(response, sizeof(response), "%s id=%s", SC_STATUS_INVALID_PARAM_ID, paramId);
    hal_serial_session_println(&s_configSession, response);
    return true;
  }

  configSessionReplyGetParamValue(param);
  return true;
}

static bool configSessionHandleScCommand(const char *line) {
  if(line == nullptr || strncmp(line, "SC_", 3u) != 0) {
    return false;
  }

  if(!configSessionActive()) {
    hal_serial_session_println(&s_configSession, "SC_NOT_READY HELLO_REQUIRED");
    return true;
  }

  if(strcmp(line, "SC_GET_META") == 0) {
    configSessionReplyGetMeta();
    return true;
  }

  if(strcmp(line, "SC_GET_PARAM_LIST") == 0) {
    configSessionReplyGetParamList();
    return true;
  }

  if(strcmp(line, "SC_GET_VALUES") == 0) {
    configSessionReplyGetValues();
    return true;
  }

  if(strncmp(line, "SC_GET_PARAM", strlen("SC_GET_PARAM")) == 0) {
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
  hal_serial_session_init(&s_configSession, MODULE_NAME, FW_VERSION, BUILD_ID);
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
