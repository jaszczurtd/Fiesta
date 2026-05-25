#include "config.h"

#include "RTC.h"

#include <hal/hal_crypto.h>
#include <hal/hal_serial_session.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "../common/scDefinitions/sc_param_handlers.h"
#include "../common/scDefinitions/sc_param_types.h"
#include "../common/scDefinitions/sc_protocol.h"
#include "../common/scDefinitions/sc_session_vocabulary.h"

typedef struct {
    int16_t rtc_year;
    int16_t rtc_month;
    int16_t rtc_day;
    int16_t rtc_hour;
    int16_t rtc_minute;
    int16_t rtc_second;
    int16_t rtc_integrity;
} clock_values_t;

static const sc_param_descriptor_t k_clock_params[] = {
    SC_PARAM_SCALAR_I16("rtc_year", clock_values_t, rtc_year,
                        2000, 2099, 2026, 1, "rtc"),
    SC_PARAM_SCALAR_I16("rtc_month", clock_values_t, rtc_month,
                        1, 12, 1, 1, "rtc"),
    SC_PARAM_SCALAR_I16("rtc_day", clock_values_t, rtc_day,
                        1, 31, 1, 1, "rtc"),
    SC_PARAM_SCALAR_I16("rtc_hour", clock_values_t, rtc_hour,
                        0, 23, 0, 1, "rtc"),
    SC_PARAM_SCALAR_I16("rtc_minute", clock_values_t, rtc_minute,
                        0, 59, 0, 1, "rtc"),
    SC_PARAM_SCALAR_I16("rtc_second", clock_values_t, rtc_second,
                        0, 59, 0, 1, "rtc"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("rtc_integrity", clock_values_t,
                                         rtc_integrity,
                                         0, 1, 1, 1, "rtc"),
};

static const size_t k_clock_params_count = COUNTOF(k_clock_params);

static hal_serial_session_t s_configSession;

static clock_values_t s_active = {
    .rtc_year = 2026,
    .rtc_month = 1,
    .rtc_day = 1,
    .rtc_hour = 0,
    .rtc_minute = 0,
    .rtc_second = 0,
    .rtc_integrity = 0,
};

static clock_values_t s_staging = {
    .rtc_year = 2026,
    .rtc_month = 1,
    .rtc_day = 1,
    .rtc_hour = 0,
    .rtc_minute = 0,
    .rtc_second = 0,
    .rtc_integrity = 0,
};

static bool s_stagingDirty = false;

static bool isLeapYear(int32_t year) {
    if((year % 400) == 0) {
        return true;
    }
    if((year % 100) == 0) {
        return false;
    }
    return ((year % 4) == 0);
}

static int32_t daysInMonth(int32_t year, int32_t month) {
    static const int32_t k_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if(month < 1 || month > 12) {
        return 0;
    }

    if(month == 2 && isLeapYear(year)) {
        return 29;
    }

    return k_days[month - 1];
}

static bool clockValuesValidate(const clock_values_t *values,
                                const char **reason) {
    if(reason != NULL) {
        *reason = NULL;
    }

    if(values == NULL) {
        if(reason != NULL) {
            *reason = "null";
        }
        return false;
    }

    if(values->rtc_year < 2000 || values->rtc_year > 2099) {
        if(reason != NULL) {
            *reason = "year_range";
        }
        return false;
    }

    if(values->rtc_month < 1 || values->rtc_month > 12) {
        if(reason != NULL) {
            *reason = "month_range";
        }
        return false;
    }

    const int32_t maxDay = daysInMonth(values->rtc_year, values->rtc_month);
    if(values->rtc_day < 1 || values->rtc_day > maxDay) {
        if(reason != NULL) {
            *reason = "day_for_month";
        }
        return false;
    }

    if(values->rtc_hour < 0 || values->rtc_hour > 23) {
        if(reason != NULL) {
            *reason = "hour_range";
        }
        return false;
    }

    if(values->rtc_minute < 0 || values->rtc_minute > 59) {
        if(reason != NULL) {
            *reason = "minute_range";
        }
        return false;
    }

    if(values->rtc_second < 0 || values->rtc_second > 59) {
        if(reason != NULL) {
            *reason = "second_range";
        }
        return false;
    }

    return true;
}

static uint8_t weekdayFromDate(int32_t year, int32_t month, int32_t day) {
    static const int32_t k_offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int32_t y = year;

    if(month < 3) {
        y -= 1;
    }

    const int32_t sundayZero =
        (y + y / 4 - y / 100 + y / 400 + k_offsets[month - 1] + day) % 7;

    return (uint8_t)((sundayZero + 6) % 7); /* 0 = Monday, ..., 6 = Sunday */
}

static bool rtcDateTimeIsValid(const PCF_DateTime *dt) {
    if(dt == NULL) {
        return false;
    }

    if(dt->year < 1900 || dt->year > 2099) {
        return false;
    }
    if(dt->month < 1u || dt->month > 12u) {
        return false;
    }
    if(dt->day < 1u || dt->day > 31u) {
        return false;
    }
    if(dt->hour > 23u || dt->minute > 59u || dt->second > 59u) {
        return false;
    }
    return true;
}

static bool clockValuesLoadFromRtc(clock_values_t *outValues) {
    if(outValues == NULL) {
        return false;
    }

    PCF_DateTime dt = {0};
    const unsigned char dtResult = PCF_GetDateTime(&dt);
    if(!rtcDateTimeIsValid(&dt)) {
        return false;
    }

    bool integrityOk = (dtResult == 0u);
    if(PCF_GetClockIntegrity(&integrityOk) != 0u) {
        integrityOk = (dtResult == 0u);
    }

    outValues->rtc_year = (int16_t)dt.year;
    outValues->rtc_month = (int16_t)dt.month;
    outValues->rtc_day = (int16_t)dt.day;
    outValues->rtc_hour = (int16_t)dt.hour;
    outValues->rtc_minute = (int16_t)dt.minute;
    outValues->rtc_second = (int16_t)dt.second;
    outValues->rtc_integrity = integrityOk ? 1 : 0;
    return true;
}

static void clockValuesRefreshActiveFromRtc(void) {
    clock_values_t refreshed = s_active;
    if(clockValuesLoadFromRtc(&refreshed)) {
        s_active = refreshed;
        if(!s_stagingDirty) {
            s_staging = s_active;
        }
    }
}

static bool clockValuesApplyToRtc(const clock_values_t *values) {
    const char *reason = NULL;
    if(!clockValuesValidate(values, &reason)) {
        (void)reason;
        return false;
    }

    PCF_DateTime dt = {0};
    dt.year = values->rtc_year;
    dt.month = (unsigned char)values->rtc_month;
    dt.day = (unsigned char)values->rtc_day;
    dt.hour = (unsigned char)values->rtc_hour;
    dt.minute = (unsigned char)values->rtc_minute;
    dt.second = (unsigned char)values->rtc_second;
    dt.weekday = weekdayFromDate(values->rtc_year,
                                 values->rtc_month,
                                 values->rtc_day);

    return (PCF_SetDateTime(&dt) == 0u);
}

static const char *configSessionSkipSpaces(const char *cursor) {
    if(cursor == NULL) {
        return NULL;
    }
    while(*cursor == ' ') {
        cursor++;
    }
    return cursor;
}

static void configSessionEmitThroughHal(const char *payload, void *user) {
    hal_serial_session_println((hal_serial_session_t *)user, payload);
}

static void configSessionReplyGetMeta(void) {
    char uidHex[HAL_DEVICE_UID_HEX_BUF_SIZE] = {0};
    if(!hal_get_device_uid_hex(uidHex, sizeof(uidHex))) {
        uidHex[0] = '\0';
    }

    char buildB64[32] = {0};
    size_t buildB64Len = 0u;
    const uint8_t *b = (const uint8_t *)(BUILD_ID);
    hal_base64_encode(b, strlen((const char *)b),
                      buildB64, sizeof(buildB64), &buildB64Len);

    char response[256] = {0};
    snprintf(response, sizeof(response), SC_REPLY_META_FMT,
             SC_MODULE_TOKEN_CLOCK,
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
    while(cursor[idLen] != '\0' && cursor[idLen] != ' '
          && idLen + 1u < sizeof(paramId)) {
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

    clockValuesRefreshActiveFromRtc();
    sc_param_reply_get_param(k_clock_params, k_clock_params_count, &s_active,
                             paramId, configSessionEmitThroughHal,
                             &s_configSession);
    return true;
}

static bool configSessionHandleScSetParamCommand(const char *line) {
    if(!hal_serial_session_is_authenticated(&s_configSession)) {
        hal_serial_session_println(&s_configSession, SC_STATUS_NOT_AUTHORIZED);
        return true;
    }

    const char *cursor = line + strlen(SC_CMD_SET_PARAM);
    cursor = configSessionSkipSpaces(cursor);
    if(cursor == NULL || cursor[0] == '\0') {
        hal_serial_session_println(&s_configSession,
            SC_STATUS_BAD_REQUEST " expected=" SC_CMD_SET_PARAM " <param_id> <value>");
        return true;
    }

    char paramId[SC_PARAM_ID_MAX] = {0};
    size_t idLen = 0u;
    while(cursor[idLen] != '\0' && cursor[idLen] != ' '
          && idLen + 1u < sizeof(paramId)) {
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
    if(cursor == NULL || cursor[0] == '\0') {
        hal_serial_session_println(&s_configSession,
            SC_STATUS_BAD_REQUEST " expected=" SC_CMD_SET_PARAM " <param_id> <value>");
        return true;
    }

    char *endptr = NULL;
    const long parsed = strtol(cursor, &endptr, 10);
    if(endptr == cursor || parsed < INT16_MIN || parsed > INT16_MAX) {
        hal_serial_session_println(&s_configSession,
            SC_STATUS_BAD_REQUEST " value_not_int16");
        return true;
    }

    const char *tail = configSessionSkipSpaces(endptr);
    if(tail != NULL && tail[0] != '\0') {
        hal_serial_session_println(&s_configSession,
            SC_STATUS_BAD_REQUEST " expected=" SC_CMD_SET_PARAM " <param_id> <value>");
        return true;
    }

    if(sc_param_reply_set_param(k_clock_params, k_clock_params_count,
                                &s_staging, &s_active,
                                paramId, (int16_t)parsed,
                                configSessionEmitThroughHal,
                                &s_configSession)) {
        s_stagingDirty = true;
    }
    return true;
}

static bool configSessionHandleScCommitParamsCommand(void) {
    if(!hal_serial_session_is_authenticated(&s_configSession)) {
        hal_serial_session_println(&s_configSession, SC_STATUS_NOT_AUTHORIZED);
        return true;
    }

    const char *reason = NULL;
    if(!clockValuesValidate(&s_staging, &reason)) {
        char response[96] = {0};
        snprintf(response, sizeof(response), SC_REPLY_COMMIT_FAILED_FMT,
                 (reason != NULL) ? reason : "invalid_datetime");
        hal_serial_session_println(&s_configSession, response);
        return true;
    }

    if(!clockValuesApplyToRtc(&s_staging)) {
        hal_serial_session_println(&s_configSession,
            SC_STATUS_COMMIT_FAILED " reason=rtc_set_failed");
        return true;
    }

    const size_t count = sc_param_copy_staging_to_active(
        k_clock_params, k_clock_params_count, &s_staging, &s_active);

    s_stagingDirty = false;
    clockValuesRefreshActiveFromRtc();
    s_staging = s_active;

    char response[64] = {0};
    snprintf(response, sizeof(response), SC_REPLY_PARAMS_COMMITTED_FMT,
             (unsigned)count);
    hal_serial_session_println(&s_configSession, response);
    return true;
}

static bool configSessionHandleScRevertParamsCommand(void) {
    if(!hal_serial_session_is_authenticated(&s_configSession)) {
        hal_serial_session_println(&s_configSession, SC_STATUS_NOT_AUTHORIZED);
        return true;
    }

    clockValuesRefreshActiveFromRtc();
    (void)sc_param_copy_active_to_staging(
        k_clock_params, k_clock_params_count, &s_active, &s_staging);
    s_stagingDirty = false;
    hal_serial_session_println(&s_configSession, SC_REPLY_PARAMS_REVERTED);
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
        sc_param_reply_get_param_list(k_clock_params, k_clock_params_count,
                                      configSessionEmitThroughHal,
                                      &s_configSession);
        return true;
    }

    if(strcmp(line, SC_CMD_GET_VALUES) == 0) {
        clockValuesRefreshActiveFromRtc();
        sc_param_reply_get_values_i16(k_clock_params, k_clock_params_count,
                                      &s_active,
                                      configSessionEmitThroughHal,
                                      &s_configSession);
        return true;
    }

    if(strcmp(line, SC_CMD_COMMIT_PARAMS) == 0) {
        return configSessionHandleScCommitParamsCommand();
    }

    if(strcmp(line, SC_CMD_REVERT_PARAMS) == 0) {
        return configSessionHandleScRevertParamsCommand();
    }

    if(strncmp(line, SC_CMD_SET_PARAM, strlen(SC_CMD_SET_PARAM)) == 0) {
        return configSessionHandleScSetParamCommand(line);
    }

    if(strncmp(line, SC_CMD_GET_PARAM, strlen(SC_CMD_GET_PARAM)) == 0) {
        return configSessionHandleScGetParamCommand(line);
    }

    hal_serial_session_println(&s_configSession, SC_STATUS_UNKNOWN_CMD);
    return true;
}

static void configSessionOnUnknownLine(const char *line, void *user) {
    (void)user;
    if(configSessionHandleScCommand(line)) {
        return;
    }

    hal_serial_session_println(&s_configSession, "ERR UNKNOWN");
}

void configSessionInit(void) {
    clockValuesRefreshActiveFromRtc();
    s_staging = s_active;
    s_stagingDirty = false;

    hal_serial_session_init_with_vocabulary(&s_configSession,
                                            SC_MODULE_TOKEN_CLOCK,
                                            FW_VERSION, BUILD_ID,
                                            &fiesta_default_vocabulary);
    hal_serial_session_set_unknown_handler(&s_configSession,
                                           &configSessionOnUnknownLine,
                                           NULL);
}

void configSessionTick(void) {
    hal_serial_session_poll(&s_configSession);
    hal_debug_set_muted(hal_serial_session_is_active(&s_configSession));
}

bool configSessionActive(void) {
    return hal_serial_session_is_active(&s_configSession);
}

uint32_t configSessionId(void) {
    return hal_serial_session_id(&s_configSession);
}
