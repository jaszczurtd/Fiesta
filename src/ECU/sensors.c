
#include "sensors.h"
#include "../common/fiesta_sensor_helpers.h"
#include "../common/fiesta_unit_testing.h"
#include "can.h"
#include "engineFuel.h"
#include "gps.h"
#include "rpm.h"

#include <utils/tools_common_defs.h>

#include <hal/analog/hal_adc_scan.h>
#include <math.h>

// Timing derived from the ADC scan; defined with the multiplexer helpers below.
TESTABLE_STATIC uint32_t sensors_muxSettleUs(void);
TESTABLE_STATIC uint16_t sensors_adcSampleDelayUs(void);

typedef struct {
  volatile float valueFields[F_LAST];
} sensors_persistent_state_t;

typedef struct {
  size_t collantTableIdx;
  size_t collantValuesSet;
  float collantTable[HAL_MATH_ROLLING_AVERAGE_DEFAULT_SIZE];
  size_t oilTableIdx;
  size_t oilValuesSet;
  float oilTable[HAL_MATH_ROLLING_AVERAGE_DEFAULT_SIZE];
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
  float driverDemandPercent;
  float driverFilteredPercent;
  uint32_t driverDemandUpdatedMs;
  bool driverDemandReady;
} sensors_runtime_state_t;

NOINIT static sensors_persistent_state_t s_sensorsPersistent;
static sensors_runtime_state_t s_sensorsState = {.collantTableIdx = 0,
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
                                                 .lastIsEngineRunning = false};

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
  if ((idx < 0) || (idx >= F_LAST)) {
    derr_limited("sensors", "%s invalid value index: %d (valid: 0..%d)", caller,
                 idx, (F_LAST - 1));
    return false;
  }
  return true;
}

/**
 * @brief Read one analog sensor routed through the HC4051 mux with averaging.
 * @param muxChannel HC4051 channel to select.
 * @param tableIdx Pointer to the averaging ring-buffer write index.
 * @param tableValuesSet Pointer to the averaging "samples seen" counter.
 * @param table Averaging ring buffer storage.
 * @return Latest averaged NTC temperature in degrees Celsius.
 */
static float sensors_readNtcViaMux(uint8_t muxChannel, size_t *tableIdx,
                                   size_t *tableValuesSet, float *table) {
  float temperature = 0.0f;
  float average = 0.0f;
  m_mutex_enter_blocking(analog4051Mutex);
  set4051ActivePin(muxChannel);
  const hal_status_t temperature_status = fiesta_ntc_read_temperature_spaced_ex(
      ADC_SENSORS_PIN, sensors_adcSampleDelayUs(), R_TEMP_A, R_TEMP_B,
      &temperature);
  const hal_status_t average_status =
      temperature_status == HAL_OK
          ? hal_math_rolling_average_f32_ex(
                tableIdx, tableValuesSet, temperature, table,
                HAL_MATH_ROLLING_AVERAGE_DEFAULT_SIZE, &average)
          : temperature_status;
  m_mutex_exit(analog4051Mutex);
  return average_status == HAL_OK ? average : 0.0f;
}

// I2C bus recovery: toggle SCL up to 9 times on GPIO level to release
// a slave that is holding SDA low (e.g. after master reset mid-transaction).
// Must be called BEFORE Wire.begin() / hal_i2c_init().

/**
 * @brief Initialize the I2C bus and its mutex-protected access path.
 */
void initI2C(void) {
  static bool i2cMutexInited = false;
  if (!i2cMutexInited) {
    m_mutex_init(i2cBusMutex);
    i2cMutexInited = true;
  }
  hal_i2c_bus_clear(PIN_SDA, PIN_SCL);
  hal_i2c_init(PIN_SDA, PIN_SCL, HAL_I2C_CLOCK_FAST_HZ);
}

void initSPI(void) {
  hal_spi_init(0, PIN_MISO, PIN_MOSI, PIN_SCK);

  // Deassert all SPI chip-selects immediately so that no MCP2515
  // floats its /CS low during another chip's SPI transactions.
  const uint8_t spiCsPins[] = {CAN0_GPIO, CAN1_GPIO};
  for (uint32_t i = 0; i < COUNTOF(spiCsPins); i++) {
    hal_gpio_set_mode(spiCsPins[i], HAL_GPIO_OUTPUT);
    hal_gpio_write(spiCsPins[i], true);
  }
}

void setGlobalValue(int idx, float val) {
  if (!sensors_isGlobalValueIndexValid(idx, "setGlobalValue")) {
    return;
  }
  m_mutex_enter_blocking(valueFieldsMutex);
  s_sensorsPersistent.valueFields[idx] = val;
  m_mutex_exit(valueFieldsMutex);
}

float getGlobalValue(int idx) {
  if (!sensors_isGlobalValueIndexValid(idx, "getGlobalValue")) {
    return 0.0f;
  }
  m_mutex_enter_blocking(valueFieldsMutex);
  float v = s_sensorsPersistent.valueFields[idx];
  m_mutex_exit(valueFieldsMutex);
  return v;
}

void initSensors(void) {
  // Firmware lifetime is process-long; in host tests setUp() may call this
  // repeatedly, so keep mutex init idempotent to avoid re-allocation churn.
  if (valueFieldsMutex == NULL) {
    m_mutex_init(valueFieldsMutex);
  }
  hal_adc_set_resolution(HAL_ADC_UTIL_DEFAULT_BITS);
  pwm_init();

  init4051();

  m_mutex_enter_blocking(valueFieldsMutex);
  for (size_t a = 0; a < F_LAST; a++) {
    s_sensorsPersistent.valueFields[a] = 0.0;
  }
  s_sensorsState.driverDemandPercent = 0.0f;
  s_sensorsState.driverFilteredPercent = 0.0f;
  s_sensorsState.driverDemandUpdatedMs = 0U;
  s_sensorsState.driverDemandReady = false;
  m_mutex_exit(valueFieldsMutex);

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
// Read coolant temperature
//-------------------------------------------------------------------------------------------------
float readCoolantTemp(void) {
  return sensors_readNtcViaMux(
      HC4051_I_COOLANT_TEMP, &s_sensorsState.collantTableIdx,
      &s_sensorsState.collantValuesSet, s_sensorsState.collantTable);
}

//-------------------------------------------------------------------------------------------------
// Read oil temperature
//-------------------------------------------------------------------------------------------------

float readOilTemp(void) {
  return sensors_readNtcViaMux(HC4051_I_OIL_TEMP, &s_sensorsState.oilTableIdx,
                               &s_sensorsState.oilValuesSet,
                               s_sensorsState.oilTable);
}

//-------------------------------------------------------------------------------------------------
// Read throttle
//-------------------------------------------------------------------------------------------------

/**
 * @brief Map a raw legacy throttle ADC reading into the internal driver-demand
 * scale.
 * @param rawVal Raw ADC value measured on the throttle input.
 * @return Driver-demand signal in the internal PWM-scale range.
 * @note The input is currently named "throttle" in code, but architecturally it
 * is the G79/G185-like driver-demand path.
 */
TESTABLE_STATIC int32_t sensors_computeThrottlePositionFromRaw(int32_t rawVal) {
  if (rawVal <= THROTTLE_MIN) {
    return PWM_RESOLUTION;
  }
  if (rawVal >= THROTTLE_MAX) {
    return 0;
  }

  int32_t range = (THROTTLE_MAX - THROTTLE_MIN);
  int32_t shifted = rawVal - THROTTLE_MIN;

  // Round to the nearest step in PWM domain, then invert the axis
  // so the historical "higher ADC -> lower demand" behavior is preserved.
  int32_t scaled = (shifted * PWM_RESOLUTION + (range / 2)) / range;
  int32_t inverted = PWM_RESOLUTION - scaled;

  if (inverted < 0) {
    return 0;
  }
  if (inverted > PWM_RESOLUTION) {
    return PWM_RESOLUTION;
  }
  return inverted;
}

#if SENSORS_THROTTLE_DIAG
/** @brief One second of demand reads: where the zero end sits and how much it
 * wanders. */
static struct {
  uint32_t windowStartMs;
  uint32_t count;
  int32_t minRaw;
  int32_t maxRaw;
  int32_t sumRaw;
  uint32_t eventsThisSecond;
} s_throttleDiag = {0U, 0U, INT32_MAX, INT32_MIN, 0, 0U};

static struct {
  uint32_t reads;
  uint32_t hits;
  uint32_t lastSurveyMs;
} s_throttleStress = {0U, 0U, 0U};

/** @brief Survey every mux input once, so a stray value can be named. */
static void sensors_throttleDiagSurvey(void) {
  int values[8];
  m_mutex_enter_blocking(analog4051Mutex);
  for (unsigned char channel = 0U; channel < 8U; channel++) {
    set4051ActivePin(channel);
    (void)hal_adc_read(ADC_SENSORS_PIN);
    hal_delay_us(10U);
    values[channel] = hal_adc_read(ADC_SENSORS_PIN);
  }
  set4051ActivePin(HC4051_I_THROTTLE_POS);
  m_mutex_exit(analog4051Mutex);
  deb("THRMUX ch0:%d ch1:%d ch2:%d ch3:%d ch4:%d ch5:%d ch6:%d ch7:%d",
      values[0], values[1], values[2], values[3], values[4], values[5],
      values[6], values[7]);
}

/**
 * @brief Demand read with every sample and its frame-mates recorded.
 * @return Driver demand in the PWM domain, as the production read gives it.
 * @note Mirrors sensors_readMuxAverage() step for step: one discarded read,
 * four kept one sample spacing apart, RP2040 transfer-gap compensation,
 * so the demand is the one the production path would have produced. The
 * shunt and supply samples come from the same scan frame right after each
 * sensors sample, so a rotated frame shows as the neighbours carrying each
 * other's values, while a stale frame shows all four samples agreeing on a
 * value that belongs to another channel.
 */
static int32_t sensors_readThrottleDiag(void) {
  int sample[5];
  uint16_t shunt[5];
  uint16_t supply[5];
  m_mutex_enter_blocking(analog4051Mutex);
  const uint32_t switchedUs = hal_micros();
  set4051ActivePin(HC4051_I_THROTTLE_POS);
  const uint32_t settledUs = hal_micros();
  for (size_t i = 0U; i < 5U; i++) {
    sample[i] = hal_adc_read(ADC_SENSORS_PIN);
    shunt[i] = 0U;
    supply[i] = 0U;
    (void)hal_adc_scan_latest(ADC_VP37_CURRENT_PIN, &shunt[i]);
    (void)hal_adc_scan_latest(ADC_VOLT_PIN, &supply[i]);
    if (i > 0U) {
      hal_delay_us(sensors_adcSampleDelayUs());
    }
  }
  // Stress the newest-sample path while the mux is fresh on this channel.
  const int reference = sample[4];
  uint32_t hits = 0U;
  uint32_t firstHit = 0U;
  int firstValue = 0;
  for (uint32_t i = 0U; i < SENSORS_THROTTLE_DIAG_STRESS_READS; i++) {
    const int value = hal_adc_read(ADC_SENSORS_PIN);
    const int delta = value - reference;
    if ((delta > SENSORS_THROTTLE_DIAG_STRESS_BAND) ||
        (delta < -SENSORS_THROTTLE_DIAG_STRESS_BAND)) {
      if (hits == 0U) {
        firstHit = i;
        firstValue = value;
      }
      hits++;
    }
  }
  const uint32_t stressEndUs = hal_micros();
  const bool muxA = hal_gpio_read(A_4051);
  const bool muxB = hal_gpio_read(B_4051);
  const bool muxC = hal_gpio_read(C_4051);
  m_mutex_exit(analog4051Mutex);
  s_throttleStress.reads += SENSORS_THROTTLE_DIAG_STRESS_READS;
  s_throttleStress.hits += hits;
  if ((hits > 0U) &&
      (s_throttleDiag.eventsThisSecond < SENSORS_THROTTLE_DIAG_EVENTS_PER_S)) {
    s_throttleDiag.eventsThisSecond++;
    deb("THRSTRESS us:%lu hits:%lu first:%lu value:%d ref:%d span:%luus "
        "mux:%u%u%u",
        (unsigned long)stressEndUs, (unsigned long)hits,
        (unsigned long)firstHit, firstValue, reference,
        (unsigned long)(stressEndUs - settledUs), muxC ? 1U : 0U,
        muxB ? 1U : 0U, muxA ? 1U : 0U);
  }

  float sum = 0.0f;
  for (size_t i = 1U; i < 5U; i++) {
    sum += (float)hal_adc_compensate_rp2040_12bit(sample[i]);
  }
  const int32_t rawVal = (int32_t)(sum / 4.0f);
  const int32_t demand = sensors_computeThrottlePositionFromRaw(rawVal);

  const uint32_t nowMs = hal_millis();
  if (s_throttleDiag.count == 0U) {
    s_throttleDiag.windowStartMs = nowMs;
  }
  s_throttleDiag.count++;
  s_throttleDiag.sumRaw += rawVal;
  if (rawVal < s_throttleDiag.minRaw) {
    s_throttleDiag.minRaw = rawVal;
  }
  if (rawVal > s_throttleDiag.maxRaw) {
    s_throttleDiag.maxRaw = rawVal;
  }

  if (((demand > 0) ||
       (rawVal < (THROTTLE_MAX + SENSORS_THROTTLE_DIAG_EDGE))) &&
      (s_throttleDiag.eventsThisSecond < SENSORS_THROTTLE_DIAG_EVENTS_PER_S)) {
    s_throttleDiag.eventsThisSecond++;
    deb("THRDIAG us:%lu raw:%ld dem:%ld s:%d/%d/%d/%d/%d sh:%u/%u/%u/%u/%u "
        "vs:%u/%u/%u/%u/%u settle:%lu",
        (unsigned long)settledUs, (long)rawVal, (long)demand, sample[0],
        sample[1], sample[2], sample[3], sample[4], (unsigned)shunt[0],
        (unsigned)shunt[1], (unsigned)shunt[2], (unsigned)shunt[3],
        (unsigned)shunt[4], (unsigned)supply[0], (unsigned)supply[1],
        (unsigned)supply[2], (unsigned)supply[3], (unsigned)supply[4],
        (unsigned long)(settledUs - switchedUs));
  }
  if (hal_millis_deadline_expired(s_throttleDiag.windowStartMs, 1000U)) {
    deb("THRSTAT n:%lu min:%ld max:%ld mean:%ld edge:%d fr:%lu stress:%lu "
        "hits:%lu",
        (unsigned long)s_throttleDiag.count, (long)s_throttleDiag.minRaw,
        (long)s_throttleDiag.maxRaw,
        (long)(s_throttleDiag.sumRaw / (int32_t)s_throttleDiag.count),
        THROTTLE_MAX, (unsigned long)hal_adc_scan_frame_period_ns(),
        (unsigned long)s_throttleStress.reads,
        (unsigned long)s_throttleStress.hits);
    if (hal_millis_deadline_expired(s_throttleStress.lastSurveyMs,
                                    SENSORS_THROTTLE_DIAG_SURVEY_MS)) {
      s_throttleStress.lastSurveyMs = nowMs;
      sensors_throttleDiagSurvey();
    }
    s_throttleDiag.count = 0U;
    s_throttleDiag.sumRaw = 0;
    s_throttleDiag.minRaw = INT32_MAX;
    s_throttleDiag.maxRaw = INT32_MIN;
    s_throttleDiag.eventsThisSecond = 0U;
  }
  return demand;
}
#endif /* SENSORS_THROTTLE_DIAG */

/** Settling wait after a multiplexer channel change, in microseconds:
 * SENSORS_MUX_ANALOG_SETTLE_US plus two scan frames while the scan runs, the
 * analog part alone when reads convert live. */
TESTABLE_STATIC uint32_t sensors_muxSettleUs(void) {
  uint32_t settle = SENSORS_MUX_ANALOG_SETTLE_US;
  if (hal_adc_scan_is_running()) {
    const uint32_t frameUs = (hal_adc_scan_frame_period_ns() + 999U) / 1000U;
    settle += 2U * frameUs;
  }
  return settle;
}

bool sensors_scanCoversInputs(void) {
  static const uint8_t scannedInputs[] = {ADC_SENSORS_PIN, ADC_VOLT_PIN};
  bool covered = true;
  if (hal_adc_scan_is_running()) {
    for (size_t i = 0U; i < (sizeof(scannedInputs) / sizeof(scannedInputs[0]));
         i++) {
      if (hal_adc_scan_pin_position(scannedInputs[i]) == UINT8_MAX) {
        covered = false;
      }
    }
  }
  return covered;
}

/** Spacing between averaged ADC samples, in microseconds: one scan frame
 * while the scan runs, so four samples come from four frames; ten
 * microseconds for a polled converter. */
TESTABLE_STATIC uint16_t sensors_adcSampleDelayUs(void) {
  return fiesta_adc_sample_spacing_us();
}

hal_status_t sensors_readMuxAverage(unsigned char channel, float *outAverage) {
  hal_status_t status = HAL_EINVAL;
  if (outAverage != NULL) {
    m_mutex_enter_blocking(analog4051Mutex);
    set4051ActivePin(channel);
    status = fiesta_adc_read_average_spaced_ex(
        ADC_SENSORS_PIN, sensors_adcSampleDelayUs(), outAverage);
    m_mutex_exit(analog4051Mutex);
    if (status != HAL_OK) {
      derr_limited("mux read", "Analog input %u unreadable: %s",
                   (unsigned)channel, hal_status_to_string(status));
    }
  }
  return status;
}

int32_t readThrottle(void) {
#if SENSORS_THROTTLE_DIAG
  return sensors_readThrottleDiag();
#else
  // A demand that cannot be read is no demand: the inverted mapping would
  // otherwise turn a missing reading into full throttle.
  float average = 0.0f;
  int32_t demand = 0;
  if (sensors_readMuxAverage(HC4051_I_THROTTLE_POS, &average) == HAL_OK) {
    demand = sensors_computeThrottlePositionFromRaw((int32_t)average);
  }
  return demand;
#endif
}

/**
 * @brief Convert the stored legacy throttle value into a 0..100 driver-demand
 * percentage.
 * @return Driver-demand percentage derived from the G79/G185-like input path.
 */
int32_t getThrottlePercentage(void) {
  int32_t currentVal = (int32_t)(getGlobalValue(F_THROTTLE_POS));
  float percent = (currentVal * 100) / PWM_RESOLUTION;
  return hal_math_percent_to_value(percent, 100);
}

float getDriverDemandPercent(void) {
  m_mutex_enter_blocking(valueFieldsMutex);
  const float demand = s_sensorsState.driverDemandPercent;
  m_mutex_exit(valueFieldsMutex);
  return demand;
}

//-------------------------------------------------------------------------------------------------
// Read air temperature
//-------------------------------------------------------------------------------------------------

/**
 * @brief Read intake air temperature from the G72-like sensor path.
 * @return Intake air temperature in degrees Celsius.
 */
float readAirTemperature(void) {
  float a = 0.0;
  m_mutex_enter_blocking(analog4051Mutex);

  set4051ActivePin(HC4051_I_AIR_TEMP);
  if (fiesta_ntc_read_temperature_spaced_ex(
          ADC_SENSORS_PIN, sensors_adcSampleDelayUs(), R_TEMP_AIR_A,
          R_TEMP_AIR_B, &a) != HAL_OK) {
    a = 0.0f;
  }
  m_mutex_exit(analog4051Mutex);
  return a;
}

//-------------------------------------------------------------------------------------------------
// Read bar pressure amount
//-------------------------------------------------------------------------------------------------

/**
 * @brief Read boost / manifold pressure from the G71-like sensor path.
 * @return Pressure in bar relative to atmosphere.
 */
float readBarPressure(void) {
  float average = 0.0f;
  (void)sensors_readMuxAverage(HC4051_I_BAR_PRESSURE, &average);
  float val = (average / DIVIDER_PRESSURE_BAR) - 1.0f; // atmospheric pressure

  if (val < 0.0) {
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
  switch (code) {
  case 1:
    return "data too long";
  case 2:
    return "NACK on address";
  case 3:
    return "NACK on data";
  case 4:
    return "other error";
  case 5:
    return "timeout";
  default:
    return "unknown";
  }
}

bool pcf8574_init(void) {
  s_sensorsState.pcf8574State = 0;

  // i2cBusMutex orders every ECU transaction on the bus, the Adjustometer
  // transfer of the other core included.
  m_mutex_enter_blocking(i2cBusMutex);
  bool success = false;
  uint8_t notFound =
      hal_i2c_write_byte(PCF8574_ADDR, s_sensorsState.pcf8574State, &success);
  m_mutex_exit(i2cBusMutex);

  if (!success || notFound) {
    derr("pcf8574_init: %s (endTx=%u)",
         notFound ? i2cEndTransmissionError(notFound) : "write failed",
         (unsigned)notFound);
  }

  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, (!success || notFound));
  return (success && !notFound);
}

void pcf8574_write(unsigned char pin, bool value) {
  if (pin > 7) {
    derr("pcf8574_write invalid pin: %u", (unsigned)pin);
    return;
  }

  // Mutate the shadow latch under the bus mutex so concurrent
  // pcf8574_write() calls on two different pins cannot race on the
  // read-modify-write sequence (lost bit update). Also keeps the
  // I2C byte we push in sync with what we just stored locally.
  m_mutex_enter_blocking(i2cBusMutex);
  if (value) {
    bitSet(s_sensorsState.pcf8574State, pin);
  } else {
    bitClear(s_sensorsState.pcf8574State, pin);
  }
  bool success = false;
  uint8_t notFound =
      hal_i2c_write_byte(PCF8574_ADDR, s_sensorsState.pcf8574State, &success);
  m_mutex_exit(i2cBusMutex);

  if (!success || notFound) {
    derr("pcf8574_write: %s (endTx=%u)",
         notFound ? i2cEndTransmissionError(notFound) : "write failed",
         (unsigned)notFound);
  }

  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, (!success || notFound));
}

bool pcf8574_read(unsigned char pin) {
  if (pin > 7) {
    derr("pcf8574_read invalid pin: %u", (unsigned)pin);
    return false;
  }

  m_mutex_enter_blocking(i2cBusMutex);
  bool readOk = false;
  uint8_t raw = hal_i2c_read_byte(PCF8574_ADDR, &readOk);
  if (readOk) {
    // Commit the refreshed shadow latch while still holding the bus mutex,
    // so pcf8574_write() cannot race on pcf8574State between the read and
    // the store.
    s_sensorsState.pcf8574State = raw;
  }
  m_mutex_exit(i2cBusMutex);

  if (!readOk) {
    derr("pcf8574_read: I2C read failed");
    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);
    return false;
  }

  dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, false);
  return (raw & (uint8_t)(1u << pin)) != 0;
}

/**
 * @brief Read the legacy throttle-named value as stored in the global value
 * table.
 * @return Current raw driver-demand value.
 * @note This is still the G79/G185-like pedal-demand signal, despite the legacy
 * name.
 */
int32_t getRAWThrottle(void) {
  return (int32_t)(getGlobalValue(F_THROTTLE_POS));
}

void readMediumValues(void) {
  switch (s_sensorsState.lowCurrentValue) {
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
  if (s_sensorsState.lowCurrentValue++ >= F_LAST) {
    s_sensorsState.lowCurrentValue = 0;
  }
}

/**
 * @brief Calculate normalized engine load from pressure and RPM inputs.
 * @param pressureBar Pressure input in bar.
 * @param rpm Engine speed in RPM.
 * @return Engine load percentage clamped to the 0..100 range.
 * @note This helper produces a project-local supervisory load estimate, not an
 * OEM EDC15 quantity or air-mass model.
 */
TESTABLE_STATIC int32_t sensors_calculateEngineLoadFromValues(float pressureBar,
                                                              float rpm) {
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
  return sensors_calculateEngineLoadFromValues(getGlobalValue(F_PRESSURE),
                                               getGlobalValue(F_RPM));
}

void readThrottleValues(void) {
  const float raw = (float)readThrottle();
  const float demand = (hal_constrain(raw, 0.0f, (float)PWM_RESOLUTION) /
                        (float)PWM_RESOLUTION) *
                       100.0f;
  const uint32_t nowMs = hal_millis();
  m_mutex_enter_blocking(valueFieldsMutex);
  s_sensorsPersistent.valueFields[F_THROTTLE_POS] = raw;
  if (!s_sensorsState.driverDemandReady || (demand == 0.0f)) {
    s_sensorsState.driverDemandPercent = demand;
    s_sensorsState.driverFilteredPercent = demand;
    s_sensorsState.driverDemandReady = true;
  } else {
    const uint32_t elapsedMs = nowMs - s_sensorsState.driverDemandUpdatedMs;
    const float dt = (float)elapsedMs * 0.001f;
    const float alpha = dt / (SENSORS_DRIVER_FILTER_S + dt);
    s_sensorsState.driverFilteredPercent =
        hal_math_low_pass(alpha, demand, s_sensorsState.driverFilteredPercent);
    if ((demand == 100.0f) &&
        ((demand - s_sensorsState.driverFilteredPercent) <=
         SENSORS_DRIVER_DEADBAND_PERCENT)) {
      s_sensorsState.driverFilteredPercent = demand;
    }
    if ((fabsf(s_sensorsState.driverFilteredPercent -
               s_sensorsState.driverDemandPercent) >=
         SENSORS_DRIVER_DEADBAND_PERCENT) ||
        (s_sensorsState.driverFilteredPercent == 100.0f)) {
      s_sensorsState.driverDemandPercent = s_sensorsState.driverFilteredPercent;
    }
  }
  s_sensorsState.driverDemandUpdatedMs = nowMs;
  m_mutex_exit(valueFieldsMutex);
}

void readHighValues(void) {
  setGlobalValue(F_RPM, RPM_getCurrentRPM(getRPMInstance()));
  setGlobalValue(F_PRESSURE, readBarPressure());
  setGlobalValue(F_GPS_CAR_SPEED, getCurrentCarSpeed());
  setGlobalValue(F_CALCULATED_ENGINE_LOAD, (float)getPercentageEngineLoad());

  // Transmit throttle/turbo each high-rate tick to avoid stale cluster values
  // after an occasional dropped frame on the bus.
  CAN_sendThrottleUpdate();
  CAN_sendTurboUpdate();
}

void init4051(void) {
  deb("4051 init");

  if (analog4051Mutex == NULL) {
    m_mutex_init(analog4051Mutex);
  }

  hal_gpio_set_mode(C_4051, HAL_GPIO_OUTPUT);
  hal_gpio_set_mode(B_4051, HAL_GPIO_OUTPUT);
  hal_gpio_set_mode(A_4051, HAL_GPIO_OUTPUT);

  set4051ActivePin(0);
}

void set4051ActivePin(unsigned char pin) {
  hal_gpio_write(A_4051, (pin & 0x01) > 0);
  hal_gpio_write(B_4051, (pin & 0x02) > 0);
  hal_gpio_write(C_4051, (pin & 0x04) > 0);
  // Under the hardware-paced scan the readers see the newest scanned sample,
  // which must already belong to the new channel; polled reads convert live.
  if (hal_adc_scan_is_running()) {
    hal_delay_us(sensors_muxSettleUs());
  }
}

bool isDPFRegenerating(void) { return getGlobalValue(F_DPF_REGEN) > 0; }

void updateValsForDebug(void) {

  float volts = hal_math_round_tenth(getGlobalValue(F_VOLTS));
  if (s_sensorsState.lastVoltage != volts) {
    s_sensorsState.lastVoltage = volts;
    deb("Voltage update: %.1fV", volts);
  }

  int32_t egt = (int32_t)getGlobalValue(F_EGT);
  if (s_sensorsState.lastEGTTemp != egt) {
    s_sensorsState.lastEGTTemp = egt;
    deb("EGT update: %dC", egt);
  }

  int32_t coolant = (int32_t)getGlobalValue(F_COOLANT_TEMP);
  if (s_sensorsState.lastCoolantTemp != coolant) {
    s_sensorsState.lastCoolantTemp = coolant;
    deb("Coolant temp. update: %dC", coolant);
  }

  int32_t oil = (int32_t)getGlobalValue(F_OIL_TEMP);
  if (s_sensorsState.lastOilTemp != oil) {
    s_sensorsState.lastOilTemp = oil;
    deb("Oil temp. update: %dC", oil);
  }

  bool running = RPM_isEngineRunning(getRPMInstance());
  if (s_sensorsState.lastIsEngineRunning != running) {
    s_sensorsState.lastIsEngineRunning = running;
    deb("Engine is running: %s", running ? "yes" : "no");
  }
}

void pwm_init(void) {
  // Reinitialization replaces channels; do not abandon their pool slots.
  if (s_sensorsState.pwmVp37 != NULL) {
    hal_pwm_freq_destroy(s_sensorsState.pwmVp37);
  }
  if (s_sensorsState.pwmTurbo != NULL) {
    hal_pwm_freq_destroy(s_sensorsState.pwmTurbo);
  }
  if (s_sensorsState.pwmAngle != NULL) {
    hal_pwm_freq_destroy(s_sensorsState.pwmAngle);
  }
  s_sensorsState.pwmVp37 =
      hal_pwm_freq_create(PIO_VP37_RPM, VP37_PWM_FREQUENCY_HZ, PWM_RESOLUTION);
  s_sensorsState.pwmTurbo =
      hal_pwm_freq_create(PIO_TURBO, TURBO_PWM_FREQUENCY_HZ, PWM_RESOLUTION);
  s_sensorsState.pwmAngle = hal_pwm_freq_create(
      PIO_VP37_ANGLE, ANGLE_PWM_FREQUENCY_HZ, PWM_RESOLUTION);
}

static hal_pwm_freq_channel_t sensors_pwmChannel(unsigned char pin) {
  hal_pwm_freq_channel_t ch = NULL;
  switch (pin) {
  case PIO_TURBO:
    ch = s_sensorsState.pwmTurbo;
    break;
  case PIO_VP37_RPM:
    ch = s_sensorsState.pwmVp37;
    break;
  case PIO_VP37_ANGLE:
    ch = s_sensorsState.pwmAngle;
    break;
  default:
    break;
  }
  return ch;
}

bool pwmChannelReady(unsigned char pin) {
  return sensors_pwmChannel(pin) != NULL;
}

bool pwmWrite(unsigned char pin, int32_t val) {
  const hal_pwm_freq_channel_t ch = sensors_pwmChannel(pin);
  if (ch != NULL) {
    hal_pwm_freq_write(ch, (PWM_RESOLUTION - val));
  }
  return ch != NULL;
}

void valToPWM(unsigned char pin, int32_t val) {
  if (pwmWrite(pin, val)) {
    dtcManagerSetActive(DTC_PWM_CHANNEL_NOT_INIT, false);
  } else {
    derr("config for this pwm is not initialized!");
    dtcManagerSetActive(DTC_PWM_CHANNEL_NOT_INIT, true);
  }
}

// ── Adjustometer bus transfer ───────────────────────────────────────────────

hal_status_t i2cReadRegisters(uint8_t address, uint8_t reg, uint8_t *data,
                              size_t len) {
  m_mutex_enter_blocking(i2cBusMutex);
  const hal_status_t status =
      hal_i2c_write_read_bus_ex(0, address, &reg, 1U, data, len);
  m_mutex_exit(i2cBusMutex);
  return status;
}

/**
 * @brief Read ECU supply voltage from the local ADC divider path.
 * @return Supply voltage in volts, clamped to 0 on invalid conversion.
 */
float getSystemSupplyVoltage(void) {
  return fiesta_adc_read_divided_volts(ADC_VOLT_PIN, (float)V_DIVIDER_R1,
                                       (float)V_DIVIDER_R2);
}
