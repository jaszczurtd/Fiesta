#include "oilPressure.h"

#include "can.h"

static float filteredPressure = 0.0f;

static float mapResistanceToBars(float resistanceOhm) {
  float normalized = (resistanceOhm - OIL_PRESSURE_SENSOR_RES_MIN_OHM) /
                     (OIL_PRESSURE_SENSOR_RES_MAX_OHM - OIL_PRESSURE_SENSOR_RES_MIN_OHM);

  normalized = constrain(normalized, 0.0f, 1.0f);
  return normalized * OIL_PRESSURE_MAX_BAR;
}

static float readSenderResistanceOhm(int raw) {
  constexpr float minVoltage = 0.001f;

  const float adcMax = float((1u << OIL_PRESSURE_ADC_BITS) - 1u);
  float voltage = (float(raw) / adcMax) * OIL_PRESSURE_ADC_REF_V;

  voltage = constrain(voltage, minVoltage, OIL_PRESSURE_ADC_REF_V - minVoltage);

#if OIL_PRESSURE_DIVIDER_PULLUP
  // Rp -> Vref, Rs(sensor) -> GND, ADC on divider node
  // Vadc = Vref * Rs / (Rp + Rs)
  // Rs = (Vadc * Rp) / (Vref - Vadc)
  float denominator = OIL_PRESSURE_ADC_REF_V - voltage;
  return (voltage * OIL_PRESSURE_PULLUP_OHM) / denominator;
#else
  // Rs(sensor) -> Vref, Rp -> GND, ADC on divider node
  // Vadc = Vref * Rp / (Rp + Rs)
  // Rs = Rp * (Vref - Vadc) / Vadc
  return (OIL_PRESSURE_PULLUP_OHM * (OIL_PRESSURE_ADC_REF_V - voltage)) / voltage;
#endif
}

bool setupOilPressure(void) {
  analogReadResolution(OIL_PRESSURE_ADC_BITS);
  pinMode(OIL_PRESSURE_ADC_PIN, INPUT);
  valueFields[F_OIL_PRESSURE] = 0.0f;
  filteredPressure = 0.0f;
  return true;
}

bool readOilPressure(void *arg) {
  (void)arg;

  int raw = analogRead(OIL_PRESSURE_ADC_PIN);
  if(raw < 0) {
    return true;
  }

  float senderResistance = readSenderResistanceOhm(raw);
  float oilPressure = mapResistanceToBars(senderResistance);

  filteredPressure = (filteredPressure * (1.0f - OIL_PRESSURE_FILTER_ALPHA)) +
                     (oilPressure * OIL_PRESSURE_FILTER_ALPHA);

  valueFields[F_OIL_PRESSURE] = filteredPressure;

  return true;
}
