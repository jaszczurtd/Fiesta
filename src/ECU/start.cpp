
#include "start.h"

static unsigned long lastThreadSeconds = 0;
static Timer generalTimer;
static Turbo turbo;
static VP37Pump injectionPump;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

void setupTimerWith(unsigned long time, bool(*function)(void *argument)) {
  watchdog_feed();
  generalTimer.every(time, function);
  m_delay(CORE_OPERATION_DELAY);
}

void setupTimers(void) {

  generalTimer = timer_create_default();

  setupTimerWith(SECOND, callAtEverySecond);
  setupTimerWith(SECOND / MEDIUM_TIME_ONE_SECOND_DIVIDER, readMediumValues);
  setupTimerWith(SECOND / FREQUENT_TIME_ONE_SECOND_DIVIDER, readHighValues);
  setupTimerWith(GPS_UPDATE, getGPSData);
  setupTimerWith(DEBUG_UPDATE, updateValsForDebug);
  setupTimerWith(CAN_UPDATE_RECIPIENTS, CAN_updaterecipients_01);
  setupTimerWith(CAN_MAIN_LOOP_READ_INTERVAL, canMainLoop);
  setupTimerWith(CAN_CHECK_CONNECTION, canCheckConnection);  
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
 
  initTests();

  //this has to be invoked as soon as possible, and twice
  initI2C();
  pcf8574_init();
  Wire.end();

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
  valueFields[F_COOLANT_TEMP] = coolant;
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

  valueFields[F_VOLTS] = getSystemSupplyVoltage();
  TEST_ASSERT_TRUE(valueFields[F_VOLTS] > 0);

  #ifdef PICO_W
  scanNetworks(WIFI_SSID);
  #endif

  initFuelMeasurement();

  canCheckConnection(NULL);
  canMainLoop(NULL);
  callAtEverySecond(NULL);
  updateValsForDebug(NULL);
  CAN_sendAll();
  setupTimers();

  deb("System temperature:%.1fC", rroundf(analogReadTemp()));
  
  setStartedCore0();

  deb("Fiesta MTDDI started: %s\n", isEnvironmentStarted() ? "yes" : "no");
#ifdef INC_FREERTOS_H
  deb("FreeRTOS is active!");
#else 
  deb("Normal Arduino build.");
#endif

  startTests();
}

static bool alertBlink = false;

//timer functions
bool callAtEverySecond(void *arg) {
  alertBlink = (alertBlink) ? false : true;
  digitalWrite(LED_BUILTIN, alertBlink);
  digitalWrite(PIO_DPF_LAMP, isDPFRegenerating());

#if SYSTEM_TEMP
  deb("System temperature: %f", analogReadTemp());
#endif

  return true;
}

void looper(void) {
  statusVariable0 = 0;
  updateWatchdogCore0();

  statusVariable0 = 1;
  getGlowPlugsInstance()->process();

  statusVariable0 = 2;
  if(!isEnvironmentStarted()) {
    statusVariable0 = -1;
    tight_loop_contents();
    return;
  }

  generalTimer.tick();
  if(lastThreadSeconds < getSeconds()) {
    lastThreadSeconds = getSeconds() + THREAD_CONTROL_SECONDS;

    deb("thread is alive, active tasks: %d", generalTimer.size());
  }
  statusVariable0 = 3;
  obdLoop();
  statusVariable0 = 4;
  getFanInstance()->process();
  statusVariable0 = 5;
  getHeaterInstance()->process();
  statusVariable0 = 6;
  getHeatedWindshieldsInstance()->process();
  statusVariable0 = 7;
  CAN_updaterecipients_02();
  statusVariable0 = 8;

#ifdef VP37
  injectionPump.showDebug();
#endif
  turbo.showDebug();

  tight_loop_contents();
  m_delay(CORE_OPERATION_DELAY);  
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
    tight_loop_contents();
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

  tight_loop_contents();
}

