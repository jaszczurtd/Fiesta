#pragma once

/** Drive frequency shared by the ECU PWM and Adjustometer ripple filter [Hz].
 * An override must be supplied to both firmware builds. */
#ifndef VP37_PWM_FREQUENCY_HZ
#define VP37_PWM_FREQUENCY_HZ 130U
#endif
