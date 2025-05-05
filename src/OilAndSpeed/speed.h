
#ifndef S_SPEED_H
#define S_SPEED_H

#include "can.h"
#include "oilPressure.h"
#include "config.h"

void setupSpeedometer(void);
void onImpulseTranslating(void);
bool calculateCircumferenceMeters(const char *tireString, double correctionFactor);
double getCircumference(void);

#endif
