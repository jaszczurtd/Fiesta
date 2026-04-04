
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

typedef struct {
  unsigned long lastThreadSecondsVal;
  hal_soft_timer_t timerEverySecondHandle;
  hal_soft_timer_t timerMediumHandle;
  hal_soft_timer_t timerHighHandle;
  hal_soft_timer_t timerGPSHandle;
  hal_soft_timer_t timerDebugHandle;
  hal_soft_timer_t timerCANUpdateHandle;
  hal_soft_timer_t timerCANLoopHandle;
  hal_soft_timer_t timerCANCheckHandle;
  int *wValuesPtr;
  int wSizeVal;
  bool alertBlinkState;
} start_runtime_state_t;

typedef struct {
  int statusVariable0Val;
  int statusVariable1Val;
} start_persistent_state_t;

static start_runtime_state_t s_startRuntimeState = {
  .lastThreadSecondsVal = 0uL,
  .timerEverySecondHandle = NULL,
  .timerMediumHandle = NULL,
  .timerHighHandle = NULL,
  .timerGPSHandle = NULL,
  .timerDebugHandle = NULL,
  .timerCANUpdateHandle = NULL,
  .timerCANLoopHandle = NULL,
  .timerCANCheckHandle = NULL,
  .wValuesPtr = NULL,
  .wSizeVal = 0,
  .alertBlinkState = false
};

NOINIT static start_persistent_state_t s_startPersistentState;

m_mutex_def(turboStateMutex);
#ifdef VP37
m_mutex_def(vp37StateMutex);
#endif

static void start_initContextMutexes(void) {
  hal_critical_section_enter();
  if(turboStateMutex == NULL) {
    m_mutex_init(turboStateMutex);
  }
#ifdef VP37
  if(vp37StateMutex == NULL) {
    m_mutex_init(vp37StateMutex);
  }
#endif
  hal_critical_section_exit();
}

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
  { &s_startRuntimeState.timerEverySecondHandle, callAtEverySecond, (uint32_t)SECOND },
  { &s_startRuntimeState.timerMediumHandle, readMediumValues,
    (uint32_t)(SECOND / MEDIUM_TIME_ONE_SECOND_DIVIDER) },
  { &s_startRuntimeState.timerHighHandle, readHighValues,
    (uint32_t)(SECOND / FREQUENT_TIME_ONE_SECOND_DIVIDER) },
  { &s_startRuntimeState.timerGPSHandle, getGPSData, (uint32_t)GPS_UPDATE },
  { &s_startRuntimeState.timerDebugHandle, updateValsForDebug, (uint32_t)DEBUG_UPDATE },
  { &s_startRuntimeState.timerCANUpdateHandle, CAN_updaterecipients_01, (uint32_t)CAN_UPDATE_RECIPIENTS },
  { &s_startRuntimeState.timerCANLoopHandle, canMainLoop, (uint32_t)CAN_MAIN_LOOP_READ_INTERVAL },
  { &s_startRuntimeState.timerCANCheckHandle, canCheckConnection, (uint32_t)CAN_CHECK_CONNECTION }
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

void executeByWatchdog(int *values, int size) {
  s_startRuntimeState.wValuesPtr = values;
  s_startRuntimeState.wSizeVal = size;
}

void initialization(void) {

  debugInit();
  setDebugPrefix("ECU:");
  deb("Build timestamp: %s", ecu_BuildDateTime);

  // Force local flash-backed EEPROM for this ECU build.
  hal_eeprom_init(HAL_EEPROM_RP2040, ECU_EEPROM_SIZE_BYTES, 0);
  deb("EEPROM backend: RP2040 (%u bytes)", (unsigned)hal_eeprom_size());

  dtcManagerInit();

  initTests();
  start_initContextMutexes();

  //this has to be invoked as soon as possible, and twice
  initI2C();
  pcf8574_init();
  hal_i2c_deinit();

  initSPI();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if(!rebooted) {
    s_startPersistentState.statusVariable0Val = s_startPersistentState.statusVariable1Val = 0;
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

  if(s_startRuntimeState.wValuesPtr != NULL) {
    char dateAndTime[GPS_TIME_DATE_BUFFER_SIZE * 2];
    memset(dateAndTime, 0, sizeof(dateAndTime));
    const bool hasWatchdogSnapshot = (s_startRuntimeState.wSizeVal >= 4);

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
    if(hasWatchdogSnapshot) {
      crashReport("core0 started: %d", s_startRuntimeState.wValuesPtr[0]);
      crashReport("core0 was running: %d", s_startRuntimeState.wValuesPtr[1]);
      crashReport("core1 started: %d", s_startRuntimeState.wValuesPtr[2]);
      crashReport("core1 was running: %d", s_startRuntimeState.wValuesPtr[3]);
    } else {
      crashReport("watchdog snapshot truncated: size=%d", s_startRuntimeState.wSizeVal);
    }
    crashReport("build: %s", ecu_BuildDateTime);

    crashReport("sv0: %d", s_startPersistentState.statusVariable0Val);
    crashReport("sv1: %d", s_startPersistentState.statusVariable1Val);

    saveCrashLoggerAndClose();
    watchdog_feed();

    s_startRuntimeState.wSizeVal = 0;
    s_startRuntimeState.wValuesPtr = NULL;
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
  m_mutex_enter_blocking(vp37StateMutex);
  VP37Pump_init(&s_ctx.injectionPump);
  m_mutex_exit(vp37StateMutex);
#endif
  watchdog_feed();

  m_mutex_enter_blocking(turboStateMutex);
  Turbo_init(&s_ctx.turbo);
  m_mutex_exit(turboStateMutex);

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

//timer functions
void callAtEverySecond(void) {
  s_startRuntimeState.alertBlinkState = (s_startRuntimeState.alertBlinkState) ? false : true;
  hal_gpio_write(HAL_LED_PIN, s_startRuntimeState.alertBlinkState);
  hal_gpio_write(PIO_DPF_LAMP, isDPFRegenerating());
  CAN_sendGpsExtended();

#if SYSTEM_TEMP
  deb("System temperature: %f", hal_read_chip_temp());
#endif
}

void looper(void) {
  s_startPersistentState.statusVariable0Val = 0;
  updateWatchdogCore0();

  s_startPersistentState.statusVariable0Val = 1;
  glowPlugs_process(getGlowPlugsInstance());

  // Drain GPS serial FIFO every iteration — the PIO SoftwareSerial
  // buffer is only 32 bytes; at 9600 baud it overflows in ~33 ms.
  hal_gps_update();

  s_startPersistentState.statusVariable0Val = 2;
  if(!isEnvironmentStarted()) {
    s_startPersistentState.statusVariable0Val = -1;
    hal_idle();
    return;
  }

  start_tickAllTimers();
  if(s_startRuntimeState.lastThreadSecondsVal < getSeconds()) {
    s_startRuntimeState.lastThreadSecondsVal = getSeconds() + THREAD_CONTROL_SECONDS;

    deb("thread is alive");
  }
  s_startPersistentState.statusVariable0Val = 3;
  CAN_updaterecipients_02();
  s_startPersistentState.statusVariable0Val = 4;
  obdLoop();
  s_startPersistentState.statusVariable0Val = 5;
  engineFan_process(getFanInstance());
  s_startPersistentState.statusVariable0Val = 6;
  engineHeater_process(getHeaterInstance());
  s_startPersistentState.statusVariable0Val = 7;
  heatedWindshields_process(getHeatedWindshieldsInstance());
  s_startPersistentState.statusVariable0Val = 8;

#ifdef VP37
  m_mutex_enter_blocking(vp37StateMutex);
  VP37Pump_showDebug(&s_ctx.injectionPump);
  m_mutex_exit(vp37StateMutex);
#endif
  m_mutex_enter_blocking(turboStateMutex);
  Turbo_showDebug(&s_ctx.turbo);
  m_mutex_exit(turboStateMutex);

  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}

void initialization1(void) {
  start_initContextMutexes();
  createRPM();

  setStartedCore1();

  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

void looper1(void) {

  s_startPersistentState.statusVariable1Val = 0;
  updateWatchdogCore1();

  if(!isEnvironmentStarted()) {
    s_startPersistentState.statusVariable1Val = -1;
    hal_idle();
    return;
  }

  s_startPersistentState.statusVariable1Val = 1;
  m_mutex_enter_blocking(turboStateMutex);
  Turbo_process(&s_ctx.turbo);
  m_mutex_exit(turboStateMutex);
  s_startPersistentState.statusVariable1Val = 2;
  RPM_process(getRPMInstance());
#ifdef VP37
  m_mutex_enter_blocking(vp37StateMutex);
  VP37Pump_process(&s_ctx.injectionPump);
  m_mutex_exit(vp37StateMutex);
#endif
  s_startPersistentState.statusVariable1Val = 3;

  hal_idle();
}
