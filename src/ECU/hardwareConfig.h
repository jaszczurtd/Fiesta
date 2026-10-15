#ifndef T_HARDWARECONFIG
#define T_HARDWARECONFIG

#include "../common/adjustometer_protocol.h"
#include "../common/vp37/vp37_config.h"
#include <libConfig.h>

#ifdef VP37
// 130 Hz: the current ripple at this frequency keeps the actuator free of
// the upper end stop and static friction on descents (bench, 2026-09-16);
#include "../common/vp37_drive_config.h"
#define TURBO_PWM_FREQUENCY_HZ 300
#define ANGLE_PWM_FREQUENCY_HZ VP37_TIMING_PWM_FREQUENCY_HZ
#else
#ifndef VP37_PWM_FREQUENCY_HZ
#define VP37_PWM_FREQUENCY_HZ 300
#endif
#define TURBO_PWM_FREQUENCY_HZ 300
#define ANGLE_PWM_FREQUENCY_HZ 300
#endif

// RP2040 flash-backed EEPROM emulation size used by ECU module.
#define ECU_EEPROM_SIZE_BYTES HAL_RP_FLASH_EEPROM_SIZE

#define PWM_WRITE_RESOLUTION VP37_PWM_WRITE_RESOLUTION
#define PWM_RESOLUTION VP37_PWM_RESOLUTION

// Pins, buses and addresses come from the rp2040-plus assembly of
// device_tree.json; the code reads the JH_HW_* macros of the generated
// jh_hardware.h directly.

// The PCF8574A writes use the default I2C bus 0.
#if JH_HW_BUS_I2C_INDEX != 0
#error "the ECU drives its I2C devices on bus 0"
#endif
// The Adjustometer firmware answers at the address of its protocol header.
#if JH_HW_NODE_ADJUSTOMETER_ADDRESS != ADJUSTOMETER_I2C_ADDR
#error                                                                         \
    "device_tree.json and adjustometer_protocol.h disagree on the Adjustometer address"
#endif

// 4051 inputs assignments
#define HC4051_I_COOLANT_TEMP 0
#define HC4051_I_OIL_TEMP 1
#define HC4051_I_THROTTLE_POS 2
#define HC4051_I_AIR_TEMP 3
#define HC4051_I_FUEL_LEVEL 4
#define HC4051_I_BAR_PRESSURE 5
// 6 not used ATM
// 7 not used ATM

// real values (resistance) for temperature (coolant/oil) measurement

#define R_TEMP_A 1506 // sensor
#define R_TEMP_B 1500

#define R_TEMP_AIR_A 5050 // sensor
#define R_TEMP_AIR_B 4800

// dividers - analog reads
#define DIVIDER_PRESSURE_BAR 955

#endif
