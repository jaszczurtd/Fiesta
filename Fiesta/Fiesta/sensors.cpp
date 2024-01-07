
#include "sensors.h"

NOINIT volatile float valueFields[F_LAST];
NOINIT volatile float reflectionValueFields[F_LAST];

static int collantTableIdx = 0;
static int collantValuesSet = 0;
static float collantTable[TEMPERATURE_TABLES_SIZE];
static int oilTableIdx = 0;
static int oilValuesSet = 0;
static float oilTable[TEMPERATURE_TABLES_SIZE];

static unsigned char pcf8574State = 0;
static mutex_t pcf8574Mutex;
static mutex_t analog4051Mutex;

static pwmConfig pwmVp37;
static pwmConfig pwmTurbo;
static pwmConfig pwmAngle;

void initI2C(void) {
  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.setClock(I2C_SPEED_HZ);
  Wire.begin();
}

void initSPI(void) {
  SPI.setRX(PIN_MISO); //MISO
  SPI.setTX(PIN_MOSI); //MOSI
  SPI.setSCK(PIN_SCK); //SCK
}

void initSensors(void) {
  analogReadResolution(ADC_BITS);
  pwm_init();

  init4051();
  
  for(int a = 0; a < F_LAST; a++) {
    valueFields[a] = reflectionValueFields[a] = 0.0;
  }

  collantTableIdx = collantValuesSet = 0;
  oilTableIdx = oilValuesSet = 0;

  initGPS();
}

void initBasicPIO(void) {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIO_DPF_LAMP, OUTPUT);
}

//-------------------------------------------------------------------------------------------------
//Read volts
//-------------------------------------------------------------------------------------------------

float readVolts(void) {
  return adcToVolt(analogRead(ADC_VOLT_PIN), V_DIVIDER_R1, V_DIVIDER_R2);
}

//-------------------------------------------------------------------------------------------------
//Read coolant temperature
//-------------------------------------------------------------------------------------------------
float readCoolantTemp(void) {
  float a = 0.0;
  mutex_enter_blocking(&analog4051Mutex);

  set4051ActivePin(HC4051_I_COOLANT_TEMP);
  a = getAverageForTable(&collantTableIdx, &collantValuesSet,
                        ntcToTemp(ADC_SENSORS_PIN, R_TEMP_A, R_TEMP_B), //real values (resitance)
                        collantTable);
  mutex_exit(&analog4051Mutex);  
  return a;
}

//-------------------------------------------------------------------------------------------------
//Read oil temperature
//-------------------------------------------------------------------------------------------------

float readOilTemp(void) {
  float a = 0.0;
  mutex_enter_blocking(&analog4051Mutex);
  set4051ActivePin(HC4051_I_OIL_TEMP);

  a = getAverageForTable(&oilTableIdx, &oilValuesSet,
                         ntcToTemp(ADC_SENSORS_PIN, R_TEMP_A, R_TEMP_B), //real values (resitance)
                         oilTable);
  mutex_exit(&analog4051Mutex);  
  return a;
}

//-------------------------------------------------------------------------------------------------
//Read throttle
//-------------------------------------------------------------------------------------------------

int readThrottle(void) {
  mutex_enter_blocking(&analog4051Mutex);
  set4051ActivePin(HC4051_I_THROTTLE_POS);

  float rawVal = getAverageValueFrom(ADC_SENSORS_PIN);
  mutex_exit(&analog4051Mutex);

  float initialVal = rawVal - THROTTLE_MIN;

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
  int currentVal = int(valueFields[F_THROTTLE_POS]);
  float percent = (currentVal * 100) / PWM_RESOLUTION;
  return percentToGivenVal(percent, 100);
}

//-------------------------------------------------------------------------------------------------
//Read air temperature
//-------------------------------------------------------------------------------------------------

float readAirTemperature(void) {
  float a = 0.0;
  mutex_enter_blocking(&analog4051Mutex);

  set4051ActivePin(HC4051_I_AIR_TEMP);
  a = ntcToTemp(ADC_SENSORS_PIN, R_TEMP_AIR_A, R_TEMP_AIR_B);
  mutex_exit(&analog4051Mutex);
  return a;
}

//-------------------------------------------------------------------------------------------------
//Read bar pressure amount
//-------------------------------------------------------------------------------------------------

float readBarPressure(void) {
  mutex_enter_blocking(&analog4051Mutex);
  set4051ActivePin(HC4051_I_BAR_PRESSURE);

  float val = ((float)analogRead(ADC_SENSORS_PIN) / DIVIDER_PRESSURE_BAR) - 
      1.0; //atmospheric pressure
  mutex_exit(&analog4051Mutex);

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
  mutex_enter_blocking(&analog4051Mutex);

  set4051ActivePin(HC4051_I_EGT);
  a = ((getAverageValueFrom(ADC_SENSORS_PIN)) / DIVIDER_EGT);
  mutex_exit(&analog4051Mutex);
  return a;
}

int readAdjustometer(void) {
  int a = 0;
  mutex_enter_blocking(&analog4051Mutex);

  set4051ActivePin(HC4051_I_ADJUSTOMETER);
  a = getAverageValueFrom(ADC_SENSORS_PIN);
  mutex_exit(&analog4051Mutex);
  return a;
}

//-------------------------------------------------------------------------------------------------

void pcf8574_init(void) {
  mutex_init(&pcf8574Mutex);
  pcf8574State = 0;

  Wire.beginTransmission(PCF8574_ADDR);
  Wire.write(pcf8574State);
  Wire.endTransmission();
}

void pcf8574_write(unsigned char pin, bool value) {
  mutex_enter_blocking(&pcf8574Mutex);
  if(value) {
    bitSet(pcf8574State, pin);
  }  else {
    bitClear(pcf8574State, pin);
  }

  Wire.beginTransmission(PCF8574_ADDR);
  bool success = Wire.write(pcf8574State);
  bool notFound = Wire.endTransmission();

  mutex_exit(&pcf8574Mutex);
  if(!success) {
    derr("error writting byte to pcf8574");
  }

  if(notFound) {
    derr("pcf8574 not found");
  }
}

bool pcf8574_read(unsigned char pin) {
  mutex_enter_blocking(&pcf8574Mutex);
  Wire.beginTransmission(PCF8574_ADDR);
  bool retVal = Wire.read();
  bool notFound = Wire.endTransmission();
  mutex_exit(&pcf8574Mutex);

  if(notFound) {
    derr("pcf8574 not found");
  }
  return retVal;
}

int getRAWThrottle(void) {
  return int(valueFields[F_THROTTLE_POS]);
}

int getEnginePercentageThrottle(void) {
  return percentToGivenVal((float)( ( (valueFields[F_THROTTLE_POS]) * 100) / PWM_RESOLUTION), 100);  
}

static unsigned char lowCurrentValue = 0;
bool readMediumValues(void *argument) {
  switch(lowCurrentValue) {
    case F_COOLANT_TEMP:
      valueFields[F_COOLANT_TEMP] = readCoolantTemp();
      break;
    case F_OIL_TEMP:
      valueFields[F_OIL_TEMP] = readOilTemp();
      break;
    case F_INTAKE_TEMP:
      valueFields[F_INTAKE_TEMP] = readAirTemperature();
      break;
    case F_VOLTS:
      valueFields[F_VOLTS] = readVolts();
      break;
    case F_FUEL:
      valueFields[F_FUEL] = readFuel();
      break;
    case F_EGT:
      valueFields[F_EGT] = readEGT();
      break;
  }
  if(lowCurrentValue++ > F_LAST) {
    lowCurrentValue = 0;
  }

  return true;
}

int getPercentageEngineLoad(void) {

  float map = (valueFields[F_PRESSURE] * 255.0f / 2.55f);
  float load = (map / 255.0f) * (valueFields[F_RPM] / float(RPM_MAX_EVER)) * 100.0f;
  int roundedLoad = (int)(load + 0.5f);

  if (roundedLoad < 0) {
      roundedLoad = 0;
  } else if (roundedLoad > 100) {
      roundedLoad = 100;
  }
  return roundedLoad;
}

bool readHighValues(void *argument) {
  for(int a = 0; a < F_LAST; a++) {
    switch(a) {
      case F_THROTTLE_POS:
        valueFields[a] = readThrottle();
        break;
      case F_PRESSURE:
        valueFields[a] = readBarPressure();
        break;
      case F_OIL_PRESSURE:
        valueFields[a] = readOilBarPressure();
        break;
    }
    if(reflectionValueFields[a] != valueFields[a]) {
        reflectionValueFields[a] = valueFields[a];
    
        triggerDrawHighImportanceValue(true);
    }
    valueFields[F_CAR_SPEED] = getCurrentCarSpeed();
    valueFields[F_CALCULATED_ENGINE_LOAD] = getPercentageEngineLoad();
  }

  return true;
}

void init4051(void) {
  deb("4051 init");

  mutex_init(&analog4051Mutex);

  pinMode(C_4051, OUTPUT);  //C
  pinMode(B_4051, OUTPUT);  //B  
  pinMode(A_4051, OUTPUT);  //A

  set4051ActivePin(0);
}

void set4051ActivePin(unsigned char pin) {
  digitalWrite(A_4051, (pin & 0x01) > 0); 
  digitalWrite(B_4051, (pin & 0x02) > 0); 
  digitalWrite(C_4051, (pin & 0x04) > 0); 
}

bool isDPFRegenerating(void) {
  return valueFields[F_DPF_REGEN] > 0;
}

static float lastVoltage = 0;
static int lastEGTTemp = 0;
static int lastCoolantTemp = 0;
static int lastOilTemp = 0;
static bool lastIsEngineRunning = false;

bool updateValsForDebug(void *arg) {

  String message = "";
  String stamp = "";

  if(isSDLoggerInitialized()) {
    stamp += "LN:" + String(getSDLoggerNumber() - 1) + " ";
  } else {
    stamp += "NL/";
  }

  float volts = rroundf(valueFields[F_VOLTS]); 
  if(lastVoltage != volts) {
    lastVoltage = volts;
    message += stamp + "Voltage update: " + String(volts, 1) + "V\n";
  }

  int egt = int(valueFields[F_EGT]);
  if(lastEGTTemp != egt) {
    lastEGTTemp = egt;
    message += stamp + "EGT update: " + String(egt) + "C\n";
  }

  int coolant = int(valueFields[F_COOLANT_TEMP]);
  if(lastCoolantTemp != coolant) {
    lastCoolantTemp = coolant;
    message += stamp + "Coolant temp. update: " + String(coolant) + "C\n";
  }

  int oil = int(valueFields[F_OIL_TEMP]);
  if(lastOilTemp != oil) {
    lastOilTemp = oil;
    message += stamp + "Oil temp. update: " + String(oil) + "C\n";
  }

  if(lastIsEngineRunning != isEngineRunning()) {
    lastIsEngineRunning= isEngineRunning();
    message += stamp + "Engine is running: " + (isEngineRunning() ? "yes" : "no") + "\n";
  }

  if(message.length() > 0) {
    if (message.endsWith("\n")) {
      message.remove(message.length() - 1);
    }    
    deb(message.c_str());
  }
  
  return true;
}

void pwm_configure_channel(pwmConfig *cfg) {

  cfg->analogScale = PWM_RESOLUTION;
  cfg->analogWritePseudoScale = 1;
  while (((clock_get_hz(clk_sys) / ((float)cfg->analogScale * cfg->analogFreq)) > 255.0) && (cfg->analogScale < 32678)) {
      cfg->analogWritePseudoScale++;
      cfg->analogScale *= 2;
  }
  cfg->analogWriteSlowScale = 1;
  while (((clock_get_hz(clk_sys) / ((float)cfg->analogScale * cfg->analogFreq)) < 1.0) && (cfg->analogScale >= 6)) {
      cfg->analogWriteSlowScale++;
      cfg->analogScale /= 2;
  }

  if (!(cfg->pwmInitted & (1 << pwm_gpio_to_slice_num(cfg->pin)))) {
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv(&c, clock_get_hz(clk_sys) / ((float)cfg->analogScale * cfg->analogFreq));
    pwm_config_set_wrap(&c, cfg->analogScale - 1);
    pwm_init(pwm_gpio_to_slice_num(cfg->pin), &c, true);
    cfg->pwmInitted |= 1 << pwm_gpio_to_slice_num(cfg->pin);
  }
}

void pwm_init(void) {

  pwmVp37.pin = PIO_VP37_RPM;
  pwmVp37.analogFreq = VP37_PWM_FREQUENCY_HZ;
  pwm_configure_channel(&pwmVp37);

  pwmTurbo.pin = PIO_TURBO;
  pwmTurbo.analogFreq = TURBO_PWM_FREQUENCY_HZ;
  pwm_configure_channel(&pwmTurbo);

  pwmAngle.pin = PIO_VP37_ANGLE;
  pwmAngle.analogFreq = ANGLE_PWM_FREQUENCY_HZ;
  pwm_configure_channel(&pwmAngle);
}

void pwm_write(pwmConfig *cfg, int val) {

  val <<= cfg->analogWritePseudoScale;
  val >>= cfg->analogWriteSlowScale;

  if (val < 0) {
      val = 0;
  } else if ((uint32_t)val > cfg->analogScale) {
      val = cfg->analogScale;
  }

  gpio_set_function(cfg->pin, GPIO_FUNC_PWM);
  pwm_set_gpio_level(cfg->pin, val);
}

void valToPWM(unsigned char pin, int val) {

  pwmConfig *cfg = NULL;
  switch(pin) {
    case PIO_TURBO:
      cfg = &pwmTurbo;
      break;
    case PIO_VP37_RPM:
      cfg = &pwmVp37;
      break;
    case PIO_VP37_ANGLE:
      cfg = &pwmAngle;
      break;
    default:
      break;
  }
  if(cfg != NULL) {
    pwm_write(cfg, (PWM_RESOLUTION - val));
  } else {
    derr("config for this pwm is not initialized!");
  }

}
