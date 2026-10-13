#ifndef T_HARDWARECONFIG
#define T_HARDWARECONFIG

#include <libConfig.h>

// Pins come from the rp2040-zero assembly of device_tree.json; the code reads
// the JH_HW_* macros of the generated jh_hardware.h directly.

// The ECU is the bus master; the module answers at ADJUSTOMETER_I2C_ADDR.
// hal_i2c_slave_init() starts the default bus 0.
#if JH_HW_BUS_ECU_INDEX != 0
#error "the Adjustometer answers the ECU on I2C bus 0"
#endif

#define R_VP37_FUEL_A 2300 // sensor
#define R_VP37_FUEL_B 3300

// Supply voltage divider: R1=47k (high-side), R2=10k (low-side).
// Ratio = (R1+R2)/R2 = 5.7, max measurable ≈ 18.8 V.
#define VDIV_R1_KOHM 47
#define VDIV_R2_KOHM 10

#endif
