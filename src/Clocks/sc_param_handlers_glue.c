/*
 * R1.4 build glue for arduino-cli - see src/ECU/sc_param_handlers_glue.c
 * for the rationale. Host CMake compiles the common .c directly via
 * CLOCKS_SOURCES, so each source TU is built exactly once on either
 * path.
 */

#include "../common/scDefinitions/sc_param_handlers.c"
