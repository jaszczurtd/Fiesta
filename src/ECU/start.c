
#include "start.h"
#include "ecuContext.h"
#include <hal/hal_soft_timer.h>

//-----------------------------------------------------------------------------
// Central ECU context — single owner of all module instances
//-----------------------------------------------------------------------------

static ecu_context_t s_ctx;

/**
 * @brief Return the single ECU context instance shared by both cores.
 * @return Pointer to the global ECU context.
 */
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
static hal_mutex_t turboStateMutex = NULL;
#ifdef VP37
static hal_mutex_t vp37StateMutex = NULL;
#endif

/**
 * @brief Create shared module mutexes once during startup.
 * @return None.
 */
static void start_initContextMutexes(void) {
  hal_critical_section_enter();
  if(turboStateMutex == NULL) {
    turboStateMutex = hal_mutex_create();
  }
#ifdef VP37
  if(vp37StateMutex == NULL) {
    vp37StateMutex = hal_mutex_create();
  }
#endif
  hal_critical_section_exit();
}

/**
 * @brief Stop feeding the watchdog and blink LED until reset occurs.
 * @param reason Human-readable reason logged before forcing reset.
 * @return None.
 */
static void start_forceWatchdogReset(const char *reason) {
  derr("Forcing watchdog reset: %s", reason);
  bool ledOn = false;
  while(true) {
    ledOn = !ledOn;
    hal_gpio_write(HAL_LED_PIN, ledOn);
    // Intentionally do not feed/update watchdog.
    hal_idle();
    hal_delay_ms(150);
  }
}

static const hal_soft_timer_table_entry_t startTimerInitTable[] = {
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

/**
 * @brief Configure the shared soft-timer table used by core 0.
 * @return None.
 */
void setupTimers(void) {
  hal_soft_timer_setup_table(startTimerInitTable, COUNTOF(startTimerInitTable),
                             watchdog_feed, CORE_OPERATION_DELAY);
}

/**
 * @brief Store watchdog snapshot data received after an automatic reboot.
 * @param values Pointer to watchdog snapshot values.
 * @param size Number of snapshot elements available at @p values.
 * @return None.
 */
void executeByWatchdog(int *values, int size) {
  s_startRuntimeState.wValuesPtr = values;
  s_startRuntimeState.wSizeVal = size;
}

/**
 * @brief Initialize all core-0 peripherals, modules and watchdog state.
 * @return None.
 */
void initialization(void) {

  debugInit();
  setDebugPrefix("ECU:");

  deb("Build timestamp: %s", ecu_BuildDateTime);

  // Force local flash-backed EEPROM for this ECU build.
  hal_eeprom_init(HAL_EEPROM_RP2040, ECU_EEPROM_SIZE_BYTES, 0);
  deb("EEPROM backend: RP2040 (%u bytes)", (unsigned)hal_eeprom_size());
  deb("EEPROM layout: FIRST_ADDR=%u", (unsigned)HAL_TOOLS_EEPROM_FIRST_ADDR);
#ifdef HAL_TOOLS_EEPROM_LOGGER_ADDR
  deb("EEPROM layout: LOGGER_ADDR=%u CRASH_ADDR=%u",
    (unsigned)HAL_TOOLS_EEPROM_LOGGER_ADDR,
    (unsigned)HAL_TOOLS_EEPROM_CRASH_ADDR);
#endif

  dtcManagerInit();

  initTests();
  start_initContextMutexes();

  initI2C();

  initSPI();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if(!rebooted) {
    s_startPersistentState.statusVariable0Val = s_startPersistentState.statusVariable1Val = 0;
    initGPSDateAndTime();
  }

  pcf8574_init();

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
  VP37InitStatus vp37InitStatus = VP37_init(&s_ctx.injectionPump);
  m_mutex_exit(vp37StateMutex);

  switch(vp37InitStatus) {
    case VP37_INIT_OK:
      break;
    case VP37_INIT_ALREADY_INITIALIZED:
      deb("VP37 already initialized");
      break;
    case VP37_INIT_BASELINE_NOT_READY:
      derr("VP37 init failed: adjustometer baseline not ready");
      start_forceWatchdogReset("VP37 baseline not ready at startup");
      break;
    case VP37_INIT_PID_CREATE_FAILED:
      derr("VP37 init failed: PID controller create failed");
      break;
    default:
      derr("VP37 init failed: unknown status=%d", (int)vp37InitStatus);
      break;
  }
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

  dtcManagerLogStorageStats();

  startTests();
}

//timer functions
/**
 * @brief Execute periodic once-per-second housekeeping outputs.
 * @return None.
 */
void callAtEverySecond(void) {
  s_startRuntimeState.alertBlinkState = (s_startRuntimeState.alertBlinkState) ? false : true;
  hal_gpio_write(HAL_LED_PIN, s_startRuntimeState.alertBlinkState);
  hal_gpio_write(PIO_DPF_LAMP, isDPFRegenerating());
  CAN_sendGpsExtended();

#if SYSTEM_TEMP
  deb("System temperature: %f", hal_read_chip_temp());
#endif
}

/**
 * @brief Run one core-0 scheduler iteration for I/O and service tasks.
 * @return None.
 */
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

  hal_soft_timer_tick_table(startTimerInitTable, COUNTOF(startTimerInitTable));
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
  VP37_showDebug(&s_ctx.injectionPump);
  m_mutex_exit(vp37StateMutex);
#endif
  m_mutex_enter_blocking(turboStateMutex);
  Turbo_showDebug(&s_ctx.turbo);
  m_mutex_exit(turboStateMutex);

  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}

/**
 * @brief Initialize the second core runtime context.
 * @return None.
 */
void initialization1(void) {
  start_initContextMutexes();
  RPM_create();

  setStartedCore1();

  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

/**
 * @brief Run one core-1 control-loop iteration.
 * @return None.
 */
void looper1(void) {

  s_startPersistentState.statusVariable1Val = 0;
  updateWatchdogCore1();

  if(!isEnvironmentStarted()) {
    s_startPersistentState.statusVariable1Val = -1;
    hal_idle();
    return;
  }

  s_startPersistentState.statusVariable1Val = 1;
  hal_mutex_lock(turboStateMutex);
  Turbo_process(&s_ctx.turbo);
  hal_mutex_unlock(turboStateMutex);
  s_startPersistentState.statusVariable1Val = 2;
  RPM_process(getRPMInstance());
#ifdef VP37
  hal_mutex_lock(vp37StateMutex);
  VP37_process(&s_ctx.injectionPump);
#ifdef START_TEST_ENABLE_VP37_CYCLIC
  tickTests();
#endif
  hal_mutex_unlock(vp37StateMutex);
#endif
  s_startPersistentState.statusVariable1Val = 3;

  hal_idle();
}
