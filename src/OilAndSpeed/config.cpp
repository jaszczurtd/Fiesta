#include "config.h"

#include <cstdio>
#include <cstring>
#include <hal/hal_serial_session.h>

static const char *SC_STATUS_OK = "SC_OK";
static const char *SC_STATUS_UNKNOWN_CMD = "SC_UNKNOWN_CMD";
static const char *SC_STATUS_BAD_REQUEST = "SC_BAD_REQUEST";
static const char *SC_STATUS_NOT_READY = "SC_NOT_READY";
static const char *SC_STATUS_INVALID_PARAM_ID = "SC_INVALID_PARAM_ID";

static hal_serial_session_t s_configSession;

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

  char response[256] = {0};
  snprintf(response,
           sizeof(response),
           "%s META module=%s proto=%u session=%lu fw=%s build=%s uid=%s",
           SC_STATUS_OK,
           MODULE_NAME,
           (unsigned)HAL_SERIAL_SESSION_PROTOCOL_VERSION,
           (unsigned long)configSessionId(),
           FW_VERSION,
           BUILD_ID,
           uidHex[0] != '\0' ? uidHex : HAL_SERIAL_SESSION_UNKNOWN);
  hal_serial_println(response);
}

static void configSessionReplyGetParamList(void) {
  hal_serial_println("SC_OK PARAM_LIST");
}

static void configSessionReplyGetValues(void) {
  hal_serial_println("SC_OK PARAM_VALUES");
}

static bool configSessionHandleScGetParamCommand(const char *line) {
  if(line == nullptr) {
    hal_serial_println(SC_STATUS_BAD_REQUEST);
    return true;
  }

  const char *cursor = line + strlen("SC_GET_PARAM");
  cursor = configSessionSkipSpaces(cursor);
  if(cursor == nullptr || cursor[0] == '\0') {
    hal_serial_println("SC_BAD_REQUEST expected=SC_GET_PARAM_<param_id>");
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
    hal_serial_println("SC_BAD_REQUEST param_id_too_long");
    return true;
  }

  cursor += idLen;
  cursor = configSessionSkipSpaces(cursor);
  if(cursor == nullptr || cursor[0] != '\0') {
    hal_serial_println("SC_BAD_REQUEST expected=SC_GET_PARAM_<param_id>");
    return true;
  }

  char response[96] = {0};
  snprintf(response, sizeof(response), "%s id=%s", SC_STATUS_INVALID_PARAM_ID, paramId);
  hal_serial_println(response);
  return true;
}

static bool configSessionHandleScCommand(const char *line) {
  if(line == nullptr || strncmp(line, "SC_", 3u) != 0) {
    return false;
  }

  if(!configSessionActive()) {
    hal_serial_println("SC_NOT_READY HELLO_REQUIRED");
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

  hal_serial_println(SC_STATUS_UNKNOWN_CMD);
  return true;
}

static void configSession_onUnknownLine(const char *line, void *user) {
  (void)user;

  if(configSessionHandleScCommand(line)) {
    return;
  }

  hal_serial_println("ERR UNKNOWN");
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
