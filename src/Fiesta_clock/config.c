#include "config.h"

#include "clockTime.h"

#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_serial_session.h>
#include <hal/time/jh_calendar.h>

#include "../common/scDefinitions/sc_command_handlers.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "../common/scDefinitions/sc_param_handlers.h"
#include "../common/scDefinitions/sc_param_types.h"
#include "../common/scDefinitions/sc_protocol.h"

/* CET/CEST date and time; the RTC keeps UTC. */
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
    SC_PARAM_SCALAR_I16("rtc_year", clock_values_t, rtc_year, CFG_RTC_MIN_YEAR,
                        CFG_RTC_MAX_YEAR, 2026, 1, "rtc"),
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

static sc_config_session_t s_sc;

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

  if (values->rtc_year < CFG_RTC_MIN_YEAR ||
      values->rtc_year > CFG_RTC_MAX_YEAR) {
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

  uint8_t maxDay = 0u;
  if (jh_calendar_days_in_month((uint16_t)values->rtc_year,
                                (uint8_t)values->rtc_month,
                                &maxDay) != HAL_OK ||
      values->rtc_day < 1 || values->rtc_day > (int16_t)maxDay) {
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

static bool clockValuesLoadFromRtc(clock_values_t *outValues) {
  if (outValues == NULL) {
    return false;
  }

  PCF_DateTime dt = {0};
  bool trusted = false;
  if (clockTimeReadLocal(&dt, &trusted) != HAL_OK) {
    return false;
  }

  const clock_values_t loaded = {
      .rtc_year = (int16_t)dt.year,
      .rtc_month = (int16_t)dt.month,
      .rtc_day = (int16_t)dt.day,
      .rtc_hour = (int16_t)dt.hour,
      .rtc_minute = (int16_t)dt.minute,
      .rtc_second = (int16_t)dt.second,
      .rtc_integrity = trusted ? 1 : 0,
  };
  if (!clockValuesValidate(&loaded, NULL)) {
    return false;
  }
  *outValues = loaded;
  return true;
}

static void clockValuesRefreshActiveFromRtc(void) {
  clock_values_t refreshed = s_active;
  if (clockValuesLoadFromRtc(&refreshed)) {
    s_active = refreshed;
  } else {
    /* Keep the last date, but do not vouch for it. */
    s_active.rtc_integrity = 0;
  }
  if (!s_stagingDirty) {
    s_staging = s_active;
  }
}

static bool clockValuesApplyToRtc(const clock_values_t *values) {
  if (!clockValuesValidate(values, NULL)) {
    return false;
  }

  const PCF_DateTime dt = {
      .second = (unsigned char)values->rtc_second,
      .minute = (unsigned char)values->rtc_minute,
      .hour = (unsigned char)values->rtc_hour,
      .day = (unsigned char)values->rtc_day,
      .weekday = 0u,
      .month = (unsigned char)values->rtc_month,
      .year = values->rtc_year,
  };
  return clockTimeWriteLocal(&dt) == HAL_OK;
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
  if (sc_config_session_stop(&s_sc, "RTC Clock") != HAL_OK) {
    return;
  }

  clockValuesRefreshActiveFromRtc();
  s_staging = s_active;
  s_stagingDirty = false;

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
  (void)sc_config_session_start(&s_sc, &serviceConfig, NULL, NULL, "RTC Clock");
}

void configSessionTick(void) {
  hal_debug_set_muted(sc_config_session_poll(&s_sc));
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_sc.session);
}

uint32_t configSessionId(void) { return hal_serial_session_id(&s_sc.session); }

bool configSessionEditPending(void) {
  return s_stagingDirty && configSessionActive();
}
