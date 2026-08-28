#include "config.h"

#include "RTC.h"

#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_serial_commands.h>
#include <hal/serial/hal_serial_session.h>

#include "../common/scDefinitions/sc_command_handlers.h"
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
    SC_PARAM_SCALAR_I16("rtc_year", clock_values_t, rtc_year, 2000, 2099, 2026,
                        1, "rtc"),
    SC_PARAM_SCALAR_I16("rtc_month", clock_values_t, rtc_month, 1, 12, 1, 1,
                        "rtc"),
    SC_PARAM_SCALAR_I16("rtc_day", clock_values_t, rtc_day, 1, 31, 1, 1, "rtc"),
    SC_PARAM_SCALAR_I16("rtc_hour", clock_values_t, rtc_hour, 0, 23, 0, 1,
                        "rtc"),
    SC_PARAM_SCALAR_I16("rtc_minute", clock_values_t, rtc_minute, 0, 59, 0, 1,
                        "rtc"),
    SC_PARAM_SCALAR_I16("rtc_second", clock_values_t, rtc_second, 0, 59, 0, 1,
                        "rtc"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("rtc_integrity", clock_values_t,
                                         rtc_integrity, 0, 1, 1, 1, "rtc"),
};

static const size_t k_clock_params_count = COUNTOF(k_clock_params);

static hal_serial_session_t s_configSession;
static hal_serial_commands_t s_serialCommands;
static sc_command_service_t s_commandService;

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
  if ((year % 400) == 0) {
    return true;
  }
  if ((year % 100) == 0) {
    return false;
  }
  return ((year % 4) == 0);
}

static int32_t daysInMonth(int32_t year, int32_t month) {
  static const int32_t k_days[12] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};

  if (month < 1 || month > 12) {
    return 0;
  }

  if (month == 2 && isLeapYear(year)) {
    return 29;
  }

  return k_days[month - 1];
}

static bool clockValuesValidate(const clock_values_t *values,
                                const char **reason) {
  if (reason != NULL) {
    *reason = NULL;
  }

  if (values == NULL) {
    if (reason != NULL) {
      *reason = "null";
    }
    return false;
  }

  if (values->rtc_year < 2000 || values->rtc_year > 2099) {
    if (reason != NULL) {
      *reason = "year_range";
    }
    return false;
  }

  if (values->rtc_month < 1 || values->rtc_month > 12) {
    if (reason != NULL) {
      *reason = "month_range";
    }
    return false;
  }

  const int32_t maxDay = daysInMonth(values->rtc_year, values->rtc_month);
  if (values->rtc_day < 1 || values->rtc_day > maxDay) {
    if (reason != NULL) {
      *reason = "day_for_month";
    }
    return false;
  }

  if (values->rtc_hour < 0 || values->rtc_hour > 23) {
    if (reason != NULL) {
      *reason = "hour_range";
    }
    return false;
  }

  if (values->rtc_minute < 0 || values->rtc_minute > 59) {
    if (reason != NULL) {
      *reason = "minute_range";
    }
    return false;
  }

  if (values->rtc_second < 0 || values->rtc_second > 59) {
    if (reason != NULL) {
      *reason = "second_range";
    }
    return false;
  }

  return true;
}

static uint8_t weekdayFromDate(int32_t year, int32_t month, int32_t day) {
  static const int32_t k_offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int32_t y = year;

  if (month < 3) {
    y -= 1;
  }

  const int32_t sundayZero =
      (y + y / 4 - y / 100 + y / 400 + k_offsets[month - 1] + day) % 7;

  return (uint8_t)((sundayZero + 6) % 7); /* 0 = Monday, ..., 6 = Sunday */
}

static bool rtcDateTimeIsValid(const PCF_DateTime *dt) {
  if (dt == NULL) {
    return false;
  }

  if (dt->year < 1900 || dt->year > 2099) {
    return false;
  }
  if (dt->month < 1u || dt->month > 12u) {
    return false;
  }
  if (dt->day < 1u || dt->day > 31u) {
    return false;
  }
  if (dt->hour > 23u || dt->minute > 59u || dt->second > 59u) {
    return false;
  }
  return true;
}

static bool clockValuesLoadFromRtc(clock_values_t *outValues) {
  if (outValues == NULL) {
    return false;
  }

  PCF_DateTime dt = {0};
  const unsigned char dtResult = PCF_GetDateTime(&dt);
  if (!rtcDateTimeIsValid(&dt)) {
    return false;
  }

  bool integrityOk = (dtResult == 0u);
  if (PCF_GetClockIntegrity(&integrityOk) != 0u) {
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
  if (clockValuesLoadFromRtc(&refreshed)) {
    s_active = refreshed;
    if (!s_stagingDirty) {
      s_staging = s_active;
    }
  }
}

static bool clockValuesApplyToRtc(const clock_values_t *values) {
  const char *reason = NULL;
  if (!clockValuesValidate(values, &reason)) {
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
  dt.weekday =
      weekdayFromDate(values->rtc_year, values->rtc_month, values->rtc_day);

  return (PCF_SetDateTime(&dt) == 0u);
}

static void configSessionRefresh(void *user) {
  (void)user;
  clockValuesRefreshActiveFromRtc();
}

static void configSessionSetApplied(void *user) {
  (void)user;
  s_stagingDirty = true;
}

static hal_status_t configSessionCommit(void *user, const char **outReason,
                                        size_t *outCount) {
  (void)user;
  if (outReason == NULL || outCount == NULL) {
    return HAL_EINVAL;
  }
  *outReason = NULL;
  *outCount = 0u;

  if (!clockValuesValidate(&s_staging, outReason)) {
    if (*outReason == NULL) {
      *outReason = "invalid_datetime";
    }
    return HAL_EINVAL;
  }
  if (!clockValuesApplyToRtc(&s_staging)) {
    *outReason = "rtc_set_failed";
    return HAL_EIO;
  }

  *outCount = sc_param_copy_staging_to_active(
      k_clock_params, k_clock_params_count, &s_staging, &s_active);
  s_stagingDirty = false;
  clockValuesRefreshActiveFromRtc();
  s_staging = s_active;
  return HAL_OK;
}

static void configSessionRevert(void *user) {
  (void)user;
  clockValuesRefreshActiveFromRtc();
  (void)sc_param_copy_active_to_staging(k_clock_params, k_clock_params_count,
                                        &s_active, &s_staging);
  s_stagingDirty = false;
}

void configSessionInit(void) {
  if (s_serialCommands.initialized) {
    const hal_status_t detachStatus =
        hal_serial_commands_deinit(&s_serialCommands);
    if (detachStatus != HAL_OK) {
      hal_derr("RTC Clock SC adapter detach failed: %s",
               hal_status_to_string(detachStatus));
      return;
    }
  }
  if (s_commandService.initialized) {
    const hal_status_t serviceStatus =
        sc_command_service_deinit(&s_commandService);
    if (serviceStatus != HAL_OK) {
      hal_derr("RTC Clock SC service detach failed: %s",
               hal_status_to_string(serviceStatus));
      return;
    }
  }

  clockValuesRefreshActiveFromRtc();
  s_staging = s_active;
  s_stagingDirty = false;

  hal_serial_session_init_with_vocabulary(&s_configSession,
                                          SC_MODULE_TOKEN_CLOCK, FW_VERSION,
                                          BUILD_ID, &fiesta_default_vocabulary);

  sc_command_service_config_t serviceConfig = {0};
  serviceConfig.module_token = SC_MODULE_TOKEN_CLOCK;
  serviceConfig.firmware_version = FW_VERSION;
  serviceConfig.build_id = BUILD_ID;
  serviceConfig.params = k_clock_params;
  serviceConfig.param_count = k_clock_params_count;
  serviceConfig.active_values = &s_active;
  serviceConfig.staging_values = &s_staging;
  serviceConfig.refresh = configSessionRefresh;
  serviceConfig.set_applied = configSessionSetApplied;
  serviceConfig.commit = configSessionCommit;
  serviceConfig.revert = configSessionRevert;
  serviceConfig.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION);

  hal_status_t status =
      sc_command_service_init(&s_commandService, NULL, &serviceConfig);
  if (status == HAL_OK) {
    hal_serial_commands_config_t adapterConfig =
        hal_serial_commands_config_defaults(&s_configSession);
    adapterConfig.router = sc_command_service_router(&s_commandService);
    adapterConfig.command_prefix = SC_COMMAND_PREFIX;
    adapterConfig.formatter = sc_command_format_serial_response;
    adapterConfig.allow_inactive = sc_command_allow_inactive_reboot;
    adapterConfig.fallback = sc_command_reply_legacy_unknown;
    adapterConfig.fallback_user = &s_configSession;
    status = hal_serial_commands_init(&s_serialCommands, &adapterConfig);
  }
  if (status != HAL_OK) {
    if (s_commandService.initialized) {
      (void)sc_command_service_deinit(&s_commandService);
    }
    hal_derr("RTC Clock SC adapter init failed: %s",
             hal_status_to_string(status));
  }
}

void configSessionTick(void) {
  hal_serial_session_poll(&s_configSession);
  sc_command_service_process_deferred(&s_commandService);
  hal_debug_set_muted(hal_serial_session_is_active(&s_configSession));
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_configSession);
}

uint32_t configSessionId(void) {
  return hal_serial_session_id(&s_configSession);
}
