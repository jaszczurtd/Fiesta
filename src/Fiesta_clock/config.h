#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

/* Fiesta-project firmware identity for the configurator HELLO response. */
#ifndef FW_VERSION
#define FW_VERSION "0.1.0"
#endif

#ifndef BUILD_ID
#define BUILD_ID (__DATE__ " " __TIME__)
#endif

#define CFG_WATCHDOG_TIMEOUT_MS 2000u

#define CFG_IDLE_SECONDS 60u

#define CFG_CHANGE_MODE_CYCLES 400
#define CFG_IDLE_LED_ON_CYCLES 400
#define CFG_IDLE_LED_OFF_CYCLES 2800

#define CFG_CLOCK_SET_STEP_DELAY_MS 80u
#define CFG_CLOCK_OPERATION_DELAY_MS 120u
#define CFG_CLOCK_ITEM_VISIBILITY_CYCLES 10
#define CFG_CLOCK_SET_MODE_DELAY_LOOPS 80

#define CFG_TEMP_DELAY_LOOPS 20
#define CFG_VOLT_DELAY_LOOPS 6

#define CFG_ADC_RESOLUTION_BITS 10u
#define CFG_ADC_VREF_VOLTS 3.3

#define CFG_VOLT_TOO_LOW_INT 5
#define CFG_VOLT_TOO_LOW_V 5.0

void configSessionInit(void);
void configSessionTick(void);
bool configSessionActive(void);
uint32_t configSessionId(void);

#endif /* CONFIG_H_ */
