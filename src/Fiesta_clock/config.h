#ifndef CONFIG_H_
#define CONFIG_H_

#include <hal/core/hal_status.h>
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

/* Years the buttons, SC and GPS may set; the RTC keeps 20xx (PCF8563 century
 * bit clear) and UTC seconds of them fit in uint32_t. */
#define CFG_RTC_MIN_YEAR 2000
#define CFG_RTC_MAX_YEAR 2099
JB_STATIC_ASSERT(CFG_RTC_MIN_YEAR >= 2000 &&
                     CFG_RTC_MIN_YEAR < CFG_RTC_MAX_YEAR &&
                     CFG_RTC_MAX_YEAR <= 2099,
                 "CFG_RTC_MIN_YEAR..CFG_RTC_MAX_YEAR must lie in 2000..2099");

/* GPS UTC from Clocks (CAN 0x131) sets the RTC. Agreed values: a trusted RTC
 * is corrected only when it is more than 2 s off, at most once in 10 min, and
 * a failed RTC or KV access is retried after 30 s. */
#define CFG_RTC_SYNC_TOLERANCE_S 2u
#define CFG_RTC_SYNC_CORRECTION_INTERVAL_MS 600000u
#define CFG_RTC_SYNC_RETRY_MS 30000u
JB_STATIC_ASSERT(CFG_RTC_SYNC_TOLERANCE_S == 2u &&
                     CFG_RTC_SYNC_CORRECTION_INTERVAL_MS == 600000u &&
                     CFG_RTC_SYNC_RETRY_MS == 30000u,
                 "the RTC correction rules are agreed values");
/* Frames read after a longer pause of the main loop (a held button, the
 * temperature reading after an ignition change) may have waited in the
 * receive queue and are dropped. A normal pass takes tens of milliseconds. */
#define CFG_RTC_SYNC_MAX_RX_GAP_MS 500u
JB_STATIC_ASSERT(CFG_RTC_SYNC_MAX_RX_GAP_MS >= 100u &&
                     CFG_RTC_SYNC_MAX_RX_GAP_MS <= 1000u,
                 "CFG_RTC_SYNC_MAX_RX_GAP_MS must be in 100..1000 ms");

void configSessionInit(void);
void configSessionTick(void);
bool configSessionActive(void);
uint32_t configSessionId(void);
/** @return true while an SC session holds staged, uncommitted time values. */
bool configSessionEditPending(void);

#endif /* CONFIG_H_ */
