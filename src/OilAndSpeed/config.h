#ifndef T_CONFIG
#define T_CONFIG

#define WATCHDOG_TIME 4000
#define UNSYNCHRONIZE_TIME 15
#define CORE_OPERATION_DELAY 1

#define CAN_MAIN_LOOP_SEND_INTERVAL 250

#define MAX_RETRIES 15

//in miliseconds, print values into serial
#define DEBUG_UPDATE 3 * SECOND

//#define ABS_CAR_SPEED_PACKET_TEST true
#define OIL_PRESSURE_PACKET_TEST true

//tire dimensions:
#define TIRE_DIMENSIONS "205/55 R16"
#define TIRE_CORRECTION_FACTOR 0.98

#endif
