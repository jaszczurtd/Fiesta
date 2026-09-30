#ifndef VP37_POWER_STAGE_H
#define VP37_POWER_STAGE_H

/**
 * @file vp37_power_stage.h
 * @brief The VP37 drive and its measurement, the same on the ECU and on the
 * pump bench: pins, parts and values. The ECU's hardwareConfig.h takes its
 * pin and divider names from here. The drive frequency lives in
 * vp37_drive_config.h because the Adjustometer build reads it too.
 */

/** PWM counter resolution of both actuator channels [bits] and full scale. */
#define VP37_PWM_WRITE_RESOLUTION 11
#define VP37_PWM_RESOLUTION 2047

/** Quantity actuator (N146-like) drive and timing actuator (N108-like) drive.
 * Both are active low: a command is written as VP37_PWM_RESOLUTION minus the
 * command. */
#define VP37_QUANTITY_PWM_PIN 9
#define VP37_TIMING_PWM_PIN 5
/** Timing actuator drive frequency [Hz]. */
#define VP37_TIMING_PWM_FREQUENCY_HZ 200

/** ADC scan inputs: the source shunt of the quantity MOSFET, a board input
 * that shares the scan (ECU sensor multiplexer, bench demand pot) and the
 * supply divider. The scan owns the converter while it runs. */
#define VP37_SHUNT_ADC_PIN 26
#define VP37_SCAN_AUX_ADC_PIN 27
#define VP37_SUPPLY_ADC_PIN 28

/** Supply divider: supply -> R1 -> ADC pin -> R2 -> ground; only the ratio is
 * used. */
#define VP37_SUPPLY_DIVIDER_R1 47
#define VP37_SUPPLY_DIVIDER_R2 10

/** Low-side shunt in the quantity MOSFET source [Ohm]. */
#define VP37_CURRENT_SHUNT_OHMS 0.22f
/** Holding-map adjustment for the installed source shunt; PID gains are
 * separate. */
#define VP37_PWM_FF_HARDWARE_GAIN 1.08f

#endif
