
#include "sensors.h"
#include "can.h"
#include "engineFuel.h"
#include "gps.h"
#include "rpm.h"

NOINIT static volatile float valueFields[F_LAST];
NOINIT volatile float reflectionValueFields[F_LAST];


static int collantTableIdx = 0;
static int collantValuesSet = 0;
static float collantTable[TEMPERATURE_TABLES_SIZE];
static int oilTableIdx = 0;
static int oilValuesSet = 0;
static float oilTable[TEMPERATURE_TABLES_SIZE];

static unsigned char pcf8574State = 0;

m_mutex_def(analog4051Mutex);
m_mutex_def(valueFieldsMutex);

static hal_pwm_freq_channel_t pwmVp37  = NULL;
static hal_pwm_freq_channel_t pwmTurbo = NULL;
static hal_pwm_freq_channel_t pwmAngle = NULL;

void initI2C(void) {
  hal_i2c_init(PIN_SDA, PIN_SCL, I2C_SPEED_HZ);
}

void initSPI(void) {
  hal_spi_init(0, PIN_MISO, PIN_MOSI, PIN_SCK);
}

void setGlobalValue(int idx, float val) {
  m_mutex_enter_blocking(valueFieldsMutex);
  valueFields[idx] = val;
  m_mutex_exit(valueFieldsMutex);
}

float getGlobalValue(int idx) {
  m_mutex_enter_blocking(valueFieldsMutex);
  float v = valueFields[idx];
  m_mutex_exit(valueFieldsMutex);
  return v;
}

void initSensors(void) {
  m_mutex_init(valueFieldsMutex);
  hal_adc_set_resolution(ADC_BITS);
  pwm_init();

  init4051();
  hal_ext_adc_init(ADS1115_ADDR, ADC_RANGE);

  for(int a = 0; a < F_LAST; a++) {
    valueFields[a] = reflectionValueFields[a] = 0.0;
  }

  collantTableIdx = collantValuesSet = 0;
  oilTableIdx = oilValuesSet = 0;

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
  a = getAverageForTable(&collantTableIdx, &collantValuesSet,
                        ntcToTemp(ADC_SENSORS_PIN, R_TEMP_A, R_TEMP_B), //real values (resitance)
                        collantTable);
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

  a = getAverageForTable(&oilTableIdx, &oilValuesSet,
                         ntcToTemp(ADC_SENSORS_PIN, R_TEMP_A, R_TEMP_B), //real values (resitance)
                         oilTable);
  m_mutex_exit(analog4051Mutex);  
  return a;
}

//-------------------------------------------------------------------------------------------------
//Read throttle
//-------------------------------------------------------------------------------------------------

int readThrottle(void) {
  m_mutex_enter_blocking(analog4051Mutex);
  set4051ActivePin(HC4051_I_THROTTLE_POS);

  int rawVal = getAverageValueFrom(ADC_SENSORS_PIN);
  m_mutex_exit(analog4051Mutex);

  //deb("rawVal %d", int(rawVal));

  int initialVal = rawVal - THROTTLE_MIN;

  if(initialVal < 0) {
      initialVal = 0;
  }
  int maxVal = (THROTTLE_MAX - THROTTLE_MIN);

  if(initialVal > maxVal) {
      initialVal = maxVal;
  }
  float divider = maxVal / (float)PWM_RESOLUTION;
  int result = (initialVal / divider);
  return abs(result - PWM_RESOLUTION);
}

int getThrottlePercentage(void) {
  int currentVal = int(getGlobalValue(F_THROTTLE_POS));
  float percent = (currentVal * 100) / PWM_RESOLUTION;
  return percentToGivenVal(percent, 100);
}

//-------------------------------------------------------------------------------------------------
//Read air temperature
//-------------------------------------------------------------------------------------------------

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

//-------------------------------------------------------------------------------------------------
//Read EGT temperature
//-------------------------------------------------------------------------------------------------

int readEGT(void) {
  int a = 0;
  m_mutex_enter_blocking(analog4051Mutex);

  set4051ActivePin(HC4051_I_EGT);
  a = ((getAverageValueFrom(ADC_SENSORS_PIN)) / DIVIDER_EGT);
  m_mutex_exit(analog4051Mutex);
  return a;
}

//-------------------------------------------------------------------------------------------------

void pcf8574_init(void) {
  pcf8574State = 0;

  hal_i2c_begin_transmission(PCF8574_ADDR);
  hal_i2c_write(pcf8574State);
  hal_i2c_end_transmission();
}

void pcf8574_write(unsigned char pin, bool value) {
  if(value) {
    bitSet(pcf8574State, pin);
  }  else {
    bitClear(pcf8574State, pin);
  }

  hal_i2c_begin_transmission(PCF8574_ADDR);
  bool success = hal_i2c_write(pcf8574State);
  bool notFound = hal_i2c_end_transmission();

  if(!success) {
    derr("error writting byte to pcf8574");
  }

  if(notFound) {
    derr("pcf8574 not found");
  }

  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, (!success || notFound));
}

bool pcf8574_read(unsigned char pin) {
  hal_i2c_begin_transmission(PCF8574_ADDR);
  bool retVal = hal_i2c_read();
  bool notFound = hal_i2c_end_transmission();

  if(notFound) {
    derr("pcf8574 not found");
  }
  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, notFound);
  return retVal;
}

int getRAWThrottle(void) {
  return int(getGlobalValue(F_THROTTLE_POS));
}

static unsigned char lowCurrentValue = 0;
void readMediumValues(void) {
  switch(lowCurrentValue) {
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
    case F_EGT:
      setGlobalValue(F_EGT, readEGT());
      break;
#ifndef VP37
    case F_VOLTS:
      setGlobalValue(F_VOLTS, getSystemSupplyVoltage());
      break;
#endif
  }
  if(lowCurrentValue++ > F_LAST) {
    lowCurrentValue = 0;
  }
}

int getPercentageEngineLoad(void) {

  float map = (getGlobalValue(F_PRESSURE) * 255.0f / 2.55f);
  float load = (map / 255.0f) * (getGlobalValue(F_RPM) / float(RPM_MAX_EVER)) * 100.0f;
  int roundedLoad = (int)(load + 0.5f);

  if (roundedLoad < 0) {
      roundedLoad = 0;
  } else if (roundedLoad > 100) {
      roundedLoad = 100;
  }
  return roundedLoad;
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
    if(reflectionValueFields[a] != v) {
        reflectionValueFields[a] = v;

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

static float lastVoltage = 0;
static int lastEGTTemp = 0;
static int lastCoolantTemp = 0;
static int lastOilTemp = 0;
static bool lastIsEngineRunning = false;

void updateValsForDebug(void) {

  char stamp[24];
  if(isSDLoggerInitialized()) {
    snprintf(stamp, sizeof(stamp), "LN:%d ", getSDLoggerNumber() - 1);
  } else {
    snprintf(stamp, sizeof(stamp), "NL/");
  }

  float volts = rroundf(getGlobalValue(F_VOLTS));
  if(lastVoltage != volts) {
    lastVoltage = volts;
    deb("%sVoltage update: %.1fV", stamp, volts);
  }

  int egt = (int)getGlobalValue(F_EGT);
  if(lastEGTTemp != egt) {
    lastEGTTemp = egt;
    deb("%sEGT update: %dC", stamp, egt);
  }

  int coolant = (int)getGlobalValue(F_COOLANT_TEMP);
  if(lastCoolantTemp != coolant) {
    lastCoolantTemp = coolant;
    deb("%sCoolant temp. update: %dC", stamp, coolant);
  }

  int oil = (int)getGlobalValue(F_OIL_TEMP);
  if(lastOilTemp != oil) {
    lastOilTemp = oil;
    deb("%sOil temp. update: %dC", stamp, oil);
  }

  bool running = RPM_isEngineRunning(getRPMInstance());
  if(lastIsEngineRunning != running) {
    lastIsEngineRunning = running;
    deb("%sEngine is running: %s", stamp, running ? "yes" : "no");
  }
}

void pwm_init(void) {
  pwmVp37  = hal_pwm_freq_create(PIO_VP37_RPM,  VP37_PWM_FREQUENCY_HZ,  PWM_RESOLUTION);
  pwmTurbo = hal_pwm_freq_create(PIO_TURBO,     TURBO_PWM_FREQUENCY_HZ, PWM_RESOLUTION);
  pwmAngle = hal_pwm_freq_create(PIO_VP37_ANGLE, ANGLE_PWM_FREQUENCY_HZ, PWM_RESOLUTION);
}

void valToPWM(unsigned char pin, int val) {
  hal_pwm_freq_channel_t ch = NULL;
  switch(pin) {
    case PIO_TURBO:      ch = pwmTurbo; break;
    case PIO_VP37_RPM:   ch = pwmVp37;  break;
    case PIO_VP37_ANGLE: ch = pwmAngle; break;
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

float getSystemSupplyVoltage(void) {
  float val = hal_ext_adc_read_scaled(ADS1115_PIN_1) / (R2 / (R1 + R2));
  return roundfWithPrecisionTo(val, 1);
}

int getVP37Adjustometer(void) {
  float val = hal_ext_adc_read_scaled(ADS1115_PIN_2);
  return (int)(roundfWithPrecisionTo(val, 3) * 1000);
}

float getVP37FuelTemperature(void) {
  float val = hal_ext_adc_read_scaled(ADS1115_PIN_0);
  val = steinhart(val, R_VP37_FUEL_A, R_VP37_FUEL_B, false);
  return roundfWithPrecisionTo(val, 1);
}
