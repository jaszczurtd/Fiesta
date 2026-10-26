#include "oilPressure.h"

#include <JaszczurHAL.h>
#include <utils/pidController.h>

static float filteredPressure = 0.0f;

static float mapResistanceToBars(float resistanceOhm) {
  float normalized =
      (resistanceOhm - OIL_PRESSURE_SENSOR_RES_MIN_OHM) /
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
  return (OIL_PRESSURE_PULLUP_OHM * (OIL_PRESSURE_ADC_REF_V - voltage)) /
         voltage;
#endif
}

bool setupOilPressure(void) {
  hal_adc_set_resolution(OIL_PRESSURE_ADC_BITS);
  hal_gpio_set_mode(JH_HW_NODE_OIL_PRESSURE_PIN_IN, HAL_GPIO_INPUT);
  filteredPressure = 0.0f;
  return true;
}

oil_sender_state_t oilPressureFromSample(int raw, float *bar) {
  if (raw < 0) {
    return OIL_SENDER_NO_SAMPLE;
  }
  const float senderResistance = readSenderResistanceOhm(raw);
  if (senderResistance < OIL_PRESSURE_SHORT_OHM) {
    return OIL_SENDER_SHORT;
  }
  if (senderResistance > OIL_PRESSURE_OPEN_OHM) {
    return OIL_SENDER_OPEN;
  }
  *bar = mapResistanceToBars(senderResistance);
  return OIL_SENDER_OK;
}

oil_sender_state_t readOilPressure(float *filteredBar) {
  float oilPressure = 0.0f;
  const oil_sender_state_t state = oilPressureFromSample(
      hal_adc_read(JH_HW_NODE_OIL_PRESSURE_PIN_IN), &oilPressure);
  if (state == OIL_SENDER_OK) {
    filteredPressure = (filteredPressure * (1.0f - OIL_PRESSURE_FILTER_ALPHA)) +
                       (oilPressure * OIL_PRESSURE_FILTER_ALPHA);
  }
  *filteredBar = filteredPressure;
  return state;
}
