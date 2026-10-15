#ifndef C_HARDWAREC
#define C_HARDWAREC

// Pins and buses come from the rp2040-zero assembly of device_tree.json; the
// code reads the JH_HW_* macros of the generated jh_hardware.h directly.

#define PWM_WRITE_RESOLUTION 11
#define PWM_RESOLUTION 2047

#endif
