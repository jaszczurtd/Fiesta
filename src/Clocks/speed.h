
#ifndef S_SPEED_H
#define S_SPEED_H

#include "can.h"
#include "config.h"

// ABS speed from the wheel impulses on JH_HW_NODE_ABS_SPEED_PIN_IN, moved
// from OilAndSpeed with its algorithm unchanged. Core 0: the interrupt and
// onImpulseTranslating() run there, the result goes to the core-0 state.

bool setupSpeedometer(void);
void onImpulseTranslating(void);
bool calculateCircumferenceMeters(const char *tireString,
                                  double correctionFactor);
double getCircumference(void);

#endif
