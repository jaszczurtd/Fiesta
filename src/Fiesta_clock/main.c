
#include "main.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include <hal/core/hal_app.h>

char s[BUF_L + 1];

static unsigned char mode_ignition = MODE_CLOCK;

static bool lastIgnition = false;
static bool idleTimer = false;
/* The panel was put to sleep by the idle timer and needs a wake-up. */
static bool displayAsleep = false;
static bool blinkLED = false;
static bool idleLEDOn = false;
static uint32_t idleLEDChangeMs = 0u;
static int changeModeCycles = 0;
static bool passStarted = false;
static uint32_t passStartMs = 0u;
/* The greeting stays on the panel until CFG_GREETING_MS after it was drawn. */
static bool greetingShown = false;
static uint32_t greetingStartMs = 0u;

static void setDisplaytMode(void);
static void setup_runtime(void);

static void displaySleep(void) {
  if (!displayAsleep && hal_display_suspend_ex() == HAL_OK) {
    displayAsleep = true;
  }
}

/* The panel can lose its settings or its RAM while asleep, so waking it
 * configures and clears it instead of only switching it on. */
static void displayWake(void) {
  if (displayAsleep) {
    (void)clock_display_restart();
    displayAsleep = false;
  }
}

/* Draw the greeting. It stays for CFG_GREETING_MS while the main loop keeps
 * running (start-up, sensors, CAN, configurator); greetingActive() then clears
 * the panel for the first screen. */
static void helloMessage(void) {
  const hal_status_t status = clock_display_init();
  displayAsleep = false; /* the init switches the panel on */
  greetingShown = false;
  if (hal_status_is_error(status)) {
    derr("OLED init failed: %d", (int)status);
    return;
  }
  (void)clock_display_clear();
  (void)clock_display_text(8, 0, 1, "FORD");
  (void)clock_display_text(4, 2, 2, "FIESTA");
  (void)hal_display_flush_ex();
  greetingShown = true;
  greetingStartMs = hal_millis();
}

/* Whether the greeting still holds the panel; when its time is over, clear
 * the panel once for the first screen. */
static bool greetingActive(void) {
  if (!greetingShown) {
    return false;
  }
  if (!hal_elapsed_u32(hal_millis(), greetingStartMs, CFG_GREETING_MS)) {
    return true;
  }
  greetingShown = false;
  (void)clock_display_restart();
  (void)hal_display_flush_ex();
  return false;
}

static void setup_runtime(void) {
  hal_watchdog_enable(CFG_WATCHDOG_TIMEOUT_MS, false);

  hal_gpio_set_mode(JH_HW_NODE_LED_RED_PIN_OUT, HAL_GPIO_OUTPUT);
  hal_gpio_set_mode(JH_HW_NODE_LED_ORANGE_PIN_OUT, HAL_GPIO_OUTPUT);
  hal_gpio_set_mode(JH_HW_NODE_LED_BLUE_PIN_OUT, HAL_GPIO_OUTPUT);
  redLED(false);
  orangeLED(false);
  blueLED(false);

  hal_gpio_set_mode(JH_HW_NODE_BUTTON_HOUR_PIN_IN,
                    JH_HW_NODE_BUTTON_HOUR_INPUT_MODE);
  hal_gpio_set_mode(JH_HW_NODE_BUTTON_MINUTE_PIN_IN,
                    JH_HW_NODE_BUTTON_MINUTE_INPUT_MODE);
  hal_gpio_set_mode(JH_HW_NODE_BUTTON_SET_PIN_IN,
                    JH_HW_NODE_BUTTON_SET_INPUT_MODE);

  hal_gpio_set_mode(JH_HW_NODE_IGNITION_PIN_IN, JH_HW_NODE_IGNITION_INPUT_MODE);

  hal_adc_set_resolution((uint8_t)CFG_ADC_RESOLUTION_BITS);

  idleTimer = false;
  blinkLED = false;
  idleLEDOn = false;
  changeModeCycles = 0;
  mode_ignition = MODE_CLOCK;

  helloMessage();

  rtc_Init(RTC_TIMER_INTERRUPT_ENABLE);
  (void)clockTimeInit(); /* logs the reason itself */
  (void)clockCanInit();  /* logs the reason itself */
  configSessionInit();

  temp_initial_read();
}

void app_start(void) {
  hal_debug_init_default();
  hal_debug_set_module_prefix(SC_MODULE_TOKEN_CLOCK);

  deb("Build timestamp: %s", BUILD_ID);

  setup_runtime();
}

/* The sleeping panel's blue LED follows hal_millis() on every call, not only
 * on main loop passes. */
static void idleLEDTick(void) {
  if (!blinkLED) {
    return;
  }
  const uint32_t phase = idleLEDOn
                             ? CFG_IDLE_LED_ON_MS
                             : CFG_IDLE_LED_PERIOD_MS - CFG_IDLE_LED_ON_MS;
  if (hal_millis_interval_elapsed(hal_millis(), &idleLEDChangeMs, phase)) {
    idleLEDOn = !idleLEDOn;
  }
  blueLED(idleLEDOn);
}

/* One pass per CFG_MAIN_PASS_MS; a pass that took longer (a held button)
 * is followed by the next one at once, without catching up. */
static bool mainPassDue(void) {
  const uint32_t nowMs = hal_millis();
  if (passStarted && nowMs - passStartMs < CFG_MAIN_PASS_MS) {
    return false;
  }
  passStarted = true;
  passStartMs = nowMs;
  return true;
}

void app_task0(void) {
  hal_watchdog_feed();
  idleLEDTick();
  if (!mainPassDue()) {
    return;
  }
  clockTimeLogTick();
  /* Conversions run in every display mode, so CAN gets fresh readings. */
  ds18b20_poll();
  clockCanTick(isClockSetMode() || configSessionEditPending());
  configSessionTick();

  bool ig = ignition();
  if (ig != lastIgnition) {
    lastIgnition = ig;
    blinkLED = false;
    blueLED(false);
    changeModeCycles = 0;

    helloMessage();

    temp_initial_read();
  }
  if (greetingActive()) {
    return; /* no screen, button or idle handling under the greeting */
  }

  if (idleTimer) {
    if (checkIfTimerReached()) {
      displaySleep();
      if (!blinkLED) {
        idleLEDOn = true;
        idleLEDChangeMs = hal_millis();
      }
      blinkLED = true;
    }

    idleLEDTick();

  } else {
    displayWake();
  }
  if (!displayAsleep) {
    clock_display_service();
  }

  if (!ig) {
    orangeLED(false);
    redLED(false);

    if (!idleTimer) {
      idleTimer = true;
      startTimerForSeconds((unsigned char)CFG_IDLE_SECONDS);
    }

    if (anyButton() || isClockSetMode()) {
      idleTimer = false;
      blinkLED = false;
      blueLED(false);
      displayWake(); /* before this pass draws */
    }

    if (!blinkLED) {
      clockMainFunction();
    }

  } else {
    idleTimer = false;
    setDisplaytMode();
  }

  if (!blinkLED) {
    (void)hal_display_flush_ex();
  }
}

static void setDisplaytMode(void) {
  switch (mode_ignition) {
  case MODE_CLOCK:
    clockMainFunction();
    break;
  case MODE_TEMP:
    temp_read_display();
    break;
  case MODE_VOLT:
    voltMainFunction();
    break;
  default:
    mode_ignition = MODE_CLOCK;
    break;
  }

  if (ignition()) {
    if (setButtonPressed()) {
      changeModeCycles = 0;
      mode_ignition++;
      if (mode_ignition > MODE_VOLT) {
        mode_ignition = MODE_CLOCK;
      }
      (void)clock_display_restart();
    }

    int countVal = CFG_CHANGE_MODE_CYCLES;
    switch (mode_ignition) {
    case MODE_TEMP:
      countVal = CFG_CHANGE_MODE_CYCLES / 6;
      break;
    case MODE_VOLT:
      countVal = CFG_CHANGE_MODE_CYCLES / 4;
      break;
    default:
      break;
    }

    if (countVal < changeModeCycles++) {
      changeModeCycles = 0;
      mode_ignition++;
      if (mode_ignition > MODE_VOLT) {
        mode_ignition = MODE_CLOCK;
      }
      (void)clock_display_restart();
    }
  }
}

void redLED(bool state) {
  const bool level = state ? LED_ACTIVE_LEVEL : !LED_ACTIVE_LEVEL;
  hal_gpio_write(JH_HW_NODE_LED_RED_PIN_OUT, level);
}

void orangeLED(bool state) {
  const bool level = state ? LED_ACTIVE_LEVEL : !LED_ACTIVE_LEVEL;
  hal_gpio_write(JH_HW_NODE_LED_ORANGE_PIN_OUT, level);
}

void blueLED(bool state) {
  const bool level = state ? LED_ACTIVE_LEVEL : !LED_ACTIVE_LEVEL;
  hal_gpio_write(JH_HW_NODE_LED_BLUE_PIN_OUT, level);
}

bool ignition(void) {
  return hal_gpio_read(JH_HW_NODE_IGNITION_PIN_IN) == IGNITION_ACTIVE_LEVEL;
}

bool setButtonPressed(void) {
  bool state = false;

  while (setButton()) {
    hal_watchdog_feed();
    state = true;
    blueLED(true);
  }
  blueLED(false);

  return state;
}

bool setButton(void) {
  return hal_gpio_read(JH_HW_NODE_BUTTON_SET_PIN_IN) == BUTTON_ACTIVE_LEVEL;
}

bool setHour(void) {
  return hal_gpio_read(JH_HW_NODE_BUTTON_HOUR_PIN_IN) == BUTTON_ACTIVE_LEVEL;
}

bool setMinute(void) {
  return hal_gpio_read(JH_HW_NODE_BUTTON_MINUTE_PIN_IN) == BUTTON_ACTIVE_LEVEL;
}

bool anyButton(void) { return setButton() || setHour() || setMinute(); }
