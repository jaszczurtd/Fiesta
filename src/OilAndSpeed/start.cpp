
#include "start.h"

void updateValsForDebug(void);
void readThermocouples(void);

static SmartTimers timerCANUpdate;
static SmartTimers timerCANLoop;
static SmartTimers timerCANCheck;
static SmartTimers timerOilPressure;
static SmartTimers timerDebug;
static SmartTimers timerThermocouples;

hal_thermocouple_t egt_pre_dpf = NULL;
hal_thermocouple_t egt_mid_dpf = NULL;

NOINIT int statusVariable0;
NOINIT int statusVariable1;

static int *wValues = NULL;
static int wSize = 0;
void executeByWatchdog(int *values, int size) {
  wValues = values;
  wSize = size;
}

void setupTimers(void) {
  timerCANUpdate.begin(updateCANrecipients, CAN_UPDATE_RECIPIENTS);    hal_delay_ms(CORE_OPERATION_DELAY);
  timerCANLoop.begin(canMainLoop, CAN_MAIN_LOOP_READ_INTERVAL);        hal_delay_ms(CORE_OPERATION_DELAY);
  timerCANCheck.begin(canCheckConnection, CAN_CHECK_CONNECTION);       hal_delay_ms(CORE_OPERATION_DELAY);
  timerOilPressure.begin(readOilPressure, OIL_PRESSURE_READ_INTERVAL); hal_delay_ms(CORE_OPERATION_DELAY);
  timerThermocouples.begin(readThermocouples, THERMOCOUPLE_READ_INTERVAL); hal_delay_ms(CORE_OPERATION_DELAY);
  timerDebug.begin(updateValsForDebug, DEBUG_UPDATE);
}

void initialization(void) {
  bool result = false;

  debugInit();
  setDebugPrefix("OIL&SPD:");
  setupOnboardLed();
  initBasicPIO();

  bool rebooted = setupWatchdog(executeByWatchdog, WATCHDOG_TIME);
  if (!rebooted) {
    statusVariable0 = statusVariable1 = 0;
  }

  initSPI();

  result = canInit();
  setLEDColor(result ? RED: GREEN);
  if (result) {
    derr("cannot setup CAN, exiting");
    return;
  }

  updateCANrecipients();
  canMainLoop();
  updateValsForDebug();

  watchdog_feed();

  result = setupOilPressure();
  if (!result) {
    derr("cannot setup oil pressure readout, exiting");
    return;
  }

  setupTimers();

  result = setupSpeedometer();
  setLEDColor(result ? GREEN: YELLOW);
  if (!result) {
    derr("cannot setup speedometer, exiting");
    return;
  }

  hal_delay_ms(2000);

  setGlobalValue(F_EGT, 0);
  setGlobalValue(F_DPF_TEMP, 0);

  hal_thermocouple_config_t egt_cfg;
  egt_cfg.chip             = HAL_THERMOCOUPLE_CHIP_MCP9600;
  egt_cfg.bus.i2c.sda_pin  = PIN_I2C_SDA;
  egt_cfg.bus.i2c.scl_pin  = PIN_I2C_SCL;
  egt_cfg.bus.i2c.clock_hz = 100000UL;
  egt_cfg.bus.i2c.i2c_addr = MCP9600_ADDR_PRE_DPF;

  egt_pre_dpf = hal_thermocouple_init(&egt_cfg);
  if (!egt_pre_dpf) {
    derr("[EGT] MCP9600 #1 not found!");
  } else {
    hal_thermocouple_set_type(egt_pre_dpf, HAL_THERMOCOUPLE_TYPE_K);
    hal_thermocouple_set_adc_resolution(egt_pre_dpf, HAL_THERMOCOUPLE_ADC_RES_18);
    hal_thermocouple_set_ambient_resolution(egt_pre_dpf, HAL_THERMOCOUPLE_AMBIENT_RES_0_0625);
    hal_thermocouple_set_filter(egt_pre_dpf, 3);
    hal_thermocouple_enable(egt_pre_dpf, true);
    deb("[EGT] MCP9600 #1 OK");
  }

  egt_mid_dpf = hal_thermocouple_init(&egt_cfg);
  if (!egt_mid_dpf) {
    derr("[EGT] MCP9600 #2 not found!");
  } else {
    hal_thermocouple_set_type(egt_mid_dpf, HAL_THERMOCOUPLE_TYPE_K);
    hal_thermocouple_set_adc_resolution(egt_mid_dpf, HAL_THERMOCOUPLE_ADC_RES_18);
    hal_thermocouple_set_ambient_resolution(egt_mid_dpf, HAL_THERMOCOUPLE_AMBIENT_RES_0_0625);
    hal_thermocouple_set_filter(egt_mid_dpf, 3);
    hal_thermocouple_enable(egt_mid_dpf, true);
    deb("[EGT] MCP9600 #2 OK");
  }

  setStartedCore0();
}

void readThermocouples(void) {
  if (egt_pre_dpf) {
    setGlobalValue(F_EGT, hal_thermocouple_read(egt_pre_dpf));
  }

  if (egt_mid_dpf) {
    setGlobalValue(F_DPF_TEMP, hal_thermocouple_read(egt_mid_dpf));
  }

  updateEGTrecipients();
}

void looper() {

  statusVariable0 = 0;
  updateWatchdogCore0();

  if (!isEnvironmentStarted()) {
    statusVariable0 = -1;
    hal_delay_ms(CORE_OPERATION_DELAY);
    hal_idle();
    return;
  }

  statusVariable0 = 1;

  timerCANUpdate.tick();
  timerCANLoop.tick();
  timerCANCheck.tick();
  timerOilPressure.tick();
  timerDebug.tick();
  timerThermocouples.tick();
  onImpulseTranslating();
  canSendLoop();

  hal_delay_ms(CORE_OPERATION_DELAY);
  hal_idle();
}


void initialization1() {
  setStartedCore1();
}

void looper1() {
  updateWatchdogCore1();

  if (!isEnvironmentStarted()) {
    statusVariable1 = -1;
    hal_delay_ms(CORE_OPERATION_DELAY);
    hal_idle();
    return;
  }
  statusVariable1 = 1;

  hal_delay_ms(CORE_OPERATION_DELAY);
  hal_idle();
}

void updateValsForDebug(void) {
  deb("ECU:%s, cluster:%s, circumference:%f, oil:%fBAR", isEcuConnected() ? "on" : "off",
                                                          isClusterConnected() ? "on" : "off",
                                                          getCircumference(),
                                                          getGlobalValue(F_OIL_PRESSURE));
  deb("thermo1: %fC, thermo2: %fC", getGlobalValue(F_EGT), getGlobalValue(F_DPF_TEMP));
}
