
#include "start.h"

#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "ecuContext.h"
#include "ecuPersistence.h"
#include "obd-2.h"
#include "vp37_current.h"
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/timers/hal_soft_timer.h>
#include <hal/usb/hal_usb.h>
#include <utils/multicoreWatchdog.h>
#include <utils/tools_common_defs.h>

//-----------------------------------------------------------------------------
// Central ECU context - single owner of all module instances
//-----------------------------------------------------------------------------

static ecu_context_t s_ctx;

/**
 * @brief Return the single ECU context instance shared by both cores.
 * @return Pointer to the global ECU context.
 */
ecu_context_t *getECUContext(void) { return &s_ctx; }

//-----------------------------------------------------------------------------

typedef struct {
  hal_soft_timer_t timerEverySecondHandle;
  hal_soft_timer_t timerMediumHandle;
  hal_soft_timer_t timerHighHandle;
  hal_soft_timer_t timerThrottleHandle;
  hal_soft_timer_t timerGPSHandle;
  hal_soft_timer_t timerDebugHandle;
  hal_soft_timer_t timerCANUpdateHandle;
  hal_soft_timer_t timerCANLoopHandle;
  hal_soft_timer_t timerCANCheckHandle;
  int *wValuesPtr;
  int wSizeVal;
  bool alertBlinkState;
  volatile hal_status_t core1InitStatus;
  bool core1InitErrorReported;
  uint32_t vp37DebugLastMs;
  uint32_t vp37CurrentLastMs;
  uint32_t vp37CurrentLastSequence;
} start_runtime_state_t;

typedef struct {
  int statusVariable0Val;
  int statusVariable1Val;
} start_persistent_state_t;

static start_runtime_state_t s_startRuntimeState = {
    .timerEverySecondHandle = NULL,
    .timerMediumHandle = NULL,
    .timerHighHandle = NULL,
    .timerThrottleHandle = NULL,
    .timerGPSHandle = NULL,
    .timerDebugHandle = NULL,
    .timerCANUpdateHandle = NULL,
    .timerCANLoopHandle = NULL,
    .timerCANCheckHandle = NULL,
    .wValuesPtr = NULL,
    .wSizeVal = 0,
    .alertBlinkState = false,
    .core1InitStatus = HAL_NONE,
    .core1InitErrorReported = false};

NOINIT static start_persistent_state_t s_startPersistentState;
static hal_mutex_t turboStateMutex = NULL;
#ifdef VP37
static hal_mutex_t vp37StateMutex = NULL;
#endif

/**
 * @brief Create shared module mutexes once during startup.
 */
static void start_initContextMutexes(void) {
  hal_critical_section_enter();
  if (turboStateMutex == NULL) {
    turboStateMutex = hal_mutex_create();
  }
#ifdef VP37
  if (vp37StateMutex == NULL) {
    vp37StateMutex = hal_mutex_create();
  }
#endif
  hal_critical_section_exit();
}

/**
 * @brief Persist and log a core-1 RPM initialization failure from core 0.
 * @note DTC persistence intentionally remains on core 0 so core 1 never writes
 *       flash while reporting its startup failure.
 */
static void start_reportCore1InitError(void) {
  const hal_status_t status = s_startRuntimeState.core1InitStatus;
  if (!hal_status_is_error(status) ||
      s_startRuntimeState.core1InitErrorReported) {
    return;
  }

  s_startRuntimeState.core1InitErrorReported = true;
  derr("Core 1 RPM initialization failed: %s (%d)",
       hal_status_to_string(status), (int)status);
  dtcManagerSetActive(DTC_RPM_IRQ_INIT_FAIL, true);
}

#ifdef VP37
/**
 * @brief Stop feeding the watchdog and blink LED until reset occurs.
 * @param reason Human-readable reason logged before forcing reset.
 */
static void start_forceWatchdogReset(const char *reason) {
  derr("Forcing watchdog reset: %s", reason);
  bool ledOn = false;
  while (true) {
    ledOn = !ledOn;
    hal_gpio_write(HAL_LED_PIN, ledOn);
    // Intentionally do not feed/update watchdog.
    hal_idle();
    hal_delay_ms(150);
  }
}
#endif

static const hal_soft_timer_table_entry_t startTimerInitTable[] = {
    {&s_startRuntimeState.timerThrottleHandle, readThrottleValues,
     THROTTLE_UPDATE_MS},
    {&s_startRuntimeState.timerEverySecondHandle, callAtEverySecond,
     (uint32_t)SECOND},
    {&s_startRuntimeState.timerMediumHandle, readMediumValues,
     (uint32_t)(SECOND / MEDIUM_TIME_ONE_SECOND_DIVIDER)},
    {&s_startRuntimeState.timerHighHandle, readHighValues,
     (uint32_t)(SECOND / FREQUENT_TIME_ONE_SECOND_DIVIDER)},
    {&s_startRuntimeState.timerGPSHandle, getGPSData, (uint32_t)GPS_UPDATE},
    {&s_startRuntimeState.timerDebugHandle, updateValsForDebug,
     (uint32_t)DEBUG_UPDATE},
    {&s_startRuntimeState.timerCANUpdateHandle, CAN_updaterecipients_01,
     (uint32_t)CAN_UPDATE_RECIPIENTS},
    {&s_startRuntimeState.timerCANLoopHandle, canMainLoop,
     (uint32_t)CAN_MAIN_LOOP_READ_INTERVAL},
    {&s_startRuntimeState.timerCANCheckHandle, canCheckConnection,
     (uint32_t)CAN_CHECK_CONNECTION}};

/**
 * @brief Configure the shared soft-timer table used by core 0.
 */
void setupTimers(void) {
  hal_soft_timer_setup_table(startTimerInitTable, COUNTOF(startTimerInitTable),
                             watchdog_feed, CORE_OPERATION_DELAY);
}

/**
 * @brief Store watchdog snapshot data received after an automatic reboot.
 * @param values Pointer to watchdog snapshot values.
 * @param size Number of snapshot elements available at @p values.
 */
void executeByWatchdog(int *values, int size) {
  s_startRuntimeState.wValuesPtr = values;
  s_startRuntimeState.wSizeVal = size;
}

/**
 * @brief Report the snapshot left by a watchdog reboot, then clear it.
 * @note The snapshot stays latched until a USB host is attached. Output sent
 * while nobody listens is dropped by the CDC stack, and a reboot is exactly
 * the moment the host is still reconnecting.
 */
static void start_reportWatchdogSnapshot(void) {
  bool hostAttached = false;
  if ((s_startRuntimeState.wValuesPtr == NULL) ||
      (hal_usb_cdc_is_connected(&hostAttached) != HAL_OK) || !hostAttached) {
    return;
  }
  watchdog_feed();
  if (hal_text_is_printable(getGPSDate(), GPS_TIME_DATE_BUFFER_SIZE) &&
      hal_text_is_printable(getGPSTime(), GPS_TIME_DATE_BUFFER_SIZE)) {
    derr("Watchdog reboot at %s %s", getGPSDate(), getGPSTime());
  } else {
    derr("Watchdog reboot, time of day unknown");
  }
  if (s_startRuntimeState.wSizeVal >= 4) {
    derr("Watchdog cores: core0 started:%d running:%d core1 started:%d "
         "running:%d",
         s_startRuntimeState.wValuesPtr[0], s_startRuntimeState.wValuesPtr[1],
         s_startRuntimeState.wValuesPtr[2], s_startRuntimeState.wValuesPtr[3]);
  } else {
    derr("Watchdog snapshot truncated: size=%d", s_startRuntimeState.wSizeVal);
  }
  derr("Watchdog build:%s sv0:%d sv1:%d", ecu_BuildDateTime,
       s_startPersistentState.statusVariable0Val,
       s_startPersistentState.statusVariable1Val);
  watchdog_feed();

  s_startRuntimeState.wSizeVal = 0;
  s_startRuntimeState.wValuesPtr = NULL;
}

static void feedWatchdogDuringPersistence(void *user) {
  (void)user;
  watchdog_feed();
}

/**
 * @brief Initialize all core-0 peripherals, modules and watchdog state.
 */
static void initializeCore0(void) {

  hal_debug_init_default();
  hal_debug_set_module_prefix(SC_MODULE_TOKEN_ECU);

  deb("Build timestamp: %s", ecu_BuildDateTime);

  const hal_status_t persistenceStatus = ecuPersistenceInit();
  if (persistenceStatus != HAL_OK) {
    derr("Persistence initialization failed: %s",
         hal_status_to_string(persistenceStatus));
  }

  // Force local flash-backed EEPROM for this ECU build.
  const hal_status_t eepromStatus =
      hal_eeprom_init(HAL_EEPROM_FLASH, ECU_EEPROM_SIZE_BYTES, 0);
  if (eepromStatus != HAL_OK) {
    derr("EEPROM initialization failed: %s (%d)",
         hal_status_to_string(eepromStatus), (int)eepromStatus);
  }
  deb("EEPROM backend: internal flash (%u bytes)", (unsigned)hal_eeprom_size());

  dtcManagerInit();
  ecuParamsInit();

  initTests();
  start_initContextMutexes();

  initI2C();

  initSPI();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  const hal_status_t progressStatus =
      hal_eeprom_set_progress_callback(feedWatchdogDuringPersistence, NULL);
  if (progressStatus != HAL_OK) {
    derr("EEPROM progress callback setup failed: %s",
         hal_status_to_string(progressStatus));
  }
  if (!rebooted) {
    s_startPersistentState.statusVariable0Val =
        s_startPersistentState.statusVariable1Val = 0;
    initGPSDateAndTime();
  }

  pcf8574_init();

#ifdef RESET_EEPROM
  resetEEPROM();
#endif

  initBasicPIO();

#ifdef I2C_SCANNER
  i2cScanner();
#endif

  initSensors();
#ifdef VP37
  VP37_currentSenseInit();
#endif
  configSessionInit();

  createFan();
  createHeater();
  createGlowPlugs();
  createHeatedWindshields();

  float coolant = readCoolantTemp();
  setGlobalValue(F_COOLANT_TEMP, coolant);
  if (coolant <= TEMP_LOWEST) {
    coolant = TEMP_LOWEST;
  }
  glowPlugs_initGlowPlugsTime(getGlowPlugsInstance(), coolant);

#ifdef VP37
  m_mutex_enter_blocking(vp37StateMutex);
  setVP37AdjustometerFastFeedback(true);
  VP37InitStatus vp37InitStatus = VP37_init(&s_ctx.injectionPump);
  m_mutex_exit(vp37StateMutex);

  switch (vp37InitStatus) {
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
  case VP37_INIT_CALIBRATION_FAILED:
    derr("VP37 init failed: actuator calibration did not settle");
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

  deb("System temperature:%.1fC", hal_math_round_tenth(hal_read_chip_temp()));

  setStartedCore0();

  start_reportCore1InitError();

  deb("Fiesta MTDDI started: %s\n", isEnvironmentStarted() ? "yes" : "no");

  dtcManagerLogStorageStats();

  startTests();
}

// timer functions
/**
 * @brief Execute periodic once-per-second housekeeping outputs.
 */
void callAtEverySecond(void) {
  s_startRuntimeState.alertBlinkState =
      (s_startRuntimeState.alertBlinkState) ? false : true;
  hal_gpio_write(HAL_LED_PIN, s_startRuntimeState.alertBlinkState);
  hal_gpio_write(PIO_DPF_LAMP, isDPFRegenerating());
  CAN_sendGpsExtended();
  start_reportWatchdogSnapshot();

#if SYSTEM_TEMP
  deb("System temperature: %f", hal_read_chip_temp());
#endif
}

#ifdef VP37
/**
 * @brief Report the newest reduced scan block from a pump snapshot.
 * @note Reduction and publishing run on core 1 inside the control
 * step; this only prints, at most every VP37_CURRENT_REPORT_MS.
 */
static void start_reportVP37Current(void) {
  if (!hal_millis_interval_elapsed_now(&s_startRuntimeState.vp37CurrentLastMs,
                                       VP37_CURRENT_REPORT_MS)) {
    return;
  }
  m_mutex_enter_blocking(vp37StateMutex);
  const VP37Pump snapshot = s_ctx.injectionPump;
  m_mutex_exit(vp37StateMutex);
  if (!snapshot.vp37Initialized ||
      (snapshot.scan.cycleResultSequence ==
       s_startRuntimeState.vp37CurrentLastSequence)) {
    return;
  }
  s_startRuntimeState.vp37CurrentLastSequence =
      snapshot.scan.cycleResultSequence;
  VP37_showCurrentPulse(&snapshot);
}
#endif

/**
 * @brief Run one core-0 scheduler iteration for I/O and service tasks.
 */
static void runCore0(void) {
  s_startPersistentState.statusVariable0Val = 0;
  updateWatchdogCore0();
  start_reportCore1InitError();

  s_startPersistentState.statusVariable0Val = 1;
  glowPlugs_process(getGlowPlugsInstance());

  hal_gps_update();
  ecuPersistencePoll();
  dtcManagerPoll();
  ecuParamsPoll();

  s_startPersistentState.statusVariable0Val = 2;
  if (!isEnvironmentStarted()) {
    s_startPersistentState.statusVariable0Val = -1;
    hal_idle();
    return;
  }

  // RPM goes out before the soft-timer table so callbacks due in this pass do
  // not delay it.
  CAN_updaterecipients_02();
  s_startPersistentState.statusVariable0Val = 3;
  hal_soft_timer_tick_table(startTimerInitTable, COUNTOF(startTimerInitTable));
  s_startPersistentState.statusVariable0Val = 4;
  obdLoop();
  s_startPersistentState.statusVariable0Val = 5;
  engineFan_process(getFanInstance());
  s_startPersistentState.statusVariable0Val = 6;
  engineHeater_process(getHeaterInstance());
  s_startPersistentState.statusVariable0Val = 7;
  heatedWindshields_process(getHeatedWindshieldsInstance());
  s_startPersistentState.statusVariable0Val = 8;
  configSessionTick();
  s_startPersistentState.statusVariable0Val = 9;

#ifdef VP37
  start_reportVP37Current();
  if (hal_millis_interval_elapsed_now(&s_startRuntimeState.vp37DebugLastMs,
                                      VP37_DEBUG_UPDATE)) {
    m_mutex_enter_blocking(vp37StateMutex);
    VP37Pump snapshot = s_ctx.injectionPump;
#if ECU_FUNCTIONAL_TESTS_ENABLED
    VP37TraceSample samples[4];
    size_t sampleCount = 0U;
    while (!hal_debug_is_muted() && (sampleCount < COUNTOF(samples)) &&
           (VP37_readTrace(&s_ctx.injectionPump, &samples[sampleCount]) ==
            HAL_OK)) {
      sampleCount++;
    }
    const bool recording = VP37_traceCapturing();
#endif
    m_mutex_exit(vp37StateMutex);
#if ECU_FUNCTIONAL_TESTS_ENABLED
    if (!recording) {
      VP37_showDebug(&snapshot);
    }
    for (size_t i = 0U; i < sampleCount; i++) {
      VP37_showTrace(&samples[i]);
    }
#else
    VP37_showDebug(&snapshot);
#endif
  }
#endif
  m_mutex_enter_blocking(turboStateMutex);
  Turbo_showDebug(&s_ctx.turbo);
  m_mutex_exit(turboStateMutex);

  hal_idle();
  hal_delay_ms(CORE_OPERATION_DELAY);
}

/**
 * @brief Initialize the second core runtime context.
 */
static void initializeCore1(void) {
  start_initContextMutexes();
  const hal_status_t rpmStatus = RPM_create();
  s_startRuntimeState.core1InitStatus = rpmStatus;
  if (rpmStatus != HAL_OK) {
    return;
  }
  createEngineOperation();
#ifdef VP37
  // The scan and its completion interrupt belong to this core.
  const hal_status_t scanStatus = VP37_currentScanStart();
  if (scanStatus != HAL_OK) {
    derr("VP37 current scan start failed: %s",
         hal_status_to_string(scanStatus));
  } else if (!sensors_scanCoversInputs()) {
    // The scan owns the converter from now on: an input it does not carry
    // cannot be read for the rest of the run.
    derr("ADC scan leaves a sensor input unreadable (pins %u, %u expected)",
         (unsigned)ADC_SENSORS_PIN, (unsigned)ADC_VOLT_PIN);
  } else {
    // The scan carries every input the sensors read through hal_adc_read().
  }
#endif

  setStartedCore1();

  deb("Second core initialized");
}

//-----------------------------------------------------------------------------
// main logic
//-----------------------------------------------------------------------------

/**
 * @brief Run one core-1 control-loop iteration.
 */
static void runCore1(void) {

  s_startPersistentState.statusVariable1Val = 0;
  if (hal_status_is_error(s_startRuntimeState.core1InitStatus)) {
    s_startPersistentState.statusVariable1Val = -2;
    hal_idle();
    hal_delay_ms(CORE_OPERATION_DELAY);
    return;
  }
  updateWatchdogCore1();

  if (!isEnvironmentStarted()) {
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
  // A running functional test owns the demand; the application produces it
  // otherwise, including every build without tests.
  if (!tickTests()) {
#if ECU_ENGINE_CONTROL_ENABLED
    engineOperation_process(&s_ctx.engineOp);
    engineOperation_showDebug(&s_ctx.engineOp);
#else
    (void)VP37_setPositionDemand(&s_ctx.injectionPump,
                                 getDriverDemandPercent());
#endif
  }
  VP37_process(&s_ctx.injectionPump);
  hal_mutex_unlock(vp37StateMutex);
#else
  RPM_showDebug(&s_ctx.rpm);
#endif
  s_startPersistentState.statusVariable1Val = 3;

  hal_idle();
}

void app_start(void) {
  initializeCore0();
// init core1 here only if target is not RP2040
#if !HAL_TARGET_IS_RP
  initializeCore1();
#endif
}

void app_task0(void) { runCore0(); }

void app_task1(void) {
#if HAL_TARGET_IS_RP
  static bool initialized = false;
  if (!initialized) {
    initializeCore1();
    initialized = true;
  }
#endif
  runCore1();
}
