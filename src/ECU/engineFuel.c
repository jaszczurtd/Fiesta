#include "engineFuel.h"

//-------------------------------------------------------------------------------------------------
//Read fuel amount
//-------------------------------------------------------------------------------------------------

typedef struct {
  int measuredValues[FUEL_MAX_SAMPLES];
  int measuedValuesIndex;
  int lastResult;
  int nextMeasurement;
  int fuelMeasurementTime;
  long measurements;
} engine_fuel_state_t;

static engine_fuel_state_t s_engineFuel = {
  .measuredValues = {0},
  .measuedValuesIndex = 0,
  .lastResult = FUEL_INIT_VALUE,
  .nextMeasurement = 0,
  .fuelMeasurementTime = 0,
  .measurements = 0
};

float readFuel(void) {
  set4051ActivePin(HC4051_I_FUEL_LEVEL);

  int result = getAverageValueFrom(ADC_SENSORS_PIN);
  int r = result;

  result -= FUEL_MAX;
  result = abs(result - (FUEL_MIN - FUEL_MAX));

  #ifdef DEBUG
  deb("tank raw value: %d result: %d", r, result);
  #endif

  #ifdef JUST_RAW_FUEL_VAL
  deb("tank raw:%d (%d)", r, result);
  s_engineFuel.lastResult = result;
  #else

  s_engineFuel.measuredValues[s_engineFuel.measuedValuesIndex] = result;
  s_engineFuel.measuedValuesIndex++;
  if(s_engineFuel.measuedValuesIndex >= FUEL_MAX_SAMPLES) {
      s_engineFuel.measuedValuesIndex = 0;
  }

  int sec = getSeconds();
  if(s_engineFuel.lastResult == FUEL_INIT_VALUE) {
      s_engineFuel.nextMeasurement = sec - 1;
  }

  if(s_engineFuel.nextMeasurement < sec) {

      if(s_engineFuel.fuelMeasurementTime < FUEL_MEASUREMENT_TIME_DEST) {
          s_engineFuel.fuelMeasurementTime++;
      }
      s_engineFuel.nextMeasurement = sec + s_engineFuel.fuelMeasurementTime;

      long average = 0;
      int i; 
      for (i = 0; i < FUEL_MAX_SAMPLES; i++) {
          int v = s_engineFuel.measuredValues[i];
          if(v == FUEL_INIT_VALUE) {
              break;
          }
          average += v;
      }
      average /= i;

      deb("raw:%d (%d) num fuel samples: %d average val: %ld next probe time: %ds probes so far:%ld", 
          r, result, i, average, s_engineFuel.fuelMeasurementTime, ++s_engineFuel.measurements);

      s_engineFuel.lastResult = average;
  }
  #endif

  return s_engineFuel.lastResult;
}

void initFuelMeasurement(void) {
  memset(s_engineFuel.measuredValues, FUEL_INIT_VALUE, sizeof(s_engineFuel.measuredValues));
  s_engineFuel.measuedValuesIndex = 0;
  s_engineFuel.lastResult = FUEL_INIT_VALUE;

  s_engineFuel.fuelMeasurementTime = FUEL_MEASUREMENT_TIME_START;
  s_engineFuel.nextMeasurement = getSeconds() + s_engineFuel.fuelMeasurementTime;
  s_engineFuel.measurements = 0;
}
