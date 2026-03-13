#ifndef T_CONFIG
#define T_CONFIG

#define WATCHDOG_TIME 4000
#define UNSYNCHRONIZE_TIME 15
#define CORE_OPERATION_DELAY 1

#define CAN_MAIN_LOOP_SEND_INTERVAL 1

#define MAX_RETRIES 15

//in miliseconds, print values into serial
#define DEBUG_UPDATE 3 * SECOND

//#define ABS_CAR_SPEED_PACKET_TEST true
#define ABS_CAR_SPEED_SEQUENCE_DELAY 5000
//#define OIL_PRESSURE_PACKET_TEST true
//#define ABS_CAR_SPEED_PACKET_LINEAR_TEST true


// Oil pressure sender parameters
#define OIL_PRESSURE_READ_INTERVAL 100
#define OIL_PRESSURE_FILTER_ALPHA 0.20f
#define OIL_PRESSURE_MAX_BAR 10.0f
#define OIL_PRESSURE_SENSOR_RES_MIN_OHM 10.0f
#define OIL_PRESSURE_SENSOR_RES_MAX_OHM 180.0f
#define OIL_PRESSURE_PULLUP_OHM 220.0f
#define OIL_PRESSURE_ADC_REF_V 3.3f

#define OIL_PRESSURE_ADC_BITS 12
// true  -> pull-up resistor to Vref, sensor to GND
// false -> pull-down resistor to GND, sensor to Vref
#define OIL_PRESSURE_DIVIDER_PULLUP 1
//tire dimensions:
#define TIRE_DIMENSIONS "185/55 R15"
#define TIRE_CORRECTION_FACTOR 0.98

#endif
