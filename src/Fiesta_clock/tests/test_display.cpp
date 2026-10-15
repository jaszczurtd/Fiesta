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
static int bus_inits = 0;
static PCF_DateTime now = {};
static std::array<uint8_t, 512> greeting;

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

static void clear() {
  assert(hal_display_fill_screen_ex(HAL_COLOR_BLACK) == HAL_OK);
}
static void flush() { assert(hal_display_flush_ex() == HAL_OK); }

extern "C" {
hal_status_t hal_i2c_init_bus(uint8_t bus, uint8_t sda, uint8_t scl,
                              uint32_t hz) {
  assert(bus == 0 && sda == 8 && scl == 9 && hz == 100000);
  ++bus_inits;
  return bus_error ? HAL_EIO : HAL_OK;
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
void hal_gpio_set_mode(uint8_t, hal_gpio_mode_t) {}
void hal_gpio_write(uint8_t, bool) {}
bool hal_gpio_read(uint8_t pin) {
  if (pin == JH_HW_NODE_IGNITION_PIN_IN) {
    return !ignition_on;
  }
  if (pin == JH_HW_NODE_BUTTON_HOUR_PIN_IN) {
    return !hour_down;
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
void hal_adc_set_resolution(uint8_t) {}
hal_status_t hal_watchdog_enable(uint32_t, bool) { return HAL_OK; }
void hal_watchdog_feed(void) {}
void hal_debug_init_default(void) {}
void hal_debug_set_module_prefix(const char *) {}
void hal_deb(const char *, ...) {}
void hal_derr(const char *, ...) {}
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

void PCF_Init(unsigned char) {}
unsigned char PCF_GetDateTime(PCF_DateTime *value) {
  *value = now;
  return 0;
}
unsigned char PCF_SetDateTime(PCF_DateTime *value) {
  now = *value;
  return 0;
}
hal_status_t clockCanInit(void) { return HAL_OK; }
void clockCanTick(void) {}
void configSessionInit(void) {}
void configSessionTick(void) {}
void ds18b20_setPin(unsigned char) {}
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
  // lcd_clrscr() used to blank the physical panel before app_start returned.
  assert(image() == (std::array<uint8_t, 512>{}));
  assert(greeting != (std::array<uint8_t, 512>{}));
  assert(std::any_of(greeting.begin(), greeting.begin() + 128,
                     [](uint8_t b) { return b != 0; }));
  assert(std::any_of(greeting.begin() + 256, greeting.end(),
                     [](uint8_t b) { return b != 0; }));
  app_task0();
  assert(image() != greeting);
  for (unsigned i = 0; i <= CFG_IDLE_SECONDS + 1; ++i) {
    now.second = i % 60;
    app_task0();
  }
  const auto asleep = frames.size();
  const auto sleeping_image = image();
  app_task0();
  assert(frames.size() == asleep + 1);
  assert(frames.back() == (std::vector<uint8_t>{0, 0xae}));
  assert(image() == sleeping_image);
  const auto wakes = command_count(0xaf);
  hour_down = true;
  app_task0();
  hour_down = false;
  app_task0();
  assert(command_count(0xaf) == wakes + 1);
  ignition_on = true;
  app_task0();
  assert(command_count(0xa8) == 2);
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
  } else {
    assert(false);
  }
}
