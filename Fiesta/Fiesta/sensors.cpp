
#include "sensors.h"

float valueFields[F_LAST];
float reflectionValueFields[F_LAST];

void initSensorsData(void) {
  for(int a = 0; a < F_LAST; a++) {
    valueFields[a] = reflectionValueFields[a] = 0.0;
  }
}

//-------------------------------------------------------------------------------------------------
//Read volts
//-------------------------------------------------------------------------------------------------

float readVolts(void) {
    return analogRead(A2) / DIVIDER_VOLTS;
}

//-------------------------------------------------------------------------------------------------
//Read coolant temperature
//-------------------------------------------------------------------------------------------------

float readCoolantTemp(void) {
    set4051ActivePin(0);
    return ntcToTemp(A1, 1506, 1500);
}

//-------------------------------------------------------------------------------------------------
//Read oil temperature
//-------------------------------------------------------------------------------------------------

float readOilTemp(void) {
    set4051ActivePin(1);
    return ntcToTemp(A1, 1506, 1500);
}

//-------------------------------------------------------------------------------------------------
//Read throttle
//-------------------------------------------------------------------------------------------------

static int lastThrottle = 0;
float readThrottle(void) {
    set4051ActivePin(2);

    float rawVal = getAverageValueFrom(A1);
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
    result = abs(result - PWM_RESOLUTION);

    int debugValue = getThrottlePercentage(result);
    if(lastThrottle != debugValue) {
        lastThrottle = debugValue;
        sendThrottleValueCAN(lastThrottle);
        deb("throttle: %d%%", debugValue);
    }

    return result;
}

int getThrottlePercentage(int currentVal) {
    float percent = (currentVal * 100) / PWM_RESOLUTION;
    return percentToGivenVal(percent, 100);
}

//-------------------------------------------------------------------------------------------------
//Read air temperature
//-------------------------------------------------------------------------------------------------

float readAirTemperature(void) {
    set4051ActivePin(3);
    return ntcToTemp(A1, 5050, 4700);
}

//-------------------------------------------------------------------------------------------------
//Read bar pressure amount
//-------------------------------------------------------------------------------------------------

float readBarPressure(void) {
    set4051ActivePin(5);

    float val = ((float)analogRead(A1) / DIVIDER_PRESSURE_BAR) - 1.0;
    if(val < 0.0) {
        val = 0.0;
    } 
    return val;
}

//-------------------------------------------------------------------------------------------------
//Read EGT temperature
//-------------------------------------------------------------------------------------------------

float readEGT(void) {
    set4051ActivePin(6);
    return (((float)getAverageValueFrom(A1)) / DIVIDER_EGT);
}

//-------------------------------------------------------------------------------------------------

void valToPWM(unsigned char pin, int val) {
    analogWriteFreq(100);
    analogWriteResolution(PWM_WRITE_RESOLUTION);
    analogWrite(pin, (PWM_RESOLUTION - val));
}

static unsigned char pcf8574State = 0;

void pcf8574_init(void) {
  pcf8574State = 0;

  Wire.beginTransmission(PCF8574_ADDR);
  Wire.write(pcf8574State);
  Wire.endTransmission();
}

void pcf8574_write(unsigned char pin, bool value) {
  if(value) {
    bitSet(pcf8574State, pin);
  }  else {
    bitClear(pcf8574State, pin);
  }

  Wire.beginTransmission(PCF8574_ADDR);
  bool success = Wire.write(pcf8574State);
  bool notFound = Wire.endTransmission();

  if(!success) {
    Serial.println("error writting byte to pcf8574");
  }

  if(notFound) {
    Serial.println("pcf8574 not found");
  }
}

int getEnginePercentageLoad(void) {
  return percentToGivenVal((float)( ( (valueFields[F_ENGINE_LOAD]) * 100) / PWM_RESOLUTION), 100);  
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

bool readHighValues(void *argument) {
  for(int a = 0; a < F_LAST; a++) {
    switch(a) {
      case F_ENGINE_LOAD:
        valueFields[a] = readThrottle();
        break;
      case F_PRESSURE:
        valueFields[a] = readBarPressure();
        break;
    }
    if(reflectionValueFields[a] != valueFields[a]) {
        reflectionValueFields[a] = valueFields[a];
    
        triggerDrawHighImportanceValue(true);
    }
  }

  return true;
}

