
#include "main.h"
#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include <hal/core/hal_app.h>

char s[BUF_L + 1];

static unsigned char mode_ignition = MODE_CLOCK;

static bool lastIgnition = false;
static bool idleTimer = false;
static bool blinkLED = false;
static int idleLEDCycles = 0;
static int changeModeCycles = 0;

static void setDisplaytMode(void);
static void setup_runtime(void);

static void helloMessage(void) {
  const hal_status_t status = clock_display_init();
  if (hal_status_is_error(status)) {
    derr("OLED init failed: %d", (int)status);
    return;
  }
  (void)hal_display_fill_screen_ex(HAL_COLOR_BLACK);
  (void)clock_display_text(8, 0, 1, "FORD");
  (void)clock_display_text(4, 2, 2, "FIESTA");
  /* Temperature conversion blocks; show the greeting before starting it. */
  (void)hal_display_flush_ex();
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
  idleLEDCycles = 0;
  changeModeCycles = 0;
  mode_ignition = MODE_CLOCK;

  helloMessage();

  PCF_Init(PCF_TIMER_INTERRUPT_ENABLE);
  (void)clockTimeInit(); /* logs the reason itself */
  (void)clockCanInit();  /* logs the reason itself */
  configSessionInit();

  temp_initial_read();

  (void)hal_display_fill_screen_ex(HAL_COLOR_BLACK);
  (void)hal_display_flush_ex();
}

void app_start(void) {
  hal_debug_init_default();
  hal_debug_set_module_prefix(SC_MODULE_TOKEN_CLOCK);

  deb("Build timestamp: %s", BUILD_ID);

  setup_runtime();
}

void app_task0(void) {
  hal_watchdog_feed();
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

    (void)hal_display_fill_screen_ex(HAL_COLOR_BLACK);
    (void)hal_display_flush_ex();
  }

  if (idleTimer) {
    if (checkIfTimerReached()) {
      (void)hal_display_suspend_ex();
      blinkLED = true;
    }

    if (blinkLED) {
      if (idleLEDCycles++ >
          (CFG_IDLE_LED_OFF_CYCLES + CFG_IDLE_LED_ON_CYCLES)) {
        idleLEDCycles = 0;
      }

      blueLED(idleLEDCycles < CFG_IDLE_LED_ON_CYCLES);
    }

  } else {
    (void)hal_display_resume_ex();
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
      (void)hal_display_fill_screen_ex(HAL_COLOR_BLACK);
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
      (void)hal_display_fill_screen_ex(HAL_COLOR_BLACK);
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
