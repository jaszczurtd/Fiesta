
#ifndef C_OIL_LAMP
#define C_OIL_LAMP

#include "config.h"

#include <stdint.h>

// Oil lamp of the cluster, from the oil pressure sender of core 1. The lamp
// is lit without pressure and blinks while the sender is shorted or open,
// when the gauge shows ERR: a lit lamp keeps meaning no pressure.

struct OilLampInput {
  uint32_t nowMs;
  bool senderFault; // the last sample found the sender shorted or open
  bool valid;       // a fresh pressure reading
  float bar;
};

class OilLamp {
public:
  OilLamp();
  /** @brief Back to the state after power-up: no sender fault seen. */
  void reset();
  /**
   * @brief Evaluate one sample; call every OIL_LAMP_UPDATE_INTERVAL.
   * @return True when the lamp is lit.
   */
  bool process(const OilLampInput &input);

private:
  bool faulty;
  uint32_t faultSinceMs;
};

/** @brief Core 0: set the lamp output lit before the first reading. */
void initOilLamp(void);
/** @brief Core 0: drive the lamp from the latest measurements of core 1. */
void updateOilLamp(void);

#endif
