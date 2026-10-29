#pragma once

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

#define WATCHDOG_TIME 4000
#define UNSYNCHRONIZE_TIME 15
#define CORE_OPERATION_DELAY 1

// Preserve the ECU's location-age limit; UTC must come from a newer RMC.
#define GPS_MAX_AGE_MS 4000u
#define GPS_UTC_MAX_AGE_MS 2000u
#define GPS_MIN_SPEED_KMH 5u
#define CAN_GPS_UPDATE_INTERVAL 1000u
JB_STATIC_ASSERT(GPS_MAX_AGE_MS > 0u && GPS_MAX_AGE_MS <= 4000u,
                 "GPS_MAX_AGE_MS must be in 1..4000 ms");
JB_STATIC_ASSERT(GPS_UTC_MAX_AGE_MS > 0u && GPS_UTC_MAX_AGE_MS <= 2000u,
                 "GPS_UTC_MAX_AGE_MS must be in 1..2000 ms");
JB_STATIC_ASSERT(GPS_UTC_MAX_AGE_MS < GPS_MAX_AGE_MS,
                 "GPS UTC must expire before the position");
JB_STATIC_ASSERT(GPS_MIN_SPEED_KMH == 5u,
                 "GPS speed cutoff must match the 5 km/h wire format");
JB_STATIC_ASSERT(CAN_GPS_UPDATE_INTERVAL == 1000u,
                 "CAN_GPS_UPDATE_INTERVAL must match the 1 s UTC cadence");
// The receiver sends NMEA at least once a second, also without a fix; no
// valid sentence for this long means no communication with it.
#define GPS_LINK_MAX_AGE_MS 3000u
JB_STATIC_ASSERT(GPS_LINK_MAX_AGE_MS > 1000u &&
                     GPS_LINK_MAX_AGE_MS <= GPS_MAX_AGE_MS,
                 "GPS_LINK_MAX_AGE_MS must outlast the 1 s NMEA cycle and "
                 "not exceed GPS_MAX_AGE_MS");

// delay time right after start, before first serious alerts will show up (in
// seconds)
#define SERIOUS_ALERTS_DELAY_TIME 1

// how many main loop cycles between next sensor's read (high importance values)
#define HIGH_READ_CYCLES_AMOUNT 8

// in seconds
#define FIESTA_INTRO_TIME 2

#define MINIMUM_FUEL_AMOUNT_PERCENTAGE 10

// minimum temp value for oil and coolant
#define TEMP_MIN 45

// minimum working temp value for oil and coolant
#define TEMP_OK_LO 70

// oil temp values
#define TEMP_OIL_MAX 140
#define TEMP_OIL_OK_HI 115

// coolant temp values
#define TEMP_MAX 125
#define TEMP_OK_HI 105

#define TEMP_LOWEST -70
#define TEMP_HIGHEST 170

#define TEMP_MINIMUM_FOR_GLOW_PLUGS 50
// when engine is cold, this is the multiplier of the time
// when the glow plugs are heating the cylinders
#define TEMP_HEATING_GLOW_PLUGS_MULTIPLIER 1.5

// EGT temperatures definitions
#define TEMP_EGT_OK_HI 950
#define TEMP_EGT_MAX 1600
#define TEMP_EGT_MIN 100

// buzzer strategy for temperature alarms
#define BUZZER_COOLANT_OVERHEAT_TEMP ((TEMP_OK_HI + TEMP_MAX) / 2)
#define BUZZER_OIL_OVERHEAT_TEMP ((TEMP_OIL_OK_HI + TEMP_OIL_MAX) / 2)
#define BUZZER_EGT_OVERHEAT_TEMP TEMP_EGT_OK_HI
#define BUZZER_COOLANT_OIL_REPEAT_INTERVAL_MS 15000UL
#define BUZZER_EGT_REPEAT_INTERVAL_MS 10000UL

// temperature when fan should start
#define TEMP_FAN_START 102
// temperature when fan should stop after start
#define TEMP_FAN_STOP 95

// air temperature when fan should start
#define AIR_TEMP_FAN_START 55
// air temperature when fan should start
#define AIR_TEMP_FAN_STOP 45

// temperature when engine heater should stop to heat
#define TEMP_HEATER_STOP 80

// minimum voltage amount - below this value no engine heater, or heated windows
#define MINIMUM_VOLTS_AMOUNT 13.0

// how long heated windows should heats? (in seconds)
#define HEATED_WINDOWS_TIME 4

// values from ADC
#define THROTTLE_MIN 1800
#define THROTTLE_MAX 3800

// values from ADC
#define FUEL_MAX 320
#define FUEL_MIN 1280

// hella actuator PWM percent: min/max values
#define TURBO_ACTUATOR_LOW 6
#define TURBO_ACTUATOR_HIGH 90
// for graphic gauge
#define TURBO_MIN_PRESSURE_FOR_SPINNING 0.15

// RPM refresh interval (in miliseconds)
#define RPM_REFRESH_INTERVAL 150

// maximum RPM for engine
#define RPM_MAX_EVER 5000

#define NOMINAL_RPM_VALUE 890
#define COLD_RPM_VALUE 1000
#define REGEN_RPM_VALUE 1100
#define PRESSED_PEDAL_RPM_VALUE 1300

#define ACCELERATE_MIN_PERCENTAGE_THROTTLE_VALUE 4
#define ACCELLERATE_RPM_PERCENT_VALUE 98
#define MAX_RPM_PERCENT_VALUE 100
#define MIN_RPM_PERCENT_VALUE 20
#define MAX_RPM_DIFFERENCE 30 // max difference for engine nominal RPM
#define RESET_RPM_WATCHDOG_TIME 2000

// time of display: EGT/DPF (if available)
#define DPF_SHOW_TIME_INTERVAL 5000

// The DPF module is planned, not installed yet: without it the display shows
// no connection alarm for it.
#define DPF_MODULE_INSTALLED 0

// tire dimensions for the ABS speed
#define TIRE_DIMENSIONS "185/55 R15"
#define TIRE_CORRECTION_FACTOR 0.98

// oil pressure readings, in miliseconds
#define OIL_PRESSURE_READ_INTERVAL 100
#define OIL_PRESSURE_READ_INTERVAL_MIN 50
#define OIL_PRESSURE_READ_INTERVAL_MAX 500
JB_STATIC_ASSERT(OIL_PRESSURE_READ_INTERVAL >= OIL_PRESSURE_READ_INTERVAL_MIN &&
                     OIL_PRESSURE_READ_INTERVAL <=
                         OIL_PRESSURE_READ_INTERVAL_MAX,
                 "OIL_PRESSURE_READ_INTERVAL must be in 50..500 ms");

// thermocouple readings, in miliseconds
#define THERMOCOUPLE_READ_INTERVAL 1000
#define THERMOCOUPLE_READ_INTERVAL_MIN 500
#define THERMOCOUPLE_READ_INTERVAL_MAX 5000
JB_STATIC_ASSERT(THERMOCOUPLE_READ_INTERVAL >= THERMOCOUPLE_READ_INTERVAL_MIN &&
                     THERMOCOUPLE_READ_INTERVAL <=
                         THERMOCOUPLE_READ_INTERVAL_MAX,
                 "THERMOCOUPLE_READ_INTERVAL must be in 500..5000 ms");
// a missing MCP9600 is looked for again after this time, in miliseconds;
// the search runs in the reading pass, so not more often than it
#define THERMOCOUPLE_RETRY_INTERVAL 10000
JB_STATIC_ASSERT(THERMOCOUPLE_RETRY_INTERVAL >= THERMOCOUPLE_READ_INTERVAL,
                 "THERMOCOUPLE_RETRY_INTERVAL must not be shorter than a read");
// failed reads in a row after which an MCP9600 counts as gone
#define THERMOCOUPLE_FAILED_READS_LIMIT 3
JB_STATIC_ASSERT(THERMOCOUPLE_FAILED_READS_LIMIT >= 1 &&
                     THERMOCOUPLE_FAILED_READS_LIMIT <= 255,
                 "THERMOCOUPLE_FAILED_READS_LIMIT must be in 1..255");

// core 0 (CAN frames, oil lamp, buzzer) takes a local reading older than
// this as no measurement, e.g. while core 1 hangs until the watchdog; in
// miliseconds, well above the longest core-1 pass (a full redraw, ~180 ms)
#define OIL_PRESSURE_MAX_AGE_MS 1000
JB_STATIC_ASSERT(OIL_PRESSURE_MAX_AGE_MS >= 5 * OIL_PRESSURE_READ_INTERVAL &&
                     OIL_PRESSURE_MAX_AGE_MS <= WATCHDOG_TIME,
                 "OIL_PRESSURE_MAX_AGE_MS must be 5 readings to WATCHDOG_TIME");
#define THERMOCOUPLE_MAX_AGE_MS 3000
JB_STATIC_ASSERT(THERMOCOUPLE_MAX_AGE_MS >= 2 * THERMOCOUPLE_READ_INTERVAL &&
                     THERMOCOUPLE_MAX_AGE_MS <= 10 * THERMOCOUPLE_READ_INTERVAL,
                 "THERMOCOUPLE_MAX_AGE_MS must be 2 to 10 readings");

// oil pressure and ABS speed frame period [ms] (CAN_UPDATE_RECIPIENTS) and
// EGT frame period [ms]
#define CAN_EGT_UPDATE_INTERVAL 250

// max BAR turbo pressure
#define MAX_BOOST_PRESSURE 1.9

// min-max value for indicator
#define VOLTS_MIN_VAL 11.6
#define VOLTS_MAX_VAL 14.7

// charge lamp (GP26) from the ECU RPM and battery voltage, in millivolts:
// below CHARGE_LAMP_LOW_MV the alternator is not charging, above
// CHARGE_LAMP_CHARGING_MV it is, in between the last state holds; above
// CHARGE_LAMP_OVERVOLTAGE_MV the lamp blinks. The TFT voltage gauge keeps
// VOLTS_MIN_VAL and VOLTS_MAX_VAL.
#define CHARGE_LAMP_LOW_MV 12900
#define CHARGE_LAMP_CHARGING_MV 13200
#define CHARGE_LAMP_OVERVOLTAGE_MV 15000
JB_STATIC_ASSERT(CHARGE_LAMP_LOW_MV < CHARGE_LAMP_CHARGING_MV &&
                     CHARGE_LAMP_CHARGING_MV < CHARGE_LAMP_OVERVOLTAGE_MV,
                 "charge lamp limits must rise: low < charging < overvoltage");
// the ECU sends tenths of a volt: a limit between two steps is never met
JB_STATIC_ASSERT(CHARGE_LAMP_LOW_MV % 100 == 0 &&
                     CHARGE_LAMP_CHARGING_MV % 100 == 0 &&
                     CHARGE_LAMP_OVERVOLTAGE_MV % 100 == 0,
                 "charge lamp limits must be whole tenths of a volt");
// how long a voltage condition must hold before the lamp follows it, and how
// long the voltage is not judged after the engine starts, in miliseconds;
// tune them to the alternator
#define CHARGE_LAMP_CONFIRM_MS 2000
#define CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS 2000
#define CHARGE_LAMP_START_SETTLE_MS 3000
#define CHARGE_LAMP_CONFIRM_MS_MIN 100
#define CHARGE_LAMP_TIME_MS_MAX 10000
JB_STATIC_ASSERT(CHARGE_LAMP_CONFIRM_MS >= CHARGE_LAMP_CONFIRM_MS_MIN &&
                     CHARGE_LAMP_CONFIRM_MS <= CHARGE_LAMP_TIME_MS_MAX,
                 "CHARGE_LAMP_CONFIRM_MS must be in 100..10000 ms");
JB_STATIC_ASSERT(CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS >=
                         CHARGE_LAMP_CONFIRM_MS_MIN &&
                     CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS <=
                         CHARGE_LAMP_TIME_MS_MAX,
                 "CHARGE_LAMP_OVERVOLTAGE_CONFIRM_MS must be in 100..10000 ms");
JB_STATIC_ASSERT(CHARGE_LAMP_START_SETTLE_MS >= 0 &&
                     CHARGE_LAMP_START_SETTLE_MS <= CHARGE_LAMP_TIME_MS_MAX,
                 "CHARGE_LAMP_START_SETTLE_MS must be in 0..10000 ms");
// 2 Hz overvoltage blinking: 250 ms lit, 250 ms dark
#define CHARGE_LAMP_BLINK_HALF_MS 250
// RPM or voltage older than this lights the lamp, in miliseconds; at least
// two RPM heartbeats of the ECU (100 ms), at most the connection check
// (CAN_CHECK_CONNECTION) that clears the ECU values
#define CHARGE_LAMP_DATA_TIMEOUT_MS 1000
#define CHARGE_LAMP_DATA_TIMEOUT_MS_MIN 200
#define CHARGE_LAMP_DATA_TIMEOUT_MS_MAX 1500
JB_STATIC_ASSERT(CHARGE_LAMP_DATA_TIMEOUT_MS >=
                         CHARGE_LAMP_DATA_TIMEOUT_MS_MIN &&
                     CHARGE_LAMP_DATA_TIMEOUT_MS <=
                         CHARGE_LAMP_DATA_TIMEOUT_MS_MAX,
                 "CHARGE_LAMP_DATA_TIMEOUT_MS must be in 200..1500 ms");

// just a general timer tasks info message on main thread
#define THREAD_CONTROL_SECONDS 5

// a CAN controller that does not answer is tried again this often from the
// core-0 loop, in miliseconds; an attempt does not wait for it
#define CAN_INIT_RETRY_INTERVAL 1000
JB_STATIC_ASSERT(CAN_INIT_RETRY_INTERVAL >= 100 &&
                     CAN_INIT_RETRY_INTERVAL <= 10000,
                 "CAN_INIT_RETRY_INTERVAL must be in 100..10000 ms");

#define MIN_OIL_PRESSURE 0.2

// in miliseconds, print values into serial
#define DEBUG_UPDATE 3 * SECOND

// Graphics definitions
#define DISPLAY_TXT_SIZE 32

#define SCREEN_W 320
#define SCREEN_H 240

#define TEXT_COLOR 0xE73C

/**
 * @brief Initialize the minimal serial configurator session context.
 */
void configSessionInit(void);

/**
 * @brief Poll serial RX and process supported session commands.
 */
void configSessionTick(void);

/**
 * @brief Return true if at least one HELLO handshake was completed.
 * @return Session active flag.
 */
bool configSessionActive(void);

/**
 * @brief Return current session id assigned by HELLO handshake.
 * @return Session id, or 0 when inactive.
 */
uint32_t configSessionId(void);
