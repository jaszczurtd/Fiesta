#ifndef T_VP37
#define T_VP37

#include <tools.h>
#include <arduino-timer.h>

#include "config.h"
#include "start.h"
#include "rpm.h"
#include "obd-2.h"
#include "turbo.h"
#include "hardwareConfig.h"

void enableVP37(bool enable);
bool isVP37Enabled(void);

#endif
