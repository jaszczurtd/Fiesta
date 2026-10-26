#ifndef S_OIL_PRESSURE_H
#define S_OIL_PRESSURE_H

#include "config.h"
#include "hardwareConfig.h"

#include <hal/core/hal_status.h>

// Oil pressure sender on JH_HW_NODE_OIL_PRESSURE_PIN_IN, moved from
// OilAndSpeed with its characteristic and filter. Core 1 reads it every
// OIL_PRESSURE_READ_INTERVAL.

// Oil pressure sender parameters
#define OIL_PRESSURE_FILTER_ALPHA 0.20f
#define OIL_PRESSURE_MAX_BAR 10.0f
#define OIL_PRESSURE_SENSOR_RES_MIN_OHM 10.0f
#define OIL_PRESSURE_SENSOR_RES_MAX_OHM 180.0f
#define OIL_PRESSURE_PULLUP_OHM 220.0f
#define OIL_PRESSURE_ADC_REF_V 3.3f

#define OIL_PRESSURE_ADC_BITS 12
// true  -> pull-up resistor to Vref, sensor to GND
// false -> pull-down resistor to GND, sensor to Vref
#define OIL_PRESSURE_DIVIDER_PULLUP 1

// Below half the lowest or above twice the highest sender resistance the
// wiring is shorted or open; such a sample is no pressure reading.
#define OIL_PRESSURE_SHORT_OHM (OIL_PRESSURE_SENSOR_RES_MIN_OHM / 2.0f)
#define OIL_PRESSURE_OPEN_OHM (OIL_PRESSURE_SENSOR_RES_MAX_OHM * 2.0f)

typedef enum {
  OIL_SENDER_OK,
  OIL_SENDER_SHORT, // sender or its wire shorted to ground
  OIL_SENDER_OPEN,  // sender disconnected or its wire broken
  OIL_SENDER_NO_SAMPLE
} oil_sender_state_t;

bool setupOilPressure(void);

/**
 * @brief Classify one ADC sample of the sender divider.
 * @param raw ADC value, 0..(1 << OIL_PRESSURE_ADC_BITS) - 1; negative means
 *        no sample.
 * @param[out] bar Unfiltered pressure for OIL_SENDER_OK, else untouched.
 * @return State of the sender for this sample.
 */
oil_sender_state_t oilPressureFromSample(int raw, float *bar);

/**
 * @brief Read and filter the sender once; core 1.
 * @param[out] filteredBar Filtered pressure; valid only for OIL_SENDER_OK.
 * @return State of the sender. A faulty sample leaves the filter unchanged.
 */
oil_sender_state_t readOilPressure(float *filteredBar);

#endif
