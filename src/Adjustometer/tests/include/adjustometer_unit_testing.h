#ifndef ADJ_UNIT_TESTING_H
#define ADJ_UNIT_TESTING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int32_t pulse;
  uint32_t lastEdgeUs;
  uint32_t signalHz;
  uint32_t windowStartUs;
  uint32_t windowCount;
  uint32_t filteredHz;
  uint32_t baselineStartUs;
  uint32_t baselineEstimate;
  uint32_t baselineStableWindows;
  uint32_t baseline;
  bool baselineReady;
  bool verifying;
  uint32_t verifyStartUs;
  bool zeroHold;
  int8_t zeroCandidateSign;
  uint8_t zeroCandidateWindows;
  float filteredFuelTemp;
  float filteredVoltage;
} adj_sensors_test_state_t;

void adj_test_sensors_reset_state(void);
void adj_test_sensors_get_state(adj_sensors_test_state_t *state);
void adj_test_sensors_set_state(const adj_sensors_test_state_t *state);
void adj_test_sensors_count_edge(void);
uint32_t adj_test_sensors_apply_adjustometer_ema(uint32_t rawHz, uint32_t filteredHz);
float adj_test_sensors_apply_adc_ema(float raw, float prev);
bool adj_test_sensors_is_signal_lost(void);

#ifdef __cplusplus
}
#endif

#endif