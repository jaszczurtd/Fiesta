#ifndef T_CONFIG
#define T_CONFIG

#include "hardwareConfig.h"

#define vehicle_Vin     "WF0BXXGAJB1R32583"
#define ecu_Name        "JASZCZUR FORD FIESTA"

// ECU identification strings (Ford EEC-V / KWP2000 / UDS)
#define ecu_CalibrationId   "YS6F-12A650-FA"
#define ecu_PartNumber      "YS6F-12A650-FA"
#define ecu_SwVersion       "1.00"
#define ecu_SwDate          "20101201"
#define ecu_HardwareId      "H1.0"
#define ecu_Model           "EEC-V"
#define ecu_Type            "DEM"
#define ecu_SubType         "VP37"
#define ecu_Copyright       "FORD MOTOR CO"
#define ecu_CatchCode       "FD18A3C5"

// Odometer value for DID DD01 (TOTDIST), 3-byte unsigned km.
// Approximate mileage for a 2001 Ford Fiesta Mk5.
// Enable OBD_ENABLE_TOTDIST to expose via DID DD01; use getter/setter at runtime.
// Disabled: ForDiag shows hardcoded value with no way to read real odometer.
//#define OBD_ENABLE_TOTDIST
#define ecu_TotalDistanceKmDefault  200000u

// Fordiag Phase 2: suppress service 0x19 so Fordiag uses EEC-V path.
// This steers Fordiag toward E217/E21A/E219 part number DB lookup.
#define FORDIAG_COMPAT_NO_UDS_DTC

// Optional verbose debug output (uncomment to enable):
//#define OBD_VERBOSE_IDENT_DEBUG   // detailed identification field hex dumps
//#define OBD_VERBOSE_RX_DEBUG      // raw hex of every incoming CAN frame

// Ford E217 binary representation of part number middle section.
// Each byte → hex with leading zero stripped → concatenated = middle string.
// For "12A650": {0x12,0x0A,0x06,0x50} → "12"+"A"+"6"+"50" = "12A650"
// If changing ecu_PartNumber, update these bytes to match the new middle section.
#define ecu_PartNumMiddleHex   0x12, 0x0A, 0x06, 0x50
#define ecu_PartNumMiddleLen   4

//BASIC CONTROL VALUES!
//#define VP37

#define WATCHDOG_TIME 4000
#define UNSYNCHRONIZE_TIME 15
#define CORE_OPERATION_DELAY 1


//how many main loop cycles between next sensor's read (high importance values)
#define HIGH_READ_CYCLES_AMOUNT 8

#define MINIMUM_FUEL_AMOUNT_PERCENTAGE 10

//minimum temp value for oil and coolant
#define TEMP_MIN 45

//minimum working temp value for oil and coolant
#define TEMP_OK_LO 70

//oil temp values
#define TEMP_OIL_MAX 140
#define TEMP_OIL_OK_HI 115

//coolant temp values
#define TEMP_MAX 125
#define TEMP_OK_HI 105

#define TEMP_LOWEST -70
#define TEMP_HIGHEST 170

//EGT temperatures definitions
#define TEMP_EGT_OK_HI 950
#define TEMP_EGT_MAX 1600
#define TEMP_EGT_MIN 100

//temperature when fan should start
#define TEMP_FAN_START  102
//temperature when fan should stop after start
#define TEMP_FAN_STOP   95

//air temperature when fan should start
#define AIR_TEMP_FAN_START 55
//air temperature when fan should start
#define AIR_TEMP_FAN_STOP 45

//temperature when engine heater should stop to heat
#define TEMP_HEATER_STOP 80

//minimum voltage amount - below this value no engine heater, or heated windows
#define MINIMUM_VOLTS_AMOUNT 13.0

//how long heated windows should heats? (in seconds)
#define HEATED_WINDOWS_TIME 4

//values from ADC
#define THROTTLE_MIN 1795
#define THROTTLE_MAX 3730

//values from ADC
#define FUEL_MAX 320
#define FUEL_MIN 1280

//hella turbo actuator PWM percent: min/max values (percentage)
#define TURBO_ACTUATOR_LOW ((2.45) * PWM_RESOLUTION / 100.0)
#define TURBO_ACTUATOR_HIGH ((95.75) * PWM_RESOLUTION / 100.0)

//for graphic gauge
#define TURBO_MIN_PRESSURE_FOR_SPINNING 0.15

//RPM refresh interval (in miliseconds)
#define RPM_REFRESH_INTERVAL 150

//maximum RPM for engine
#define RPM_MAX_EVER 5000

#define NOMINAL_RPM_VALUE 890
#define COLD_RPM_VALUE 1000
#define REGEN_RPM_VALUE 1100
#define PRESSED_PEDAL_RPM_VALUE 1300

#define ACCELERATE_MIN_PERCENTAGE_THROTTLE_VALUE 4
#define ACCELLERATE_RPM_PERCENT_VALUE 98
#define MAX_RPM_PERCENT_VALUE 100
#define MIN_RPM_PERCENT_VALUE 20
#define MAX_RPM_DIFFERENCE 30 //max difference for engine nominal RPM
#define RESET_RPM_WATCHDOG_TIME 2000

//max BAR turbo pressure
#define MAX_BOOST_PRESSURE 1.9

//in miliseconds
#define GPS_UPDATE 4 * 1000
#define GPS_MIN_KMPH_SPEED 5.0

//used for determine if GPS is available
#define MAX_GPS_AGE 4000

//min-max value for indicator
#define VOLTS_MIN_VAL 11.6
#define VOLTS_MAX_VAL 14.7

//just a general timer tasks info message on main thread
#define THREAD_CONTROL_SECONDS 5

//for PID controller
#define PID_MAX_INTEGRAL 16384


//glow plugs values
#define MAX_GLOW_PLUGS_TIME SECONDS_IN_MINUTE

//glow plugs threshold temperature definitions
#define TEMP_VERY_LOW             -25.0   // Very low temperature, requiring the maximum glow plug lamp time
#define TEMP_MINIMUM_FOR_GLOW_PLUGS TEMP_COLD_ENGINE  // Minimum temperature, at which the lamp lights for the minimum time

//glow plugs lamp time definitions
#define MAX_LAMP_TIME             10      // Maximum lamp time in seconds
#define MIN_LAMP_TIME             1       // Minimum lamp time in seconds

#endif

