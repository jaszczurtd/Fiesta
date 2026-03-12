#include "oilPressure.h"

#include "can.h"

static float filteredPressure = 0.0f;

static float mapResistanceToBars(float resistanceOhm) {
  float normalized = (resistanceOhm - OIL_PRESSURE_SENSOR_RES_MIN_OHM) /
                     (OIL_PRESSURE_SENSOR_RES_MAX_OHM - OIL_PRESSURE_SENSOR_RES_MIN_OHM);

  normalized = constrain(normalized, 0.0f, 1.0f);
  return normalized * OIL_PRESSURE_MAX_BAR;
}

bool setupOilPressure(void) {
  analogReadResolution(12);
  pinMode(OIL_PRESSURE_ADC_PIN, INPUT);
  valueFields[F_OIL_PRESSURE] = 0.0f;
  filteredPressure = 0.0f;
  return true;
}

bool readOilPressure(void *arg) {
  (void)arg;

  int raw = analogRead(OIL_PRESSURE_ADC_PIN);
  if(raw <= 0) {
    return true;
  }

  constexpr float adcMax = 4095.0f;
  float voltage = (float(raw) / adcMax) * OIL_PRESSURE_ADC_REF_V;

  const float minDenominator = 0.001f;
  float denominator = OIL_PRESSURE_ADC_REF_V - voltage;
  if(denominator < minDenominator) {
    denominator = minDenominator;
  }

  float senderResistance = (voltage * OIL_PRESSURE_PULLUP_OHM) / denominator;
  float oilPressure = mapResistanceToBars(senderResistance);

  filteredPressure = (filteredPressure * (1.0f - OIL_PRESSURE_FILTER_ALPHA)) +
                     (oilPressure * OIL_PRESSURE_FILTER_ALPHA);

  valueFields[F_OIL_PRESSURE] = filteredPressure;

  return true;
}
