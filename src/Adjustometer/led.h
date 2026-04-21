#ifndef T_LED
#define T_LED

#include <libConfig.h>
#include "config.h"

#include <tools_c.h>
#include "hardwareConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the RGB status LED driver and internal LED state.
 * @return None.
 */
void initLed(void);

/**
 * @brief Refresh the RGB status LED pattern from current module status.
 * @return None.
 */
void updateLed(void);

#ifdef __cplusplus
}
#endif

#endif
