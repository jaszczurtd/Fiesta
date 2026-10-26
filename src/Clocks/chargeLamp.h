#ifndef C_CHARGE_LAMP
#define C_CHARGE_LAMP

#include "config.h"

#include <stdint.h>

// Charge lamp of the cluster, from the RPM and battery voltage of the ECU.
// The lamp is lit until fresh data confirm charging with the engine running,
// and blinks at 2 Hz while the voltage stays too high.

struct ChargeLampInput {
  uint32_t nowMs;
  bool rpmFresh;   // RPM decoded within CHARGE_LAMP_DATA_TIMEOUT_MS
  bool voltsFresh; // voltage decoded within CHARGE_LAMP_DATA_TIMEOUT_MS
  int rpm;
  int32_t millivolts;
};

// A condition that counts only after it has held without a break.
class ChargeLampConfirmation {
public:
  void clear();
  /**
   * @brief Track the condition; a break restarts its time.
   * @return True once the condition has held for @p holdMs.
   */
  bool update(bool condition, uint32_t nowMs, uint32_t holdMs);

private:
  bool holding = false;
  uint32_t sinceMs = 0u;
};

class ChargeLamp {
public:
  ChargeLamp();
  /** @brief Back to the state after power-up: lit, engine not started. */
  void reset();
  /**
   * @brief Evaluate one sample; call on every core-0 loop pass.
   * @return True when the lamp is lit.
   */
  bool process(const ChargeLampInput &input);

private:
  bool engineRunning;
  uint32_t startedMs;
  bool charging;
  bool overvoltage;
  uint32_t overvoltageSinceMs;
  ChargeLampConfirmation low;
  ChargeLampConfirmation ok;
  ChargeLampConfirmation high;
};

/** @brief Core 0: set the lamp output lit before the first data arrive. */
void initChargeLamp(void);
/** @brief Core 0: drive the lamp from the latest RPM and voltage frames. */
void updateChargeLamp(void);

#endif
