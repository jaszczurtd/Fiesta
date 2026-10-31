/* Production time code of the clock (RTC, UTC model, GPS sync, CAN, SC and
 * the button editor) over the HAL RTC stack and a PCF8563 register model. */
extern "C" {
#include "main.h"
#include "rtcSync.h"

#include "../common/canDefinitions/canDefinitions.h"
#include "../common/scDefinitions/sc_command_handlers.h"
}
#include <hal/time/hal_time.h>
#include <hal/time/jh_calendar.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

/* ── Board models ────────────────────────────────────────────────────── */

static uint32_t now_ms = 1000u;

struct Pcf8563 {
  uint8_t regs[32];
  bool busError;              // every transfer fails
  bool dropWrites;            // time writes are acknowledged but not stored
  bool vlStuck;               // the integrity bit stays set after a write
  int failTimeReads;          // reads of the time registers that fail first
  int failTimeWrites;         // time writes that fail first
  int ticksBeforeRead;        // seconds the RTC runs before the next time read
  int writesBeforeError = -1; // time writes that still succeed; -1: all
  int timeWrites;
};
static Pcf8563 pcf = {};
static std::vector<uint8_t> i2c_tx;

#if defined(RTC_ds3231)
static constexpr uint8_t RTC_ADDRESS = 0x68;
static constexpr uint8_t TIME_REG = 0, MONTH_REG = 5, INTEGRITY_REG = 15;
#else
static constexpr uint8_t RTC_ADDRESS = 0x51;
static constexpr uint8_t TIME_REG = 2, MONTH_REG = 7, INTEGRITY_REG = 2;
#endif

static uint8_t bcd(int value) {
  return (uint8_t)(((value / 10) << 4) | (value % 10));
}
static int unbcd(uint8_t value) { return (value >> 4) * 10 + (value & 0x0f); }

static void rtc_fields(int year, int month, int day, int hour, int minute,
                       int second, bool integrityLost = false) {
#if defined(RTC_ds3231)
  pcf.regs[0] = bcd(second);
  pcf.regs[1] = bcd(minute);
  pcf.regs[2] = bcd(hour);
  pcf.regs[3] = 1;
  pcf.regs[4] = bcd(day);
  pcf.regs[5] = bcd(month);
  pcf.regs[6] = bcd(year % 100);
  pcf.regs[15] = integrityLost ? 0x80 : 0;
#else
  pcf.regs[2] = (uint8_t)(bcd(second) | (integrityLost ? 0x80 : 0));
  pcf.regs[3] = bcd(minute);
  pcf.regs[4] = bcd(hour);
  pcf.regs[5] = bcd(day);
  pcf.regs[6] = 0;
  pcf.regs[7] = bcd(month);
  pcf.regs[8] = bcd(year % 100);
#endif
}

static uint32_t utc(int year, int month, int day, int hour, int minute,
                    int second) {
  const uint32_t epoch =
      hal_time_from_components(year, month, day, hour, minute, second);
  assert(epoch != 0u);
  return epoch;
}

static void rtc_set_utc(uint32_t epoch, bool integrityLost = false) {
  jh_calendar_datetime_t dt = {};
  assert(jh_calendar_epoch_to_datetime(epoch, 2099u, &dt) == HAL_OK);
  rtc_fields(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second,
             integrityLost);
}

static uint32_t rtc_utc() {
#if defined(RTC_ds3231)
  return hal_time_from_components(
      2000 + unbcd(pcf.regs[6]), unbcd(pcf.regs[5] & 0x1f),
      unbcd(pcf.regs[4] & 0x3f), unbcd(pcf.regs[2] & 0x3f),
      unbcd(pcf.regs[1] & 0x7f), unbcd(pcf.regs[0] & 0x7f));
#else
  return hal_time_from_components(
      2000 + unbcd(pcf.regs[8]), unbcd(pcf.regs[7] & 0x1f),
      unbcd(pcf.regs[5] & 0x3f), unbcd(pcf.regs[4] & 0x3f),
      unbcd(pcf.regs[3] & 0x7f), unbcd(pcf.regs[2] & 0x7f));
#endif
}

static bool rtc_integrity_lost() {
  return (pcf.regs[INTEGRITY_REG] & 0x80) != 0;
}

// The RTC and the loop both run for this long.
static void run_for(uint32_t seconds) {
  rtc_set_utc(rtc_utc() + seconds, rtc_integrity_lost());
  now_ms += seconds * 1000u;
}

static std::map<uint16_t, uint32_t> kv;
static bool kv_broken = false;
static bool kv_write_fails = false;
static int kv_get_fails = 0; // gets that fail before the flash answers
static bool kv_set_misreports = false; // the value is stored, set says EIO
static int kv_writes = 0;

struct Frame {
  uint32_t id;
  uint8_t len;
  uint8_t data[8];
};
static std::deque<Frame> can_rx;
static std::vector<Frame> can_tx;
static hal_can_filter_t can_filters[HAL_CAN_MAX_FILTERS];
static int can_filter_writes = 0;
static uint8_t gps_frame_number = 0;
static bool can_create_fails = false;

static sc_command_service_config_t sc = {};
static bool sc_active = false;

struct ClockValues {
  int16_t year, month, day, hour, minute, second, integrity;
};

static std::vector<std::string> texts;
static bool button_set = false, button_hour = false;
static int set_presses = 0;

// DS18B20 on a data pin: a 12-bit conversion takes 750 ms; a sensor that
// stops answering keeps its last sample in the HAL, like the driver.
struct Ds18b20 {
  bool present;
  float celsius;
  bool converting;
  uint32_t doneMs;
  bool sampleValid;
  bool sampleFresh;
  float sample;
  uint32_t sampleMs;
};
static Ds18b20 temp_inside = {}, temp_outside = {};
static const uint32_t ds18b20_conversion_ms = 750u;

static Ds18b20 *ds18b20_on(uint8_t pin) {
  assert(pin == JH_HW_NODE_TEMP_INSIDE_PIN_DATA ||
         pin == JH_HW_NODE_TEMP_OUTSIDE_PIN_DATA);
  return pin == JH_HW_NODE_TEMP_INSIDE_PIN_DATA ? &temp_inside : &temp_outside;
}

extern "C" {
char s[BUF_L + 1];

uint32_t hal_millis(void) { return now_ms; }
void hal_delay_ms(uint32_t) {}
void hal_watchdog_feed(void) {}
void hal_deb(const char *, ...) {}
void hal_derr(const char *, ...) {}
void hal_derr_limited(const char *, const char *, ...) {}
void hal_debug_set_muted(bool) {}
void hal_critical_section_enter(void) {}
void hal_critical_section_exit(void) {}
struct hal_mutex_impl_t {
  int unused;
};
hal_mutex_t hal_mutex_create(void) {
  static hal_mutex_impl_t mutex;
  return &mutex;
}
void hal_mutex_destroy(hal_mutex_t) {}
void hal_mutex_lock(hal_mutex_t) {}
void hal_mutex_unlock(hal_mutex_t) {}

hal_status_t hal_i2c_init_bus(uint8_t bus, uint8_t sda, uint8_t scl, uint32_t) {
  assert(bus == JH_HW_BUS_I2C_INDEX && sda == JH_HW_BUS_I2C_PIN_SDA &&
         scl == JH_HW_BUS_I2C_PIN_SCL);
  return HAL_OK;
}
void hal_i2c_begin_transmission_bus(uint8_t bus, hal_i2c_address_t address) {
  assert(bus == JH_HW_BUS_I2C_INDEX && address == RTC_ADDRESS);
  i2c_tx.clear();
}
size_t hal_i2c_write_bus(uint8_t, uint8_t value) {
  i2c_tx.push_back(value);
  return 1;
}
uint8_t hal_i2c_end_transmission_bus(uint8_t) {
  if (pcf.busError) {
    return 2;
  }
  if (i2c_tx.size() > 1u) {
    const uint8_t reg = i2c_tx[0];
    if (reg == TIME_REG) {
      if (pcf.failTimeWrites > 0) {
        --pcf.failTimeWrites;
        return 2;
      }
      if (pcf.writesBeforeError == 0) {
        return 2;
      }
      if (pcf.writesBeforeError > 0) {
        --pcf.writesBeforeError;
      }
      ++pcf.timeWrites;
    }
    if (pcf.dropWrites) {
      return 0;
    }
    for (size_t i = 1; i < i2c_tx.size(); ++i) {
      pcf.regs[(reg + i - 1u) & 0x1fu] = i2c_tx[i];
    }
    if (pcf.vlStuck) {
      pcf.regs[INTEGRITY_REG] |= 0x80;
    }
  }
  return 0;
}
bool hal_i2c_write_read_bus(uint8_t bus, hal_i2c_address_t address,
                            const uint8_t *tx, size_t tx_len, uint8_t *rx,
                            size_t rx_len) {
  assert(bus == JH_HW_BUS_I2C_INDEX && address == RTC_ADDRESS && tx_len == 1u);
  if (pcf.busError) {
    return false;
  }
  // The one-byte integrity read still answers.
  if (tx[0] == TIME_REG && rx_len > 1u && pcf.failTimeReads > 0) {
    --pcf.failTimeReads;
    return false;
  }
  if (tx[0] == TIME_REG && rx_len > 1u && pcf.ticksBeforeRead > 0) {
    rtc_set_utc(rtc_utc() + (uint32_t)pcf.ticksBeforeRead,
                rtc_integrity_lost());
    pcf.ticksBeforeRead = 0;
  }
  for (size_t i = 0; i < rx_len; ++i) {
    rx[i] = pcf.regs[(tx[0] + i) & 0x1fu];
  }
  return true;
}

hal_status_t hal_eeprom_init(hal_eeprom_type_t type, uint16_t size, uint8_t) {
  assert(type == HAL_EEPROM_FLASH && size == HAL_RP_FLASH_EEPROM_SIZE);
  return HAL_OK;
}
hal_status_t hal_kv_init_ex(uint16_t base, uint16_t size) {
  assert(base == 0u && size == HAL_RP_FLASH_EEPROM_SIZE);
  return kv_broken ? HAL_EIO : HAL_OK;
}
hal_status_t hal_kv_set_read_through(bool enabled) {
  assert(enabled);
  return HAL_OK;
}
hal_status_t hal_kv_get_u32_ex(uint16_t key, uint32_t *value) {
  if (kv_broken) {
    return HAL_EUNINIT;
  }
  if (kv_get_fails > 0) {
    --kv_get_fails;
    return HAL_EIO;
  }
  const auto found = kv.find(key);
  if (found == kv.end()) {
    return HAL_ENOENT;
  }
  *value = found->second;
  return HAL_OK;
}
hal_status_t hal_kv_set_u32_ex(uint16_t key, uint32_t value) {
  if (kv_broken) {
    return HAL_EUNINIT;
  }
  if (kv_write_fails) {
    return HAL_EIO;
  }
  ++kv_writes;
  kv[key] = value;
  return kv_set_misreports ? HAL_EIO : HAL_OK;
}

hal_status_t hal_spi_init(uint8_t, uint8_t, uint8_t, uint8_t) { return HAL_OK; }
void hal_gpio_set_mode(uint8_t, hal_gpio_mode_t) {}
void hal_gpio_write(uint8_t, bool) {}
hal_status_t hal_can_create_with_retry(const hal_can_config_t *, uint8_t,
                                       void (*)(void), int, void (*)(void),
                                       hal_can_t *out) {
  static int handle;
  *out = can_create_fails ? nullptr : reinterpret_cast<hal_can_t>(&handle);
  return can_create_fails ? HAL_EIO : HAL_OK;
}
void hal_can_destroy(hal_can_t) {}

hal_ds18b20_t hal_ds18b20_init(const hal_ds18b20_config_t *cfg) {
  Ds18b20 *sensor = ds18b20_on(cfg->data_pin);
  return sensor->present ? reinterpret_cast<hal_ds18b20_t>(sensor) : nullptr;
}
bool hal_ds18b20_request(hal_ds18b20_t h) {
  Ds18b20 *sensor = reinterpret_cast<Ds18b20 *>(h);
  if (sensor->converting || !sensor->present) {
    return false;
  }
  sensor->converting = true;
  sensor->doneMs = now_ms + ds18b20_conversion_ms;
  return true;
}
hal_status_t hal_ds18b20_poll(hal_ds18b20_t h) {
  Ds18b20 *sensor = reinterpret_cast<Ds18b20 *>(h);
  if (!sensor->converting) {
    return HAL_ESTATE;
  }
  if ((int32_t)(now_ms - sensor->doneMs) < 0) {
    return HAL_EAGAIN;
  }
  sensor->converting = false;
  if (!sensor->present) {
    return HAL_EPROTO;
  }
  sensor->sample = sensor->celsius;
  sensor->sampleValid = true;
  sensor->sampleFresh = true;
  sensor->sampleMs = now_ms;
  return HAL_OK;
}
bool hal_ds18b20_is_busy(hal_ds18b20_t h) {
  return reinterpret_cast<Ds18b20 *>(h)->converting;
}
bool hal_ds18b20_take_latest(hal_ds18b20_t h, float *temp_c, bool *fresh) {
  Ds18b20 *sensor = reinterpret_cast<Ds18b20 *>(h);
  if (!sensor->sampleValid) {
    return false;
  }
  *temp_c = sensor->sample;
  if (fresh != nullptr) {
    *fresh = sensor->sampleFresh;
  }
  sensor->sampleFresh = false;
  return true;
}
hal_status_t hal_can_set_filter(hal_can_t, uint8_t index,
                                const hal_can_filter_t *filter) {
  assert(index < HAL_CAN_MAX_FILTERS);
  can_filters[index] = *filter;
  ++can_filter_writes;
  return HAL_OK;
}
hal_status_t hal_can_process_all(hal_can_t, hal_can_frame_cb_t cb,
                                 uint32_t *delivered) {
  uint32_t count = 0;
  while (!can_rx.empty()) {
    cb(can_rx.front().id, can_rx.front().len, can_rx.front().data);
    can_rx.pop_front();
    ++count;
  }
  if (delivered != nullptr) {
    *delivered = count;
  }
  return HAL_OK;
}
hal_status_t hal_can_send(hal_can_t, uint32_t id, uint8_t len,
                          const uint8_t *data) {
  Frame frame = {id, len, {}};
  std::memcpy(frame.data, data, len);
  can_tx.push_back(frame);
  return HAL_OK;
}

hal_status_t sc_config_session_stop(sc_config_session_t *, const char *) {
  return HAL_OK;
}
hal_status_t sc_config_session_start(sc_config_session_t *,
                                     const sc_command_service_config_t *config,
                                     hal_serial_session_unknown_cb_t, void *,
                                     const char *) {
  sc = *config;
  return HAL_OK;
}
bool sc_config_session_poll(sc_config_session_t *) { return sc_active; }
bool hal_serial_session_is_active(const hal_serial_session_t *) {
  return sc_active;
}
uint32_t hal_serial_session_id(const hal_serial_session_t *) { return 1u; }

hal_status_t clock_display_text(uint8_t, uint8_t, uint8_t, const char *text) {
  texts.emplace_back(text);
  return HAL_OK;
}
hal_status_t hal_display_fill_screen_ex(uint16_t) { return HAL_OK; }
hal_status_t hal_display_flush_ex(void) { return HAL_OK; }
bool setButton(void) { return button_set; }
bool setHour(void) { return button_hour; }
bool setMinute(void) { return false; }
bool ignition(void) { return true; }
bool anyButton(void) { return button_set || button_hour; }
bool setButtonPressed(void) {
  if (set_presses > 0) {
    --set_presses;
    return true;
  }
  return false;
}
}

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void start(void) {
  PCF_Init(PCF_TIMER_INTERRUPT_ENABLE);
  assert(clockTimeInit() == HAL_OK || kv_broken);
  assert(clockCanInit() == HAL_OK);
}

static void gps(uint32_t epoch, bool valid = true, uint8_t len = 8) {
  Frame frame = {CAN_ID_GPS_UTC_UPDATE, len, {}};
  frame.data[CAN_FRAME_NUMBER] = gps_frame_number++;
  jh_store_be32(&frame.data[CAN_FRAME_GPS_UTC_EPOCH_B3], epoch);
  frame.data[CAN_FRAME_GPS_UTC_STATUS] = valid ? CAN_GPS_UTC_STATUS_VALID : 0;
  can_rx.push_back(frame);
}

// One pass of the main loop after a pause of pauseMs.
static void loop_pass(uint32_t pauseMs = 20u, bool edit = false) {
  now_ms += pauseMs;
  ds18b20_poll();
  clockCanTick(edit);
}

// Normal 20 ms passes for durationMs.
static void loop_for(uint32_t durationMs) {
  for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 20u) {
    loop_pass();
  }
}

// GPS sends the current RTC time shifted by offsetS; one loop pass reads it.
static void gps_offset(int offsetS, bool edit = false) {
  gps((uint32_t)((int64_t)rtc_utc() + offsetS));
  loop_pass(20u, edit);
}

static Frame last_frame(uint32_t id) {
  for (auto frame = can_tx.rbegin(); frame != can_tx.rend(); ++frame) {
    if (frame->id == id) {
      return *frame;
    }
  }
  assert(false);
  return {};
}

static Frame last_rtc_frame() { return last_frame(CAN_ID_RTC_UPDATE); }

static size_t sent_frames(uint32_t id) {
  return (size_t)std::count_if(can_tx.begin(), can_tx.end(),
                               [id](const Frame &f) { return f.id == id; });
}

// Advance past the next 0x130 deadline and return the frame.
static Frame next_rtc_frame() {
  const size_t sent = sent_frames(CAN_ID_RTC_UPDATE);
  loop_pass(1000u);
  assert(sent_frames(CAN_ID_RTC_UPDATE) == sent + 1u);
  return last_rtc_frame();
}

// Every frame of the clock takes the next number, whatever its ID.
static void expect_consecutive_numbers() {
  for (size_t i = 1; i < can_tx.size(); ++i) {
    assert(can_tx[i].data[CAN_FRAME_NUMBER] ==
           (uint8_t)(can_tx[i - 1].data[CAN_FRAME_NUMBER] + 1u));
  }
}

static uint32_t packed(const Frame &frame) {
  return ((uint32_t)frame.data[CAN_FRAME_RTC_UPDATE_DT_HI] << 16) |
         ((uint32_t)frame.data[CAN_FRAME_RTC_UPDATE_DT_MD] << 8) |
         frame.data[CAN_FRAME_RTC_UPDATE_DT_LO];
}

static uint32_t pack(int year, int month, int day, int hour, int minute) {
  return ((uint32_t)(year - 2020) << 20) | ((uint32_t)month << 16) |
         ((uint32_t)day << 11) | ((uint32_t)hour << 6) | (uint32_t)minute;
}

static PCF_DateTime read_local(bool *trusted = nullptr) {
  PCF_DateTime local = {};
  assert(clockTimeReadLocal(&local, trusted) == HAL_OK);
  return local;
}

static void expect_local(int year, int month, int day, int hour, int minute,
                         int second, int weekday) {
  const PCF_DateTime local = read_local();
  assert(local.year == year && local.month == month && local.day == day &&
         local.hour == hour && local.minute == minute &&
         local.second == second && local.weekday == weekday);
}

static PCF_DateTime local_time(int year, int month, int day, int hour,
                               int minute, int second) {
  PCF_DateTime local = {};
  local.second = (unsigned char)second;
  local.minute = (unsigned char)minute;
  local.hour = (unsigned char)hour;
  local.day = (unsigned char)day;
  local.month = (unsigned char)month;
  local.year = year;
  return local;
}

static void write_local(int year, int month, int day, int hour, int minute,
                        int second) {
  const PCF_DateTime local = local_time(year, month, day, hour, minute, second);
  assert(clockTimeWriteLocal(&local) == HAL_OK);
}

// A first UTC write that also stores the marker.
static void make_utc(uint32_t epoch) {
  assert(clockTimeWriteUtc(epoch) == HAL_OK);
  assert(clockTimeIsUtc());
}

/* ── Scenarios ───────────────────────────────────────────────────────── */

// CET/CEST shown from UTC, local writes stored as UTC, calendar edges.
static void test_local() {
  rtc_fields(2026, 10, 15, 21, 56, 0);
  start();
  // The older firmware kept local time: shown as is, not trusted.
  bool trusted = true;
  const PCF_DateTime legacy = read_local(&trusted);
  assert(!trusted && legacy.hour == 21 && legacy.minute == 56 &&
         legacy.weekday == 3); // Thursday
  uint32_t epoch = 1u;
  assert(clockTimeReadUtc(&epoch, &trusted) == HAL_OK && !trusted &&
         epoch == 0u);

  make_utc(utc(2026, 1, 15, 12, 0, 0));
  read_local(&trusted);
  assert(trusted);
  expect_local(2026, 1, 15, 13, 0, 0, 3);
  rtc_set_utc(utc(2026, 7, 1, 12, 0, 0));
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  // Spring change at 01:00 UTC.
  rtc_set_utc(utc(2026, 3, 29, 0, 59, 59));
  expect_local(2026, 3, 29, 1, 59, 59, 6);
  rtc_set_utc(utc(2026, 3, 29, 1, 0, 0));
  expect_local(2026, 3, 29, 3, 0, 0, 6);
  // Autumn change at 01:00 UTC.
  rtc_set_utc(utc(2026, 10, 25, 0, 59, 59));
  expect_local(2026, 10, 25, 2, 59, 59, 6);
  rtc_set_utc(utc(2026, 10, 25, 1, 0, 0));
  expect_local(2026, 10, 25, 2, 0, 0, 6);
  // Date and year change, leap and common February.
  rtc_set_utc(utc(2026, 12, 31, 23, 30, 0));
  expect_local(2027, 1, 1, 0, 30, 0, 4);
  rtc_set_utc(utc(2028, 2, 28, 23, 30, 0));
  expect_local(2028, 2, 29, 0, 30, 0, 1);
  rtc_set_utc(utc(2027, 2, 28, 23, 30, 0));
  expect_local(2027, 3, 1, 0, 30, 0, 0);
  // A lost integrity bit keeps the time on screen but not trusted.
  rtc_set_utc(utc(2026, 7, 1, 12, 0, 0), true);
  read_local(&trusted);
  assert(!trusted);
  assert(clockTimeReadUtc(&epoch, &trusted) == HAL_OK && !trusted);

  write_local(2026, 1, 15, 13, 0, 0);
  assert(rtc_utc() == utc(2026, 1, 15, 12, 0, 0) && !rtc_integrity_lost());
  write_local(2026, 7, 1, 14, 0, 0);
  assert(rtc_utc() == utc(2026, 7, 1, 12, 0, 0));
  write_local(2026, 3, 29, 1, 59, 59);
  assert(rtc_utc() == utc(2026, 3, 29, 0, 59, 59));
  write_local(2026, 3, 29, 3, 0, 0);
  assert(rtc_utc() == utc(2026, 3, 29, 1, 0, 0));
  // 02:30 does not exist on that day: taken as CET, shown as 03:30 CEST.
  write_local(2026, 3, 29, 2, 30, 0);
  assert(rtc_utc() == utc(2026, 3, 29, 1, 30, 0));
  expect_local(2026, 3, 29, 3, 30, 0, 6);
  // The repeated hour is its first, CEST pass; the hour after is CET.
  write_local(2026, 10, 25, 2, 30, 0);
  assert(rtc_utc() == utc(2026, 10, 25, 0, 30, 0));
  write_local(2026, 10, 25, 3, 0, 0);
  assert(rtc_utc() == utc(2026, 10, 25, 2, 0, 0));
  write_local(2027, 1, 1, 0, 30, 0);
  assert(rtc_utc() == utc(2026, 12, 31, 23, 30, 0));
  write_local(2028, 2, 29, 0, 30, 0);
  assert(rtc_utc() == utc(2028, 2, 28, 23, 30, 0));

  const uint32_t kept = rtc_utc();
  const PCF_DateTime invalid[] = {
      local_time(2027, 2, 29, 12, 0, 0),
      local_time(2026, 4, 31, 12, 0, 0),
      local_time(2026, 13, 1, 12, 0, 0),
      local_time(2026, 1, 1, 24, 0, 0),
      local_time(CFG_RTC_MIN_YEAR - 1, 12, 31, 12, 0, 0),
      local_time(CFG_RTC_MAX_YEAR + 1, 1, 1, 12, 0, 0),
      local_time(CFG_RTC_MAX_YEAR + 1, 1, 1, 0, 30, 0), // UTC still in range
      local_time(CFG_RTC_MIN_YEAR, 1, 1, 0, 30, 0)}; // UTC in the year before
  for (const PCF_DateTime &value : invalid) {
    assert(clockTimeWriteLocal(&value) == HAL_EINVAL);
  }
  assert(clockTimeWriteLocal(nullptr) == HAL_EINVAL);
  assert(rtc_utc() == kept);
  write_local(CFG_RTC_MIN_YEAR, 1, 1, 1, 0, 0);
  assert(rtc_utc() == utc(CFG_RTC_MIN_YEAR, 1, 1, 0, 0, 0));
  write_local(CFG_RTC_MAX_YEAR, 12, 31, 23, 59, 59);
  assert(rtc_utc() == utc(CFG_RTC_MAX_YEAR, 12, 31, 22, 59, 59));
  assert(clockTimeWriteUtc(utc(CFG_RTC_MIN_YEAR, 1, 1, 0, 0, 0) - 1u) ==
         HAL_EINVAL);
  assert(clockTimeWriteUtc(utc(CFG_RTC_MAX_YEAR, 12, 31, 23, 59, 59) + 1u) ==
         HAL_EINVAL);

  PCF_DateTime weekday = local_time(2026, 10, 26, 0, 0, 0);
  clockTimeUpdateWeekday(&weekday);
  assert(weekday.weekday == 0); // Monday
  weekday.weekday = 5;
  weekday.day = 31;
  weekday.month = 2;
  clockTimeUpdateWeekday(&weekday);
  assert(weekday.weekday == 5);
}

// The marker: created by the first write, kept across a restart, needed
// before the time counts as UTC.
static void test_marker() {
  rtc_fields(2026, 10, 15, 21, 56, 0);
  start();
  assert(!clockTimeIsUtc() && kv.empty());
  // A failed RTC write stores no marker.
  pcf.busError = true;
  assert(clockTimeWriteUtc(utc(2026, 10, 15, 19, 56, 0)) == HAL_EIO);
  pcf.busError = false;
  assert(!clockTimeIsUtc() && kv.empty());
  // Without a stored marker the RTC keeps the instant as local time, so it
  // reads right now and after a restart, and is no trusted UTC.
  kv_write_fails = true;
  assert(clockTimeWriteUtc(utc(2026, 10, 15, 19, 56, 0)) == HAL_EIO);
  assert(!clockTimeIsUtc() && rtc_utc() == utc(2026, 10, 15, 21, 56, 0));
  expect_local(2026, 10, 15, 21, 56, 0, 3);
  const PCF_DateTime summer = local_time(2026, 7, 1, 14, 0, 0);
  assert(clockTimeWriteLocal(&summer) == HAL_EIO);
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  start();
  assert(!clockTimeIsUtc());
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  uint32_t epoch = 1u;
  bool trusted = true;
  assert(clockTimeReadUtc(&epoch, &trusted) == HAL_OK && !trusted);
  kv_write_fails = false;
  make_utc(utc(2026, 10, 15, 19, 56, 0));
  assert(kv.size() == 1u && kv_writes == 1);
  make_utc(utc(2026, 10, 15, 19, 57, 0));
  assert(kv_writes == 1); // written once
  // The read-back allows the RTC one tick, not two.
  pcf.ticksBeforeRead = 1;
  make_utc(utc(2026, 10, 15, 19, 57, 0));
  pcf.ticksBeforeRead = 2;
  assert(clockTimeWriteUtc(utc(2026, 10, 15, 19, 57, 0)) == HAL_EIO);
  rtc_set_utc(utc(2026, 10, 15, 19, 57, 0));
  // Restart: the marker comes back from flash.
  start();
  assert(clockTimeIsUtc());
  // Another value under the key is no marker of this model.
  kv.begin()->second ^= 1u;
  start();
  assert(!clockTimeIsUtc());
  kv.begin()->second ^= 1u;
  // Broken storage at start: no marker, and writes keep trying.
  kv_broken = true;
  assert(clockTimeInit() != HAL_OK && !clockTimeIsUtc());
  // The marker of the earlier start is likely there: UTC, not trusted.
  expect_local(2026, 10, 15, 21, 57, 0, 3);
  read_local(&trusted);
  assert(!trusted && clockTimeSettled());
  assert(clockTimeWriteUtc(utc(2026, 10, 15, 19, 58, 0)) != HAL_OK);
  // The marker may be in the flash: UTC stays, shown right, not trusted.
  assert(rtc_utc() == utc(2026, 10, 15, 19, 58, 0) && !clockTimeSettled());
  expect_local(2026, 10, 15, 21, 58, 0, 3);
  read_local(&trusted);
  assert(!trusted);
  kv_broken = false;
  make_utc(utc(2026, 10, 15, 19, 58, 0));
}

// The change to UTC with failed checks: the RTC follows what the flash
// answers about the marker, and the time is trusted only with the marker.
static void test_transition() {
  const uint32_t retryMs = CFG_RTC_SYNC_RETRY_MS;
  const PCF_DateTime set = local_time(2026, 7, 1, 14, 0, 0);
  bool trusted = true;
  // The marker is written but its check fails: it may be in the flash, so
  // the RTC keeps UTC. A restart reads the marker; 14:00 stays 14:00.
  rtc_fields(2026, 7, 1, 13, 0, 0);
  start();
  kv_get_fails = 1;
  assert(clockTimeWriteLocal(&set) == HAL_EIO);
  assert(rtc_utc() == utc(2026, 7, 1, 12, 0, 0) && !clockTimeIsUtc() &&
         !clockTimeSettled());
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  read_local(&trusted);
  assert(!trusted);
  start();
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  read_local(&trusted);
  assert(trusted && clockTimeSettled());

  // Without a restart the loop confirms the marker, once per retry time
  // while the flash gives no answer.
  kv.clear();
  rtc_fields(2026, 7, 1, 13, 0, 0);
  start();
  kv_get_fails = 1000;
  assert(clockTimeWriteLocal(&set) == HAL_EIO);
  const int sets = kv_writes;
  loop_pass();
  assert(kv_writes == sets + 1 && !clockTimeIsUtc());
  loop_for(retryMs - 40u);
  kv_get_fails = 0;
  loop_pass(); // the retry time minus 20 ms
  assert(kv_writes == sets + 1 && !clockTimeIsUtc());
  loop_pass();
  assert(clockTimeIsUtc() && clockTimeSettled());
  expect_local(2026, 7, 1, 14, 0, 0, 2);

  // The same without CAN: the loop still confirms the marker.
  kv.clear();
  rtc_fields(2026, 7, 1, 13, 0, 0);
  can_create_fails = true;
  PCF_Init(PCF_TIMER_INTERRUPT_ENABLE);
  assert(clockTimeInit() == HAL_OK);
  assert(clockCanInit() == HAL_EIO);
  kv_get_fails = 1000;
  assert(clockTimeWriteLocal(&set) == HAL_EIO);
  const int setsWithoutCan = kv_writes;
  const size_t framesWithoutCan = can_tx.size();
  loop_pass();
  assert(kv_writes == setsWithoutCan + 1 && !clockTimeIsUtc());
  kv_get_fails = 0;
  loop_for(retryMs);
  assert(clockTimeIsUtc() && clockTimeSettled() &&
         can_tx.size() == framesWithoutCan);
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  can_create_fails = false;

  // The RTC cannot be read while the marker waits: neither the flash nor
  // the RTC is written, and the read error is returned for the retry.
  kv.clear();
  rtc_fields(2026, 7, 1, 13, 0, 0);
  start();
  kv_get_fails = 1;
  assert(clockTimeWriteLocal(&set) == HAL_EIO && !clockTimeSettled());
  const int kvBefore = kv_writes;
  const int rtcBefore = pcf.timeWrites;
  pcf.failTimeReads = 1;
  assert(clockTimeSettle() == HAL_EIO);
  assert(kv_writes == kvBefore && pcf.timeWrites == rtcBefore &&
         !clockTimeIsUtc() && !clockTimeSettled());
  assert(clockTimeSettle() == HAL_OK && clockTimeIsUtc());
  expect_local(2026, 7, 1, 14, 0, 0, 2);

  // A marker found in the flash counts even when its write reported an error.
  kv.clear();
  rtc_fields(2026, 7, 1, 13, 0, 0);
  start();
  kv_set_misreports = true;
  assert(clockTimeWriteLocal(&set) == HAL_OK && clockTimeIsUtc());
  read_local(&trusted);
  assert(trusted);
  kv_set_misreports = false;

  // The RTC takes the write but the read-back fails. Without a marker in
  // the flash the same instant goes back as local time, also after a restart.
  kv.clear();
  rtc_fields(2026, 7, 1, 13, 0, 0);
  start();
  pcf.failTimeReads = 1;
  assert(clockTimeWriteLocal(&set) == HAL_EIO);
  assert(rtc_utc() == utc(2026, 7, 1, 14, 0, 0) && kv.empty() &&
         clockTimeSettled());
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  start();
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  read_local(&trusted);
  assert(!trusted);
  // With the marker the RTC is UTC either way.
  make_utc(utc(2026, 7, 1, 12, 0, 0));
  pcf.failTimeReads = 1;
  const PCF_DateTime later = local_time(2026, 7, 1, 15, 0, 0);
  assert(clockTimeWriteLocal(&later) == HAL_EIO);
  assert(rtc_utc() == utc(2026, 7, 1, 13, 0, 0) && clockTimeIsUtc());
  expect_local(2026, 7, 1, 15, 0, 0, 2);

  // A refused write without a marker in the flash: the same instant is
  // written as local time instead.
  kv.clear();
  rtc_fields(2026, 7, 1, 13, 0, 0);
  start();
  pcf.failTimeWrites = 1;
  assert(clockTimeWriteLocal(&set) == HAL_EIO);
  assert(rtc_utc() == utc(2026, 7, 1, 14, 0, 0) && clockTimeSettled());
  expect_local(2026, 7, 1, 14, 0, 0, 2);

  // A failed read-back while the flash gives no answer: UTC stays, and
  // only a write that is read back may store the marker.
  kv.clear();
  rtc_fields(2026, 7, 1, 13, 0, 0);
  kv_broken = true;
  start();
  pcf.failTimeReads = 1;
  assert(clockTimeWriteLocal(&set) == HAL_EIO);
  assert(rtc_utc() == utc(2026, 7, 1, 12, 0, 0) && clockTimeSettled());
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  read_local(&trusted);
  assert(!trusted);
  kv_broken = false;
  loop_for(retryMs);
  assert(kv.empty());
  // The same after a write over UTC that waited for its marker, whether
  // the RTC refused it or took it without the read-back.
  int *faults[] = {&pcf.failTimeWrites, &pcf.failTimeReads};
  for (int *fault : faults) {
    kv_broken = true;
    start();
    assert(clockTimeWriteLocal(&set) == HAL_EIO && !clockTimeSettled());
    *fault = 1;
    assert(clockTimeWriteLocal(&later) == HAL_EIO && clockTimeSettled());
    kv_broken = false;
    loop_for(retryMs);
    assert(kv.empty());
  }
  assert(clockTimeWriteLocal(&set) == HAL_OK && clockTimeIsUtc());

  // The flash answers without the marker but the RTC refuses the local
  // write: UTC stays and the loop puts the local time back later.
  kv.clear();
  rtc_fields(2026, 7, 1, 13, 0, 0);
  start();
  kv_write_fails = true;
  pcf.writesBeforeError = 1;
  assert(clockTimeWriteLocal(&set) == HAL_EIO);
  assert(rtc_utc() == utc(2026, 7, 1, 12, 0, 0) && !clockTimeSettled());
  expect_local(2026, 7, 1, 14, 0, 0, 2);
  pcf.writesBeforeError = -1;
  // A date outside the clock's years is never put back as local time.
  rtc_fields(CFG_RTC_MIN_YEAR, 1, 1, 0, 0, 0);
#if defined(RTC_ds3231)
  pcf.regs[MONTH_REG] |= 0x80; // next century is outside 2000..2099
#else
  pcf.regs[MONTH_REG] |= 0x80; // century bit: 1900 + 00
#endif
  const int writes = pcf.timeWrites;
  loop_pass();
  assert(pcf.timeWrites == writes && !clockTimeSettled());
  rtc_set_utc(utc(2026, 7, 1, 12, 0, 0));
  loop_for(retryMs);
  assert(rtc_utc() == utc(2026, 7, 1, 14, 0, 0) && clockTimeSettled() &&
         !clockTimeIsUtc());
  expect_local(2026, 7, 1, 14, 0, 0, 2);
}

// 0x130: UTC once a second, integrity only for trusted UTC in the frame's
// year range; only GPS UTC passes the receive filters.
static void test_rtc_frame() {
  rtc_fields(2026, 10, 15, 21, 56, 7);
  start();
  assert(can_filter_writes == (int)HAL_CAN_MAX_FILTERS);
  for (const hal_can_filter_t &filter : can_filters) {
    assert(filter.id == CAN_ID_GPS_UTC_UPDATE &&
           filter.mask == HAL_CAN_STD_ID_MASK && filter.flags == 0u);
  }
  loop_pass(500u);
  assert(can_tx.empty()); // the first frame one second after start
  Frame frame = next_rtc_frame();
  assert(frame.len == CAN_FRAME_MAX_LENGTH);
  // Legacy local time: no integrity and no date.
  for (int i = 1; i < CAN_FRAME_MAX_LENGTH; ++i) {
    assert(frame.data[i] == 0u);
  }

  make_utc(utc(2026, 10, 15, 19, 56, 7));
  frame = next_rtc_frame();
  assert(sent_frames(CAN_ID_RTC_UPDATE) == 2u);
  expect_consecutive_numbers();
  assert(frame.data[CAN_FRAME_RTC_UPDATE_INTEGRITY] == 1u);
  assert(packed(frame) == pack(2026, 10, 15, 19, 56));
  assert(frame.data[CAN_FRAME_RTC_UPDATE_SECOND] == 7u);
  // The RTC runs on; the frames follow it.
  run_for(3600u);
  frame = next_rtc_frame();
  assert(packed(frame) == pack(2026, 10, 15, 20, 56) &&
         frame.data[CAN_FRAME_RTC_UPDATE_SECOND] == 7u);
  // Edges of the frame's year range.
  make_utc(utc(FIESTA_CAN_DATETIME_MIN_YEAR, 1, 1, 0, 0, 0));
  frame = next_rtc_frame();
  assert(frame.data[CAN_FRAME_RTC_UPDATE_INTEGRITY] == 1u &&
         packed(frame) == pack(FIESTA_CAN_DATETIME_MIN_YEAR, 1, 1, 0, 0));
  make_utc(utc(FIESTA_CAN_DATETIME_MAX_YEAR, 12, 31, 23, 59, 0));
  frame = next_rtc_frame();
  assert(frame.data[CAN_FRAME_RTC_UPDATE_INTEGRITY] == 1u &&
         packed(frame) == pack(FIESTA_CAN_DATETIME_MAX_YEAR, 12, 31, 23, 59));
  const uint32_t outside[] = {
      utc(FIESTA_CAN_DATETIME_MIN_YEAR, 1, 1, 0, 0, 0) - 2u,
      utc(FIESTA_CAN_DATETIME_MAX_YEAR + 1, 1, 1, 0, 0, 0)};
  for (uint32_t epoch : outside) {
    make_utc(epoch);
    frame = next_rtc_frame();
    for (int i = 1; i < CAN_FRAME_MAX_LENGTH; ++i) {
      assert(frame.data[i] == 0u);
    }
  }
  // Lost integrity and a silent RTC: frames go on without integrity.
  rtc_set_utc(utc(2026, 10, 15, 19, 56, 7), true);
  frame = next_rtc_frame();
  assert(frame.data[CAN_FRAME_RTC_UPDATE_INTEGRITY] == 0u &&
         packed(frame) == 0u);
  pcf.busError = true;
  frame = next_rtc_frame();
  assert(frame.data[CAN_FRAME_RTC_UPDATE_INTEGRITY] == 0u &&
         packed(frame) == 0u);
}

// First synchronisation, then the RTC runs alone without GPS.
static void test_sync_first() {
  // The RTC lost its supply: the first valid frame sets it.
  rtc_fields(2000, 1, 1, 0, 0, 5, true);
  start();
  const uint32_t gpsTime = utc(2026, 10, 26, 18, 0, 0);
  gps(gpsTime);
  loop_pass();
  assert(rtc_utc() == gpsTime && !rtc_integrity_lost() && clockTimeIsUtc());
  assert(pcf.timeWrites == 1);
  // GPS goes away; the RTC keeps running and stays trusted.
  run_for(7200u);
  for (int i = 0; i < 5; ++i) {
    loop_pass(1000u);
  }
  assert(next_rtc_frame().data[CAN_FRAME_RTC_UPDATE_INTEGRITY] == 1u);
  assert(rtc_utc() == gpsTime + 7200u && pcf.timeWrites == 1);
  // A restart of the clock keeps the marker and needs no new write.
  start();
  assert(clockTimeIsUtc());
  gps_offset(0);
  assert(pcf.timeWrites == 1);
}

// The legacy local time of the older firmware is replaced at once, even with
// a valid integrity bit.
static void test_sync_legacy() {
  rtc_fields(2026, 10, 26, 19, 0, 0);
  start();
  const uint32_t gpsTime = utc(2026, 10, 26, 18, 0, 1);
  gps(gpsTime);
  loop_pass();
  assert(rtc_utc() == gpsTime && clockTimeIsUtc() && pcf.timeWrites == 1);
  expect_local(2026, 10, 26, 19, 0, 1, 0);
}

// Tolerance, both directions, the 10 min limit and a millisecond wrap.
static void test_sync_correction() {
  // The correction limit spans the wrap of hal_millis().
  now_ms = 0xffffffffu - CFG_RTC_SYNC_CORRECTION_INTERVAL_MS / 2u;
  rtc_fields(2026, 10, 26, 18, 0, 0);
  start();
  make_utc(utc(2026, 10, 26, 18, 0, 0));
  const int tolerance = (int)CFG_RTC_SYNC_TOLERANCE_S;
  gps_offset(tolerance);
  gps_offset(-tolerance);
  assert(pcf.timeWrites == 1);
  const uint32_t ahead = rtc_utc() + (uint32_t)tolerance + 1u;
  gps(ahead);
  loop_pass();
  assert(pcf.timeWrites == 2 && rtc_utc() == ahead);
  // Within the limit even a large error waits.
  gps_offset(-60);
  assert(pcf.timeWrites == 2);
  const uint32_t limitMs = CFG_RTC_SYNC_CORRECTION_INTERVAL_MS;
  const uint32_t passMs = 1000u;
  uint32_t elapsed = 20u; // the pass since the correction
  while (elapsed + passMs < limitMs) {
    loop_pass(passMs);
    elapsed += passMs;
  }
  gps_offset(-60);
  elapsed += 20u;
  assert(elapsed < limitMs && pcf.timeWrites == 2);
  loop_pass(limitMs - elapsed - 20u);
  const uint32_t behind = rtc_utc() - (uint32_t)tolerance - 1u;
  gps(behind);
  loop_pass(); // exactly the limit after the correction
  assert(pcf.timeWrites == 3 && rtc_utc() == behind);
  assert(now_ms < 0x10000000u); // the limit ran across the wrap
  // An RTC that lost its integrity does not wait for the limit.
  pcf.regs[INTEGRITY_REG] |= 0x80;
  gps_offset(0);
  assert(pcf.timeWrites == 4 && !rtc_integrity_lost());
}

// Bus, verification and KV errors: no use of frames for 30 s.
static void test_sync_errors() {
  rtc_fields(2026, 10, 26, 18, 0, 0, true);
  start();
  const uint32_t retryMs = CFG_RTC_SYNC_RETRY_MS;
  bool *faults[] = {&pcf.busError, &pcf.dropWrites, &pcf.vlStuck,
                    &kv_write_fails};
  for (bool *fault : faults) {
    *fault = true;
    gps(utc(2026, 10, 26, 17, 0, 0));
    loop_pass();
    assert(!clockTimeIsUtc());
    if (fault == &kv_write_fails) {
      // Without the marker the GPS time stays in the RTC as local CET.
      assert(rtc_utc() == utc(2026, 10, 26, 18, 0, 0));
    }
    *fault = false;
    // Until the retry time fresh frames stay unused, then the next one counts.
    const int attempted = pcf.timeWrites;
    loop_for(retryMs - 40u);
    gps(utc(2026, 10, 26, 17, 0, 0));
    loop_pass(); // the retry time minus 20 ms
    assert(pcf.timeWrites == attempted && !clockTimeIsUtc());
    gps(utc(2026, 10, 26, 17, 0, 1));
    loop_pass();
    assert(pcf.timeWrites == attempted + 1 && clockTimeIsUtc());
    assert(rtc_utc() == utc(2026, 10, 26, 17, 0, 1));
    // Back to an RTC without trusted UTC for the next fault.
    pcf.regs[INTEGRITY_REG] |= 0x80;
    kv.clear();
    start();
    assert(!clockTimeIsUtc());
  }
  // Contents that are no valid date are set at once, not retried.
  pcf.regs[MONTH_REG] = 0x13; // month 13
  const int before = pcf.timeWrites;
  gps(utc(2026, 10, 26, 17, 30, 0));
  loop_pass();
  assert(pcf.timeWrites == before + 1 &&
         rtc_utc() == utc(2026, 10, 26, 17, 30, 0));
  // The 10 min limit runs. A failed date read is a bus error even when the
  // integrity read answers: no write, and frames wait for the retry time,
  // also for an RTC that would otherwise be set at once.
  const int corrected = pcf.timeWrites;
  pcf.failTimeReads = 1;
  gps_offset(-60);
  assert(pcf.timeWrites == corrected && pcf.failTimeReads == 0);
  pcf.regs[INTEGRITY_REG] |= 0x80;
  loop_for(retryMs - 40u);
  gps_offset(0); // the retry time minus 20 ms
  assert(pcf.timeWrites == corrected);
  gps_offset(0);
  assert(pcf.timeWrites == corrected + 1 && !rtc_integrity_lost());
  // Contents that are no valid date are set at once also within the limit.
  pcf.regs[MONTH_REG] = 0x13;
  gps(utc(2026, 10, 26, 17, 40, 0));
  loop_pass();
  assert(pcf.timeWrites == corrected + 2 &&
         rtc_utc() == utc(2026, 10, 26, 17, 40, 0));
}

// Frames the clock must not use.
static void test_sync_rejects() {
  rtc_fields(2026, 10, 26, 18, 0, 0, true);
  start();
  const uint32_t gpsTime = utc(2026, 10, 26, 17, 0, 0);
  gps(gpsTime, false);
  loop_pass();
  gps(gpsTime, true, CAN_FRAME_GPS_UTC_STATUS); // no status byte
  loop_pass();
  gps(utc(CFG_RTC_MIN_YEAR, 1, 1, 0, 0, 0) - 1u);
  loop_pass();
  gps(utc(CFG_RTC_MAX_YEAR, 12, 31, 23, 59, 59) + 1u);
  loop_pass();
  // Frames read after a long pause may have waited in the queue.
  gps(gpsTime);
  loop_pass(CFG_RTC_SYNC_MAX_RX_GAP_MS + 1u);
  // Edited time stays as the user leaves it.
  gps(gpsTime);
  loop_pass(20u, true);
  // The newest frame of a pass counts: fix lost after a valid frame.
  gps(gpsTime);
  gps(gpsTime, false);
  loop_pass();
  assert(pcf.timeWrites == 0 && rtc_integrity_lost());
  // A pause of exactly the limit still counts as fresh.
  gps(gpsTime);
  loop_pass(CFG_RTC_SYNC_MAX_RX_GAP_MS);
  assert(pcf.timeWrites == 1 && rtc_utc() == gpsTime);
  // After the edit the next fresh frame is evaluated again.
  write_local(2026, 10, 26, 12, 0, 0);
  run_for(CFG_RTC_SYNC_CORRECTION_INTERVAL_MS / 1000u);
  gps_offset(3600, true);
  assert(pcf.timeWrites == 2);
  gps(utc(2026, 10, 26, 17, 0, 0));
  gps(utc(2026, 10, 26, 17, 0, 1));
  loop_pass();
  assert(pcf.timeWrites == 3 && rtc_utc() == utc(2026, 10, 26, 17, 0, 1));
}

static ClockValues *sc_values(bool staging) {
  return static_cast<ClockValues *>(
      staging ? sc.staging_values : const_cast<void *>(sc.active_values));
}

static hal_status_t sc_commit(const ClockValues &values, const char **reason) {
  *sc_values(true) = values;
  sc.set_applied(sc.user);
  size_t count = 0;
  return sc.commit(sc.user, reason, &count);
}

// SC reads and sets CET/CEST; the RTC keeps UTC.
static void test_sc() {
  static_assert(sizeof(ClockValues) == 7 * sizeof(int16_t), "SC values");
  rtc_fields(2026, 10, 15, 21, 56, 0);
  start();
  configSessionInit();
  assert(sc.commit != nullptr && sc.refresh != nullptr);
  ClockValues *active = sc_values(false);
  assert(active->year == 2026 && active->hour == 21 && active->integrity == 0);

  sc_active = true;
  assert(!configSessionEditPending());
  *sc_values(true) = {2026, 7, 1, 14, 0, 0, 0};
  sc.set_applied(sc.user);
  assert(configSessionEditPending());
  sc_active = false;
  assert(!configSessionEditPending()); // an ended session edits nothing
  sc_active = true;
  const char *reason = nullptr;
  size_t count = 0;
  assert(sc.commit(sc.user, &reason, &count) == HAL_OK);
  assert(!configSessionEditPending());
  assert(rtc_utc() == utc(2026, 7, 1, 12, 0, 0) && clockTimeIsUtc());
  assert(active->hour == 14 && active->integrity == 1);
  run_for(3600u);
  sc.refresh(sc.user);
  assert(active->hour == 15 && active->integrity == 1);

  assert(sc_commit({2027, 2, 29, 12, 0, 0, 0}, &reason) == HAL_EINVAL &&
         std::strcmp(reason, "day_for_month") == 0);
  assert(sc_commit({CFG_RTC_MAX_YEAR + 1, 1, 1, 12, 0, 0, 0}, &reason) ==
             HAL_EINVAL &&
         std::strcmp(reason, "year_range") == 0);
  assert(sc_commit({2028, 2, 29, 12, 0, 0, 0}, &reason) == HAL_OK);
  assert(rtc_utc() == utc(2028, 2, 29, 11, 0, 0));
  sc.revert(sc.user);
  assert(!configSessionEditPending());

  pcf.busError = true;
  assert(sc_commit({2026, 1, 1, 12, 0, 0, 0}, &reason) == HAL_EIO &&
         std::strcmp(reason, "rtc_set_failed") == 0);
  sc.refresh(sc.user);
  assert(active->year == 2028 && active->integrity == 0);
  pcf.busError = false;
  sc.refresh(sc.user);
  assert(active->integrity == 1);
}

static void set_mode_pass(bool hour, int presses) {
  button_hour = hour;
  set_presses = presses;
  texts.clear();
  clockMainFunction();
  button_hour = false;
}

// Buttons show and set CET/CEST; the weekday follows the date.
static void test_buttons() {
  rtc_fields(2026, 10, 15, 21, 56, 0);
  start();
  texts.clear();
  clockMainFunction();
  assert(texts.at(0) == "15-10 2026, czw. " && texts.at(1) == "21:56");

  make_utc(utc(2026, 10, 24, 22, 30, 0));
  texts.clear();
  clockMainFunction();
  assert(texts.at(0) == "25-10 2026, niedz. " && texts.at(1) == "00:30");

  // The hour button in normal mode stores local + 1 h.
  button_hour = true;
  clockMainFunction();
  button_hour = false;
  assert(rtc_utc() == utc(2026, 10, 24, 23, 30, 0));

  setClockSetMode(true);
  set_mode_pass(true, 0); // day + 1 on the first item
  assert(texts.at(0) == "26-10 2026, pon. ");
  // Counting the seconds of the idle timer keeps the edited values.
  startTimerForSeconds(60u);
  (void)checkIfTimerReached();
  set_mode_pass(false, 0);
  assert(texts.at(0) == "26-10 2026, pon. " && texts.at(1) == "01:30");
  // Day, month, year, hour, minute, second: the weekday is not an item.
  for (int item = 0; item < 5; ++item) {
    set_mode_pass(false, 1);
    assert(isClockSetMode());
  }
  assert(rtc_utc() == utc(2026, 10, 24, 23, 30, 0));
  set_mode_pass(false, 1);
  assert(!isClockSetMode());
  assert(rtc_utc() == utc(2026, 10, 26, 0, 30, 0)); // CET after the change
}

static int16_t air_temp(const Frame &frame, int hi) {
  return (int16_t)jh_load_be16(&frame.data[hi]);
}

static bool air_temp_valid(const Frame &frame, uint8_t bit) {
  return (frame.data[CAN_FRAME_AIR_TEMP_STATUS] & bit) != 0u;
}

// Passes until the next 0x132 frame; returns the milliseconds it took.
static uint32_t next_air_frame(uint32_t limitMs) {
  const size_t sent = sent_frames(CAN_ID_AIR_TEMP_UPDATE);
  const uint32_t startMs = now_ms;
  while (sent_frames(CAN_ID_AIR_TEMP_UPDATE) == sent) {
    assert(now_ms - startMs < limitMs);
    loop_pass();
  }
  return now_ms - startMs;
}

// Air temperatures on 0x132: with the RTC frame every second and at once when
// a reading or its validity changes.
static void test_air_temp() {
  // Nearest 0.1 °C, negative values and the int16 limits.
  const struct {
    float celsius;
    int16_t tenths;
  } values[] = {{21.4375f, 214},     {-3.0625f, -31},    {0.0625f, 1},
                {-0.0625f, -1},      {0.03125f, 0},      {-0.03125f, 0},
                {125.0f, 1250},      {-55.0f, -550},     {1e30f, INT16_MAX},
                {-1e30f, INT16_MIN}, {3e38f, INT16_MAX}, {-3e38f, INT16_MIN}};
  uint8_t field[2] = {};
  for (const auto &value : values) {
    assert(fiesta_can_put_temp_tenths_i16(field, value.celsius) &&
           (int16_t)jh_load_be16(field) == value.tenths);
  }
  for (float none : {NAN, INFINITY, -INFINITY}) {
    field[0] = field[1] = 0xffu;
    assert(!fiesta_can_put_temp_tenths_i16(field, none) && field[0] == 0u &&
           field[1] == 0u);
  }

  rtc_fields(2026, 10, 15, 21, 56, 7);
  temp_inside.present = temp_outside.present = true;
  temp_inside.celsius = 21.4375f;
  temp_outside.celsius = -3.0625f;
  start();
  ds18b20_setPin(JH_HW_NODE_TEMP_INSIDE_PIN_DATA); // as temp_initial_read()
  ds18b20_setPin(JH_HW_NODE_TEMP_OUTSIDE_PIN_DATA);

  // The first readings go out when they arrive, before the RTC deadline.
  assert(next_air_frame(1000u) <= ds18b20_conversion_ms + 20u);
  assert(sent_frames(CAN_ID_RTC_UPDATE) == 0u);
  Frame frame = last_frame(CAN_ID_AIR_TEMP_UPDATE);
  assert(frame.len == CAN_FRAME_MAX_LENGTH);
  assert(air_temp_valid(frame, CAN_AIR_TEMP_STATUS_INSIDE_VALID) &&
         air_temp_valid(frame, CAN_AIR_TEMP_STATUS_OUTSIDE_VALID));
  assert(air_temp(frame, CAN_FRAME_AIR_TEMP_INSIDE_HI) == 214 &&
         air_temp(frame, CAN_FRAME_AIR_TEMP_OUTSIDE_HI) == -31);
  assert(frame.data[CAN_FRAME_AIR_TEMP_STATUS] ==
             (CAN_AIR_TEMP_STATUS_INSIDE_VALID |
              CAN_AIR_TEMP_STATUS_OUTSIDE_VALID) &&
         frame.data[6] == 0u && frame.data[7] == 0u);

  // Steady readings, also a change below 0.1 °C: one frame per RTC frame.
  next_rtc_frame();
  temp_inside.celsius = 21.375f;
  size_t rtc = sent_frames(CAN_ID_RTC_UPDATE);
  size_t air = sent_frames(CAN_ID_AIR_TEMP_UPDATE);
  loop_for(10000u);
  assert(sent_frames(CAN_ID_RTC_UPDATE) - rtc == 10u &&
         sent_frames(CAN_ID_AIR_TEMP_UPDATE) - air == 10u);
  assert(air_temp(last_frame(CAN_ID_AIR_TEMP_UPDATE),
                  CAN_FRAME_AIR_TEMP_INSIDE_HI) == 214);

  // A new value goes out with its conversion, not at the next deadline.
  next_rtc_frame();
  temp_outside.celsius = -2.9375f;
  rtc = sent_frames(CAN_ID_RTC_UPDATE);
  assert(next_air_frame(1000u) <= ds18b20_conversion_ms + 20u);
  assert(sent_frames(CAN_ID_RTC_UPDATE) == rtc);
  assert(air_temp(last_frame(CAN_ID_AIR_TEMP_UPDATE),
                  CAN_FRAME_AIR_TEMP_OUTSIDE_HI) == -29);

  // A sensor that stops answering loses its bit once its last reading is
  // CFG_TEMP_MAX_AGE_MS old; the change goes out at once.
  next_rtc_frame();
  temp_outside.present = false;
  for (;;) {
    loop_pass();
    frame = last_frame(CAN_ID_AIR_TEMP_UPDATE);
    if (!air_temp_valid(frame, CAN_AIR_TEMP_STATUS_OUTSIDE_VALID)) {
      break;
    }
    assert(now_ms - temp_outside.sampleMs < CFG_TEMP_MAX_AGE_MS);
  }
  assert(now_ms - temp_outside.sampleMs < CFG_TEMP_MAX_AGE_MS + 20u);
  assert(air_temp(frame, CAN_FRAME_AIR_TEMP_OUTSIDE_HI) == 0 &&
         air_temp_valid(frame, CAN_AIR_TEMP_STATUS_INSIDE_VALID) &&
         air_temp(frame, CAN_FRAME_AIR_TEMP_INSIDE_HI) == 214);
  rtc = sent_frames(CAN_ID_RTC_UPDATE);
  air = sent_frames(CAN_ID_AIR_TEMP_UPDATE);
  loop_for(5000u);
  assert(sent_frames(CAN_ID_AIR_TEMP_UPDATE) - air ==
         sent_frames(CAN_ID_RTC_UPDATE) - rtc);
  assert(!air_temp_valid(last_frame(CAN_ID_AIR_TEMP_UPDATE),
                         CAN_AIR_TEMP_STATUS_OUTSIDE_VALID));

  // It answers again: the next conversion brings the bit back.
  next_rtc_frame();
  temp_outside.present = true;
  assert(next_air_frame(1000u) <= ds18b20_conversion_ms + 40u);
  frame = last_frame(CAN_ID_AIR_TEMP_UPDATE);
  assert(air_temp_valid(frame, CAN_AIR_TEMP_STATUS_OUTSIDE_VALID) &&
         air_temp(frame, CAN_FRAME_AIR_TEMP_OUTSIDE_HI) == -29);
  expect_consecutive_numbers();
}

// A sensor missing at start stays without its bit until it is opened again,
// as the temperature display and an ignition change do.
static void test_air_temp_missing() {
  rtc_fields(2026, 10, 15, 21, 56, 7);
  temp_outside.present = true;
  temp_outside.celsius = 5.0f;
  start();
  ds18b20_setPin(JH_HW_NODE_TEMP_INSIDE_PIN_DATA);
  ds18b20_setPin(JH_HW_NODE_TEMP_OUTSIDE_PIN_DATA);
  temp_inside.present = true;
  temp_inside.celsius = 20.0f;
  loop_for(5000u);
  Frame frame = last_frame(CAN_ID_AIR_TEMP_UPDATE);
  assert(frame.data[CAN_FRAME_AIR_TEMP_STATUS] ==
             CAN_AIR_TEMP_STATUS_OUTSIDE_VALID &&
         air_temp(frame, CAN_FRAME_AIR_TEMP_INSIDE_HI) == 0 &&
         air_temp(frame, CAN_FRAME_AIR_TEMP_OUTSIDE_HI) == 50);
  ds18b20_setPin(JH_HW_NODE_TEMP_INSIDE_PIN_DATA);
  assert(next_air_frame(1000u) <= ds18b20_conversion_ms + 20u);
  frame = last_frame(CAN_ID_AIR_TEMP_UPDATE);
  assert(air_temp_valid(frame, CAN_AIR_TEMP_STATUS_INSIDE_VALID) &&
         air_temp(frame, CAN_FRAME_AIR_TEMP_INSIDE_HI) == 200);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const struct {
    const char *name;
    void (*run)(void);
  } scenarios[] = {
      {"local", test_local},
      {"marker", test_marker},
      {"transition", test_transition},
      {"rtc_frame", test_rtc_frame},
      {"sync_first", test_sync_first},
      {"sync_legacy", test_sync_legacy},
      {"sync_correction", test_sync_correction},
      {"sync_errors", test_sync_errors},
      {"sync_rejects", test_sync_rejects},
      {"sc", test_sc},
      {"buttons", test_buttons},
      {"air_temp", test_air_temp},
      {"air_temp_missing", test_air_temp_missing},
  };
  for (const auto &scenario : scenarios) {
    if (std::strcmp(argv[1], scenario.name) == 0) {
      scenario.run();
      std::puts("PASS");
      return 0;
    }
  }
  assert(false);
  return 1;
}
