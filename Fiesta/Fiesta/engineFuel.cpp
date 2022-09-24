#include "engineFuel.h"

//-------------------------------------------------------------------------------------------------
//Read fuel amount
//-------------------------------------------------------------------------------------------------

static int measuredValues[FUEL_MAX_SAMPLES];
static int measuedValuesIndex = 0;
static int lastResult = FUEL_INIT_VALUE;
static int nextMeasurement = 0;
static int fuelMeasurementTime = 0;
static long measurements = 0;

void initFuelMeasurement(void) {
    memset(measuredValues, FUEL_INIT_VALUE, sizeof(measuredValues));
    measuedValuesIndex = 0;
    lastResult = FUEL_INIT_VALUE;

    fuelMeasurementTime = FUEL_MEASUREMENT_TIME_START;
    nextMeasurement = getSeconds() + fuelMeasurementTime;
    measurements = 0;
}

float readFuel(void) {
    set4051ActivePin(4);

    int result = getAverageValueFrom(A1);
    #ifdef DEBUG
    deb("tank raw value: %d", result);
    #endif

    result -= FUEL_MAX;
    result = abs(result - (FUEL_MIN - FUEL_MAX));

    measuredValues[measuedValuesIndex] = result;
    measuedValuesIndex++;
    if(measuedValuesIndex > FUEL_MAX_SAMPLES) {
        measuedValuesIndex = 0;
    }

    int sec = getSeconds();
    if(lastResult == FUEL_INIT_VALUE) {
        nextMeasurement = sec - 1;
    }

    if(nextMeasurement < sec) {

        if(fuelMeasurementTime < FUEL_MEASUREMENT_TIME_DEST) {
            fuelMeasurementTime++;
        }
        nextMeasurement = sec + fuelMeasurementTime;

        long average = 0;
        int i; 
        for (i = 0; i < FUEL_MAX_SAMPLES; i++) {
            int v = measuredValues[i];
            if(v == FUEL_INIT_VALUE) {
                break;
            }
            average += v;
        }
        average /= i;

        deb("num fuel samples: %d average val: %ld next measurement time: %ds measurements so far:%ld", i, average, fuelMeasurementTime, ++measurements);

        lastResult = average;
    }

    return lastResult;
}

