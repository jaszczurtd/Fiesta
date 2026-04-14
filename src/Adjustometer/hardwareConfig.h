#ifndef T_HARDWARECONFIG
#define T_HARDWARECONFIG

#include <libConfig.h>

#define I2C_SPEED_HZ 400000

#define PWM_WRITE_RESOLUTION 11
#define PWM_RESOLUTION 2047

//rpi pio pin numbers
#define PIO_INTERRUPT_HALL 2    

#define PIN_SDA 0
#define PIN_SCL 1

#define ADC_VOLT_PIN 28
#define ADC_FUEL_TEMP_PIN 27

#define R_VP37_FUEL_A 2300 //sensor
#define R_VP37_FUEL_B 3300

// Supply voltage divider: R1=47k (high-side), R2=10k (low-side).
// Ratio = (R1+R2)/R2 = 5.7, max measurable ≈ 18.8 V.
#define VDIV_R1_KOHM  47
#define VDIV_R2_KOHM  10

#endif
