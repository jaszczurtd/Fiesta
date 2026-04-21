
#include "sensors.h"
#include "ecu_unit_testing.h"
#include "can.h"
#include "engineFuel.h"
#include "gps.h"
#include "rpm.h"

typedef struct {
  volatile float valueFields[F_LAST];
  volatile float reflectionValueFields[F_LAST];
} sensors_persistent_state_t;

typedef struct {
  int collantTableIdx;
  int collantValuesSet;
  float collantTable[HAL_TOOLS_TEMPERATURE_TABLES_SIZE];
  int oilTableIdx;
  int oilValuesSet;
  float oilTable[HAL_TOOLS_TEMPERATURE_TABLES_SIZE];
  unsigned char pcf8574State;
  hal_pwm_freq_channel_t pwmVp37;
  hal_pwm_freq_channel_t pwmTurbo;
  hal_pwm_freq_channel_t pwmAngle;
  unsigned char lowCurrentValue;
  float lastVoltage;
  int32_t lastEGTTemp;
  int32_t lastCoolantTemp;
  int32_t lastOilTemp;
  bool lastIsEngineRunning;
  adjustometer_reading_t adjustometer;
  uint8_t adjCommErrors;
} sensors_runtime_state_t;

NOINIT static sensors_persistent_state_t s_sensorsPersistent;
static sensors_runtime_state_t s_sensorsState = {
  .collantTableIdx = 0,
  .collantValuesSet = 0,
  .collantTable = {0.0f},
  .oilTableIdx = 0,
  .oilValuesSet = 0,
  .oilTable = {0.0f},
  .pcf8574State = 0,
  .pwmVp37 = NULL,
  .pwmTurbo = NULL,
  .pwmAngle = NULL,
  .lowCurrentValue = 0,
  .lastVoltage = 0.0f,
  .lastEGTTemp = 0,
  .lastCoolantTemp = 0,
  .lastOilTemp = 0,
  .lastIsEngineRunning = false,
  .adjustometer = {0, 0, 0, ADJ_STATUS_SIGNAL_LOST, false},
  .adjCommErrors = 0
};

m_mutex_def(analog4051Mutex);
m_mutex_def(valueFieldsMutex);
m_mutex_def(i2cBusMutex);

/**
 * @brief Validate a global value index before accessing the value table.
 * @param idx Global value index to validate.
 * @param caller Name of the caller used in diagnostics.
 * @return True when the index is valid, otherwise false.
 */
static bool sensors_isGlobalValueIndexValid(int idx, const char *caller) {
  if((idx < 0) || (idx >= F_LAST)) {
    derr_limited("sensors", "%s invalid value index: %d (valid: 0..%d)",
                 caller, idx, (F_LAST - 1));
    return false;
  }
  return true;
}

//I2C bus recovery: toggle SCL up to 9 times on GPIO level to release
//a slave that is holding SDA low (e.g. after master reset mid-transaction).
//Must be called BEFORE Wire.begin() / hal_i2c_init().

/**
 * @brief Initialize the I2C bus and its mutex-protected access path.
 * @return None.
 */
void initI2C(void) {
  static bool i2cMutexInited = false;
  if(!i2cMutexInited) {
    m_mutex_init(i2cBusMutex);
    i2cMutexInited = true;
  }
  hal_i2c_bus_clear(PIN_SDA, PIN_SCL);
  hal_i2c_init(PIN_SDA, PIN_SCL, I2C_SPEED_HZ);
}

void initSPI(void) {
  hal_spi_init(0, PIN_MISO, PIN_MOSI, PIN_SCK);

  // Deassert all SPI chip-selects immediately so that no MCP2515
  // floats its /CS low during another chip's SPI transactions.
  const uint8_t spiCsPins[] = { CAN0_GPIO, CAN1_GPIO, SD_CARD_CS };
  for(uint32_t i = 0; i < COUNTOF(spiCsPins); i++) {
    hal_gpio_set_mode(spiCsPins[i], HAL_GPIO_OUTPUT);
    hal_gpio_write(spiCsPins[i], true);
  }
}

void setGlobalValue(int idx, float val) {
  if(!sensors_isGlobalValueIndexValid(idx, "setGlobalValue")) {
    return;
  }
  m_mutex_enter_blocking(valueFieldsMutex);
  s_sensorsPersistent.valueFields[idx] = val;
  m_mutex_exit(valueFieldsMutex);
}

float getGlobalValue(int idx) {
  if(!sensors_isGlobalValueIndexValid(idx, "getGlobalValue")) {
    return 0.0f;
  }
  m_mutex_enter_blocking(valueFieldsMutex);
  float v = s_sensorsPersistent.valueFields[idx];
  m_mutex_exit(valueFieldsMutex);
  return v;
}

void initSensors(void) {
  m_mutex_init(valueFieldsMutex);
  hal_adc_set_resolution(HAL_TOOLS_ADC_BITS);
  pwm_init();

  init4051();

  for(int a = 0; a < F_LAST; a++) {
    s_sensorsPersistent.valueFields[a] = s_sensorsPersistent.reflectionValueFields[a] = 0.0;
  }

  s_sensorsState.collantTableIdx = s_sensorsState.collantValuesSet = 0;
  s_sensorsState.oilTableIdx = s_sensorsState.oilValuesSet = 0;
  s_sensorsState.lowCurrentValue = 0;
  s_sensorsState.lastVoltage = 0.0f;
  s_sensorsState.lastEGTTemp = 0;
  s_sensorsState.lastCoolantTemp = 0;
  s_sensorsState.lastOilTemp = 0;
  s_sensorsState.lastIsEngineRunning = false;

  initGPS();
}

void initBasicPIO(void) {
  hal_gpio_set_mode(HAL_LED_PIN, HAL_GPIO_OUTPUT);
  hal_gpio_set_mode(PIO_DPF_LAMP, HAL_GPIO_OUTPUT);
}

//-------------------------------------------------------------------------------------------------
//Read coolant temperature
//-------------------------------------------------------------------------------------------------
float readCoolantTemp(void) {
  float a = 0.0;
  m_mutex_enter_blocking(analog4051Mutex);

  set4051ActivePin(HC4051_I_COOLANT_TEMP);
  a = getAverageForTable(&s_sensorsState.collantTableIdx, &s_sensorsState.collantValuesSet,
                        ntcToTemp(ADC_SENSORS_PIN, R_TEMP_A, R_TEMP_B), //real values (resitance)
                        s_sensorsState.collantTable);
  m_mutex_exit(analog4051Mutex);  
  return a;
}

//-------------------------------------------------------------------------------------------------
//Read oil temperature
//-------------------------------------------------------------------------------------------------

float readOilTemp(void) {
  float a = 0.0;
  m_mutex_enter_blocking(analog4051Mutex);
  set4051ActivePin(HC4051_I_OIL_TEMP);

  a = getAverageForTable(&s_sensorsState.oilTableIdx, &s_sensorsState.oilValuesSet,
                         ntcToTemp(ADC_SENSORS_PIN, R_TEMP_A, R_TEMP_B), //real values (resitance)
                         s_sensorsState.oilTable);
  m_mutex_exit(analog4051Mutex);  
  return a;
}

//-------------------------------------------------------------------------------------------------
//Read throttle
//-------------------------------------------------------------------------------------------------

/**
 * @brief Map a raw legacy throttle ADC reading into the internal driver-demand scale.
 * @param rawVal Raw ADC value measured on the throttle input.
 * @return Driver-demand signal in the internal PWM-scale range.
 * @note The input is currently named "throttle" in code, but architecturally it is the
 *       G79/G185-like driver-demand path.
 */
TESTABLE_STATIC int32_t sensors_computeThrottlePositionFromRaw(int32_t rawVal) {
  int32_t initialVal = rawVal - THROTTLE_MIN;

  if(initialVal < 0) {
      initialVal = 0;
  }
  int32_t maxVal = (THROTTLE_MAX - THROTTLE_MIN);

  if(initialVal > maxVal) {
      initialVal = maxVal;
  }
  float divider = maxVal / (float)PWM_RESOLUTION;
  int32_t result = (int32_t)(initialVal / divider);
  return abs(result - PWM_RESOLUTION);
}

int32_t readThrottle(void) {
  m_mutex_enter_blocking(analog4051Mutex);
  set4051ActivePin(HC4051_I_THROTTLE_POS);

  int32_t rawVal = (int32_t)getAverageValueFrom(ADC_SENSORS_PIN);
  m_mutex_exit(analog4051Mutex);

  return sensors_computeThrottlePositionFromRaw(rawVal);
}

/**
 * @brief Convert the stored legacy throttle value into a 0..100 driver-demand percentage.
 * @return Driver-demand percentage derived from the G79/G185-like input path.
 */
int32_t getThrottlePercentage(void) {
  int32_t currentVal = (int32_t)(getGlobalValue(F_THROTTLE_POS));
  float percent = (currentVal * 100) / PWM_RESOLUTION;
  return percentToGivenVal(percent, 100);
}

//-------------------------------------------------------------------------------------------------
//Read air temperature
//-------------------------------------------------------------------------------------------------

/**
 * @brief Read intake air temperature from the G72-like sensor path.
 * @return Intake air temperature in degrees Celsius.
 */
float readAirTemperature(void) {
  float a = 0.0;
  m_mutex_enter_blocking(analog4051Mutex);

  set4051ActivePin(HC4051_I_AIR_TEMP);
  a = ntcToTemp(ADC_SENSORS_PIN, R_TEMP_AIR_A, R_TEMP_AIR_B);
  m_mutex_exit(analog4051Mutex);
  return a;
}

//-------------------------------------------------------------------------------------------------
//Read bar pressure amount
//-------------------------------------------------------------------------------------------------

/**
 * @brief Read boost / manifold pressure from the G71-like sensor path.
 * @return Pressure in bar relative to atmosphere.
 */
float readBarPressure(void) {
  m_mutex_enter_blocking(analog4051Mutex);
  set4051ActivePin(HC4051_I_BAR_PRESSURE);

  float val = ((float)getAverageValueFrom(ADC_SENSORS_PIN) / DIVIDER_PRESSURE_BAR) -
      1.0; //atmospheric pressure
  m_mutex_exit(analog4051Mutex);

  if(val < 0.0) {
      val = 0.0;
  } 
  return val;
}

/**
 * @brief Translate an I2C end-transmission status code into readable text.
 * @param code HAL I2C end-transmission status code.
 * @return Constant string describing the error code.
 */
static const char *i2cEndTransmissionError(uint8_t code) {
  switch(code) {
    case 1:  return "data too long";
    case 2:  return "NACK on address";
    case 3:  return "NACK on data";
    case 4:  return "other error";
    case 5:  return "timeout";
    default: return "unknown";
  }
}

bool pcf8574_init(void) {
  s_sensorsState.pcf8574State = 0;

  m_mutex_enter_blocking(i2cBusMutex);
  hal_i2c_begin_transmission(PCF8574_ADDR);
  bool success = hal_i2c_write(s_sensorsState.pcf8574State);
  bool notFound = hal_i2c_end_transmission();
  m_mutex_exit(i2cBusMutex);

  if(!success || notFound) {
    derr("pcf8574_init: %s (endTx=%u)", notFound ? i2cEndTransmissionError(notFound) : "write failed", (unsigned)notFound);
  }

  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, (!success || notFound));
  return (success && !notFound);
}

void pcf8574_write(unsigned char pin, bool value) {
  if(pin > 7) {
    derr("pcf8574_write invalid pin: %u", (unsigned)pin);
    return;
  }

  if(value) {
    bitSet(s_sensorsState.pcf8574State, pin);
  }  else {
    bitClear(s_sensorsState.pcf8574State, pin);
  }

  m_mutex_enter_blocking(i2cBusMutex);
  hal_i2c_begin_transmission(PCF8574_ADDR);
  bool success = hal_i2c_write(s_sensorsState.pcf8574State);
  bool notFound = hal_i2c_end_transmission();
  m_mutex_exit(i2cBusMutex);

  if(!success || notFound) {
    derr("pcf8574_write: %s (endTx=%u)", notFound ? i2cEndTransmissionError(notFound) : "write failed", (unsigned)notFound);
  }

  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, (!success || notFound));
}

bool pcf8574_read(unsigned char pin) {
  if(pin > 7) {
    derr("pcf8574_read invalid pin: %u", (unsigned)pin);
    return false;
  }

  m_mutex_enter_blocking(i2cBusMutex);
  uint8_t received = hal_i2c_request_from(PCF8574_ADDR, 1);
  if(received != 1) {
    m_mutex_exit(i2cBusMutex);
    derr("pcf8574_read: expected 1 byte, got %d", (int)received);
    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);
    return false;
  }

  int raw = hal_i2c_read();
  m_mutex_exit(i2cBusMutex);

  if(raw < 0) {
    derr("pcf8574_read: hal_i2c_read failed: %d", raw);
    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);
    return false;
  }

  s_sensorsState.pcf8574State = (uint8_t)raw;
  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, false);
  return bitRead(s_sensorsState.pcf8574State, pin);
}

/**
 * @brief Read the legacy throttle-named value as stored in the global value table.
 * @return Current raw driver-demand value.
 * @note This is still the G79/G185-like pedal-demand signal, despite the legacy name.
 */
int32_t getRAWThrottle(void) {
  return (int32_t)(getGlobalValue(F_THROTTLE_POS));
}

void readMediumValues(void) {
  switch(s_sensorsState.lowCurrentValue) {
    case F_COOLANT_TEMP:
      setGlobalValue(F_COOLANT_TEMP, readCoolantTemp());
      break;
    case F_OIL_TEMP:
      setGlobalValue(F_OIL_TEMP, readOilTemp());
      break;
    case F_INTAKE_TEMP:
      setGlobalValue(F_INTAKE_TEMP, readAirTemperature());
      break;
    case F_FUEL:
      setGlobalValue(F_FUEL, readFuel());
      break;
#ifndef VP37
    case F_VOLTS:
      setGlobalValue(F_VOLTS, getSystemSupplyVoltage());
      break;
#endif
  }
  if(s_sensorsState.lowCurrentValue++ >= F_LAST) {
    s_sensorsState.lowCurrentValue = 0;
  }
}

/**
 * @brief Calculate normalized engine load from pressure and RPM inputs.
 * @param pressureBar Pressure input in bar.
 * @param rpm Engine speed in RPM.
 * @return Engine load percentage clamped to the 0..100 range.
 * @note This helper produces a project-local supervisory load estimate, not an OEM
 *       EDC15 quantity or air-mass model.
 */
TESTABLE_STATIC int32_t sensors_calculateEngineLoadFromValues(float pressureBar, float rpm) {
  float map = (pressureBar * 255.0f / 2.55f);
  float load = (map / 255.0f) * (rpm / (float)(RPM_MAX_EVER)) * 100.0f;
  int32_t roundedLoad = (int32_t)(load + 0.5f);

  if (roundedLoad < 0) {
      roundedLoad = 0;
  } else if (roundedLoad > 100) {
      roundedLoad = 100;
  }
  return roundedLoad;
}

int32_t getPercentageEngineLoad(void) {
  return sensors_calculateEngineLoadFromValues(getGlobalValue(F_PRESSURE), getGlobalValue(F_RPM));
}

void readHighValues(void) {
  for(int a = 0; a < F_LAST; a++) {
    float v = getGlobalValue(a);
    switch(a) {
      case F_RPM:
        v = RPM_getCurrentRPM(getRPMInstance());
        setGlobalValue(a, v);
        break;
      case F_THROTTLE_POS:
        v = readThrottle();
        setGlobalValue(a, v);
        break;
      case F_PRESSURE:
        v = readBarPressure();
        setGlobalValue(a, v);
        break;
      case F_GPS_CAR_SPEED:
        v = getCurrentCarSpeed();
        setGlobalValue(a, v);
        break;
      case F_CALCULATED_ENGINE_LOAD:
        v = getPercentageEngineLoad();
        setGlobalValue(a, v);
        break;
    }
    if(s_sensorsPersistent.reflectionValueFields[a] != v) {
        s_sensorsPersistent.reflectionValueFields[a] = v;

        CAN_sendThrottleUpdate();
        CAN_sendTurboUpdate();
    }
  }
}

void init4051(void) {
  deb("4051 init");

  m_mutex_init(analog4051Mutex);

  hal_gpio_set_mode(C_4051, HAL_GPIO_OUTPUT);
  hal_gpio_set_mode(B_4051, HAL_GPIO_OUTPUT);
  hal_gpio_set_mode(A_4051, HAL_GPIO_OUTPUT);

  set4051ActivePin(0);
}

void set4051ActivePin(unsigned char pin) {
  hal_gpio_write(A_4051, (pin & 0x01) > 0);
  hal_gpio_write(B_4051, (pin & 0x02) > 0);
  hal_gpio_write(C_4051, (pin & 0x04) > 0);
}

bool isDPFRegenerating(void) {
  return getGlobalValue(F_DPF_REGEN) > 0;
}

void updateValsForDebug(void) {

  char stamp[24];
  if(isSDLoggerInitialized()) {
    snprintf(stamp, sizeof(stamp), "LN:%d ", getSDLoggerNumber() - 1);
  } else {
    snprintf(stamp, sizeof(stamp), "NL/");
  }

  float volts = rroundf(getGlobalValue(F_VOLTS));
  if(s_sensorsState.lastVoltage != volts) {
    s_sensorsState.lastVoltage = volts;
    deb("%sVoltage update: %.1fV", stamp, volts);
  }

  int32_t egt = (int32_t)getGlobalValue(F_EGT);
  if(s_sensorsState.lastEGTTemp != egt) {
    s_sensorsState.lastEGTTemp = egt;
    deb("%sEGT update: %dC", stamp, egt);
  }

  int32_t coolant = (int32_t)getGlobalValue(F_COOLANT_TEMP);
  if(s_sensorsState.lastCoolantTemp != coolant) {
    s_sensorsState.lastCoolantTemp = coolant;
    deb("%sCoolant temp. update: %dC", stamp, coolant);
  }

  int32_t oil = (int32_t)getGlobalValue(F_OIL_TEMP);
  if(s_sensorsState.lastOilTemp != oil) {
    s_sensorsState.lastOilTemp = oil;
    deb("%sOil temp. update: %dC", stamp, oil);
  }

  bool running = RPM_isEngineRunning(getRPMInstance());
  if(s_sensorsState.lastIsEngineRunning != running) {
    s_sensorsState.lastIsEngineRunning = running;
    deb("%sEngine is running: %s", stamp, running ? "yes" : "no");
  }
}

void pwm_init(void) {
  s_sensorsState.pwmVp37  = hal_pwm_freq_create(PIO_VP37_RPM,  VP37_PWM_FREQUENCY_HZ,  PWM_RESOLUTION);
  s_sensorsState.pwmTurbo = hal_pwm_freq_create(PIO_TURBO,     TURBO_PWM_FREQUENCY_HZ, PWM_RESOLUTION);
  s_sensorsState.pwmAngle = hal_pwm_freq_create(PIO_VP37_ANGLE, ANGLE_PWM_FREQUENCY_HZ, PWM_RESOLUTION);
}

void valToPWM(unsigned char pin, int32_t val) {
  hal_pwm_freq_channel_t ch = NULL;
  switch(pin) {
    case PIO_TURBO:      ch = s_sensorsState.pwmTurbo; break;
    case PIO_VP37_RPM:   ch = s_sensorsState.pwmVp37;  break;
    case PIO_VP37_ANGLE: ch = s_sensorsState.pwmAngle; break;
    default: break;
  }
  if(ch != NULL) {
    hal_pwm_freq_write(ch, (PWM_RESOLUTION - val));
    dtcManagerSetActive(DTC_PWM_CHANNEL_NOT_INIT, false);
  } else {
    derr("config for this pwm is not initialized!");
    dtcManagerSetActive(DTC_PWM_CHANNEL_NOT_INIT, true);
  }
}

// ── Adjustometer I2C readout (replaces ADS1115) ─────────────────────────────

// Runtime I2C bus recovery: if the Adjustometer (or any I2C slave) resets
// mid-transaction, the ECU master can get stuck.  After several consecutive
// failed transactions, reinitialise the bus with GPIO-level recovery.
#define I2C_RECOVERY_THRESHOLD 5
#define ADJ_COMM_ERROR_THRESHOLD 3
static uint8_t i2cConsecutiveErrors = 0;

/**
 * @brief Count I2C errors and recover the bus when the threshold is reached.
 * @return None.
 */
static void i2cCheckRecovery(void) {
  i2cConsecutiveErrors++;
  if(i2cConsecutiveErrors >= I2C_RECOVERY_THRESHOLD) {
    deb("I2C: %u consecutive errors, performing bus recovery", (unsigned)i2cConsecutiveErrors);
    hal_i2c_deinit();
    initI2C();
    i2cConsecutiveErrors = 0;
  }
}

/**
 * @brief Read the full Adjustometer register block over I2C for the VP37 quantity-feedback path.
 * @return Latest Adjustometer reading structure, reusing previous values on failure.
 * @note The returned pulse, status, and fuel-temperature fields form a project-local
 *       G149/G81-like telemetry bundle, not a literal OEM sensor block.
 */
static adjustometer_reading_t readAdjustometer(void) {

  m_mutex_enter_blocking(i2cBusMutex);

  // Set register pointer to 0x00 (PULSE_HI)
  hal_i2c_begin_transmission(ADJUSTOMETER_I2C_ADDR);
  hal_i2c_write(ADJUSTOMETER_REG_PULSE_HI);
  uint8_t txErr = hal_i2c_end_transmission();
  if(txErr) {
    m_mutex_exit(i2cBusMutex);
    derr("Adjustometer I2C tx error: %s (%d)", i2cEndTransmissionError(txErr), (int)txErr);
    i2cCheckRecovery();
    s_sensorsState.adjCommErrors++;
    if(s_sensorsState.adjCommErrors >= ADJ_COMM_ERROR_THRESHOLD) {
      s_sensorsState.adjustometer.commOk = false;
    }
    return s_sensorsState.adjustometer;
  }

  // Read 5 registers starting from 0x00
  uint8_t received = hal_i2c_request_from(ADJUSTOMETER_I2C_ADDR, ADJUSTOMETER_REG_COUNT);
  if(received != ADJUSTOMETER_REG_COUNT) {
    m_mutex_exit(i2cBusMutex);
    //dtcManagerSetActive(DTC_ADJ_COMM_LOST, true);
    derr("Adjustometer I2C read error: expected %d bytes, got %d", ADJUSTOMETER_REG_COUNT, (int)received);
    i2cCheckRecovery();
    s_sensorsState.adjCommErrors++;
    if(s_sensorsState.adjCommErrors >= ADJ_COMM_ERROR_THRESHOLD) {
      s_sensorsState.adjustometer.commOk = false;
    }
    return s_sensorsState.adjustometer;
  }

  uint8_t buf[ADJUSTOMETER_REG_COUNT];
  for(uint8_t i = 0; i < ADJUSTOMETER_REG_COUNT; i++) {
    int b = hal_i2c_read();
    if(b < 0) {
      m_mutex_exit(i2cBusMutex);
      derr("Adjustometer I2C read error at byte %d: %d", (int)i, b);
      //dtcManagerSetActive(DTC_ADJ_COMM_LOST, true);
      i2cCheckRecovery();
      s_sensorsState.adjCommErrors++;
      if(s_sensorsState.adjCommErrors >= ADJ_COMM_ERROR_THRESHOLD) {
        s_sensorsState.adjustometer.commOk = false;
      }
      return s_sensorsState.adjustometer;
    }
    buf[i] = (uint8_t)b;
  }

  m_mutex_exit(i2cBusMutex);

  //dtcManagerSetActive(DTC_ADJ_COMM_LOST, false);

  s_sensorsState.adjustometer.pulseHz    = (int16_t)((uint16_t)buf[0] << 8 | buf[1]);
  s_sensorsState.adjustometer.voltageRaw = buf[2];
  s_sensorsState.adjustometer.fuelTempC  = buf[3];
  s_sensorsState.adjustometer.status     = buf[4];
  s_sensorsState.adjustometer.commOk     = true;
  s_sensorsState.adjCommErrors = 0;
  i2cConsecutiveErrors = 0;

  //dtcManagerSetActive(DTC_ADJ_SIGNAL_LOST,     (r.status & ADJ_STATUS_SIGNAL_LOST) != 0);
  //dtcManagerSetActive(DTC_ADJ_FUEL_TEMP_BROKEN, (r.status & ADJ_STATUS_FUEL_TEMP_BROKEN) != 0);
  //dtcManagerSetActive(DTC_ADJ_VOLTAGE_BAD,     (r.status & ADJ_STATUS_VOLTAGE_BAD) != 0);

  return s_sensorsState.adjustometer;
}

/**
 * @brief Wait until the project-local G149-like baseline capture is finished.
 * @return True when baseline becomes ready before timeout, otherwise false.
 */
bool waitForAdjustometerBaseline(void) {
  uint32_t start = hal_millis();
  while((hal_millis() - start) < ADJUSTOMETER_BASELINE_WAIT_MS) {
    adjustometer_reading_t r = readAdjustometer();
    if(!r.commOk) {
      /* device may still be booting – keep retrying until timeout */
      hal_delay_ms(10);
      watchdog_feed();
      continue;
    }
    if((r.status & ADJ_STATUS_BASELINE_PENDING) == 0) {
      deb("Adjustometer baseline ready (%lu ms)", (unsigned long)(hal_millis() - start));
      return true;
    }
    hal_delay_ms(10);
    watchdog_feed();
  }
  derr("Adjustometer baseline timeout (%u ms)", ADJUSTOMETER_BASELINE_WAIT_MS);
  return false;
}

/**
 * @brief Return the latest Adjustometer reading for the N146/G149-like inner loop.
 * @return Pointer to the shared Adjustometer reading structure.
 */
adjustometer_reading_t *getVP37Adjustometer(void) {
  readAdjustometer();
  return &s_sensorsState.adjustometer;
}

/**
 * @brief Read system supply voltage from the Adjustometer telemetry bundle.
 * @return Supply voltage in volts, or 0 when Adjustometer telemetry is unavailable.
 */
float getSystemSupplyVoltage(void) {
  adjustometer_reading_t *reading = getVP37Adjustometer();
  if(reading == NULL || !reading->commOk) {
    return 0.0f;
  }
  return reading->voltageRaw * 0.1f;
}