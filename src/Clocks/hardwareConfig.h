#ifndef C_HARDWAREC
#define C_HARDWAREC

// Pins and buses come from the rp2040-zero assembly of device_tree.json; the
// code reads the JH_HW_* macros of the generated jh_hardware.h directly.

// hal_can_default_config() puts the MCP2515 on the default SPI bus 0.
#if JH_HW_BUS_SPI_INDEX != 0
#error "Clocks drives the MCP2515 on SPI bus 0"
#endif

#define PWM_WRITE_RESOLUTION 11
#define PWM_RESOLUTION 2047

#endif
