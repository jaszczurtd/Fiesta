#pragma once
#include "sensors.h"

#ifdef __cplusplus
extern "C" {
#endif
/** @brief Publish a non-NULL oscillator snapshot on core 0; no ADC or USB I/O.
 */
void publishAdjustometerFeedback(const adjustometer_feedback_t *sample);
/** @brief Publish a non-NULL diagnostic snapshot on core 1; chip temperature in
 * 0.1 C. */
void publishAdjustometerExtension(const adjustometer_feedback_t *sample,
                                  int16_t chipTemp);
#ifdef __cplusplus
}
#endif
