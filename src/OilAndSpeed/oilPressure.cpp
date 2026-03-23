#include "oilPressure.h"
#include <pidController.h>

static float filteredPressure = 0.0f;

static float mapResistanceToBars(float resistanceOhm) {
  float normalized = (resistanceOhm - OIL_PRESSURE_SENSOR_RES_MIN_OHM) /
                     (OIL_PRESSURE_SENSOR_RES_MAX_OHM - OIL_PRESSURE_SENSOR_RES_MIN_OHM);

  normalized = pid_clamp(normalized, 0.0f, 1.0f);
  return normalized * OIL_PRESSURE_MAX_BAR;
}

static float readSenderResistanceOhm(int raw) {
  constexpr float minVoltage = 0.001f;

  const float adcMax = float((1u << OIL_PRESSURE_ADC_BITS) - 1u);
  float voltage = (float(raw) / adcMax) * OIL_PRESSURE_ADC_REF_V;

  voltage = pid_clamp(voltage, minVoltage, OIL_PRESSURE_ADC_REF_V - minVoltage);

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
  hal_adc_set_resolution(OIL_PRESSURE_ADC_BITS);
  hal_gpio_set_mode(OIL_PRESSURE_ADC_PIN, HAL_GPIO_INPUT);
  setGlobalValue(F_OIL_PRESSURE, 0.0f);
  filteredPressure = 0.0f;
  return true;
}

void readOilPressure(void) {
  int raw = hal_adc_read(OIL_PRESSURE_ADC_PIN);
  if (raw < 0) {
    return;
  }

  float senderResistance = readSenderResistanceOhm(raw);
  float oilPressure = mapResistanceToBars(senderResistance);

  filteredPressure = (filteredPressure * (1.0f - OIL_PRESSURE_FILTER_ALPHA)) +
                     (oilPressure * OIL_PRESSURE_FILTER_ALPHA);

  setGlobalValue(F_OIL_PRESSURE, filteredPressure);
}
