#ifndef C_LOGIC
#define C_LOGIC

#include <JaszczurHAL.h>
#include <hal/timers/smart_timers/SmartTimers.h>
#include <utils/multicoreWatchdog.h>
#include <utils/tools_common_defs.h>

#include "../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "config.h"
#include "hardwareConfig.h"
#include "peripherials.h"
#include "vp37_bench_adapter.h"

// HAL_PROVIDE_APP_ENTRY entry points (see hal/core/hal_app.h). Implemented in
// logic.cpp and dispatched by HAL from setup/loop/loop1 (loop1 requires
// HAL_ENABLE_APP_TASK1).
extern "C" void app_start(void);
extern "C" void app_task0(void);
extern "C" void app_task1(void);

#ifdef UNIT_TEST
/** @brief The bench logic state, for its host tests. */
VP37InitStatus benchLogicStartStatus(void);
bool benchLogicAdjConnected(void);
#endif

#endif
