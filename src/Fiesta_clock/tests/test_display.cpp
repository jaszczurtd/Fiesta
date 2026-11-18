/* Production UI + HAL renderer/SSD1306, with only board I/O replaced. */
extern "C" {
#include "main.h"
#include <hal/core/hal_app.h>
}
#include <hal/spi/hal_spi_device.h>

#include "support/ssd1306_panel.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<std::vector<uint8_t>> frames;
static std::vector<uint8_t> tx;
static bool bus_error = false;
static bool ignition_on = false;
static bool set_down = false;
static int set_reads_until_release = 0;
static bool hour_down = false;
static bool minute_down = false;
static uint32_t fake_ms = 0;
static bool blue_level = false;
static bool sc_edit_pending = false;
static bool can_time_edit = false;
static int ds18b20_polls = 0;
static int bus_inits = 0;
static int bus_clears = 0;
static RTC_DateTime now = {};
static std::array<uint8_t, 512> greeting;
static std::array<hal_gpio_mode_t, 32> gpio_modes;

static Ssd1306Panel panel;

static std::array<uint8_t, 512> image() {
  std::array<uint8_t, 512> result;
  std::copy_n(panel.ram.begin(), result.size(), result.begin());
  return result;
}

static bool lit(const std::array<uint8_t, 512> &pixels, int x, int y) {
  return (pixels[(y / 8) * 128 + x] & (1u << (y % 8))) != 0;
}

static int command_count(uint8_t command) {
  return (int)std::count(frames.begin(), frames.end(),
                         std::vector<uint8_t>{0, command});
}

static void clear() { assert(clock_display_clear() == HAL_OK); }
static void flush() { assert(hal_display_flush_ex() == HAL_OK); }

/* One main loop pass: app_task0() runs a pass every CFG_MAIN_PASS_MS. */
static void pass() {
  fake_ms += CFG_MAIN_PASS_MS;
  app_task0();
}

extern "C" {
hal_status_t hal_i2c_init_bus(uint8_t bus, uint8_t sda, uint8_t scl,
                              uint32_t hz) {
  assert(bus == 0 && sda == 8 && scl == 9 && hz == 100000);
  // Every bus start first releases a slave that may hold SDA low.
  assert(bus_clears == bus_inits + 1);
  ++bus_inits;
  return bus_error ? HAL_EIO : HAL_OK;
}
hal_status_t hal_i2c_bus_clear_bus(uint8_t bus, uint8_t sda, uint8_t scl) {
  assert(bus == 0 && sda == 8 && scl == 9);
  ++bus_clears;
  return HAL_OK;
}
hal_status_t hal_i2c_set_clock_bus(uint8_t bus, uint32_t hz) {
  assert(bus == 0 && hz == 100000);
  return HAL_OK;
}
void hal_i2c_begin_transmission_bus(uint8_t bus, hal_i2c_address_t addr) {
  assert(bus == 0 && addr == 0x3c);
  tx.clear();
}
size_t hal_i2c_write_bus(uint8_t, uint8_t value) {
  tx.push_back(value);
  return 1;
}
uint8_t hal_i2c_end_transmission_bus(uint8_t) {
  if (bus_error) {
    return 2;
  }
  panel.frame(tx.data(), tx.size());
  frames.push_back(tx);
  return 0;
}
void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode) {
  gpio_modes.at(pin) = mode;
}
void hal_gpio_write(uint8_t pin, bool value) {
  if (pin == JH_HW_NODE_LED_BLUE_PIN_OUT) {
    blue_level = value;
  }
}
bool hal_gpio_read(uint8_t pin) {
  if (pin == JH_HW_NODE_IGNITION_PIN_IN) {
    return !ignition_on;
  }
  if (pin == JH_HW_NODE_BUTTON_HOUR_PIN_IN) {
    return !hour_down;
  }
  if (pin == JH_HW_NODE_BUTTON_MINUTE_PIN_IN) {
    return !minute_down;
  }
  if (pin == JH_HW_NODE_BUTTON_SET_PIN_IN) {
    if (set_reads_until_release > 0 && --set_reads_until_release == 0) {
      // The prompt must already be visible when the blocking wait reads SET.
      const auto pixels = image();
      assert(std::any_of(pixels.begin() + 256, pixels.end(),
                         [](uint8_t b) { return b != 0; }));
      set_down = false;
    }
    return !set_down;
  }
  return true;
}
void hal_delay_ms(uint32_t) {}
uint32_t hal_millis(void) { return fake_ms; }
void hal_adc_set_resolution(uint8_t) {}
hal_status_t hal_watchdog_enable(uint32_t, bool) { return HAL_OK; }
void hal_watchdog_feed(void) {}
void hal_debug_init_default(void) {}
void hal_debug_set_module_prefix(const char *) {}
void hal_deb(const char *, ...) {}
void hal_derr(const char *, ...) {}
void hal_derr_limited(const char *, const char *, ...) {}
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
hal_status_t hal_spi_device_init(hal_spi_device_t *, uint8_t, uint8_t,
                                 const hal_spi_settings_t *) {
  assert(false);
  return HAL_EIO;
}
hal_status_t hal_spi_device_acquire(hal_spi_device_t *) {
  assert(false);
  return HAL_EIO;
}
hal_status_t hal_spi_device_finish(hal_spi_device_t *, hal_status_t) {
  assert(false);
  return HAL_EIO;
}
hal_status_t hal_spi_write(uint8_t, const uint8_t *, size_t) {
  assert(false);
  return HAL_EIO;
}

void rtc_Init(unsigned char) {}
unsigned char rtc_GetDateTime(RTC_DateTime *value) {
  *value = now;
  return 0;
}
hal_status_t clockTimeInit(void) { return HAL_OK; }
hal_status_t clockTimeReadLocal(RTC_DateTime *value, bool *trusted) {
  *value = now;
  if (trusted) {
    *trusted = true;
  }
  return HAL_OK;
}
hal_status_t clockTimeWriteLocal(const RTC_DateTime *value) {
  now = *value;
  return HAL_OK;
}
/* Every step of an edited date updates its weekday. */
static int edited_day = 0;
void clockTimeUpdateWeekday(RTC_DateTime *dt) { edited_day = dt->day; }
void clockTimeLogTick(void) {}
hal_status_t clockCanInit(void) { return HAL_OK; }
void clockCanTick(bool timeEditActive) { can_time_edit = timeEditActive; }
void configSessionInit(void) {}
void configSessionTick(void) {}
bool configSessionEditPending(void) { return sc_edit_pending; }
void ds18b20_setPin(unsigned char) {}
void ds18b20_poll(void) { ++ds18b20_polls; }
void ds18b20_gettemp(int *hi, int *lo, double *native) {
  if (greeting[0] == 0 && greeting[48] == 0) {
    greeting = image();
  }
  *hi = 21;
  *lo = 5;
  if (native) {
    *native = 21.5;
  }
}
int getADCValue(unsigned char) { return 240; }
}

static void test_text() {
  assert(clock_display_init() == HAL_OK);
  clear();
  assert(clock_display_text(4, 2, 2, "88:88") == HAL_OK);
  assert(clock_display_text(4, 2, 2, "11 11") == HAL_OK);
  flush();
  const auto replaced = image();
  clear();
  assert(clock_display_text(4, 2, 2, "11 11") == HAL_OK);
  flush();
  assert(image() == replaced);
  assert(clock_display_text(4, 2, 2, "     ") == HAL_OK);
  flush();
  assert(image() == (std::array<uint8_t, 512>{}));
  assert(clock_display_text(20, 0, 1, "ABCDE") == HAL_OK);
  flush();
  auto pixels = image();
  assert(std::any_of(pixels.begin(), pixels.end(),
                     [](uint8_t b) { return b != 0; }));
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 128; ++x) {
      if (x < 120 || y >= 8) {
        assert(!lit(pixels, x, y));
      }
    }
  }
  assert(clock_display_text(0, 0, 1, "") == HAL_OK);
  assert(clock_display_text(21, 0, 2, "clipped") == HAL_OK);
  flush();
  assert(image() == pixels);
  assert(clock_display_text(0, 0, 0, "bad") == HAL_EINVAL);
  assert(clock_display_text(0, 3, 2, "bad") == HAL_EINVAL);
  assert(clock_display_text(22, 0, 1, "bad") == HAL_EINVAL);
  assert(clock_display_text(0, 0, 1, nullptr) == HAL_EINVAL);

  // A padded field erases what a longer text left in it.
  clear();
  assert(clock_display_field(0, 0, 1, CLOCK_DISPLAY_COLUMNS, "niedz. 12") ==
         HAL_OK);
  assert(clock_display_field(0, 0, 1, CLOCK_DISPLAY_COLUMNS, "wt.") == HAL_OK);
  const auto shorter = image();
  clear();
  assert(clock_display_text(0, 0, 1, "wt.") == HAL_OK);
  assert(image() == shorter);
  // At scale 2 the field holds whole 12-pixel cells and ends before
  // end_column: (9 - 2) / 2 = 3 cells, x 12..47.
  clear();
  assert(clock_display_text(10, 2, 2, "8") == HAL_OK);
  const auto neighbour = image();
  assert(clock_display_field(2, 2, 2, 9, "8888") == HAL_OK);
  pixels = image();
  for (int y = 16; y < 32; ++y) {
    for (int x = 0; x < 128; ++x) {
      if (x < 12 || (x >= 48 && x < 60)) {
        assert(!lit(pixels, x, y));
      } else if (x >= 60) {
        assert(lit(pixels, x, y) == lit(neighbour, x, y));
      }
    }
  }
  assert(clock_display_field(5, 0, 1, 4, "bad") == HAL_EINVAL);
  assert(clock_display_field(0, 0, 1, CLOCK_DISPLAY_COLUMNS + 1, "bad") ==
         HAL_EINVAL);
}

static void test_recover() {
  assert(clock_display_init() == HAL_OK);
  const int configs = command_count(0xa8);
  clock_display_service(); // nothing failed
  assert(bus_inits == 1 && command_count(0xa8) == configs);
  bus_error = true;
  assert(clock_display_text(0, 0, 1, "12") == HAL_EIO);
  fake_ms = 5000;
  clock_display_service(); // the retry fails on the broken bus
  assert(bus_inits == 2);
  bus_error = false;
  fake_ms += 500;
  clock_display_service(); // one retry per second
  assert(bus_inits == 2);
  fake_ms += 500;
  clock_display_service();
  assert(bus_inits == 3 && command_count(0xa8) == configs + 1);
  fake_ms += 5000;
  clock_display_service(); // recovered: nothing to do
  assert(bus_inits == 3);
  bus_error = true;
  assert(clock_display_restart() == HAL_EIO);
  bus_error = false;
  fake_ms += 1000;
  clock_display_service();
  assert(bus_inits == 4);
  // Invalid arguments are a caller error, not a panel failure.
  assert(clock_display_text(0, 0, 0, "x") == HAL_EINVAL);
  fake_ms += 1000;
  clock_display_service();
  assert(bus_inits == 4);
}

static void test_weekday() {
  assert(clock_display_init() == HAL_OK);
  now = {};
  now.day = 15;
  now.month = 11;
  now.year = 2026;
  now.weekday = 6; // niedz.
  clockMainFunction();
  now.weekday = 1; // wt.
  clockMainFunction();
  const auto shorter = image();
  assert(clock_display_restart() == HAL_OK);
  clockMainFunction();
  assert(image() == shorter);
}

static void press_set() {
  set_down = true;
  set_reads_until_release = 2;
  clockMainFunction();
  assert(!set_down);
}

static void press(bool &button) {
  button = true;
  clockMainFunction();
  button = false;
}

/* Edit the date from day/month/year with the buttons and leave set mode,
 * which stores it. hour = increase, minute = decrease, SET = next item. */
static RTC_DateTime edit(int day, int month, int year, bool next_item,
                         bool increase) {
  now = {};
  now.day = (unsigned char)day;
  now.month = (unsigned char)month;
  now.year = year;
  getTime();
  setClockSetMode(true);
  clockMainFunction();
  int item = 0;
  if (next_item) {
    press_set();
    item = 1;
  }
  press(increase ? hour_down : minute_down);
  // Items 0, 1, 2, 4, 5, 6 (the weekday is skipped), then set mode ends.
  for (int presses = item == 0 ? 6 : 5; presses > 0; --presses) {
    press_set();
  }
  assert(!isClockSetMode());
  return now;
}

static void test_dates() {
  assert(clock_display_init() == HAL_OK);
  RTC_DateTime stored = edit(30, 11, 2026, false, true);
  assert(stored.day == 1 && stored.month == 11); // November has 30 days
  stored = edit(1, 11, 2026, false, false);
  assert(stored.day == 30 && stored.month == 11);
  stored = edit(1, 2, 2028, false, false);
  assert(stored.day == 29 && stored.month == 2); // leap year
  stored = edit(1, 2, 2027, false, false);
  assert(stored.day == 28 && stored.month == 2);
  stored = edit(31, 10, 2026, true, true);
  assert(stored.day == 30 && stored.month == 11); // moved to the last day
  stored = edit(31, 12, 2026, false, true);
  assert(stored.day == 1 && stored.month == 12);
}

static void test_repeat() {
  assert(clock_display_init() == HAL_OK);
  now = {};
  now.day = 1;
  now.month = 11;
  now.year = 2026;
  getTime();
  setClockSetMode(true); // the day is the edited item
  fake_ms = 1000u;
  hour_down = true;
  clockMainFunction(); // the press steps at once
  assert(edited_day == 2);
  fake_ms += CFG_CLOCK_REPEAT_DELAY_MS - 1u;
  clockMainFunction(); // held, not long enough to repeat
  assert(edited_day == 2);
  fake_ms += 1u;
  clockMainFunction(); // repeating starts
  assert(edited_day == 3);
  fake_ms += CFG_CLOCK_REPEAT_INTERVAL_MS - 1u;
  clockMainFunction();
  assert(edited_day == 3);
  fake_ms += 1u;
  clockMainFunction();
  assert(edited_day == 4);
  hour_down = false;
  clockMainFunction();
  // A short press seen by several passes is one step.
  minute_down = true;
  for (int i = 0; i < 5; ++i) {
    fake_ms += 10u;
    clockMainFunction();
  }
  assert(edited_day == 3);
  minute_down = false;
  clockMainFunction();
  // A button held while set mode starts again counts as a new press.
  hour_down = true;
  fake_ms += 10u;
  clockMainFunction();
  assert(edited_day == 4);
  setClockSetMode(false);
  setClockSetMode(true);
  fake_ms += 10u;
  clockMainFunction();
  assert(edited_day == 5);
  hour_down = false;
  clockMainFunction();
  // Items 0, 1, 2, 4, 5, 6 (the weekday is skipped), then set mode ends.
  for (int presses = 6; presses > 0; --presses) {
    press_set();
  }
  assert(!isClockSetMode());
  assert(now.day == 5 && now.month == 11);
}

static void test_power() {
  assert(hal_display_suspend_ex() == HAL_EUNINIT);
  bus_error = true;
  assert(clock_display_init() == HAL_EIO);
  bus_error = false;
  assert(clock_display_init() == HAL_OK);
  assert(bus_inits == 2);
  assert(command_count(0xa8) == 1); // One panel initialisation.
  assert(clock_display_init() == HAL_OK);
  bus_error = true;
  assert(hal_display_suspend_ex() == HAL_EIO);
  bus_error = false;
  assert(hal_display_suspend_ex() == HAL_OK);
  bus_error = true;
  assert(clock_display_init() == HAL_EIO);
  bus_error = false;
  assert(clock_display_init() == HAL_OK); // Reapply settings after a bus error.
  assert(command_count(0xaf) == 3);
  assert(command_count(0xa8) == 3);
  assert(bus_inits == 5);
}

static void test_app() {
  now.day = 15;
  now.month = 10;
  now.year = 2026;
  now.hour = 21;
  now.minute = 56;
  app_start();
  assert(gpio_modes.at(JH_HW_NODE_BUTTON_HOUR_PIN_IN) == HAL_GPIO_INPUT);
  assert(gpio_modes.at(JH_HW_NODE_BUTTON_MINUTE_PIN_IN) ==
         HAL_GPIO_INPUT_PULLDOWN);
  assert(gpio_modes.at(JH_HW_NODE_BUTTON_SET_PIN_IN) == HAL_GPIO_INPUT_PULLUP);
  assert(gpio_modes.at(JH_HW_NODE_IGNITION_PIN_IN) == HAL_GPIO_INPUT_PULLDOWN);
  // The greeting is on the panel during the start-up work and stays after
  // app_start returned.
  assert(greeting != (std::array<uint8_t, 512>{}));
  assert(std::any_of(greeting.begin(), greeting.begin() + 128,
                     [](uint8_t b) { return b != 0; }));
  assert(std::any_of(greeting.begin() + 256, greeting.end(),
                     [](uint8_t b) { return b != 0; }));
  assert(image() == greeting);
  fake_ms += CFG_GREETING_MS;
  pass();
  assert(image() != greeting);
  for (unsigned i = 0; i <= CFG_IDLE_SECONDS + 1; ++i) {
    now.second = i % 60;
    pass();
  }
  const auto asleep = frames.size();
  const auto sleeping_image = image();
  const uint32_t slept_ms = fake_ms; // the pass that put the panel to sleep
  assert(frames.back() == (std::vector<uint8_t>{0, 0xae}));
  // The blue LED blinks by time on every call, whatever the number of calls
  // and passes.
  assert(blue_level == LED_ACTIVE_LEVEL);
  for (int i = 0; i < 5000; ++i) {
    app_task0();
  }
  assert(blue_level == LED_ACTIVE_LEVEL);
  fake_ms = slept_ms + CFG_IDLE_LED_ON_MS - 1u;
  app_task0();
  assert(blue_level == LED_ACTIVE_LEVEL);
  fake_ms += 1u;
  app_task0();
  assert(blue_level != LED_ACTIVE_LEVEL);
  fake_ms = slept_ms + CFG_IDLE_LED_PERIOD_MS - 1u;
  app_task0();
  assert(blue_level != LED_ACTIVE_LEVEL);
  fake_ms += 1u;
  app_task0();
  assert(blue_level == LED_ACTIVE_LEVEL);
  const int polls = ds18b20_polls;
  pass();
  // Asleep, the panel gets no further commands.
  assert(frames.size() == asleep);
  // The sensors convert for CAN also with the panel asleep.
  assert(ds18b20_polls == polls + 1);
  assert(image() == sleeping_image);
  // The controller lost its settings and RAM while asleep (supply dip).
  panel = Ssd1306Panel();
  panel.ram.fill(0x5a);
  const auto wakes = command_count(0xaf);
  hour_down = true;
  pass();
  hour_down = false;
  // The wake-up configures and clears the panel before this pass draws.
  assert(panel.enabled);
  assert(panel.registers[0xa8] == 31 && panel.registers[0x8d] == 0x14 &&
         panel.registers[0x20] == 0x02);
  const auto woken = image();
  assert(clock_display_restart() == HAL_OK);
  clockMainFunction();
  assert(image() == woken);
  pass();
  assert(command_count(0xaf) == wakes + 2); // wake-up and the restart above
  const int configs = command_count(0xa8);
  ignition_on = true;
  pass();
  // The greeting again, then the cleared first screen after its time.
  assert(command_count(0xa8) == configs + 1);
  assert(image() == greeting);
  fake_ms += CFG_GREETING_MS;
  pass();
  assert(command_count(0xa8) == configs + 2);
  assert(image() != greeting);
  assert(bus_inits == 2);
  clear();
  temp_read_display();
  flush();
  const auto temperature = image();
  assert(temperature != (std::array<uint8_t, 512>{}));
  clear();
  voltMainFunction();
  flush();
  assert(image() != temperature);
  // GPS leaves the RTC alone while SC or the buttons edit the time.
  sc_edit_pending = true;
  pass();
  assert(can_time_edit);
  sc_edit_pending = false;
  pass();
  assert(!can_time_edit);
  setClockSetMode(true);
  pass();
  assert(can_time_edit);
  setClockSetMode(false);
  pass();
  assert(!can_time_edit);
}

/* The panel image a fresh, full render of the clock screen gives. */
static std::array<uint8_t, 512> fresh_clock() {
  assert(clock_display_restart() == HAL_OK);
  clockMainFunction();
  return image();
}

static void test_unchanged() {
  assert(clock_display_init() == HAL_OK);
  now = {};
  now.day = 15;
  now.month = 11;
  now.year = 2026;
  now.hour = 12;
  now.minute = 34;
  size_t before = frames.size();
  clockMainFunction();
  const size_t full = frames.size() - before;
  assert(full > 0);
  // The same screen again sends nothing.
  before = frames.size();
  clockMainFunction();
  assert(frames.size() == before);
  // A new second sends the blinking colon and the seconds, not the screen.
  now.second = 1;
  clockMainFunction();
  const size_t changed = frames.size() - before;
  assert(changed > 0 && changed < full / 2);
  const auto partial = image();
  assert(fresh_clock() == partial);
  // The bus and panel init clears the panel, so the same screen is sent again.
  assert(clock_display_init() == HAL_OK);
  before = frames.size();
  clockMainFunction();
  assert(frames.size() - before == full);
  assert(image() == partial);

  // A failed write forgets the panel; the next draw sends the whole screen.
  bus_error = true;
  now.second = 2;
  clockMainFunction();
  bus_error = false;
  before = frames.size();
  clockMainFunction();
  assert(frames.size() - before == full);
  const auto repaired = image();
  assert(fresh_clock() == repaired);
}

static void test_refresh() {
  assert(clock_display_init() == HAL_OK);
  now = {};
  now.day = 15;
  now.month = 11;
  now.year = 2026;
  now.hour = 12;
  now.minute = 34;
  size_t before = frames.size();
  clockMainFunction();
  const size_t full = frames.size() - before;
  const auto shown = image();
  // A pixel changes on the panel without a write error (a disturbance).
  panel.ram[2 * 128 + 30] ^= 0xff;
  fake_ms += CFG_DISPLAY_REFRESH_MS - 1u;
  clock_display_service();
  before = frames.size();
  clockMainFunction();
  assert(frames.size() == before);
  assert(image() != shown);
  fake_ms += 1u;
  clock_display_service();
  clockMainFunction();
  assert(frames.size() - before == full);
  assert(image() == shown);
}

static void test_greeting() {
  now = {};
  now.day = 15;
  now.month = 11;
  now.year = 2026;
  now.hour = 12;
  now.minute = 34;
  app_start(); // greeting drawn at fake_ms 0
  assert(image() == greeting);
  app_task0(); // the first pass
  const size_t shown_frames = frames.size();
  const int polls = ds18b20_polls;
  // One ms before its time the greeting stays; the sensors keep converting
  // and a button neither draws nor wakes anything.
  fake_ms = CFG_GREETING_MS - 1u;
  hour_down = true;
  app_task0();
  hour_down = false;
  assert(ds18b20_polls == polls + 1);
  assert(image() == greeting && frames.size() == shown_frames);
  // An ignition change shows it again from that moment.
  const uint32_t again = 2u * CFG_GREETING_MS;
  fake_ms = again;
  ignition_on = true;
  app_task0();
  assert(image() == greeting);
  fake_ms = again + CFG_GREETING_MS - CFG_MAIN_PASS_MS;
  app_task0();
  assert(image() == greeting);
  // The pass at exactly its time clears the panel and draws the first screen.
  fake_ms = again + CFG_GREETING_MS;
  app_task0();
  const auto first = image();
  assert(first != greeting);
  assert(fresh_clock() == first);

  // A greeting that fails to draw does not hold the screens.
  fake_ms += CFG_MAIN_PASS_MS;
  ignition_on = false;
  app_task0();
  assert(image() == greeting);
  fake_ms += CFG_MAIN_PASS_MS;
  ignition_on = true;
  bus_error = true;
  app_task0(); // the panel init fails, so there is no greeting this time
  bus_error = false;
  fake_ms += 1000u; // the display retries the panel once a second
  app_task0();
  assert(image() != greeting);
}

static void test_pace() {
  now = {};
  now.day = 15;
  now.month = 11;
  now.year = 2026;
  app_start();
  app_task0(); // the first call always runs a pass
  int polls = ds18b20_polls;
  assert(polls == 1);
  const size_t sent = frames.size();
  fake_ms += CFG_MAIN_PASS_MS - 1u;
  app_task0();
  assert(ds18b20_polls == polls); // not due yet
  fake_ms += 1u;
  app_task0();
  assert(ds18b20_polls == polls + 1);
  // A pass with nothing new on the screen leaves the bus idle.
  assert(frames.size() == sent);
  // After a long gap one pass runs, without catching up.
  polls = ds18b20_polls;
  fake_ms += 10u * CFG_MAIN_PASS_MS;
  app_task0();
  app_task0();
  assert(ds18b20_polls == polls + 1);
}

static void test_prompt() {
  assert(clock_display_init() == HAL_OK);
  clear();
  flush();
  set_down = true;
  for (int i = 0; i <= CFG_CLOCK_SET_MODE_DELAY_LOOPS; ++i) {
    manageSetMode();
  }
  // First read enters manageSetMode, second enters the blocking button wait.
  set_reads_until_release = 2;
  manageSetMode();
  assert(!set_down);
}

static void test_glyphs() {
  assert(clock_display_init() == HAL_OK);
  for (uint8_t scale = 1; scale <= 2; ++scale) {
    for (unsigned code = 0x20; code <= 0x7f; ++code) {
      clear();
      const char text[] = {(char)code, 0};
      assert(clock_display_text(0, 0, scale, text) == HAL_OK);
      const auto pixels = image();
      for (uint8_t byte : pixels)
        std::printf("%02x", byte);
      std::puts("");
    }
  }
}

int main(int argc, char **argv) {
  assert(argc == 2);
  if (std::strcmp(argv[1], "text") == 0) {
    test_text();
  } else if (std::strcmp(argv[1], "power") == 0) {
    test_power();
  } else if (std::strcmp(argv[1], "app") == 0) {
    test_app();
  } else if (std::strcmp(argv[1], "prompt") == 0) {
    test_prompt();
  } else if (std::strcmp(argv[1], "glyphs") == 0) {
    test_glyphs();
  } else if (std::strcmp(argv[1], "recover") == 0) {
    test_recover();
  } else if (std::strcmp(argv[1], "weekday") == 0) {
    test_weekday();
  } else if (std::strcmp(argv[1], "dates") == 0) {
    test_dates();
  } else if (std::strcmp(argv[1], "unchanged") == 0) {
    test_unchanged();
  } else if (std::strcmp(argv[1], "refresh") == 0) {
    test_refresh();
  } else if (std::strcmp(argv[1], "pace") == 0) {
    test_pace();
  } else if (std::strcmp(argv[1], "greeting") == 0) {
    test_greeting();
  } else if (std::strcmp(argv[1], "repeat") == 0) {
    test_repeat();
  } else {
    assert(false);
  }
}
