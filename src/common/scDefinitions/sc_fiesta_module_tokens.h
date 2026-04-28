#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ── Module roster / identity ─────────────────────────────────────── */
#define SC_MODULE_COUNT 3u

// Module tokens, used for debug prefix and other places where module name is needed,
// like Fiesta SerialConfigurator tool, and the flashing process.
#define SC_MODULE_TOKEN_ECU "ECU"
#define SC_MODULE_TOKEN_CLOCKS "CLOCKS"
#define SC_MODULE_TOKEN_OIL_AND_SPEED "OIL&SPD"
//this module is out of scope of SerialConfigurator, but is defined here to keep the 
//token roster in one place and avoid accidental reuse of "ADJ" literal elsewhere.
#define SC_MODULE_TOKEN_ADJUSTOMETER "ADJ"

// Module printable names, used in Fiesta SerialConfigurator UI and other places 
// where user-facing name is needed.
#define SC_MODULE_ECU "ECU"
#define SC_MODULE_CLOCKS "Clocks"
#define SC_MODULE_OIL_AND_SPEED "OilAndSpeed"

#ifdef __cplusplus
}
#endif
