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

#define CFG_WATCHDOG_TIMEOUT_MS 4000u

#define CFG_IDLE_SECONDS 60u

/* The main loop runs one pass every CFG_MAIN_PASS_MS. Screens send only text
 * that changed, so a pass takes a few ms; this pace keeps the counts below
 * that are given in passes (screen rotation, set-mode blink, SET hold,
 * sensor reads) at the timing they had when every pass redrew the whole
 * screen (about 39-40 ms). */
#define CFG_MAIN_PASS_MS 40u
JB_STATIC_ASSERT(CFG_MAIN_PASS_MS >= 20u && CFG_MAIN_PASS_MS <= 100u,
                 "CFG_MAIN_PASS_MS must be in 20..100 ms");

/* The panel's whole content is sent again this often, which repairs text
 * that changed on the panel without a reported write error. */
#define CFG_DISPLAY_REFRESH_MS 10000u
JB_STATIC_ASSERT(CFG_DISPLAY_REFRESH_MS >= 1000u &&
                     CFG_DISPLAY_REFRESH_MS <= 60000u,
                 "CFG_DISPLAY_REFRESH_MS must be in 1..60 s");

/* The greeting stays on the panel this long after start-up and after every
 * ignition change; the main loop keeps running meanwhile. */
#define CFG_GREETING_MS 1500u
JB_STATIC_ASSERT(CFG_GREETING_MS >= 500u && CFG_GREETING_MS <= 5000u,
                 "CFG_GREETING_MS must be in 500..5000 ms");

#define CFG_CHANGE_MODE_CYCLES 400
/* While the panel sleeps the blue LED lights for the first part of each
 * period. hal_millis() times it, so the cadence does not depend on how long
 * a main loop pass takes. */
#define CFG_IDLE_LED_PERIOD_MS 1000u
#define CFG_IDLE_LED_ON_MS 125u
JB_STATIC_ASSERT(CFG_IDLE_LED_ON_MS >= 20u &&
                     CFG_IDLE_LED_ON_MS < CFG_IDLE_LED_PERIOD_MS &&
                     CFG_IDLE_LED_PERIOD_MS <= 10000u,
                 "CFG_IDLE_LED_ON_MS must be 20 ms or more and shorter than "
                 "CFG_IDLE_LED_PERIOD_MS, at most 10 s");

#define CFG_CLOCK_SET_STEP_DELAY_MS 80u
#define CFG_CLOCK_OPERATION_DELAY_MS 120u
/* Set mode: a held hour/minute button steps again after this delay, then at
 * this interval; a shorter press gives one step. */
#define CFG_CLOCK_REPEAT_DELAY_MS 500u
#define CFG_CLOCK_REPEAT_INTERVAL_MS 160u
JB_STATIC_ASSERT(CFG_CLOCK_REPEAT_INTERVAL_MS >= 50u &&
                     CFG_CLOCK_REPEAT_INTERVAL_MS <=
                         CFG_CLOCK_REPEAT_DELAY_MS &&
                     CFG_CLOCK_REPEAT_DELAY_MS <= 2000u,
                 "CFG_CLOCK_REPEAT_INTERVAL_MS must be 50 ms to "
                 "CFG_CLOCK_REPEAT_DELAY_MS, at most 2 s");
#define CFG_CLOCK_ITEM_VISIBILITY_CYCLES 10
#define CFG_CLOCK_SET_MODE_DELAY_LOOPS 80

/* The main loop logs the shown date and time this often. */
#define CFG_TIME_LOG_INTERVAL_MS 1000u

#define CFG_TEMP_DELAY_LOOPS 20
/* CAN 0x132 sends a DS18B20 reading while its newest 12-bit conversion
 * (750 ms each, back to back) is younger than this. */
#define CFG_TEMP_MAX_AGE_MS 3000u
JB_STATIC_ASSERT(CFG_TEMP_MAX_AGE_MS >= 1500u && CFG_TEMP_MAX_AGE_MS <= 10000u,
                 "CFG_TEMP_MAX_AGE_MS must be in 1500..10000 ms");
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
JB_STATIC_ASSERT(CFG_RTC_SYNC_TOLERANCE_S >= 1u &&
                     CFG_RTC_SYNC_TOLERANCE_S <= 5u,
                 "CFG_RTC_SYNC_TOLERANCE_S must be in 1..5 s");
JB_STATIC_ASSERT(CFG_RTC_SYNC_CORRECTION_INTERVAL_MS >= 60000u &&
                     CFG_RTC_SYNC_CORRECTION_INTERVAL_MS <= 3600000u,
                 "CFG_RTC_SYNC_CORRECTION_INTERVAL_MS must be in 1..60 min");
JB_STATIC_ASSERT(
    CFG_RTC_SYNC_RETRY_MS >= 5000u &&
        CFG_RTC_SYNC_RETRY_MS <= CFG_RTC_SYNC_CORRECTION_INTERVAL_MS,
    "CFG_RTC_SYNC_RETRY_MS must be 5 s to the correction interval");
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
