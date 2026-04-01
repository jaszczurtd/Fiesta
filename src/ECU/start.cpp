
#include "start.h"

static unsigned long lastThreadSeconds = 0;
static SmartTimers timerEverySecond;
static SmartTimers timerMedium;
static SmartTimers timerHigh;
static SmartTimers timerGPS;
static SmartTimers timerDebug;
static SmartTimers timerCANUpdate;
static SmartTimers timerCANLoop;
static SmartTimers timerCANCheck;
static Turbo turbo;
static VP37Pump injectionPump;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

void setupTimers(void) {
watchdog_feed(); timerEverySecond.begin(callAtEverySecond, SECOND);                               hal_delay_ms(CORE_OPERATION_DELAY);
  watchdog_feed(); timerMedium.begin(readMediumValues, SECOND / MEDIUM_TIME_ONE_SECOND_DIVIDER);  hal_delay_ms(CORE_OPERATION_DELAY);
  watchdog_feed(); timerHigh.begin(readHighValues, SECOND / FREQUENT_TIME_ONE_SECOND_DIVIDER);    hal_delay_ms(CORE_OPERATION_DELAY);
  watchdog_feed(); timerGPS.begin(getGPSData, GPS_UPDATE);                                        hal_delay_ms(CORE_OPERATION_DELAY);
  watchdog_feed(); timerDebug.begin(updateValsForDebug, DEBUG_UPDATE);                            hal_delay_ms(CORE_OPERATION_DELAY);
  watchdog_feed(); timerCANUpdate.begin(CAN_updaterecipients_01, CAN_UPDATE_RECIPIENTS);          hal_delay_ms(CORE_OPERATION_DELAY);
  watchdog_feed(); timerCANLoop.begin(canMainLoop, CAN_MAIN_LOOP_READ_INTERVAL);                  hal_delay_ms(CORE_OPERATION_DELAY);
  watchdog_feed(); timerCANCheck.begin(canCheckConnection, CAN_CHECK_CONNECTION);                 hal_delay_ms(CORE_OPERATION_DELAY);
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
  getGlowPlugsInstance()->initGlowPlugsTime(coolant);

#ifdef VP37
  injectionPump.init();
#endif
  watchdog_feed();

  turbo.init();

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

#if SYSTEM_TEMP
  deb("System temperature: %f", hal_read_chip_temp());
#endif
}

void looper(void) {
  statusVariable0 = 0;
  updateWatchdogCore0();

  statusVariable0 = 1;
  getGlowPlugsInstance()->process();

  statusVariable0 = 2;
  if(!isEnvironmentStarted()) {
    statusVariable0 = -1;
    hal_idle();
    return;
  }

  timerEverySecond.tick();
  timerMedium.tick();
  timerHigh.tick();
  timerGPS.tick();
  timerDebug.tick();
  timerCANUpdate.tick();
  timerCANLoop.tick();
  timerCANCheck.tick();
  if(lastThreadSeconds < getSeconds()) {
    lastThreadSeconds = getSeconds() + THREAD_CONTROL_SECONDS;

    deb("thread is alive");
  }
  statusVariable0 = 3;
  CAN_updaterecipients_02();
  statusVariable0 = 4;
  obdLoop();
  statusVariable0 = 5;
  getFanInstance()->process();
  statusVariable0 = 6;
  getHeaterInstance()->process();
  statusVariable0 = 7;
  getHeatedWindshieldsInstance()->process();
  statusVariable0 = 8;

#ifdef VP37
  injectionPump.showDebug();
#endif
  turbo.showDebug();

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
  turbo.process();
  statusVariable1 = 2;
#ifdef VP37
  injectionPump.process();
#else
  getRPMInstance()->process();
#endif
  statusVariable1 = 3;

  hal_idle();
}

