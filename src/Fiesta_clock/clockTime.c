#include "clockTime.h"

#include "config.h"

#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>
#include <hal/time/hal_time.h>
#include <hal/time/jh_calendar.h>

/* The value names the time model, so a later change of meaning gets a new
 * one instead of reusing this marker. */
#define CLOCK_KV_UTC_KEY 1u
#define CLOCK_KV_UTC_MODEL 0x55544331u /* "UTC1" */
#define CLOCK_KV_SIZE ((uint16_t)HAL_RP_FLASH_EEPROM_SIZE)

/* The RTC may tick once between the write and the check. */
#define CLOCK_WRITE_CHECK_SLACK_S 1u
#define CLOCK_CET_OFFSET_MIN 60u
#define CLOCK_CEST_OFFSET_MIN 120u

/* Only an answer of the flash tells whether the marker is there. */
typedef enum {
  CLOCK_MARKER_ABSENT,
  CLOCK_MARKER_STORED,
  CLOCK_MARKER_UNKNOWN,
} clock_marker_t;

/* What the RTC fields hold, after the last write the RTC took. */
typedef enum {
  CLOCK_RTC_LOCAL,         /* local time of the older model */
  CLOCK_RTC_UTC,           /* UTC, read back after the write */
  CLOCK_RTC_UTC_UNCHECKED, /* UTC not read back, maybe partly written */
} clock_rtc_model_t;

static bool s_kvReady = false;
static clock_marker_t s_marker = CLOCK_MARKER_UNKNOWN;
static clock_rtc_model_t s_rtc = CLOCK_RTC_LOCAL;

static hal_status_t clockTimeOpenKv(void) {
  hal_status_t status = hal_eeprom_init(HAL_EEPROM_FLASH, CLOCK_KV_SIZE, 0u);
  if (status == HAL_OK) {
    status = hal_kv_init_ex(0u, CLOCK_KV_SIZE);
  }
  if (status == HAL_OK) {
    /* Gets compare with the flash, so the marker is checked where it lives. */
    status = hal_kv_set_read_through(true);
  }
  s_kvReady = (status == HAL_OK);
  return status;
}

static clock_marker_t clockTimeReadMarker(hal_status_t *outStatus) {
  uint32_t model = 0u;
  *outStatus = hal_kv_get_u32_ex(CLOCK_KV_UTC_KEY, &model);
  if (*outStatus == HAL_OK) {
    return model == CLOCK_KV_UTC_MODEL ? CLOCK_MARKER_STORED
                                       : CLOCK_MARKER_ABSENT;
  }
  return *outStatus == HAL_ENOENT ? CLOCK_MARKER_ABSENT : CLOCK_MARKER_UNKNOWN;
}

hal_status_t clockTimeInit(void) {
  s_marker = CLOCK_MARKER_UNKNOWN;
  hal_status_t status = clockTimeOpenKv();
  if (status == HAL_OK) {
    s_marker = clockTimeReadMarker(&status);
    if (status == HAL_ENOENT) {
      status = HAL_OK;
    }
  }
  /* An unread flash most likely holds the marker of an earlier start; that
   * UTC is shown but never trusted or settled without a new write. */
  s_rtc = s_marker == CLOCK_MARKER_STORED   ? CLOCK_RTC_UTC
          : s_marker == CLOCK_MARKER_ABSENT ? CLOCK_RTC_LOCAL
                                            : CLOCK_RTC_UTC_UNCHECKED;
  if (status != HAL_OK) {
    derr("RTC time model not read: %s", hal_status_to_string(status));
  }
  return status;
}

bool clockTimeIsUtc(void) { return s_marker == CLOCK_MARKER_STORED; }

bool clockTimeSettled(void) {
  return s_rtc != CLOCK_RTC_UTC || s_marker == CLOCK_MARKER_STORED;
}

static hal_status_t clockTimeStoreUtcMarker(void) {
  hal_status_t status = s_kvReady ? HAL_OK : clockTimeOpenKv();
  if (status != HAL_OK) {
    return status; /* nothing written, the flash is as before */
  }
  const hal_status_t setStatus =
      hal_kv_set_u32_ex(CLOCK_KV_UTC_KEY, CLOCK_KV_UTC_MODEL);
  s_marker = clockTimeReadMarker(&status);
  if (s_marker == CLOCK_MARKER_STORED) {
    return HAL_OK;
  }
  if (setStatus != HAL_OK) {
    return setStatus;
  }
  return (status == HAL_OK || status == HAL_ENOENT) ? HAL_EIO : status;
}

static uint32_t clockTimeSecondsOf(const hal_rtc_datetime_t *dt) {
  return hal_time_from_components(dt->year, dt->month, dt->day, dt->hour,
                                  dt->minute, dt->second);
}

bool clockTimeEpochInRange(uint32_t epoch) {
  return epoch >= hal_time_from_components(CFG_RTC_MIN_YEAR, 1, 1, 0, 0, 0) &&
         epoch <=
             hal_time_from_components(CFG_RTC_MAX_YEAR, 12, 31, 23, 59, 59);
}

hal_status_t clockTimeReadUtc(uint32_t *outEpoch, bool *outTrusted) {
  if (outEpoch == NULL || outTrusted == NULL) {
    return HAL_EINVAL;
  }
  *outEpoch = 0u;
  *outTrusted = false;

  hal_rtc_datetime_t dt = {0};
  const hal_status_t status = PCF_ReadDateTime(&dt);
  if (status == HAL_EPROTO) {
    /* The RTC answered without a valid date, e.g. after it lost its supply. */
    return HAL_OK;
  }
  if (status != HAL_OK) {
    return status;
  }

  const uint32_t epoch = clockTimeSecondsOf(&dt);
  if (clockTimeIsUtc() && dt.clock_integrity && clockTimeEpochInRange(epoch)) {
    *outEpoch = epoch;
    *outTrusted = true;
  }
  return HAL_OK;
}

hal_status_t clockTimeReadLocal(PCF_DateTime *outLocal, bool *outTrusted) {
  if (outLocal == NULL) {
    return HAL_EINVAL;
  }
  hal_rtc_datetime_t dt = {0};
  const hal_status_t status = PCF_ReadDateTime(&dt);
  if (status != HAL_OK) {
    return status;
  }

  PCF_DateTime local = {.second = dt.second,
                        .minute = dt.minute,
                        .hour = dt.hour,
                        .day = dt.day,
                        .weekday = 0u,
                        .month = dt.month,
                        .year = (int)dt.year};
  const uint32_t epoch = clockTimeSecondsOf(&dt);
  hal_time_local_datetime_t converted = {0};
  const bool fromUtc = s_rtc != CLOCK_RTC_LOCAL &&
                       clockTimeEpochInRange(epoch) &&
                       hal_time_utc_to_cet_cest_ex(epoch, &converted) == HAL_OK;
  if (fromUtc) {
    local.second = converted.second;
    local.minute = converted.minute;
    local.hour = converted.hour;
    local.day = converted.day;
    local.month = converted.month;
    local.year = (int)converted.year;
  }
  clockTimeUpdateWeekday(&local);

  *outLocal = local;
  if (outTrusted != NULL) {
    *outTrusted = fromUtc && clockTimeIsUtc() && dt.clock_integrity;
  }
  return HAL_OK;
}

/* Read the RTC back right after it took these seconds. */
static hal_status_t clockTimeCheckWrite(uint32_t seconds) {
  hal_rtc_datetime_t dt = {0};
  hal_status_t status = PCF_ReadDateTime(&dt);
  if (status == HAL_OK) {
    const uint32_t stored = clockTimeSecondsOf(&dt);
    if (!dt.clock_integrity || stored < seconds ||
        stored - seconds > CLOCK_WRITE_CHECK_SLACK_S) {
      status = HAL_EIO;
    }
  }
  return status;
}

/* The flash answered without the marker, so the RTC is read as local time,
 * now and after a restart: write the same instant in CET/CEST. */
static void clockTimeKeepLocal(uint32_t epoch) {
  hal_time_local_datetime_t local = {0};
  hal_status_t status = clockTimeEpochInRange(epoch)
                            ? hal_time_utc_to_cet_cest_ex(epoch, &local)
                            : HAL_EINVAL;
  if (status == HAL_OK) {
    status = PCF_WriteEpoch(epoch + (uint32_t)local.utc_offset_minutes * 60u);
  }
  if (status == HAL_OK) {
    s_rtc = CLOCK_RTC_LOCAL;
  } else {
    derr("RTC time not put back as local time: %s",
         hal_status_to_string(status));
  }
}

/* Store the marker for UTC that was read back, or go back to local time once
 * the flash answers without it. Without an answer the RTC keeps UTC: the
 * marker may already be in the flash. */
static hal_status_t clockTimeFinish(hal_status_t status, uint32_t epoch) {
  if (status == HAL_OK && !clockTimeSettled()) {
    status = clockTimeStoreUtcMarker();
  }
  if (status != HAL_OK && s_marker == CLOCK_MARKER_ABSENT) {
    clockTimeKeepLocal(epoch);
  }
  return status;
}

hal_status_t clockTimeWriteUtc(uint32_t epoch) {
  if (!clockTimeEpochInRange(epoch)) {
    return HAL_EINVAL;
  }
  hal_status_t status = PCF_WriteEpoch(epoch);
  if (status == HAL_OK) {
    s_rtc = CLOCK_RTC_UTC_UNCHECKED;
    status = clockTimeCheckWrite(epoch);
    if (status == HAL_OK) {
      s_rtc = CLOCK_RTC_UTC;
    }
  } else if (s_rtc != CLOCK_RTC_LOCAL) {
    s_rtc = CLOCK_RTC_UTC_UNCHECKED; /* the RTC may hold part of the write */
  }
  return clockTimeFinish(status, epoch);
}

hal_status_t clockTimeSettle(void) {
  if (clockTimeSettled()) {
    return HAL_OK;
  }
  hal_rtc_datetime_t dt = {0};
  const hal_status_t status = PCF_ReadDateTime(&dt);
  if (status != HAL_OK) {
    return status;
  }
  return clockTimeFinish(HAL_OK, clockTimeSecondsOf(&dt));
}

hal_status_t clockTimeWriteLocal(const PCF_DateTime *local) {
  if (local == NULL || local->year < CFG_RTC_MIN_YEAR ||
      local->year > CFG_RTC_MAX_YEAR) {
    return HAL_EINVAL;
  }
  const uint32_t localSeconds =
      hal_time_from_components(local->year, local->month, local->day,
                               local->hour, local->minute, local->second);
  if (localSeconds == 0u) {
    return HAL_EINVAL;
  }

  /* CEST when the time two hours earlier in UTC is summer time, else CET. */
  uint32_t offsetMin = CLOCK_CET_OFFSET_MIN;
  hal_time_local_datetime_t check = {0};
  if (hal_time_utc_to_cet_cest_ex(localSeconds - CLOCK_CEST_OFFSET_MIN * 60u,
                                  &check) == HAL_OK &&
      check.utc_offset_minutes == CLOCK_CEST_OFFSET_MIN) {
    offsetMin = CLOCK_CEST_OFFSET_MIN;
  }
  return clockTimeWriteUtc(localSeconds - offsetMin * 60u);
}

void clockTimeUpdateWeekday(PCF_DateTime *dt) {
  uint8_t sundayFirst = 0u;
  if (dt != NULL && dt->year >= (int)JH_CALENDAR_MIN_YEAR &&
      dt->year <= (int)JH_CALENDAR_MAX_YEAR &&
      jh_calendar_day_of_week((uint16_t)dt->year, dt->month, dt->day,
                              &sundayFirst) == HAL_OK) {
    dt->weekday = (unsigned char)((sundayFirst + 6u) % 7u);
  }
}
