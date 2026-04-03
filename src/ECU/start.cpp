
#include "start.h"
#include "ecuContext.h"
#include <hal/hal_soft_timer.h>

//-----------------------------------------------------------------------------
// Central ECU context — single owner of all module instances
//-----------------------------------------------------------------------------

static ecu_context_t s_ctx;

ecu_context_t *getECUContext(void) {
  return &s_ctx;
}

//-----------------------------------------------------------------------------

static unsigned long lastThreadSeconds = 0;
static hal_soft_timer_t timerEverySecond = NULL;
static hal_soft_timer_t timerMedium = NULL;
static hal_soft_timer_t timerHigh = NULL;
static hal_soft_timer_t timerGPS = NULL;
static hal_soft_timer_t timerDebug = NULL;
static hal_soft_timer_t timerCANUpdate = NULL;
static hal_soft_timer_t timerCANLoop = NULL;
static hal_soft_timer_t timerCANCheck = NULL;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

static void start_ensureTimerCreated(hal_soft_timer_t *timer) {
  if(*timer == NULL) {
    *timer = hal_soft_timer_create();
  }
}

typedef struct {
  hal_soft_timer_t *timer;
  hal_soft_timer_callback_t callback;
  uint32_t intervalMs;
} start_timer_init_t;

static const start_timer_init_t startTimerInitTable[] = {
  { &timerEverySecond, callAtEverySecond, (uint32_t)SECOND },
  { &timerMedium, readMediumValues,
    (uint32_t)(SECOND / MEDIUM_TIME_ONE_SECOND_DIVIDER) },
  { &timerHigh, readHighValues,
    (uint32_t)(SECOND / FREQUENT_TIME_ONE_SECOND_DIVIDER) },
  { &timerGPS, getGPSData, (uint32_t)GPS_UPDATE },
  { &timerDebug, updateValsForDebug, (uint32_t)DEBUG_UPDATE },
  { &timerCANUpdate, CAN_updaterecipients_01, (uint32_t)CAN_UPDATE_RECIPIENTS },
  { &timerCANLoop, canMainLoop, (uint32_t)CAN_MAIN_LOOP_READ_INTERVAL },
  { &timerCANCheck, canCheckConnection, (uint32_t)CAN_CHECK_CONNECTION }
};

static void start_setupSingleTimer(hal_soft_timer_t *timer,
                                   hal_soft_timer_callback_t callback,
                                   uint32_t intervalMs) {
  watchdog_feed();
  start_ensureTimerCreated(timer);
  (void)hal_soft_timer_begin(*timer, callback, intervalMs);
  hal_delay_ms(CORE_OPERATION_DELAY);
}

static void start_tickAllTimers(void) {
  for(uint32_t i = 0u; i < COUNTOF(startTimerInitTable); i++) {
    hal_soft_timer_tick(*startTimerInitTable[i].timer);
  }
}

void setupTimers(void) {
  const uint32_t timerCount = COUNTOF(startTimerInitTable);

  for(uint32_t i = 0u; i < timerCount; i++) {
    start_setupSingleTimer(startTimerInitTable[i].timer,
                           startTimerInitTable[i].callback,
                           startTimerInitTable[i].intervalMs);
  }
}

static int *wValues = NULL;
static int wSize = 0;
void executeByWatchdog(int *values, int size) {
  wValues = values;
  wSize = size;
}

void initialization(void) {

  debugInit();
  setDebugPrefix("ECU:");

  // Force local flash-backed EEPROM for this ECU build.
  hal_eeprom_init(HAL_EEPROM_RP2040, ECU_EEPROM_SIZE_BYTES, 0);
  deb("EEPROM backend: RP2040 (%u bytes)", (unsigned)hal_eeprom_size());

  dtcManagerInit();

  initTests();

  //this has to be invoked as soon as possible, and twice
  initI2C();
  pcf8574_init();
  hal_i2c_deinit();

  initSPI();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if(!rebooted) {
    statusVariable0 = statusVariable1 = 0;
    initGPSDateAndTime();
  }

  initI2C();

  #ifdef RESET_EEPROM
  resetEEPROM();
  #endif

  initSDLogger(SD_CARD_CS);
  if (!isSDLoggerInitialized()) {
    deb("SD Card failed, or not present");
  } else {
    deb("SD Card initialized");
  }

  if(wValues != NULL) {
    char dateAndTime[GPS_TIME_DATE_BUFFER_SIZE * 2];
    memset(dateAndTime, 0, sizeof(dateAndTime));

    bool validDateAndTime = isValidString(getGPSDate(), GPS_TIME_DATE_BUFFER_SIZE) &&
      isValidString(getGPSTime(), GPS_TIME_DATE_BUFFER_SIZE);

    if(validDateAndTime) {
      snprintf(dateAndTime, sizeof(dateAndTime) - 1, "%s-%s",
        getGPSDate(), getGPSTime());
    }

    watchdog_feed();
    initCrashLogger(dateAndTime, SD_CARD_CS);
    if(validDateAndTime) {
      crashReport("date:%s time:%s", getGPSDate(), getGPSTime());
    }
    crashReport("core0 started: %d", wValues[0]);
    crashReport("core0 was running: %d", wValues[1]);
    crashReport("core1 started: %d", wValues[2]);
    crashReport("core1 was running: %d", wValues[3]);

    crashReport("sv0: %d", statusVariable0);
    crashReport("sv1: %d", statusVariable1);

    saveCrashLoggerAndClose();
    watchdog_feed();

    wSize = 0;
    wValues = NULL;
  }

  initBasicPIO();

  #ifdef I2C_SCANNER
  i2cScanner();
  #endif

  initSensors();

  createFan();
  createHeater();
  createGlowPlugs();
  createHeatedWindshields();

  float coolant = readCoolantTemp();
  setGlobalValue(F_COOLANT_TEMP, coolant);
  if(coolant <= TEMP_LOWEST) {
    coolant = TEMP_LOWEST;
  }
  glowPlugs_initGlowPlugsTime(getGlowPlugsInstance(), coolant);

#ifdef VP37
  VP37Pump_init(&s_ctx.injectionPump);
#endif
  watchdog_feed();

  Turbo_init(&s_ctx.turbo);

  canInit(CAN_RETRIES);
  obdInit(CAN_RETRIES);

  setGlobalValue(F_VOLTS, getSystemSupplyVoltage());

  initFuelMeasurement();

  canCheckConnection();
  canMainLoop();
  callAtEverySecond();
  updateValsForDebug();
  CAN_sendAll();
  setupTimers();

  deb("System temperature:%.1fC", rroundf(hal_read_chip_temp()));

  setStartedCore0();

  deb("Fiesta MTDDI started: %s\n", isEnvironmentStarted() ? "yes" : "no");

  startTests();
}

static bool alertBlink = false;

//timer functions
void callAtEverySecond(void) {
  alertBlink = (alertBlink) ? false : true;
  hal_gpio_write(HAL_LED_PIN, alertBlink);
  hal_gpio_write(PIO_DPF_LAMP, isDPFRegenerating());
  CAN_sendGpsExtended();

#if SYSTEM_TEMP
  deb("System temperature: %f", hal_read_chip_temp());
#endif
}

void looper(void) {
  statusVariable0 = 0;
  updateWatchdogCore0();

  statusVariable0 = 1;
  glowPlugs_process(getGlowPlugsInstance());

  // Drain GPS serial FIFO every iteration — the PIO SoftwareSerial
  // buffer is only 32 bytes; at 9600 baud it overflows in ~33 ms.
  hal_gps_update();

  statusVariable0 = 2;
  if(!isEnvironmentStarted()) {
    statusVariable0 = -1;
    hal_idle();
    return;
  }

  start_tickAllTimers();
  if(lastThreadSeconds < getSeconds()) {
    lastThreadSeconds = getSeconds() + THREAD_CONTROL_SECONDS;

    deb("thread is alive");
  }
  statusVariable0 = 3;
  CAN_updaterecipients_02();
  statusVariable0 = 4;
  obdLoop();
  statusVariable0 = 5;
  engineFan_process(getFanInstance());
  statusVariable0 = 6;
  engineHeater_process(getHeaterInstance());
  statusVariable0 = 7;
  heatedWindshields_process(getHeatedWindshieldsInstance());
  statusVariable0 = 8;

#ifdef VP37
  VP37Pump_showDebug(&s_ctx.injectionPump);
#endif
  Turbo_showDebug(&s_ctx.turbo);

  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}

void initialization1(void) {
  createRPM();

  setStartedCore1();

  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

void looper1(void) {

  statusVariable1 = 0;
  updateWatchdogCore1();

  if(!isEnvironmentStarted()) {
    statusVariable1 = -1;
    hal_idle();
    return;
  }

  statusVariable1 = 1;
  Turbo_process(&s_ctx.turbo);
  statusVariable1 = 2;
  RPM_process(getRPMInstance());
#ifdef VP37
  VP37Pump_process(&s_ctx.injectionPump);
#endif
  statusVariable1 = 3;

  hal_idle();
}
