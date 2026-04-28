#pragma once

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

#define CAN_MAIN_LOOP_SEND_INTERVAL 1

#define MAX_RETRIES 15

//in miliseconds, print values into serial
#define DEBUG_UPDATE 3 * SECOND

//#define ABS_CAR_SPEED_PACKET_TEST true
#define ABS_CAR_SPEED_SEQUENCE_DELAY 5000
//#define OIL_PRESSURE_PACKET_TEST true
//#define ABS_CAR_SPEED_PACKET_LINEAR_TEST true

//tire dimensions:
#define TIRE_DIMENSIONS "185/55 R15"
#define TIRE_CORRECTION_FACTOR 0.98

//oil pressure readings
#define OIL_PRESSURE_READ_INTERVAL 100

//thermocouples readings
#define THERMOCOUPLE_READ_INTERVAL 1000

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
